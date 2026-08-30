#include "hydra/audio_routing.hpp"

#include <algorithm>
#include <cwctype>
#include <set>
#include <tuple>
#include <utility>

namespace hydra::audio {
namespace {

constexpr unsigned kMaximumSessionRefreshAttempts = 3u;

std::wstring canonical(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
    return result;
}

bool hasEmbeddedNull(std::wstring_view value) noexcept {
    return value.find(L'\0') != std::wstring_view::npos;
}

bool validSessionRecord(const SessionRecord& session, std::string& error) {
    if (session.endpointId.empty() || session.endpointId.size() > kMaxEndpointIdChars ||
        hasEmbeddedNull(session.endpointId)) {
        error = "audio session has an invalid endpoint ID";
        return false;
    }
    if (session.sessionInstanceId.empty() ||
        session.sessionInstanceId.size() > kMaxSessionIdentifierChars ||
        hasEmbeddedNull(session.sessionInstanceId)) {
        error = "audio session has an invalid instance identifier";
        return false;
    }
    if (session.sessionIdentifier.size() > kMaxSessionIdentifierChars ||
        hasEmbeddedNull(session.sessionIdentifier)) {
        error = "audio session has an invalid session identifier";
        return false;
    }
    if (session.executablePath.size() > kMaxEndpointIdChars ||
        hasEmbeddedNull(session.executablePath)) {
        error = "audio session process path exceeds the bound or is malformed";
        return false;
    }
    if (session.systemSoundsSession) {
        return true;
    }
    if (session.processId == 0) {
        error = "non-system audio session has no process ID";
        return false;
    }
    if (session.processIdentityVerified &&
        (session.processCreationTime100ns == 0 || session.executablePath.empty())) {
        error = "verified audio session process identity is incomplete";
        return false;
    }
    return true;
}

bool normalizeSessions(std::vector<SessionRecord>& sessions,
                       std::span<const EndpointRecord> endpoints,
                       std::string& error) {
    if (sessions.size() > kMaxAudioSessions) {
        error = "audio session count exceeds the bounded inventory";
        return false;
    }

    std::set<std::wstring> allowedRenderEndpoints;
    for (const auto& endpoint : endpoints) {
        if (endpoint.flow == DataFlow::Render) {
            allowedRenderEndpoints.insert(canonical(endpoint.endpointId));
        }
    }

    std::set<std::pair<std::wstring, std::wstring>> identities;
    for (const auto& session : sessions) {
        if (!validSessionRecord(session, error)) return false;
        const auto endpointKey = canonical(session.endpointId);
        if (!allowedRenderEndpoints.contains(endpointKey)) {
            error = "audio session source returned a session outside the supplied render endpoints";
            return false;
        }
        const auto key = std::make_pair(endpointKey, canonical(session.sessionInstanceId));
        if (!identities.insert(key).second) {
            error = "audio session inventory contains a duplicate endpoint/session instance";
            return false;
        }
    }

    std::sort(sessions.begin(), sessions.end(),
              [](const SessionRecord& left, const SessionRecord& right) {
                  return std::tuple{canonical(left.endpointId),
                                    canonical(left.sessionInstanceId),
                                    left.processId} <
                         std::tuple{canonical(right.endpointId),
                                    canonical(right.sessionInstanceId),
                                    right.processId};
              });
    return true;
}

const EndpointRecord* findTargetEndpoint(const EndpointSnapshot& endpoints,
                                         std::wstring_view endpointId) {
    const auto target = canonical(endpointId);
    const auto found = std::find_if(
        endpoints.endpoints.begin(), endpoints.endpoints.end(),
        [&](const EndpointRecord& endpoint) {
            return endpoint.flow == DataFlow::Render &&
                   canonical(endpoint.endpointId) == target;
        });
    return found == endpoints.endpoints.end() ? nullptr : &*found;
}

bool sameEndpoint(std::wstring_view left, std::wstring_view right) {
    return canonical(left) == canonical(right);
}

bool processTreeContainsPid(const process::ProcessTreeSnapshot& tree,
                            std::uint32_t processId) noexcept {
    if (tree.root.processId == processId && processId != 0) return true;
    return std::any_of(tree.processes.begin(), tree.processes.end(),
                       [&](const process::ProcessRecord& record) {
                           return !record.exited && record.identity.processId == processId &&
                                  processId != 0;
                       });
}

bool processTreeHasExactIdentity(const process::ProcessTreeSnapshot& tree,
                                 std::uint32_t processId,
                                 std::uint64_t creationTime100ns) noexcept {
    if (tree.root.processId == processId &&
        tree.root.creationTime100ns == creationTime100ns && processId != 0) {
        return true;
    }
    return std::any_of(tree.processes.begin(), tree.processes.end(),
                       [&](const process::ProcessRecord& record) {
                           return !record.exited &&
                                  record.identity.processId == processId &&
                                  record.identity.creationTime100ns == creationTime100ns &&
                                  processId != 0;
                       });
}

bool verifyBeforeEndpoints(const OwnedSessionEvidence& before,
                           const OwnedSessionEvidence& after,
                           std::string& error) {
    const auto sameSessionIdentity = [](const SessionRecord& left,
                                        const SessionRecord& right) {
        return canonical(left.sessionInstanceId) ==
                   canonical(right.sessionInstanceId) &&
               left.processId == right.processId &&
               left.processCreationTime100ns == right.processCreationTime100ns;
    };

    for (const auto& current : after.ownedSessions) {
        const auto captured = std::find_if(
            before.ownedSessions.begin(), before.ownedSessions.end(),
            [&](const SessionRecord& original) {
                return sameSessionIdentity(current, original);
            });
        if (captured == before.ownedSessions.end()) {
            error = "an exact owned audio session appeared after route state capture; its rollback endpoint is unknown";
            return false;
        }
    }

    for (const auto& original : before.ownedSessions) {
        const auto found = std::find_if(
            after.ownedSessions.begin(), after.ownedSessions.end(),
            [&](const SessionRecord& current) {
                return sameSessionIdentity(current, original);
            });
        if (found == after.ownedSessions.end()) {
            error = "an originally owned audio session disappeared before rollback could be verified";
            return false;
        }
        if (!sameEndpoint(found->endpointId, original.endpointId)) {
            error = "an owned audio session did not return to its captured endpoint";
            return false;
        }
    }
    return true;
}

} // namespace

SessionInventory::SessionInventory(std::shared_ptr<SessionSource> source)
    : source_(std::move(source)) {}

bool SessionInventory::refresh(const EndpointSnapshot& endpoints,
                               std::string* error) {
    if (error != nullptr) error->clear();
    if (!source_) {
        if (error != nullptr) *error = "audio session source is unavailable";
        return false;
    }

    for (unsigned attempt = 0; attempt < kMaximumSessionRefreshAttempts; ++attempt) {
        const auto beforeGeneration = source_->changeGeneration();
        std::vector<SessionRecord> sessions;
        std::string sourceError;
        if (!source_->enumerate(endpoints.endpoints, sessions, sourceError)) {
            if (error != nullptr) {
                *error = sourceError.empty()
                    ? "audio session enumeration failed"
                    : std::move(sourceError);
            }
            return false;
        }
        const auto afterGeneration = source_->changeGeneration();
        if (beforeGeneration != afterGeneration) continue;
        if (!normalizeSessions(sessions, endpoints.endpoints, sourceError)) {
            if (error != nullptr) *error = std::move(sourceError);
            return false;
        }
        current_ = SessionSnapshot{afterGeneration, std::move(sessions)};
        return true;
    }

    if (error != nullptr) {
        *error = "audio sessions changed during every bounded refresh attempt";
    }
    return false;
}

bool SessionInventory::needsRefresh() const noexcept {
    if (!source_ || !current_) return true;
    return source_->changeGeneration() != current_->sourceGeneration;
}

bool sessionMatchesOwnedProcess(const process::ProcessTreeSnapshot& processTree,
                                const SessionRecord& session) noexcept {
    return !session.systemSoundsSession && !session.spansMultipleProcesses &&
           session.processIdentityVerified &&
           processTreeHasExactIdentity(processTree, session.processId,
                                       session.processCreationTime100ns);
}

OwnedSessionEvidence collectOwnedSessionEvidence(
    const RouteRequest& request,
    std::span<const SessionRecord> sessions) {
    OwnedSessionEvidence evidence;
    for (const auto& session : sessions) {
        if (session.systemSoundsSession) continue;
        if (sessionMatchesOwnedProcess(request.processTree, session)) {
            evidence.ownedSessions.push_back(session);
        } else if ((!session.processIdentityVerified || session.spansMultipleProcesses) &&
                   processTreeContainsPid(request.processTree, session.processId)) {
            evidence.pidMatchesWithoutVerifiedIdentity.push_back(session);
        }
    }
    evidence.allOwnedSessionsOnTarget = !evidence.ownedSessions.empty() &&
        std::all_of(evidence.ownedSessions.begin(), evidence.ownedSessions.end(),
                    [&](const SessionRecord& session) {
                        return sameEndpoint(session.endpointId,
                                            request.targetEndpointId);
                    });
    return evidence;
}

RouteCapability ObserveOnlyRouteBackend::capability(
    const RouteRequest&,
    const OwnedSessionEvidence& evidence) const noexcept {
    return evidence.allOwnedSessionsOnTarget
        ? RouteCapability::SatisfiedWithoutMutation
        : RouteCapability::Unsupported;
}

bool ObserveOnlyRouteBackend::captureState(const RouteRequest&,
                                            const OwnedSessionEvidence&,
                                            BackendState& state,
                                            std::string& error) noexcept {
    state.opaque.clear();
    error.clear();
    return true;
}

bool ObserveOnlyRouteBackend::apply(const RouteRequest&,
                                    const BackendState&,
                                    std::string& error) noexcept {
    error = "observe-only audio backend cannot change another process's endpoint";
    return false;
}

bool ObserveOnlyRouteBackend::rollback(const RouteRequest&,
                                       const BackendState&,
                                       std::string& error) noexcept {
    error.clear();
    return true;
}

RouteTransaction::RouteTransaction(RouteRequest request,
                                   std::shared_ptr<SessionInventory> sessions,
                                   std::shared_ptr<RouteBackend> backend)
    : request_(std::move(request)),
      sessions_(std::move(sessions)),
      backend_(std::move(backend)) {
    if (!backend_) backend_ = std::make_shared<ObserveOnlyRouteBackend>();
    status_.backendKind = backend_->kind();
}

bool RouteTransaction::validateRequest(const EndpointSnapshot& endpoints,
                                       std::string& error) {
    if (request_.seatId == 0 || request_.processTree.seatId != request_.seatId) {
        status_.error = RouteError::InvalidSeat;
        error = "audio route Seat ID is missing or does not match the owned process tree";
        return false;
    }
    if (!request_.processTree.root.valid()) {
        status_.error = RouteError::InvalidProcessTree;
        error = "audio route requires an exact valid root process identity";
        return false;
    }
    if (request_.targetEndpointId.empty() ||
        request_.targetEndpointId.size() > kMaxEndpointIdChars ||
        hasEmbeddedNull(request_.targetEndpointId)) {
        status_.error = RouteError::TargetEndpointMissing;
        error = "audio route target endpoint ID is invalid";
        return false;
    }
    const auto* endpoint = findTargetEndpoint(endpoints, request_.targetEndpointId);
    if (endpoint == nullptr) {
        status_.error = RouteError::TargetEndpointMissing;
        error = "audio route target render endpoint is absent";
        return false;
    }
    if (!isEndpointCurrentlyAvailable(*endpoint)) {
        status_.error = RouteError::TargetEndpointUnavailable;
        error = "audio route target render endpoint is currently unavailable";
        return false;
    }
    if (!sessions_) {
        status_.error = RouteError::SnapshotFailed;
        error = "audio route session inventory is unavailable";
        return false;
    }
    return true;
}

bool RouteTransaction::refreshEvidence(const EndpointSnapshot& endpoints,
                                       OwnedSessionEvidence& evidence,
                                       std::string& error) {
    if (!sessions_->refresh(endpoints, &error) || !sessions_->current()) {
        status_.error = RouteError::SnapshotFailed;
        return false;
    }
    evidence = collectOwnedSessionEvidence(request_, sessions_->current()->sessions);
    if (!evidence.pidMatchesWithoutVerifiedIdentity.empty()) {
        status_.error = RouteError::OwnershipUnverified;
        error = "an audio session reused an owned PID but exact process identity could not be verified";
        return false;
    }
    return true;
}

bool RouteTransaction::verifyApplied(const EndpointSnapshot& endpoints,
                                     OwnedSessionEvidence& evidence,
                                     std::string& error) {
    if (!refreshEvidence(endpoints, evidence, error)) return false;
    if (evidence.ownedSessions.empty()) {
        status_.error = RouteError::VerificationFailed;
        error = "owned audio sessions disappeared before routing could be verified";
        return false;
    }
    if (!evidence.allOwnedSessionsOnTarget) {
        status_.error = RouteError::VerificationFailed;
        error = "one or more exact owned audio sessions remain on a different endpoint";
        return false;
    }
    return true;
}

bool RouteTransaction::verifyRollback(const EndpointSnapshot& endpoints,
                                      OwnedSessionEvidence& evidence,
                                      std::string& error) {
    if (!beforeEvidence_) {
        status_.error = RouteError::RollbackFailed;
        error = "audio route has no captured pre-apply evidence";
        return false;
    }
    if (!refreshEvidence(endpoints, evidence, error)) return false;
    if (!verifyBeforeEndpoints(*beforeEvidence_, evidence, error)) {
        status_.error = RouteError::RollbackFailed;
        return false;
    }
    return true;
}

RouteStatus RouteTransaction::attempt(const EndpointSnapshot& endpoints,
                                      std::string* error) {
    std::string localError;
    if (error != nullptr) error->clear();
    status_.backendKind = backend_ ? backend_->kind() : RouteBackendKind::ObserveOnly;
    status_.rollbackVerified = false;

    if (!validateRequest(endpoints, localError)) {
        // Once a backend mutation has occurred, losing the target endpoint (or
        // another request precondition) is no longer an ordinary preflight
        // failure. Retain exact recovery ownership until rollback is verified.
        status_.phase = status_.mutated ? RoutePhase::RecoveryRequired
                                        : RoutePhase::Failed;
        if (error != nullptr) *error = std::move(localError);
        return status_;
    }

    OwnedSessionEvidence evidence;
    if (status_.mutated) {
        if (!refreshEvidence(endpoints, evidence, localError)) {
            status_.phase = RoutePhase::RecoveryRequired;
            status_.evidence = std::move(evidence);
            if (error != nullptr) *error = std::move(localError);
            return status_;
        }
        status_.evidence = evidence;
        if (evidence.ownedSessions.empty()) {
            status_.phase = RoutePhase::RecoveryRequired;
            status_.error = RouteError::VerificationFailed;
            if (error != nullptr) {
                *error = "owned audio sessions disappeared after routing mutation; active route state cannot be verified";
            }
            return status_;
        }
        if (evidence.allOwnedSessionsOnTarget) {
            status_.phase = RoutePhase::Applied;
            status_.error = RouteError::None;
            status_.capability = RouteCapability::Mutable;
            return status_;
        }
        status_.phase = RoutePhase::RecoveryRequired;
        status_.error = RouteError::ExternalDrift;
        if (error != nullptr) {
            *error = "an applied audio route drifted away from the requested endpoint";
        }
        return status_;
    }

    status_.error = RouteError::None;
    if (!refreshEvidence(endpoints, evidence, localError)) {
        status_.phase = RoutePhase::Failed;
        status_.evidence = std::move(evidence);
        if (error != nullptr) *error = std::move(localError);
        return status_;
    }
    status_.evidence = evidence;

    if (evidence.ownedSessions.empty()) {
        status_.phase = RoutePhase::WaitingForSession;
        status_.capability = RouteCapability::Unsupported;
        return status_;
    }
    if (evidence.allOwnedSessionsOnTarget) {
        status_.phase = RoutePhase::Satisfied;
        status_.capability = RouteCapability::SatisfiedWithoutMutation;
        return status_;
    }

    status_.capability = backend_->capability(request_, evidence);
    if (status_.capability != RouteCapability::Mutable) {
        status_.phase = RoutePhase::Unsupported;
        status_.error = RouteError::BackendUnsupported;
        if (error != nullptr) {
            *error = "no documented selected audio backend can move the exact owned sessions to the requested endpoint";
        }
        return status_;
    }

    BackendState captured;
    if (!backend_->captureState(request_, evidence, captured, localError)) {
        status_.phase = RoutePhase::Failed;
        status_.error = RouteError::SnapshotFailed;
        if (error != nullptr) *error = std::move(localError);
        return status_;
    }
    before_ = captured;
    beforeEvidence_ = evidence;
    status_.phase = RoutePhase::Ready;

    if (!backend_->apply(request_, *before_, localError)) {
        std::string rollbackError;
        const bool rollbackCalled = backend_->rollback(request_, *before_, rollbackError);
        OwnedSessionEvidence restoredEvidence;
        const bool rollbackVerified = rollbackCalled &&
            verifyRollback(endpoints, restoredEvidence, rollbackError);
        status_.evidence = std::move(restoredEvidence);
        status_.rollbackVerified = rollbackVerified;
        status_.mutated = !rollbackVerified;
        status_.phase = rollbackVerified ? RoutePhase::Failed
                                         : RoutePhase::RecoveryRequired;
        status_.error = rollbackVerified ? RouteError::ApplyFailed
                                         : RouteError::RollbackFailed;
        if (error != nullptr) {
            *error = rollbackVerified
                ? (localError.empty() ? "audio routing apply failed" : std::move(localError))
                : (rollbackError.empty() ? "audio routing rollback could not be verified"
                                         : std::move(rollbackError));
        }
        return status_;
    }

    status_.mutated = true;
    OwnedSessionEvidence appliedEvidence;
    if (verifyApplied(endpoints, appliedEvidence, localError)) {
        status_.phase = RoutePhase::Applied;
        status_.error = RouteError::None;
        status_.evidence = std::move(appliedEvidence);
        return status_;
    }

    std::string rollbackError;
    const bool rollbackCalled = backend_->rollback(request_, *before_, rollbackError);
    OwnedSessionEvidence restoredEvidence;
    const bool rollbackVerified = rollbackCalled &&
        verifyRollback(endpoints, restoredEvidence, rollbackError);
    status_.evidence = std::move(restoredEvidence);
    status_.rollbackVerified = rollbackVerified;
    status_.mutated = !rollbackVerified;
    status_.phase = rollbackVerified ? RoutePhase::Failed
                                     : RoutePhase::RecoveryRequired;
    status_.error = rollbackVerified ? RouteError::VerificationFailed
                                     : RouteError::RollbackFailed;
    if (error != nullptr) {
        *error = rollbackVerified
            ? (localError.empty() ? "audio routing verification failed" : std::move(localError))
            : (rollbackError.empty() ? "audio routing rollback could not be verified"
                                     : std::move(rollbackError));
    }
    return status_;
}

RouteStatus RouteTransaction::rollback(const EndpointSnapshot& endpoints,
                                       std::string* error) {
    if (error != nullptr) error->clear();
    if (!status_.mutated) {
        status_.rollbackVerified = true;
        if (status_.phase == RoutePhase::Applied) status_.phase = RoutePhase::Satisfied;
        return status_;
    }
    if (!backend_ || !before_ || !beforeEvidence_) {
        status_.phase = RoutePhase::RecoveryRequired;
        status_.error = RouteError::RollbackFailed;
        if (error != nullptr) *error = "audio route rollback state is incomplete";
        return status_;
    }

    std::string localError;
    if (!backend_->rollback(request_, *before_, localError)) {
        status_.phase = RoutePhase::RecoveryRequired;
        status_.error = RouteError::RollbackFailed;
        if (error != nullptr) *error = std::move(localError);
        return status_;
    }

    OwnedSessionEvidence evidence;
    if (!verifyRollback(endpoints, evidence, localError)) {
        status_.phase = RoutePhase::RecoveryRequired;
        status_.error = RouteError::RollbackFailed;
        status_.evidence = std::move(evidence);
        if (error != nullptr) *error = std::move(localError);
        return status_;
    }

    status_.phase = RoutePhase::Ready;
    status_.error = RouteError::None;
    status_.evidence = std::move(evidence);
    status_.mutated = false;
    status_.rollbackVerified = true;
    before_.reset();
    beforeEvidence_.reset();
    return status_;
}

std::string_view routePhaseName(RoutePhase value) noexcept {
    switch (value) {
        case RoutePhase::Unprepared: return "unprepared";
        case RoutePhase::WaitingForSession: return "waiting-for-session";
        case RoutePhase::Ready: return "ready";
        case RoutePhase::Satisfied: return "satisfied";
        case RoutePhase::Applied: return "applied";
        case RoutePhase::Unsupported: return "unsupported";
        case RoutePhase::Failed: return "failed";
        case RoutePhase::RecoveryRequired: return "recovery-required";
    }
    return "unknown";
}

std::string_view routeErrorName(RouteError value) noexcept {
    switch (value) {
        case RouteError::None: return "none";
        case RouteError::InvalidSeat: return "invalid-seat";
        case RouteError::InvalidProcessTree: return "invalid-process-tree";
        case RouteError::TargetEndpointMissing: return "target-endpoint-missing";
        case RouteError::TargetEndpointUnavailable: return "target-endpoint-unavailable";
        case RouteError::OwnershipUnverified: return "ownership-unverified";
        case RouteError::BackendUnsupported: return "backend-unsupported";
        case RouteError::SnapshotFailed: return "snapshot-failed";
        case RouteError::ApplyFailed: return "apply-failed";
        case RouteError::VerificationFailed: return "verification-failed";
        case RouteError::RollbackFailed: return "rollback-failed";
        case RouteError::ExternalDrift: return "external-drift";
    }
    return "unknown";
}

std::string_view routeBackendKindName(RouteBackendKind value) noexcept {
    switch (value) {
        case RouteBackendKind::ObserveOnly: return "observe-only";
        case RouteBackendKind::ProviderManaged: return "provider-managed";
        case RouteBackendKind::ProcessLoopbackRelayExperimental:
            return "process-loopback-relay-experimental";
    }
    return "unknown";
}

} // namespace hydra::audio
