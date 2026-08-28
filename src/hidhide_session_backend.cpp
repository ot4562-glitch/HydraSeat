#include "hydra/hidhide_session_backend.hpp"

#include "hydra/hardware_identity.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace hydra {
namespace {

constexpr std::size_t kMaximumDiagnosticBytes = 2048u;

std::string boundedDiagnostic(std::string value) {
    if (value.size() > kMaximumDiagnosticBytes) {
        value.resize(kMaximumDiagnosticBytes);
    }
    return value;
}

bool boundedText(std::wstring_view value) {
    return !value.empty() && value.size() <= kHidHideSessionMaxIdentifierChars &&
           value.find(L'\0') == std::wstring_view::npos;
}

std::wstring normalizeApplication(std::wstring_view value) {
    return hardware::normalizeDevicePath(value);
}

std::vector<std::wstring> normalizedUnique(
    const std::vector<std::wstring>& values,
    bool application) {
    std::vector<std::wstring> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        auto normalized = application
            ? normalizeApplication(value)
            : hardware::canonicalizeInstanceId(value);
        if (std::find(result.begin(), result.end(), normalized) == result.end()) {
            result.push_back(std::move(normalized));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

void mergeUnique(std::vector<std::wstring>& destination,
                 const std::vector<std::wstring>& additions,
                 bool application) {
    destination = normalizedUnique(destination, application);
    const auto normalizedAdditions = normalizedUnique(additions, application);
    for (const auto& value : normalizedAdditions) {
        if (!std::binary_search(destination.begin(), destination.end(), value)) {
            destination.push_back(value);
            std::sort(destination.begin(), destination.end());
        }
    }
}

HidHideSessionSnapshot normalizedSnapshot(HidHideSessionSnapshot snapshot) {
    snapshot.blockedDeviceInstanceIds =
        normalizedUnique(snapshot.blockedDeviceInstanceIds, false);
    snapshot.allowedApplications =
        normalizedUnique(snapshot.allowedApplications, true);
    return snapshot;
}

bool sameSnapshot(const HidHideSessionSnapshot& lhs,
                  const HidHideSessionSnapshot& rhs) {
    return normalizedSnapshot(lhs) == normalizedSnapshot(rhs);
}

} // namespace

HidHideSessionTransaction::HidHideSessionTransaction(
    std::shared_ptr<HidHideSessionPlatform> platform)
    : platform_(std::move(platform)) {}

HidHideSessionTransaction::~HidHideSessionTransaction() {
    if (phase_ == HidHideSessionPhase::Active ||
        phase_ == HidHideSessionPhase::Prepared) {
        (void)rollbackInternal(false);
    }
}

HidHideSessionResult HidHideSessionTransaction::prepare(
    HidHideSessionRequest request,
    std::uint64_t nowMilliseconds) {
    if (phase_ == HidHideSessionPhase::RecoveryRequired) {
        return result(HidHideSessionResultCode::RecoveryRequired,
                      "HidHide session is recovery-required");
    }
    if (phase_ == HidHideSessionPhase::Active) {
        return result(HidHideSessionResultCode::UnsupportedState,
                      "active HidHide session must roll back before replanning");
    }

    std::string error;
    if (!validateHidHideSessionRequest(request, error)) {
        return result(HidHideSessionResultCode::InvalidRequest, std::move(error));
    }
    if (!platform_) {
        return result(HidHideSessionResultCode::BackendFailure,
                      "HidHide session platform is unavailable");
    }

    HidHideSessionSnapshot before;
    if (!platform_->readState(before, error)) {
        return result(HidHideSessionResultCode::BackendFailure,
                      boundedDiagnostic("HidHide state snapshot failed: " + error));
    }
    before = normalizedSnapshot(std::move(before));
    if (before.inverseWhitelist) {
        return result(HidHideSessionResultCode::UnsupportedState,
                      "inverse HidHide application-list mode is not supported by the guarded v1 transaction");
    }

    if (request.expiryMilliseconds >
        std::numeric_limits<std::uint64_t>::max() - nowMilliseconds) {
        return result(HidHideSessionResultCode::InvalidRequest,
                      "HidHide session expiry would overflow the monotonic deadline");
    }

    request.deviceInstanceIds = normalizedUnique(request.deviceInstanceIds, false);
    request.allowedApplications = normalizedUnique(request.allowedApplications, true);

    HidHideSessionPlan plan;
    plan.request = std::move(request);
    plan.before = before;
    plan.applied = makeHidHideSessionAppliedState(plan.before, plan.request);
    plan.preparedAtMilliseconds = nowMilliseconds;
    plan.expiryAtMilliseconds = nowMilliseconds + plan.request.expiryMilliseconds;

    plan_ = std::move(plan);
    phase_ = HidHideSessionPhase::Prepared;
    return result(HidHideSessionResultCode::Ok,
                  "HidHide session plan prepared without mutation");
}

HidHideSessionResult HidHideSessionTransaction::activate(
    std::uint64_t nowMilliseconds) {
    if (phase_ == HidHideSessionPhase::Active) {
        return result(HidHideSessionResultCode::AlreadySatisfied,
                      "HidHide session is already active");
    }
    if (phase_ == HidHideSessionPhase::RecoveryRequired) {
        return result(HidHideSessionResultCode::RecoveryRequired,
                      "HidHide session requires recovery before activation");
    }
    if (phase_ != HidHideSessionPhase::Prepared || !plan_) {
        return result(HidHideSessionResultCode::UnsupportedState,
                      "HidHide activation requires a prepared plan");
    }
    if (nowMilliseconds >= plan_->expiryAtMilliseconds) {
        phase_ = HidHideSessionPhase::Idle;
        plan_.reset();
        return result(HidHideSessionResultCode::Expired,
                      "prepared HidHide session expired before activation");
    }
    if (!plan_->request.physicalAcceptanceRecorded) {
        return result(HidHideSessionResultCode::PhysicalGateRequired,
                      "native HidHide mutation is disabled until physical Gate A/B/C evidence is recorded");
    }
    if (!plan_->request.nativeMutationApproved) {
        return result(HidHideSessionResultCode::NativeMutationDisabled,
                      "native HidHide mutation requires explicit high-risk approval");
    }
    if (!platform_->mutationSupported()) {
        return result(HidHideSessionResultCode::NativeMutationDisabled,
                      "selected HidHide platform exposes no native mutation capability");
    }
    if (!platform_->sessionBlacklistSupported()) {
        return result(HidHideSessionResultCode::UnsupportedState,
                      "verified HidHide process-lifetime session blacklist support is required");
    }

    std::string error;
    if (!platform_->writeState(plan_->applied, error)) {
        // A backend may have partially mutated before reporting failure. Keep
        // the exact snapshot/plan owned and immediately enter verified cleanup
        // rather than assuming the failed call was side-effect free.
        phase_ = HidHideSessionPhase::RollingBack;
        const auto cleanup = rollbackInternal(false);
        if (!cleanup.succeeded()) return cleanup;
        return result(HidHideSessionResultCode::BackendFailure,
                      boundedDiagnostic("HidHide session apply failed and prior state was restored: " + error));
    }

    HidHideSessionSnapshot observed;
    if (!platform_->readState(observed, error)) {
        phase_ = HidHideSessionPhase::RollingBack;
        const auto cleanup = rollbackInternal(false);
        if (!cleanup.succeeded()) return cleanup;
        return result(HidHideSessionResultCode::VerificationFailed,
                      boundedDiagnostic("HidHide apply verification read failed: " + error));
    }
    if (!sameSnapshot(observed, plan_->applied)) {
        phase_ = HidHideSessionPhase::RollingBack;
        const auto cleanup = rollbackInternal(false);
        if (!cleanup.succeeded()) return cleanup;
        return result(HidHideSessionResultCode::VerificationFailed,
                      "HidHide persistent state did not match the exact prepared session plan");
    }

    // Device IDs are added only through HidHide's process-lifetime session
    // blacklist. No HydraSeat device is appended to the persistent blacklist.
    if (!platform_->addSessionBlacklist(plan_->request.deviceInstanceIds, error)) {
        phase_ = HidHideSessionPhase::RollingBack;
        const auto cleanup = rollbackInternal(false);
        if (!cleanup.succeeded()) return cleanup;
        return result(HidHideSessionResultCode::BackendFailure,
                      boundedDiagnostic("HidHide session blacklist apply failed and persistent state was restored: " + error));
    }
    sessionEntriesActive_ = true;

    phase_ = HidHideSessionPhase::Active;
    return result(HidHideSessionResultCode::Ok,
                  "HidHide persistent state and process-lifetime session blacklist applied");
}

HidHideSessionResult HidHideSessionTransaction::expireIfNeeded(
    std::uint64_t nowMilliseconds) {
    if (phase_ != HidHideSessionPhase::Active || !plan_) {
        return result(HidHideSessionResultCode::AlreadySatisfied,
                      "no active HidHide session requires expiry cleanup");
    }
    if (nowMilliseconds < plan_->expiryAtMilliseconds) {
        return result(HidHideSessionResultCode::AlreadySatisfied,
                      "HidHide session expiry has not elapsed");
    }
    return rollbackInternal(true);
}

HidHideSessionResult HidHideSessionTransaction::rollback() {
    return rollbackInternal(false);
}

HidHideSessionResult HidHideSessionTransaction::rollbackInternal(bool expired) {
    if (!plan_) {
        phase_ = HidHideSessionPhase::Idle;
        return result(expired ? HidHideSessionResultCode::Expired
                              : HidHideSessionResultCode::AlreadySatisfied,
                      expired ? "HidHide session already expired and clean"
                              : "HidHide session is already clean");
    }
    if (phase_ == HidHideSessionPhase::RecoveryRequired) {
        return result(HidHideSessionResultCode::RecoveryRequired,
                      "HidHide cleanup is already recovery-required");
    }

    // Prepared plans are read-only. Cancelling one needs no backend write.
    if (phase_ == HidHideSessionPhase::Prepared) {
        plan_.reset();
        phase_ = HidHideSessionPhase::Idle;
        return result(expired ? HidHideSessionResultCode::Expired
                              : HidHideSessionResultCode::Ok,
                      expired ? "prepared HidHide session expired without mutation"
                              : "prepared HidHide session cancelled without mutation");
    }

    if (!platform_) {
        phase_ = HidHideSessionPhase::RecoveryRequired;
        return result(HidHideSessionResultCode::RecoveryRequired,
                      "HidHide cleanup lost its platform owner");
    }

    phase_ = HidHideSessionPhase::RollingBack;
    std::string error;
    std::string sessionClearError;
    bool sessionCleared = true;
    if (sessionEntriesActive_) {
        sessionCleared = platform_->clearSessionBlacklist(sessionClearError);
        if (sessionCleared) sessionEntriesActive_ = false;
    }

    HidHideSessionSnapshot current;
    if (!platform_->readState(current, error)) {
        phase_ = HidHideSessionPhase::RecoveryRequired;
        std::string diagnostic = "HidHide rollback preflight read failed: " + error;
        if (!sessionCleared) diagnostic += "; session blacklist clear failed: " + sessionClearError;
        return result(HidHideSessionResultCode::RecoveryRequired,
                      boundedDiagnostic(std::move(diagnostic)));
    }
    current = normalizedSnapshot(std::move(current));

    if (sameSnapshot(current, plan_->before)) {
        if (!sessionCleared) {
            phase_ = HidHideSessionPhase::RecoveryRequired;
            return result(HidHideSessionResultCode::RecoveryRequired,
                          boundedDiagnostic("persistent state is restored but session blacklist clear failed: " + sessionClearError));
        }
        plan_.reset();
        phase_ = HidHideSessionPhase::Idle;
        return result(expired ? HidHideSessionResultCode::Expired
                              : HidHideSessionResultCode::AlreadySatisfied,
                      expired ? "HidHide session expired; prior state was already restored"
                              : "HidHide prior state is already restored");
    }

    // Never clobber a third-party/user change that occurred after activation.
    // A mismatch against both the before and exact applied state is surfaced for
    // explicit recovery rather than silently restoring a stale snapshot.
    if (!sameSnapshot(current, plan_->applied)) {
        phase_ = HidHideSessionPhase::RecoveryRequired;
        std::string diagnostic =
            "HidHide persistent state changed outside the owned transaction; stale snapshot restore refused";
        if (!sessionCleared) diagnostic += "; session blacklist clear failed: " + sessionClearError;
        return result(HidHideSessionResultCode::RecoveryRequired,
                      boundedDiagnostic(std::move(diagnostic)));
    }

    if (!platform_->writeState(plan_->before, error)) {
        phase_ = HidHideSessionPhase::RecoveryRequired;
        std::string diagnostic = "HidHide snapshot restore failed: " + error;
        if (!sessionCleared) diagnostic += "; session blacklist clear failed: " + sessionClearError;
        return result(HidHideSessionResultCode::RecoveryRequired,
                      boundedDiagnostic(std::move(diagnostic)));
    }

    HidHideSessionSnapshot restored;
    if (!platform_->readState(restored, error) ||
        !sameSnapshot(restored, plan_->before)) {
        phase_ = HidHideSessionPhase::RecoveryRequired;
        std::string diagnostic = error.empty()
            ? "HidHide snapshot restore verification failed"
            : "HidHide snapshot restore verification failed: " + error;
        if (!sessionCleared) diagnostic += "; session blacklist clear failed: " + sessionClearError;
        return result(HidHideSessionResultCode::RecoveryRequired,
                      boundedDiagnostic(std::move(diagnostic)));
    }
    if (!sessionCleared) {
        phase_ = HidHideSessionPhase::RecoveryRequired;
        return result(HidHideSessionResultCode::RecoveryRequired,
                      boundedDiagnostic("persistent state restored but session blacklist clear failed: " + sessionClearError));
    }

    plan_.reset();
    phase_ = HidHideSessionPhase::Idle;
    return result(expired ? HidHideSessionResultCode::Expired
                          : HidHideSessionResultCode::Ok,
                  expired ? "HidHide session expiry cleared process-lifetime entries and restored prior state"
                          : "HidHide session cleared process-lifetime entries and restored prior state");
}

HidHideSessionResult HidHideSessionTransaction::result(
    HidHideSessionResultCode code,
    std::string diagnostic) const {
    return {code, phase_, plan_, boundedDiagnostic(std::move(diagnostic))};
}

bool validateHidHideSessionRequest(const HidHideSessionRequest& request,
                                   std::string& error) {
    error.clear();
    if (request.generation == 0) {
        error = "HidHide session generation must be nonzero";
        return false;
    }
    if (!request.replacementPathVerified || !request.recoveryReady) {
        error = "replacement input path and independent recovery must be verified before session cloaking";
        return false;
    }
    if (request.deviceInstanceIds.empty() ||
        request.deviceInstanceIds.size() > kHidHideSessionMaxRequestedDevices) {
        error = "HidHide session requires between one and sixteen explicit device instance IDs";
        return false;
    }
    if (request.allowedApplications.empty() ||
        request.allowedApplications.size() > kHidHideSessionMaxRequestedApplications) {
        error = "HidHide session requires between one and eight explicit allowed applications";
        return false;
    }
    for (const auto& value : request.deviceInstanceIds) {
        if (!boundedText(value)) {
            error = "HidHide device instance IDs must be nonempty and bounded";
            return false;
        }
    }
    for (const auto& value : request.allowedApplications) {
        if (!boundedText(value)) {
            error = "HidHide allowed application paths must be nonempty and bounded";
            return false;
        }
    }
    if (normalizedUnique(request.deviceInstanceIds, false).size() !=
        request.deviceInstanceIds.size()) {
        error = "HidHide session device instance IDs must be unique";
        return false;
    }
    if (normalizedUnique(request.allowedApplications, true).size() !=
        request.allowedApplications.size()) {
        error = "HidHide session allowed application paths must be unique";
        return false;
    }
    if (request.expiryMilliseconds < kHidHideSessionMinExpiryMs ||
        request.expiryMilliseconds > kHidHideSessionMaxExpiryMs) {
        error = "HidHide session expiry must be between one and sixty seconds";
        return false;
    }
    if (!request.spareRecoveryInputPresent &&
        request.expiryMilliseconds > kHidHideSessionNoSpareInputMaxExpiryMs) {
        error = "without a spare recovery input, HidHide session expiry must not exceed ten seconds";
        return false;
    }
    return true;
}

bool equivalentHidHideSessionSnapshots(
    const HidHideSessionSnapshot& left,
    const HidHideSessionSnapshot& right) {
    return sameSnapshot(left, right);
}

HidHideSessionSnapshot makeHidHideSessionAppliedState(
    const HidHideSessionSnapshot& before,
    const HidHideSessionRequest& request) {
    auto applied = normalizedSnapshot(before);
    applied.active = true;
    // The persistent blacklist is preserved byte-for-byte at the logical
    // identity level. Requested HydraSeat devices are owned only by HidHide's
    // process-lifetime session blacklist.
    mergeUnique(applied.allowedApplications,
                request.allowedApplications, true);
    return applied;
}

watchdog::RollbackActionDescriptor makeHidHideSessionRollbackAction(
    std::uint32_t actionId,
    std::uint32_t activationOrdinal,
    std::uint32_t timeoutMilliseconds,
    std::uint64_t generation,
    std::uint64_t snapshotResourceId) noexcept {
    watchdog::RollbackActionDescriptor action;
    action.actionId = actionId;
    action.kind = watchdog::RollbackActionKind::RestoreSnapshotState;
    action.activationOrdinal = activationOrdinal;
    action.timeoutMilliseconds = timeoutMilliseconds;
    action.generation = generation;
    action.resourceId = snapshotResourceId;
    return action;
}

std::string_view hidHideSessionPhaseName(HidHideSessionPhase phase) noexcept {
    switch (phase) {
        case HidHideSessionPhase::Idle: return "idle";
        case HidHideSessionPhase::Prepared: return "prepared";
        case HidHideSessionPhase::Active: return "active";
        case HidHideSessionPhase::RollingBack: return "rolling-back";
        case HidHideSessionPhase::RecoveryRequired: return "recovery-required";
    }
    return "unknown";
}

std::string_view hidHideSessionResultCodeName(
    HidHideSessionResultCode code) noexcept {
    switch (code) {
        case HidHideSessionResultCode::Ok: return "ok";
        case HidHideSessionResultCode::AlreadySatisfied: return "already-satisfied";
        case HidHideSessionResultCode::InvalidRequest: return "invalid-request";
        case HidHideSessionResultCode::UnsupportedState: return "unsupported-state";
        case HidHideSessionResultCode::PhysicalGateRequired: return "physical-gate-required";
        case HidHideSessionResultCode::NativeMutationDisabled: return "native-mutation-disabled";
        case HidHideSessionResultCode::BackendFailure: return "backend-failure";
        case HidHideSessionResultCode::VerificationFailed: return "verification-failed";
        case HidHideSessionResultCode::RecoveryRequired: return "recovery-required";
        case HidHideSessionResultCode::Expired: return "expired";
        case HidHideSessionResultCode::RecoveryNotArmed: return "recovery-not-armed";
    }
    return "unknown";
}

} // namespace hydra
