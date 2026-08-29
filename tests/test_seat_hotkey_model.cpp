#include "hydra/seat_hotkey_model.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace hydra;
using namespace hydra::seatui;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

SeatHotkeyContext context(SeatId seatId, SeatLauncherPhase phase,
                          std::uint64_t generation = 4u,
                          std::uint64_t sequence = 8u) {
    SeatHotkeyContext value;
    value.seatId = seatId;
    value.phase = phase;
    value.authorityGeneration = generation;
    value.transitionSequence = sequence;
    value.canReconnect = true;
    value.canEndPlaying = phase == SeatLauncherPhase::Playing ||
                          phase == SeatLauncherPhase::Warning;
    return value;
}

void testOwnSeatActionsAreBounded() {
    SeatHotkeyModel model(2u);
    SeatHotkeyDecision decision;
    std::string error;
    check(model.evaluate(context(2u, SeatLauncherPhase::Playing),
                         SeatHotkeyChord::EndPlaying, decision, &error) &&
              decision.action == SeatHotkeyAction::ConfirmEndPlaying &&
              decision.seatId == 2u && decision.requiresConfirmation,
          "End Playing hotkey remains assigned to own Seat and requires confirmation");
    check(model.evaluate(context(2u, SeatLauncherPhase::Recovery, 5u, 9u),
                         SeatHotkeyChord::RecoveryHelp, decision, &error) &&
              decision.action == SeatHotkeyAction::ShowRecoveryHelp,
          "recovery state exposes Seat-local help only");
    check(model.evaluate(context(2u, SeatLauncherPhase::Recovery, 5u, 9u),
                         SeatHotkeyChord::RefreshStatus, decision, &error) &&
              decision.action == SeatHotkeyAction::Resnapshot,
          "refresh hotkey requests bounded resnapshot");
}

void testCrossSeatAndStaleContextsFailClosed() {
    SeatHotkeyModel model(1u);
    SeatHotkeyDecision output{SeatHotkeyAction::ShowRecoveryHelp, 1u, true};
    const auto sentinel = output;
    std::string error;
    check(!model.evaluate(context(2u, SeatLauncherPhase::Playing),
                          SeatHotkeyChord::EndPlaying, output, &error) &&
              output.action == sentinel.action,
          "cross-Seat semantic hotkey cannot alter caller output");
    check(model.evaluate(context(1u, SeatLauncherPhase::Playing, 10u, 20u),
                         SeatHotkeyChord::EndPlaying, output, &error),
          "new authority context is accepted");
    output = sentinel;
    check(!model.evaluate(context(1u, SeatLauncherPhase::Playing, 9u, 19u),
                          SeatHotkeyChord::EndPlaying, output, &error) &&
              output.action == sentinel.action,
          "stale authority cannot produce a Seat command");
}

void testEmergencyResetHotkeyNeverExecutesGlobalReset() {
    SeatHotkeyModel model(2u);
    SeatHotkeyDecision decision;
    std::string error;
    check(model.evaluate(context(2u, SeatLauncherPhase::Disconnected, 0u, 0u),
                         SeatHotkeyChord::EmergencyResetHelp, decision, &error) &&
              decision.action == SeatHotkeyAction::ShowEmergencyResetHelp &&
              decision.requiresConfirmation,
          "emergency shortcut surfaces the independent reset path instead of executing it");
    check(seatHotkeyActionName(decision.action) == "ShowEmergencyResetHelp",
          "hotkey policy has no whole-machine stop/reconfigure action");
}

void testUnavailableActionIsNoOpNotFallback() {
    SeatHotkeyModel model(1u);
    SeatHotkeyDecision decision;
    std::string error;
    auto idle = context(1u, SeatLauncherPhase::Idle);
    idle.canEndPlaying = false;
    check(model.evaluate(idle, SeatHotkeyChord::EndPlaying, decision, &error) &&
              decision.action == SeatHotkeyAction::None && !decision.requiresConfirmation,
          "invalid End Playing context stays a no-op instead of falling back globally");
}

} // namespace

int main() {
    testOwnSeatActionsAreBounded();
    testCrossSeatAndStaleContextsFailClosed();
    testEmergencyResetHotkeyNeverExecutesGlobalReset();
    testUnavailableActionIsNoOpNotFallback();
    if (failures != 0) {
        std::cerr << failures << " Seat hotkey test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Seat hotkey policy tests passed.\n";
    return EXIT_SUCCESS;
}
