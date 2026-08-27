#include "hydra/session_control_transition.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace hydra::control {
namespace {

bool containsSeat(const std::vector<SeatConfig>& profile, SeatId seatId) noexcept {
    return std::any_of(profile.begin(), profile.end(), [seatId](const SeatConfig& seat) {
        return seat.seatId == seatId;
    });
}

bool draftShapeValid(SeatId managementSeatId, const std::vector<SeatConfig>& profile,
                     std::string* error) {
    if (managementSeatId == 0 || profile.empty()) {
        if (error) *error = "draft requires a nonzero Management Seat and at least one Seat";
        return false;
    }
    std::set<SeatId> ids;
    for (const auto& seat : profile) {
        if (seat.seatId == 0 || !ids.insert(seat.seatId).second) {
            if (error) *error = "draft Seat IDs must be unique and nonzero";
            return false;
        }
    }
    if (!containsSeat(profile, managementSeatId)) {
        if (error) *error = "draft Management Seat must reference a configured Seat";
        return false;
    }
    return true;
}

} // namespace

bool SessionControlTransition::safeInactive(
    const runtime::HostRuntimeSnapshot& snapshot) noexcept {
    return snapshot.hostPhase == runtime::HostLifecyclePhase::Running &&
           snapshot.sessionPhase == runtime::SeatSessionPhase::Idle &&
           !snapshot.mutationInProgress;
}

bool SessionControlTransition::transitionInProgress(
    const runtime::HostRuntimeSnapshot& snapshot) noexcept {
    return snapshot.mutationInProgress ||
           snapshot.sessionPhase == runtime::SeatSessionPhase::Stopping ||
           snapshot.sessionPhase == runtime::SeatSessionPhase::RollingBack;
}

bool SessionControlTransition::permissionAllowed(
    const GlobalControlPermission& permission,
    const runtime::HostRuntimeSnapshot& snapshot) noexcept {
    return permission.managementSeatId == snapshot.managementSeatId &&
           permission.permitsGlobalMutation();
}

void SessionControlTransition::observeSnapshot(
    const runtime::HostRuntimeSnapshot& snapshot) {
    snapshot_ = snapshot;
    diagnostic_ = snapshot.diagnostic;
    if (snapshot.sessionPhase == runtime::SeatSessionPhase::RecoveryRequired) {
        phase_ = SessionControlUiPhase::RecoveryRequired;
        if (diagnostic_.empty()) diagnostic_ = "runtime recovery is required";
        return;
    }
    if (phase_ == SessionControlUiPhase::HostUnknown ||
        phase_ == SessionControlUiPhase::Viewing ||
        (phase_ == SessionControlUiPhase::RecoveryRequired && safeInactive(snapshot))) {
        phase_ = SessionControlUiPhase::Viewing;
    }
}

void SessionControlTransition::markHostDisconnected(std::string diagnostic) {
    snapshot_.reset();
    phase_ = SessionControlUiPhase::HostUnknown;
    diagnostic_ = diagnostic.empty() ? "host state is unknown" : std::move(diagnostic);
}

SessionControlRequest SessionControlTransition::requestMutation(
    hostipc::MessageType message, SessionControlUiPhase pendingPhase,
    const GlobalControlPermission& permission, bool requireIdle) {
    SessionControlRequest request;
    request.message = message;
    if (!snapshot_) {
        request.diagnostic = "host state is unknown; resnapshot before mutating runtime";
        return request;
    }
    if (snapshot_->sessionPhase == runtime::SeatSessionPhase::RecoveryRequired ||
        phase_ == SessionControlUiPhase::RecoveryRequired) {
        request.diagnostic = "runtime recovery is required before this operation";
        return request;
    }
    if (!permissionAllowed(permission, *snapshot_)) {
        request.diagnostic = "global runtime operation requires current Management Seat authority";
        return request;
    }
    if (transitionInProgress(*snapshot_)) {
        request.alreadyPending = true;
        request.diagnostic = "a verified runtime transition is already in progress";
        return request;
    }
    if (requireIdle && !safeInactive(*snapshot_)) {
        request.diagnostic = "background host exit requires verified Idle state first";
        return request;
    }
    if (message == hostipc::MessageType::StopAndReturnToWindows && safeInactive(*snapshot_)) {
        request.alreadyPending = true;
        request.diagnostic = "ordinary Windows state is already verified";
        return request;
    }
    request.send = true;
    phase_ = pendingPhase;
    diagnostic_.clear();
    return request;
}

SessionControlRequest SessionControlTransition::requestStop(
    const GlobalControlPermission& permission) {
    return requestMutation(hostipc::MessageType::StopAndReturnToWindows,
                           SessionControlUiPhase::StopPending, permission, false);
}

SessionControlRequest SessionControlTransition::requestReconfigure(
    const GlobalControlPermission& permission) {
    return requestMutation(hostipc::MessageType::BeginReconfigure,
                           SessionControlUiPhase::ReconfigureStopPending, permission, false);
}

SessionControlRequest SessionControlTransition::requestExitHost(
    const GlobalControlPermission& permission) {
    return requestMutation(hostipc::MessageType::ExitHostWhenIdle,
                           SessionControlUiPhase::Viewing, permission, true);
}

bool SessionControlTransition::observeCommandResult(
    SessionControlIntent intent, const runtime::RuntimeCommandResult& result) {
    snapshot_ = result.snapshot;
    diagnostic_ = result.diagnostic;
    if (result.code == runtime::RuntimeResultCode::RollbackFailure ||
        result.code == runtime::RuntimeResultCode::RecoveryRequired ||
        result.snapshot.sessionPhase == runtime::SeatSessionPhase::RecoveryRequired) {
        phase_ = SessionControlUiPhase::RecoveryRequired;
        return false;
    }
    if (!result.succeeded()) {
        phase_ = SessionControlUiPhase::Viewing;
        return false;
    }

    switch (intent) {
        case SessionControlIntent::StopAndReturnToWindows:
            if (!safeInactive(result.snapshot)) {
                phase_ = SessionControlUiPhase::RecoveryRequired;
                diagnostic_ = "Stop result did not prove verified Idle state";
                return false;
            }
            phase_ = SessionControlUiPhase::Viewing;
            return true;
        case SessionControlIntent::Reconfigure:
            if (!safeInactive(result.snapshot)) {
                phase_ = SessionControlUiPhase::RecoveryRequired;
                diagnostic_ = "Reconfigure result did not prove safe inactive state";
                return false;
            }
            generationBeforeReconfigure_ = result.snapshot.generation;
            draft_.reset();
            phase_ = SessionControlUiPhase::EditorReady;
            return true;
        case SessionControlIntent::ExitHostWhenIdle:
            if (result.snapshot.hostPhase != runtime::HostLifecyclePhase::ExitRequested &&
                result.snapshot.hostPhase != runtime::HostLifecyclePhase::Stopped) {
                diagnostic_ = "ExitHostWhenIdle result did not confirm host exit state";
                return false;
            }
            phase_ = SessionControlUiPhase::Viewing;
            return true;
    }
    return false;
}

bool SessionControlTransition::beginDraft(SeatId managementSeatId,
                                          std::vector<SeatConfig> profile,
                                          std::string* error) {
    if (!configurationEditingAllowed() || !snapshot_ || !safeInactive(*snapshot_)) {
        if (error) *error = "configuration editor is not available before verified Idle reconfigure";
        return false;
    }
    if (!draftShapeValid(managementSeatId, profile, error)) return false;
    ReconfigureDraft draft;
    draft.savedManagementSeatId = managementSeatId;
    draft.savedProfile = profile;
    draft.draftManagementSeatId = managementSeatId;
    draft.draftProfile = std::move(profile);
    draft_ = std::move(draft);
    return true;
}

bool SessionControlTransition::replaceDraft(SeatId managementSeatId,
                                            std::vector<SeatConfig> profile,
                                            std::string* error) {
    if (!configurationEditingAllowed() || !draft_) {
        if (error) *error = "no editable reconfiguration draft is active";
        return false;
    }
    if (!draftShapeValid(managementSeatId, profile, error)) return false;
    draft_->draftManagementSeatId = managementSeatId;
    draft_->draftProfile = std::move(profile);
    draft_->dirty = draft_->draftManagementSeatId != draft_->savedManagementSeatId ||
                    draft_->draftProfile != draft_->savedProfile;
    draft_->committed = false;
    return true;
}

void SessionControlTransition::cancelDraft() noexcept {
    if (!draft_) return;
    draft_->draftManagementSeatId = draft_->savedManagementSeatId;
    draft_->draftProfile = draft_->savedProfile;
    draft_->dirty = false;
    draft_->committed = false;
    if (snapshot_ && safeInactive(*snapshot_)) phase_ = SessionControlUiPhase::EditorReady;
}

std::optional<hostipc::ProfilePayload> SessionControlTransition::requestApplyDraft(
    const GlobalControlPermission& permission, std::string* error) {
    if (!snapshot_ || !configurationEditingAllowed() || !safeInactive(*snapshot_) || !draft_) {
        if (error) *error = "profile apply requires editor-ready verified Idle state";
        return std::nullopt;
    }
    if (!permissionAllowed(permission, *snapshot_)) {
        if (error) *error = "profile apply requires current Management Seat authority";
        return std::nullopt;
    }
    hostipc::ProfilePayload payload;
    payload.managementSeatId = draft_->draftManagementSeatId;
    payload.seats = draft_->draftProfile;
    if (hostipc::encodeProfilePayload(payload).empty()) {
        if (error) *error = "draft profile exceeds bounded host protocol constraints";
        return std::nullopt;
    }
    phase_ = SessionControlUiPhase::ProfileApplyPending;
    return payload;
}

bool SessionControlTransition::observeProfileApplied(
    const runtime::RuntimeCommandResult& result, std::string* error) {
    snapshot_ = result.snapshot;
    diagnostic_ = result.diagnostic;
    if (result.code == runtime::RuntimeResultCode::RecoveryRequired ||
        result.snapshot.sessionPhase == runtime::SeatSessionPhase::RecoveryRequired) {
        phase_ = SessionControlUiPhase::RecoveryRequired;
        if (error) *error = result.diagnostic;
        return false;
    }
    if (!result.succeeded() || !safeInactive(result.snapshot)) {
        phase_ = SessionControlUiPhase::EditorReady;
        if (error) *error = result.diagnostic.empty()
            ? "edited profile was not accepted in verified Idle state" : result.diagnostic;
        return false;
    }
    if (!draft_) {
        phase_ = SessionControlUiPhase::EditorReady;
        if (error) *error = "profile result arrived without an active draft";
        return false;
    }
    draft_->savedManagementSeatId = draft_->draftManagementSeatId;
    draft_->savedProfile = draft_->draftProfile;
    draft_->dirty = false;
    draft_->committed = true;
    phase_ = SessionControlUiPhase::EditorReady;
    return true;
}

SessionControlRequest SessionControlTransition::requestPlanAfterSave(
    const GlobalControlPermission& permission) {
    SessionControlRequest request;
    request.message = hostipc::MessageType::PlanSession;
    if (!snapshot_ || !safeInactive(*snapshot_)) {
        request.diagnostic = "new plan requires verified Idle state";
        return request;
    }
    if (!permissionAllowed(permission, *snapshot_)) {
        request.diagnostic = "new plan requires current Management Seat authority";
        return request;
    }
    if (!draft_ || !draft_->committed) {
        request.diagnostic = "edited profile must be accepted before compiling a fresh plan";
        return request;
    }
    request.send = true;
    phase_ = SessionControlUiPhase::PlanPending;
    return request;
}

bool SessionControlTransition::observePlanResult(
    const runtime::RuntimeCommandResult& result) {
    snapshot_ = result.snapshot;
    diagnostic_ = result.diagnostic;
    if (!result.succeeded() ||
        result.snapshot.sessionPhase != runtime::SeatSessionPhase::Planning ||
        result.snapshot.generation <= generationBeforeReconfigure_) {
        phase_ = result.snapshot.sessionPhase == runtime::SeatSessionPhase::RecoveryRequired
            ? SessionControlUiPhase::RecoveryRequired : SessionControlUiPhase::EditorReady;
        return false;
    }
    lastCommittedPlanGeneration_ = result.snapshot.generation;
    phase_ = SessionControlUiPhase::PlanReady;
    return true;
}

SessionControlRequest SessionControlTransition::requestStartAfterPlan(
    const GlobalControlPermission& permission) {
    SessionControlRequest request;
    request.message = hostipc::MessageType::StartSession;
    if (!snapshot_ || snapshot_->sessionPhase != runtime::SeatSessionPhase::Planning ||
        lastCommittedPlanGeneration_ == 0 ||
        snapshot_->generation != lastCommittedPlanGeneration_) {
        request.diagnostic = "Start requires the accepted edited profile's fresh plan";
        return request;
    }
    if (!permissionAllowed(permission, *snapshot_)) {
        request.diagnostic = "Start requires current Management Seat authority";
        return request;
    }
    request.send = true;
    phase_ = SessionControlUiPhase::StartPending;
    return request;
}

bool SessionControlTransition::observeStartResult(
    const runtime::RuntimeCommandResult& result) {
    snapshot_ = result.snapshot;
    diagnostic_ = result.diagnostic;
    if (result.code == runtime::RuntimeResultCode::RollbackFailure ||
        result.code == runtime::RuntimeResultCode::RecoveryRequired ||
        result.snapshot.sessionPhase == runtime::SeatSessionPhase::RecoveryRequired) {
        phase_ = SessionControlUiPhase::RecoveryRequired;
        return false;
    }
    if (!result.succeeded() ||
        (result.snapshot.sessionPhase != runtime::SeatSessionPhase::Active &&
         result.snapshot.sessionPhase != runtime::SeatSessionPhase::Degraded)) {
        phase_ = SessionControlUiPhase::PlanReady;
        return false;
    }
    phase_ = SessionControlUiPhase::Viewing;
    return true;
}

} // namespace hydra::control
