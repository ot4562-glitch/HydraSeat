#include "hydra/seat_launcher_model.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace hydra;
using namespace hydra::runtime;
using namespace hydra::seatui;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

RuntimeSessionId session(std::uint8_t marker) {
    RuntimeSessionId value;
    value.bytes[0] = marker;
    return value;
}

HostRuntimeSnapshot snapshot(SeatGamePhase firstPhase = SeatGamePhase::Idle,
                             SeatGamePhase secondPhase = SeatGamePhase::Idle) {
    HostRuntimeSnapshot value;
    value.schemaVersion = 3;
    value.hostPhase = HostLifecyclePhase::Running;
    value.sessionId = session(1);
    value.generation = 4;
    value.transitionSequence = 8;
    value.profileLoaded = true;
    value.managementSeatId = 1;

    SeatConfig first;
    first.seatId = 1;
    first.name = L"Left Seat";
    first.displayIds = {L"display-left"};
    first.primaryDisplayId = first.displayIds.front();
    SeatConfig second;
    second.seatId = 2;
    second.name = L"Right Seat";
    second.displayIds = {L"display-right"};
    second.primaryDisplayId = second.displayIds.front();
    value.configuredSeats = {first, second};
    value.seats = {
        SeatRuntimeState{1, SeatSessionPhase::Idle, {}},
        SeatRuntimeState{2, SeatSessionPhase::Idle, {}}};
    value.seatGames = {
        SeatGameState{1, firstPhase, std::nullopt, 11, {}},
        SeatGameState{2, secondPhase, std::nullopt, 17, {}}};
    return value;
}

void testIndependentAuthoritativeViewsAndCommands() {
    auto authority = snapshot(SeatGamePhase::Playing, SeatGamePhase::RecoveryRequired);
    authority.seatGames[0].binding = SeatGameBinding{"player-left", "game-left"};
    authority.seatGames[1].binding = SeatGameBinding{"player-right", "game-right"};
    authority.seatGames[1].diagnostic = "The game process needs recovery.";

    SeatLauncherModel left(1);
    SeatLauncherModel right(2);
    std::string error;
    check(left.applySnapshot(authority, &error) &&
              left.state().phase == SeatLauncherPhase::Playing &&
              left.state().currentBinding == authority.seatGames[0].binding &&
              left.state().assignedDisplayIds == std::vector<std::wstring>{L"display-left"} &&
              left.state().canEndPlaying && left.state().nonIntrusiveWhilePlaying,
          "Seat 1 presents only its authoritative playing state and display group");
    check(right.applySnapshot(authority, &error) &&
              right.state().phase == SeatLauncherPhase::Recovery &&
              right.state().currentBinding == authority.seatGames[1].binding &&
              right.state().warning == "The game process needs recovery." &&
              right.state().canEndPlaying && right.state().canReconnect,
          "Seat 2 independently presents its authoritative recovery state");

    const auto leftStop = left.endPlayingCommand(&error);
    const auto rightStop = right.endPlayingCommand(&error);
    check(leftStop && leftStop->seatId == 1 && !leftStop->binding &&
              rightStop && rightStop->seatId == 2 && !rightStop->binding,
          "each launcher can construct an End Playing command only for its fixed Seat");
}

void testSelectionBoundsAndLifecycleActions() {
    SeatLauncherModel model(1);
    auto authority = snapshot();
    std::string error;
    check(model.applySnapshot(authority, &error), "idle snapshot is accepted");
    SeatLauncherChoices choices;
    choices.selectedPlayerId = "player-one";
    choices.selectedGameId = "game-one";
    choices.recentGameIds = {"game-one"};
    choices.availableGameIds = {"game-one", "game-two"};
    check(model.setChoices(choices, &error), "bounded Player/game choices are accepted");
    const auto assign = model.assignCommand(&error);
    check(assign && assign->seatId == 1 && assign->binding ==
              SeatGameBinding{"player-one", "game-one"},
          "idle launcher constructs a typed temporary binding for its own Seat");
    check(!model.startCommand(&error) && !model.endPlayingCommand(&error),
          "idle launcher cannot skip assignment or stop an absent game");

    authority.transitionSequence++;
    authority.seatGames[0].generation++;
    authority.seatGames[0].phase = SeatGamePhase::Planning;
    authority.seatGames[0].binding = assign->binding;
    check(model.applySnapshot(authority, &error) && model.state().canStart,
          "authoritative Planning state enables Seat-local start");
    const auto start = model.startCommand(&error);
    check(start && start->seatId == 1 && !start->binding,
          "start command carries no arbitrary process or application payload");

    choices.availableGameIds.assign(kMaximumPresentedGames + 1u, "duplicate");
    const auto prior = model.state().choices;
    check(!model.setChoices(std::move(choices), &error) &&
              model.state().choices == prior,
          "invalid or oversized local choices fail transactionally");
}

void testMinimalPresentationPolicy() {
    std::string error;

    SeatLauncherModel idle(1);
    auto idleAuthority = snapshot(SeatGamePhase::Idle, SeatGamePhase::Idle);
    check(idle.applySnapshot(idleAuthority, &error),
          "idle presentation baseline is accepted");
    const auto idleView = seatLauncherPresentation(idle.state());
    check(!idleView.compact && idleView.showPlayer && idleView.showGame &&
              !idleView.showNotification && !idleView.showEndPlaying &&
              !idleView.showReconnect,
          "idle Seat surface keeps only identity/game context without irrelevant actions");

    SeatLauncherModel starting(1);
    auto startingAuthority = snapshot(SeatGamePhase::Starting, SeatGamePhase::Idle);
    startingAuthority.seatGames[0].binding = SeatGameBinding{"player", "game"};
    check(starting.applySnapshot(startingAuthority, &error),
          "starting presentation baseline is accepted");
    const auto startingView = seatLauncherPresentation(starting.state());
    check(!startingView.compact && startingView.showPlayer && startingView.showGame &&
              !startingView.showNotification && startingView.showEndPlaying &&
              !startingView.showReconnect,
          "startup surface remains concise while preserving the one relevant stop action");

    SeatLauncherModel playing(1);
    auto playingAuthority = snapshot(SeatGamePhase::Playing, SeatGamePhase::Idle);
    playingAuthority.seatGames[0].binding = SeatGameBinding{"player", "game"};
    check(playing.applySnapshot(playingAuthority, &error),
          "playing presentation baseline is accepted");
    const auto playingView = seatLauncherPresentation(playing.state());
    check(playingView.compact && !playingView.showPlayer && !playingView.showGame &&
              !playingView.showNotification && playingView.showEndPlaying &&
              !playingView.showReconnect,
          "playing surface is genuinely compact and keeps only End Playing");

    SeatLauncherModel recovery(2);
    auto recoveryAuthority = snapshot(SeatGamePhase::Idle, SeatGamePhase::RecoveryRequired);
    recoveryAuthority.seatGames[1].binding = SeatGameBinding{"player-two", "game-two"};
    recoveryAuthority.seatGames[1].diagnostic = "Recovery required";
    check(recovery.applySnapshot(recoveryAuthority, &error),
          "recovery presentation baseline is accepted");
    const auto recoveryView = seatLauncherPresentation(recovery.state());
    check(!recoveryView.compact && recoveryView.showPlayer && recoveryView.showGame &&
              recoveryView.showNotification && recoveryView.showEndPlaying &&
              recoveryView.showReconnect,
          "recovery surface exposes only identity, localized attention and relevant recovery actions");

    recovery.markDisconnected("host unavailable");
    const auto disconnectedView = seatLauncherPresentation(recovery.state());
    check(!disconnectedView.compact && !disconnectedView.showPlayer &&
              !disconnectedView.showGame && disconnectedView.showNotification &&
              !disconnectedView.showEndPlaying && disconnectedView.showReconnect,
          "disconnected surface hides stale game identity and offers reconnect only");
}

void testStaleAndAuthorityChangeFailClosed() {
    SeatLauncherModel model(1);
    auto current = snapshot(SeatGamePhase::Playing, SeatGamePhase::Idle);
    current.seatGames[0].binding = SeatGameBinding{"player", "game"};
    std::string error;
    check(model.applySnapshot(current, &error), "baseline authority is accepted");
    const auto accepted = model.state();

    auto stale = current;
    --stale.transitionSequence;
    stale.configuredSeats[0].displayIds = {L"other-display"};
    stale.configuredSeats[0].primaryDisplayId = stale.configuredSeats[0].displayIds.front();
    check(!model.applySnapshot(stale, &error) &&
              model.state().assignedDisplayIds == accepted.assignedDisplayIds,
          "stale snapshot cannot redirect the launcher to another display group");

    auto restarted = current;
    restarted.sessionId = session(2);
    restarted.generation = 0;
    restarted.transitionSequence = 1;
    restarted.seatGames[0].generation = 0;
    check(!model.applySnapshot(restarted, &error),
          "new host authority is rejected until explicit disconnect/reconnect");
    model.markDisconnected("host restarted");
    check(model.state().canReconnect &&
              model.applySnapshot(restarted, &error) && model.state().connected,
          "explicit reconnect accepts a full snapshot from the new authority");
}

void testMalformedSnapshotsDoNotReplaceState() {
    SeatLauncherModel model(2);
    auto valid = snapshot();
    std::string error;
    check(model.applySnapshot(valid, &error), "valid Seat 2 snapshot is accepted");

    auto duplicate = valid;
    duplicate.configuredSeats.push_back(duplicate.configuredSeats.back());
    check(!model.applySnapshot(duplicate, &error) && model.state().connected,
          "duplicate configured Seat identities fail without dropping valid state");

    auto missing = valid;
    missing.seatGames.pop_back();
    check(!model.applySnapshot(missing, &error) &&
              model.state().seatName == L"Right Seat",
          "missing assigned Seat lifecycle fails without replacing the last view");

    auto badPrimary = valid;
    badPrimary.configuredSeats[1].primaryDisplayId = L"display-left";
    check(!model.applySnapshot(badPrimary, &error),
          "primary display outside the assigned group is rejected");
}

} // namespace

int main() {
    testIndependentAuthoritativeViewsAndCommands();
    testSelectionBoundsAndLifecycleActions();
    testMinimalPresentationPolicy();
    testStaleAndAuthorityChangeFailClosed();
    testMalformedSnapshotsDoNotReplaceState();
    if (failures != 0) {
        std::cerr << failures << " Seat launcher model test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Seat launcher model tests passed.\n";
    return EXIT_SUCCESS;
}
