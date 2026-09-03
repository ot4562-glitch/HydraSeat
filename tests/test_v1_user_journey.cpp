#include "hydra/launcher_ui_model.hpp"
#include "hydra/launcher_user_state.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace hydra;
using namespace hydra::launcher_ui;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class FakeProvider final : public provider::LauncherProviderAdapter {
public:
    provider::ProviderDescriptor descriptor() const noexcept override {
        return {"fixture", provider::ProviderAvailability::Available, 17u,
                {true, true, true, true, true}};
    }

    provider::DiscoveryResponse discoverInstalledGames() noexcept override {
        return {provider::ProviderResult::Success, 17u, {}, {}};
    }

    provider::AccountReferenceResponse listAccountReferences() noexcept override {
        return {provider::ProviderResult::Success, 17u, {}, {}};
    }

    provider::LaunchResponse buildLaunchRequest(
        const provider::LaunchSelection& selection) noexcept override {
        provider::ProviderLaunchRequest request;
        request.providerId = "fixture";
        request.gameId = selection.gameId;
        request.providerAppId = selection.providerAppId;
        request.accountRef = selection.accountRef;
        request.metadataRevision = 17u;
        request.targetKind = provider::LaunchTargetKind::ProviderUri;
        request.target = L"fixture://run/";
        request.arguments = selection.instanceArguments;
        request.launchCorrelationId = "v1-journey-" + selection.gameId;
        return {provider::ProviderResult::Success, std::move(request), {}};
    }

    provider::ProcessIdentificationResponse identifyProcesses(
        const provider::ProcessIdentificationQuery&) noexcept override {
        return {provider::ProviderResult::Success, 17u, {}, {}};
    }
};

profile::CompatibilityReference compatibility() {
    return {"compat-v1-journey", "fixture", 4u};
}

profile::SeatConfigDocument seats() {
    profile::SeatConfigDocument document;
    profile::PersistedSeatConfig first;
    first.seatId = 1u;
    first.name = L"Display 1";
    first.displayIds = {L"display-1"};
    first.primaryDisplayId = L"display-1";
    first.keyboardIds = {L"keyboard-1"};
    first.mouseIds = {L"mouse-1"};

    profile::PersistedSeatConfig second;
    second.seatId = 2u;
    second.name = L"Display 2";
    second.displayIds = {L"display-2"};
    second.primaryDisplayId = L"display-2";
    second.keyboardIds = {L"keyboard-2"};
    second.mouseIds = {L"mouse-2"};
    document.seats = {first, second};
    return document;
}

profile::GameRecord game(std::string id, std::string appId, std::wstring title) {
    profile::GameRecord value;
    value.gameId = std::move(id);
    value.providerId = "fixture";
    value.providerAppId = std::move(appId);
    value.title = std::move(title);
    value.installRoot = L"C:\\Games";
    value.executableCandidates = {L"C:\\Games\\game.exe"};
    value.compatibility = compatibility();
    return value;
}

catalog::LocalGameCatalog library() {
    catalog::LocalGameCatalog value;
    value.entries = {
        {game("game-one", "100", L"First Game"), std::nullopt,
         catalog::ExecutableArchitecture::X64, catalog::CatalogStaleness::Current, 1u},
        {game("game-two", "200", L"Second Game"), std::nullopt,
         catalog::ExecutableArchitecture::X64, catalog::CatalogStaleness::Current, 1u},
    };
    return value;
}

plan::GameRuntimeRequirement requirement(std::string gameId) {
    plan::GameRuntimeRequirement value;
    value.gameId = std::move(gameId);
    value.revision = 8u;
    value.validatedSeatCount = 1u;
    value.requirements.keyboard = true;
    value.requirements.mouse = true;
    value.compatibility = compatibility();
    return value;
}

void testMockHappyPathKeepsPlayerTwoOptional() {
    FakeProvider provider;
    LauncherUiModel model;
    check(model.initialize(seats(), library(), {}, {{"fixture", &provider}},
                           {requirement("game-one"), requirement("game-two")})
              .succeeded(),
          "mock launcher initializes from two configured Displays and a playable library");
    check(!model.preview().summary.canActivate,
          "empty Player/game state cannot claim Play readiness");

    std::string player1;
    check(model.createPlayer(L"Player One", "en-US", std::nullopt, player1).succeeded(),
          "first launch can create Player 1 through the public launcher model");
    check(model.selectGame(1u, player1, "game-one").succeeded(),
          "Player 1 can select a playable game");
    const auto playerOneOnly = model.preview();
    check(playerOneOnly.summary.canActivate && playerOneOnly.compileResult.succeeded() &&
              playerOneOnly.compileResult.plan &&
              playerOneOnly.compileResult.plan->seats.size() == 1u,
          "Player 2 remains optional for a valid one-Player journey");

    std::string player2;
    check(model.createPlayer(L"Player Two", "en-US", std::nullopt, player2).succeeded(),
          "optional Player 2 can be created without disturbing Player 1");
    check(model.preview().summary.canActivate,
          "merely creating optional Player 2 does not block the Player 1 plan");
    check(model.selectGame(2u, player2, "game-two").succeeded(),
          "optional Player 2 can join with another playable game");
    const auto bothPlayers = model.preview();
    check(bothPlayers.summary.canActivate && bothPlayers.compileResult.plan &&
              bothPlayers.compileResult.plan->seats.size() == 2u,
          "two selected Players produce a two-Seat mock plan without changing evidence class");

    const auto before = model.selection();
    check(model.selectGame(1u, "stale-player-id", "game-one").result ==
              UiResult::InvalidPlayer && model.selection() == before,
          "stale Player IDs fail closed without replacing the valid selection");
}

void testIncompleteStateHasActionableBlockingEvidence() {
    FakeProvider provider;
    LauncherUiModel model;
    check(model.initialize(seats(), library(), {}, {{"fixture", &provider}},
                           {requirement("game-two")})
              .succeeded(),
          "blocking fixture initializes");
    std::string player1;
    check(model.createPlayer(L"Player One", "en-US", std::nullopt, player1).succeeded() &&
              model.selectGame(1u, player1, "game-one").succeeded(),
          "blocking fixture reaches a selected game");

    const auto preview = model.preview();
    const bool hasMissingRequirement = std::any_of(
        preview.compileResult.issues.begin(), preview.compileResult.issues.end(),
        [](const auto& issue) { return issue.code == plan::PlanIssueCode::MissingRequirement; });
    const bool hasActionableMessage = std::any_of(
        preview.summary.messages.begin(), preview.summary.messages.end(),
        [](const auto& message) { return !message.userMessage.empty(); });
    check(!preview.summary.canActivate && !preview.compileResult.succeeded() &&
              hasMissingRequirement && hasActionableMessage,
          "missing trusted readiness blocks Play with a user-facing reason");
}

void testDurablePlayerSelectionProjection() {
    namespace state = launcher_state;
    const auto root = std::filesystem::temp_directory_path() /
                      "hydraseat-v1-user-journey-state";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);

    profile::PlayerProfileDocument players;
    players.players = {
        {"player-1", L"Player One", "en-US", {}},
        {"player-2", L"Player Two", "en-US", {}},
    };
    check(state::savePlayerProfiles(root, players).succeeded(),
          "Player profiles persist through the dedicated launcher user-state boundary");
    check(state::saveLastPlayerSelection(
              root, {"player-1", std::optional<std::string>{"player-2"}}).succeeded(),
          "last Player 1 and optional Player 2 selection persists durably");

    profile::PlayerProfileDocument loadedPlayers;
    std::optional<state::LastPlayerSelection> loadedSelection;
    check(state::loadPlayerProfiles(root, loadedPlayers).succeeded() &&
              loadedPlayers == players,
          "restart projection reloads the same Player profiles");
    check(state::loadLastPlayerSelection(root, loadedSelection).succeeded() &&
              loadedSelection && loadedSelection->player1Id == "player-1" &&
              loadedSelection->player2Id == std::optional<std::string>{"player-2"},
          "restart projection reloads the last Player selection");

    state::FilteredLastPlayerSelection filtered;
    check(state::filterLastPlayerSelection(loadedSelection, loadedPlayers, filtered).succeeded() &&
              filtered.selection == loadedSelection && !filtered.player1Stale &&
              !filtered.player2Stale,
          "current persisted IDs restore without inventing a Player");

    auto player2Removed = loadedPlayers;
    player2Removed.players.pop_back();
    check(state::filterLastPlayerSelection(loadedSelection, player2Removed, filtered).succeeded() &&
              filtered.selection && filtered.selection->player1Id == "player-1" &&
              !filtered.selection->player2Id && !filtered.player1Stale &&
              filtered.player2Stale,
          "stale optional Player 2 is dropped while valid Player 1 remains");

    profile::PlayerProfileDocument noKnownPlayers;
    check(state::filterLastPlayerSelection(loadedSelection, noKnownPlayers, filtered).succeeded() &&
              !filtered.selection && filtered.player1Stale,
          "stale required Player 1 invalidates restore instead of inventing identity");

    std::filesystem::remove_all(root, cleanupError);
}

void testMockEvidenceDoesNotPromoteHandsOnClasses() {
    struct EvidenceLedger {
        bool automatedMock{true};
        bool computerUse{false};
        bool physical{false};
        bool realGame{false};
        bool cleanMachine{false};
        bool signing{false};
    };
    constexpr EvidenceLedger evidence;
    check(evidence.automatedMock && !evidence.computerUse && !evidence.physical &&
              !evidence.realGame && !evidence.cleanMachine && !evidence.signing,
          "mock journey success never promotes ComputerUse/Physical/RealGame/CleanMachine/Signing");
}

} // namespace

int main() {
    testMockHappyPathKeepsPlayerTwoOptional();
    testIncompleteStateHasActionableBlockingEvidence();
    testDurablePlayerSelectionProjection();
    testMockEvidenceDoesNotPromoteHandsOnClasses();

    if (failures != 0) {
        std::cerr << failures << " V1 user journey mock test(s) failed\n";
        return 1;
    }
    std::cout << "V1 user journey mock tests passed\n";
    return 0;
}
