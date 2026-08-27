#pragma once

#include "hydra/management_seat.hpp"
#include "hydra/runtime_state.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hydra::control {

enum class StartupMode : std::uint8_t {
    Manual = 0,
    BackgroundIdle = 1,
    AutoActivateValidatedSession = 2,
};

enum class AssignmentSource : std::uint8_t {
    None = 0,
    AuthoritativeHost = 1,
    ValidatedInactiveConfiguration = 2,
};

enum class RuntimeDisplayMode : std::uint8_t {
    Unknown = 0,
    BackgroundIdle = 1,
    SplitActive = 2,
    Degraded = 3,
    Transitioning = 4,
    RecoveryRequired = 5,
    HostExitRequested = 6,
};

struct ControlSurfaceActions {
    bool start{false};
    bool stopAndReturnToWindows{false};
    bool reconfigure{false};
    bool emergencyReset{false};
    bool exitBackgroundHost{false};
};

struct SeatAssignmentView {
    SeatConfig config;
    std::optional<runtime::SeatRuntimeState> runtime;
};

struct ControlSurfaceState {
    bool hostConnected{false};
    bool runtimeStateKnown{false};
    AssignmentSource assignmentSource{AssignmentSource::None};
    RuntimeDisplayMode runtimeMode{RuntimeDisplayMode::Unknown};
    StartupMode startupMode{StartupMode::Manual};
    SeatId managementSeatId{1};
    std::optional<runtime::HostLifecyclePhase> hostPhase;
    std::optional<runtime::SeatSessionPhase> sessionPhase;
    std::vector<SeatAssignmentView> seats;
    ControlSurfaceActions actions;
    std::string diagnostic;
};

class ControlSurfaceModel {
public:
    bool setValidatedConfiguration(SeatId managementSeatId,
                                   std::vector<SeatConfig> seats,
                                   StartupMode startupMode = StartupMode::Manual,
                                   std::string* error = nullptr);
    void setControlContext(SeatId callerSeatId,
                           bool sameWindowsUserSession,
                           bool authenticatedControlRole) noexcept;
    void observeHostSnapshot(const runtime::HostRuntimeSnapshot& snapshot);
    void markHostDisconnected(std::string diagnostic = "host disconnected");

    const ControlSurfaceState& state() const noexcept { return state_; }

private:
    static RuntimeDisplayMode modeFor(const runtime::HostRuntimeSnapshot& snapshot) noexcept;
    void rebuild();

    SeatId validatedManagementSeatId_{1};
    std::vector<SeatConfig> validatedSeats_;
    StartupMode startupMode_{StartupMode::Manual};
    SeatId callerSeatId_{0};
    bool sameWindowsUserSession_{false};
    bool authenticatedControlRole_{false};
    bool hostConnected_{false};
    std::optional<runtime::HostRuntimeSnapshot> snapshot_;
    std::string disconnectDiagnostic_{"host state is unknown"};
    ControlSurfaceState state_;
};

} // namespace hydra::control
