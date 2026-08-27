#include "hydra/control_surface_model.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace hydra::control {
namespace {

bool validConfigurationShape(SeatId managementSeatId,
                             const std::vector<SeatConfig>& seats,
                             std::string* error) {
    if (managementSeatId == 0 || seats.empty()) {
        if (error) *error = "validated control configuration requires a Management Seat and Seats";
        return false;
    }
    std::set<SeatId> ids;
    bool managementPresent = false;
    for (const auto& seat : seats) {
        if (seat.seatId == 0 || !ids.insert(seat.seatId).second) {
            if (error) *error = "validated control configuration has duplicate or zero Seat ID";
            return false;
        }
        managementPresent = managementPresent || seat.seatId == managementSeatId;
    }
    if (!managementPresent) {
        if (error) *error = "validated Management Seat is not configured";
        return false;
    }
    return true;
}

std::optional<runtime::SeatRuntimeState> runtimeFor(
    const runtime::HostRuntimeSnapshot& snapshot, SeatId seatId) {
    const auto found = std::find_if(snapshot.seats.begin(), snapshot.seats.end(),
                                    [seatId](const auto& seat) {
                                        return seat.seatId == seatId;
                                    });
    if (found == snapshot.seats.end()) return std::nullopt;
    return *found;
}

} // namespace

bool ControlSurfaceModel::setValidatedConfiguration(
    SeatId managementSeatId, std::vector<SeatConfig> seats,
    StartupMode startupMode, std::string* error) {
    if (!validConfigurationShape(managementSeatId, seats, error)) return false;
    validatedManagementSeatId_ = managementSeatId;
    validatedSeats_ = std::move(seats);
    startupMode_ = startupMode;
    rebuild();
    return true;
}

void ControlSurfaceModel::setControlContext(SeatId callerSeatId,
                                            bool sameWindowsUserSession,
                                            bool authenticatedControlRole) noexcept {
    callerSeatId_ = callerSeatId;
    sameWindowsUserSession_ = sameWindowsUserSession;
    authenticatedControlRole_ = authenticatedControlRole;
    rebuild();
}

void ControlSurfaceModel::observeHostSnapshot(
    const runtime::HostRuntimeSnapshot& snapshot) {
    hostConnected_ = true;
    snapshot_ = snapshot;
    disconnectDiagnostic_.clear();
    rebuild();
}

void ControlSurfaceModel::markHostDisconnected(std::string diagnostic) {
    hostConnected_ = false;
    snapshot_.reset();
    disconnectDiagnostic_ = diagnostic.empty() ? "host state is unknown" : std::move(diagnostic);
    rebuild();
}

RuntimeDisplayMode ControlSurfaceModel::modeFor(
    const runtime::HostRuntimeSnapshot& snapshot) noexcept {
    if (snapshot.hostPhase == runtime::HostLifecyclePhase::ExitRequested ||
        snapshot.hostPhase == runtime::HostLifecyclePhase::Stopped) {
        return RuntimeDisplayMode::HostExitRequested;
    }
    switch (snapshot.sessionPhase) {
        case runtime::SeatSessionPhase::Idle:
            return RuntimeDisplayMode::BackgroundIdle;
        case runtime::SeatSessionPhase::Active:
            return RuntimeDisplayMode::SplitActive;
        case runtime::SeatSessionPhase::Degraded:
            return RuntimeDisplayMode::Degraded;
        case runtime::SeatSessionPhase::RecoveryRequired:
            return RuntimeDisplayMode::RecoveryRequired;
        case runtime::SeatSessionPhase::Planning:
        case runtime::SeatSessionPhase::Prepared:
        case runtime::SeatSessionPhase::Starting:
        case runtime::SeatSessionPhase::Stopping:
        case runtime::SeatSessionPhase::RollingBack:
            return RuntimeDisplayMode::Transitioning;
    }
    return RuntimeDisplayMode::Unknown;
}

void ControlSurfaceModel::rebuild() {
    ControlSurfaceState next;
    next.hostConnected = hostConnected_;
    next.startupMode = startupMode_;
    next.managementSeatId = validatedManagementSeatId_;

    if (!hostConnected_ || !snapshot_) {
        next.runtimeStateKnown = false;
        next.runtimeMode = RuntimeDisplayMode::Unknown;
        next.diagnostic = disconnectDiagnostic_.empty()
            ? "host state is unknown" : disconnectDiagnostic_;
        if (!validatedSeats_.empty()) {
            next.assignmentSource = AssignmentSource::ValidatedInactiveConfiguration;
            for (const auto& seat : validatedSeats_) {
                next.seats.push_back(SeatAssignmentView{seat, std::nullopt});
            }
        }
        state_ = std::move(next);
        return;
    }

    next.runtimeStateKnown = true;
    next.hostPhase = snapshot_->hostPhase;
    next.sessionPhase = snapshot_->sessionPhase;
    next.managementSeatId = snapshot_->managementSeatId;
    next.runtimeMode = modeFor(*snapshot_);
    next.diagnostic = snapshot_->diagnostic;

    if (!snapshot_->configuredSeats.empty()) {
        next.assignmentSource = AssignmentSource::AuthoritativeHost;
        for (const auto& config : snapshot_->configuredSeats) {
            next.seats.push_back(SeatAssignmentView{config, runtimeFor(*snapshot_, config.seatId)});
        }
    } else if (snapshot_->sessionPhase == runtime::SeatSessionPhase::Idle &&
               !validatedSeats_.empty()) {
        next.assignmentSource = AssignmentSource::ValidatedInactiveConfiguration;
        for (const auto& config : validatedSeats_) {
            next.seats.push_back(SeatAssignmentView{config, runtimeFor(*snapshot_, config.seatId)});
        }
    }

    GlobalControlPermission permission;
    permission.managementSeatId = snapshot_->managementSeatId;
    permission.callerSeatId = callerSeatId_;
    permission.sameWindowsUserSession = sameWindowsUserSession_;
    permission.authenticatedControlRole = authenticatedControlRole_;
    const bool allowed = permission.permitsGlobalMutation();

    if (!allowed || snapshot_->hostPhase != runtime::HostLifecyclePhase::Running) {
        state_ = std::move(next);
        return;
    }

    switch (snapshot_->sessionPhase) {
        case runtime::SeatSessionPhase::Idle:
            next.actions.start = snapshot_->profileLoaded;
            next.actions.reconfigure = true;
            next.actions.exitBackgroundHost = true;
            break;
        case runtime::SeatSessionPhase::Planning:
        case runtime::SeatSessionPhase::Prepared:
            next.actions.start = true;
            next.actions.stopAndReturnToWindows = true;
            next.actions.reconfigure = true;
            break;
        case runtime::SeatSessionPhase::Active:
        case runtime::SeatSessionPhase::Degraded:
            next.actions.stopAndReturnToWindows = true;
            next.actions.reconfigure = true;
            break;
        case runtime::SeatSessionPhase::Starting:
        case runtime::SeatSessionPhase::Stopping:
        case runtime::SeatSessionPhase::RollingBack:
            break;
        case runtime::SeatSessionPhase::RecoveryRequired:
            next.actions.emergencyReset = true;
            break;
    }
    state_ = std::move(next);
}

} // namespace hydra::control
