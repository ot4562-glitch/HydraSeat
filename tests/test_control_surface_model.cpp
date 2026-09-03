#include "hydra/control_surface_model.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace hydra;
using namespace hydra::control;
using namespace hydra::runtime;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<SeatConfig> config(std::wstring suffix = {}) {
    SeatConfig first;
    first.seatId = 1;
    first.name = L"Seat 1" + suffix;
    first.displayIds = {L"display-1a", L"display-1b"};
    first.primaryDisplayId = L"display-1a";
    first.keyboardIds = {L"keyboard-1"};
    first.mouseIds = {L"mouse-1"};
    first.controllerIds = {L"controller-1"};
    first.audioOutputEndpointId = L"audio-1";
    SeatConfig second;
    second.seatId = 2;
    second.name = L"Seat 2" + suffix;
    second.displayIds = {L"display-2"};
    second.primaryDisplayId = L"display-2";
    second.keyboardIds = {L"keyboard-2"};
    second.mouseIds = {L"mouse-2"};
    return {first, second};
}

HostRuntimeSnapshot hostSnapshot(SeatSessionPhase phase, SeatId managementSeatId = 1,
                                 std::vector<SeatConfig> authoritative = config(L" host")) {
    HostRuntimeSnapshot snapshot;
    snapshot.hostPhase = HostLifecyclePhase::Running;
    snapshot.sessionPhase = phase;
    snapshot.managementSeatId = managementSeatId;
    snapshot.profileLoaded = !authoritative.empty();
    snapshot.configuredSeats = std::move(authoritative);
    snapshot.seats = {{1, phase, "seat1"}, {2, phase, "seat2"}};
    SeatGameState firstGame;
    firstGame.seatId = 1;
    firstGame.phase = phase == SeatSessionPhase::Active
        ? SeatGamePhase::Playing : SeatGamePhase::Idle;
    SeatGameState secondGame;
    secondGame.seatId = 2;
    secondGame.phase = SeatGamePhase::Idle;
    snapshot.seatGames = {firstGame, secondGame};
    return snapshot;
}

void testDisconnectedUnknownAndValidatedAssignments() {
    ControlSurfaceModel model;
    std::string error;
    check(model.setValidatedConfiguration(1, config(), StartupMode::BackgroundIdle, &error),
          "validated inactive configuration is accepted");
    model.setControlContext(1, true, true);
    model.markHostDisconnected("named pipe unavailable");
    const auto& state = model.state();
    check(!state.runtimeStateKnown && state.runtimeMode == RuntimeDisplayMode::Unknown &&
              state.assignmentSource == AssignmentSource::ValidatedInactiveConfiguration &&
              state.seats.size() == 2u && state.diagnostic == "named pipe unavailable",
          "disconnected UI shows unknown runtime while retaining only validated inactive assignments");
    check(!state.actions.start && !state.actions.stopAndReturnToWindows &&
              !state.actions.reconfigure && !state.actions.exitBackgroundHost,
          "unknown host state disables optimistic global mutations");
}

void testIncompleteSeatHardwareRemainsValidConfiguration() {
    ControlSurfaceModel model;
    auto incomplete = config();
    for (auto& seat : incomplete) {
        seat.displayIds.clear();
        seat.primaryDisplayId.reset();
        seat.keyboardIds.clear();
        seat.mouseIds.clear();
        seat.controllerIds.clear();
        seat.audioOutputEndpointId.reset();
        seat.audioInputEndpointId.reset();
    }

    std::string error;
    check(model.setValidatedConfiguration(
              1u, incomplete, StartupMode::Manual, &error),
          "validated configuration accepts Seats with device categories set later");
    model.setControlContext(1u, true, true);
    model.markHostDisconnected("host unavailable while editing later");
    check(model.state().assignmentSource ==
              AssignmentSource::ValidatedInactiveConfiguration &&
              model.state().seats.size() == 2u &&
              model.state().seats[0].config.displayIds.empty() &&
              model.state().seats[0].config.keyboardIds.empty() &&
              model.state().seats[1].config.mouseIds.empty(),
          "incomplete Seat hardware remains representable without invented defaults");

    auto idle = hostSnapshot(SeatSessionPhase::Idle, 1u, incomplete);
    model.observeHostSnapshot(idle);
    check(model.state().runtimeMode == RuntimeDisplayMode::BackgroundIdle &&
              model.state().actions.start && model.state().actions.reconfigure,
          "control surface does not make complete hardware a prerequisite for a saved Idle profile");
}

void testV1SeatLimitFailsClosedBeforeHostMutation() {
    ControlSurfaceModel model;
    auto three = config();
    SeatConfig third;
    third.seatId = 3;
    third.name = L"Seat 3";
    three.push_back(third);
    std::string error;
    check(!model.setValidatedConfiguration(1, std::move(three), StartupMode::Manual,
                                           &error) &&
              error.find("more than two") != std::string::npos &&
              model.state().assignmentSource == AssignmentSource::None,
          "control surface rejects a third active v1 Seat before any Host mutation");
}

void testAuthoritativeActiveSnapshotOverridesInactiveMemory() {
    ControlSurfaceModel model;
    std::string error;
    check(model.setValidatedConfiguration(1, config(L" disk"), StartupMode::Manual, &error),
          "disk validated configuration loads");
    model.setControlContext(1, true, true);
    model.observeHostSnapshot(hostSnapshot(SeatSessionPhase::Active));
    const auto& active = model.state();
    check(active.runtimeStateKnown && active.runtimeMode == RuntimeDisplayMode::SplitActive &&
              active.assignmentSource == AssignmentSource::AuthoritativeHost &&
              active.seats.size() == 2u && active.seats.front().config.name == L"Seat 1 host" &&
              active.seats[0].game &&
              active.seats[0].game->phase == SeatGamePhase::Playing &&
              active.seats[1].game &&
              active.seats[1].game->phase == SeatGamePhase::Idle,
          "Active UI assignments and per-Seat game phases come from the authoritative host snapshot");
    check(active.actions.stopAndReturnToWindows && active.actions.reconfigure &&
              !active.actions.start && !active.actions.exitBackgroundHost,
          "Active Management Seat exposes Stop/Reconfigure without Exit Host or duplicate Start");

    ControlSurfaceModel reopened;
    check(reopened.setValidatedConfiguration(1, config(L" stale"), StartupMode::Manual, &error),
          "reopened UI may start with stale inactive config before resnapshot");
    reopened.setControlContext(1, true, true);
    reopened.observeHostSnapshot(hostSnapshot(SeatSessionPhase::Active));
    check(reopened.state().runtimeMode == RuntimeDisplayMode::SplitActive &&
              reopened.state().seats.front().config.name == L"Seat 1 host" &&
              reopened.state().seats.front().game &&
              reopened.state().seats.front().game->phase == SeatGamePhase::Playing,
          "UI kill/reopen resnapshot restores Active state and per-Seat game phases");
}

void testSeatGameFaultAndReturnPolicyProjection() {
    ControlSurfaceModel model;
    std::string error;
    check(model.setValidatedConfiguration(1, config(), StartupMode::Manual, &error),
          "Seat fault projection config loads");
    model.setControlContext(1, true, true);

    auto degraded = hostSnapshot(SeatSessionPhase::Active);
    degraded.seatGames[1].phase = SeatGamePhase::Degraded;
    degraded.seatGames[1].diagnostic = "controlled target exited unexpectedly";
    model.observeHostSnapshot(degraded);
    check(model.state().runtimeMode == RuntimeDisplayMode::Degraded &&
              model.state().seats[0].game->phase == SeatGamePhase::Playing &&
              model.state().seats[1].game->phase == SeatGamePhase::Degraded &&
              model.state().actions.stopAndReturnToWindows,
          "one degraded Seat is visible without hiding the other Playing Seat");

    auto recovery = degraded;
    recovery.seatGames[1].phase = SeatGamePhase::RecoveryRequired;
    model.observeHostSnapshot(recovery);
    check(model.state().runtimeMode == RuntimeDisplayMode::RecoveryRequired &&
              model.state().actions.emergencyReset &&
              !model.state().actions.start &&
              !model.state().actions.reconfigure &&
              !model.state().actions.stopAndReturnToWindows,
          "Seat-local RecoveryRequired exposes only the fail-closed global reset action");

    auto bothEnded = hostSnapshot(SeatSessionPhase::Active);
    bothEnded.seatGames[0].phase = SeatGamePhase::Idle;
    bothEnded.wholeMachineReturnRequested = true;
    model.observeHostSnapshot(bothEnded);
    check(model.state().runtimeMode == RuntimeDisplayMode::SplitActive &&
              model.state().wholeMachineReturnRequested &&
              model.state().actions.stopAndReturnToWindows,
          "both-ended policy is projected without inventing an automatic global rollback");
}

void testIdleActionsAuthorityAndManagementTransfer() {
    ControlSurfaceModel model;
    std::string error;
    check(model.setValidatedConfiguration(1, config(), StartupMode::AutoActivateValidatedSession, &error),
          "validated configuration accepts startup mode selection");
    model.setControlContext(1, true, true);
    model.observeHostSnapshot(hostSnapshot(SeatSessionPhase::Idle, 1));
    check(model.state().actions.start && model.state().actions.reconfigure &&
              model.state().actions.exitBackgroundHost &&
              !model.state().actions.stopAndReturnToWindows,
          "verified Idle Management Seat exposes Start/Reconfigure/Exit Host but no redundant Stop");

    model.setControlContext(2, true, true);
    check(!model.state().actions.start && !model.state().actions.reconfigure &&
              !model.state().actions.exitBackgroundHost,
          "other Seat remains read-mostly for whole-machine operations");

    model.observeHostSnapshot(hostSnapshot(SeatSessionPhase::Idle, 2));
    check(model.state().managementSeatId == 2 && model.state().actions.start,
          "authoritative Management Seat transfer immediately updates UI permission source");
}

void testTransitionRecoveryAndHostExitPresentation() {
    ControlSurfaceModel model;
    std::string error;
    check(model.setValidatedConfiguration(1, config(), StartupMode::Manual, &error),
          "transition model config loads");
    model.setControlContext(1, true, true);

    auto stopping = hostSnapshot(SeatSessionPhase::Stopping);
    stopping.mutationInProgress = true;
    model.observeHostSnapshot(stopping);
    check(model.state().runtimeMode == RuntimeDisplayMode::Transitioning &&
              !model.state().actions.start && !model.state().actions.stopAndReturnToWindows &&
              !model.state().actions.reconfigure,
          "Stopping/RollingBack presentation disables competing mutations");

    model.observeHostSnapshot(hostSnapshot(SeatSessionPhase::RecoveryRequired));
    check(model.state().runtimeMode == RuntimeDisplayMode::RecoveryRequired &&
              model.state().actions.emergencyReset && !model.state().actions.start &&
              !model.state().actions.reconfigure,
          "RecoveryRequired exposes reset while unsafe Start/Reconfigure remain blocked");

    auto exiting = hostSnapshot(SeatSessionPhase::Idle);
    exiting.hostPhase = HostLifecyclePhase::ExitRequested;
    model.observeHostSnapshot(exiting);
    check(model.state().runtimeMode == RuntimeDisplayMode::HostExitRequested &&
              !model.state().actions.exitBackgroundHost,
          "host exit request is shown explicitly without offering another mutation");
}

} // namespace

int main() {
    testDisconnectedUnknownAndValidatedAssignments();
    testIncompleteSeatHardwareRemainsValidConfiguration();
    testV1SeatLimitFailsClosedBeforeHostMutation();
    testAuthoritativeActiveSnapshotOverridesInactiveMemory();
    testSeatGameFaultAndReturnPolicyProjection();
    testIdleActionsAuthorityAndManagementTransfer();
    testTransitionRecoveryAndHostExitPresentation();
    if (failures != 0) {
        std::cerr << failures << " control-surface model test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "control-surface model tests passed\n";
    return EXIT_SUCCESS;
}
