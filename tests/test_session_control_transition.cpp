#include "hydra/session_control_transition.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace hydra;
using namespace hydra::control;
using namespace hydra::hostipc;
using namespace hydra::runtime;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<SeatConfig> profile() {
    SeatConfig first;
    first.seatId = 1;
    first.name = L"Seat 1";
    first.displayIds = {L"display-a"};
    first.primaryDisplayId = L"display-a";
    SeatConfig second;
    second.seatId = 2;
    second.name = L"Seat 2";
    second.displayIds = {L"display-b"};
    second.primaryDisplayId = L"display-b";
    return {first, second};
}

HostRuntimeSnapshot snapshot(SeatSessionPhase phase, std::uint64_t generation = 4,
                             SeatId managementSeatId = 1, bool mutating = false) {
    HostRuntimeSnapshot value;
    value.hostPhase = HostLifecyclePhase::Running;
    value.sessionPhase = phase;
    value.generation = generation;
    value.managementSeatId = managementSeatId;
    value.profileLoaded = true;
    value.mutationInProgress = mutating;
    value.seats = {{1, phase, {}}, {2, phase, {}}};
    return value;
}

RuntimeCommandResult result(RuntimeResultCode code, SeatSessionPhase phase,
                            std::uint64_t generation = 4,
                            RuntimeCommand command = RuntimeCommand::BeginReconfigure,
                            SeatId managementSeatId = 1) {
    RuntimeCommandResult value;
    value.code = code;
    value.snapshot = snapshot(phase, generation, managementSeatId);
    RuntimeTransition transition;
    transition.sequence = 1;
    transition.correlationId = 9;
    transition.command = command;
    transition.to = phase;
    transition.result = code;
    value.snapshot.lastTransition = transition;
    return value;
}

GlobalControlPermission permission(SeatId managementSeatId = 1,
                                   SeatId callerSeatId = 1) {
    GlobalControlPermission value;
    value.managementSeatId = managementSeatId;
    value.callerSeatId = callerSeatId;
    value.sameWindowsUserSession = true;
    value.authenticatedControlRole = true;
    return value;
}

void testStopAndDuplicateTransition() {
    SessionControlTransition transition;
    transition.observeSnapshot(snapshot(SeatSessionPhase::Active));
    auto stop = transition.requestStop(permission());
    check(stop.send && stop.message == MessageType::StopAndReturnToWindows &&
              transition.phase() == SessionControlUiPhase::StopPending,
          "Active Stop requests the verified host Stop transition");

    transition.observeSnapshot(snapshot(SeatSessionPhase::Stopping, 4, 1, true));
    const auto duplicate = transition.requestStop(permission());
    check(!duplicate.send && duplicate.alreadyPending,
          "duplicate Stop during Stopping is idempotent and does not create another mutation");

    const auto stopped = result(RuntimeResultCode::Ok, SeatSessionPhase::Idle, 4,
                                RuntimeCommand::StopAndReturnToWindows);
    check(transition.observeCommandResult(SessionControlIntent::StopAndReturnToWindows, stopped) &&
              transition.phase() == SessionControlUiPhase::Viewing,
          "Stop is considered complete only after authoritative verified Idle");

    const auto alreadyIdle = transition.requestStop(permission());
    check(!alreadyIdle.send && alreadyIdle.alreadyPending,
          "Stop while already verified Idle is a no-op");
}

void testReconfigureDraftCancelInvalidAndFreshStart() {
    SessionControlTransition transition;
    transition.observeSnapshot(snapshot(SeatSessionPhase::Active, 10));
    const auto requested = transition.requestReconfigure(permission());
    check(requested.send && requested.message == MessageType::BeginReconfigure,
          "Reconfigure requests its distinct host transition");

    const auto idle = result(RuntimeResultCode::Ok, SeatSessionPhase::Idle, 10,
                             RuntimeCommand::BeginReconfigure);
    check(transition.observeCommandResult(SessionControlIntent::Reconfigure, idle) &&
              transition.configurationEditingAllowed(),
          "editor opens only after Reconfigure proves safe inactive state");

    std::string error;
    auto saved = profile();
    check(transition.beginDraft(1, saved, &error),
          "editor snapshots the last valid saved profile before mutation");
    auto edited = saved;
    edited[0].name = L"Seat 1 edited";
    check(transition.replaceDraft(1, edited, &error) &&
              transition.draft() && transition.draft()->dirty,
          "draft changes are isolated from the saved profile");

    transition.cancelDraft();
    check(transition.draft() && !transition.draft()->dirty &&
              transition.draft()->draftProfile == saved &&
              transition.draft()->savedProfile == saved,
          "reconfigure cancel restores draft and leaves prior saved profile untouched");

    auto invalid = saved;
    invalid[0].primaryDisplayId = L"not-owned";
    check(transition.replaceDraft(1, invalid, &error),
          "structurally encodable but semantically invalid edits may remain a draft for validation");
    const auto payload = transition.requestApplyDraft(permission(), &error);
    check(payload.has_value() && transition.phase() == SessionControlUiPhase::ProfileApplyPending,
          "draft is sent through bounded host validation instead of mutating active state directly");

    auto rejected = result(RuntimeResultCode::InvalidProfile, SeatSessionPhase::Idle, 10,
                           RuntimeCommand::LoadProfile);
    rejected.diagnostic = "primary display is not assigned to Seat";
    check(!transition.observeProfileApplied(rejected, &error) &&
              transition.draft() && !transition.draft()->committed &&
              transition.draft()->savedProfile == saved,
          "invalid edited profile cannot replace the last valid profile");
    const auto blockedPlan = transition.requestPlanAfterSave(permission());
    check(!blockedPlan.send,
          "invalid/uncommitted edited profile cannot compile or Start a new session");

    edited = saved;
    edited[1].name = L"Seat 2 new";
    check(transition.replaceDraft(1, edited, &error), "valid edited draft replaces rejected draft");
    check(transition.requestApplyDraft(permission(), &error).has_value(),
          "valid draft may be submitted after a rejected edit");
    const auto accepted = result(RuntimeResultCode::Ok, SeatSessionPhase::Idle, 10,
                                 RuntimeCommand::LoadProfile);
    check(transition.observeProfileApplied(accepted, &error) &&
              transition.draft() && transition.draft()->committed &&
              transition.draft()->savedProfile == edited,
          "accepted edited profile transactionally becomes the new saved baseline");

    const auto planRequest = transition.requestPlanAfterSave(permission());
    check(planRequest.send && planRequest.message == MessageType::PlanSession,
          "Save + Start begins by compiling a fresh plan");
    const auto planned = result(RuntimeResultCode::Ok, SeatSessionPhase::Planning, 11,
                                RuntimeCommand::Plan);
    check(transition.observePlanResult(planned) &&
              transition.lastCommittedPlanGeneration() == 11 &&
              transition.phase() == SessionControlUiPhase::PlanReady,
          "new plan must have a generation newer than the pre-reconfigure plan");

    const auto startRequest = transition.requestStartAfterPlan(permission());
    check(startRequest.send && startRequest.message == MessageType::StartSession,
          "only the fresh accepted plan can issue Start");
    const auto active = result(RuntimeResultCode::Ok, SeatSessionPhase::Active, 11,
                               RuntimeCommand::Start);
    check(transition.observeStartResult(active) &&
              transition.phase() == SessionControlUiPhase::Viewing,
          "fresh plan returns the control surface to authoritative Active viewing");
}

void testRecoveryAuthorityDisconnectAndExitRules() {
    SessionControlTransition transition;
    transition.observeSnapshot(snapshot(SeatSessionPhase::Active, 2, 2));
    auto unauthorized = transition.requestReconfigure(permission(2, 1));
    check(!unauthorized.send,
          "non-Management Seat cannot request global Reconfigure");

    const auto exitActive = transition.requestExitHost(permission(2, 2));
    check(!exitActive.send,
          "Exit Background Host is blocked until session rollback reaches Idle");

    const auto requested = transition.requestReconfigure(permission(2, 2));
    check(requested.send, "Management Seat may start Reconfigure");
    auto failed = result(RuntimeResultCode::RollbackFailure,
                         SeatSessionPhase::RecoveryRequired, 2,
                         RuntimeCommand::BeginReconfigure, 2);
    failed.diagnostic = "rollback verification failed";
    check(!transition.observeCommandResult(SessionControlIntent::Reconfigure, failed) &&
              transition.phase() == SessionControlUiPhase::RecoveryRequired &&
              !transition.configurationEditingAllowed(),
          "rollback failure enters RecoveryRequired without false editor-ready/Stopped state");
    check(!transition.requestStop(permission(2, 2)).send,
          "unsafe ordinary transitions remain blocked while recovery is required");

    transition.markHostDisconnected("pipe closed during UI restart");
    check(!transition.hostStateKnown() && transition.phase() == SessionControlUiPhase::HostUnknown &&
              !transition.requestStop(permission(2, 2)).send,
          "UI disconnect reports unknown state instead of assuming Active or Stopped");

    transition.observeSnapshot(snapshot(SeatSessionPhase::Active, 2, 2));
    check(transition.hostStateKnown() && transition.phase() == SessionControlUiPhase::Viewing &&
              transition.snapshot()->sessionPhase == SeatSessionPhase::Active,
          "reopened UI resnapshots Active host without changing the session");
}

} // namespace

int main() {
    testStopAndDuplicateTransition();
    testReconfigureDraftCancelInvalidAndFreshStart();
    testRecoveryAuthorityDisconnectAndExitRules();
    if (failures != 0) {
        std::cerr << failures << " session-control transition test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "session-control transition tests passed\n";
    return EXIT_SUCCESS;
}
