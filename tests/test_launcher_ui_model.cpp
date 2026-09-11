#include "hydra/launcher_ui_model.hpp"

#include <iostream>
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
    provider::ProviderDescriptor descriptorValue{
        "fixture", provider::ProviderAvailability::Available, 17u,
        {true, true, true, true, true}};

    provider::ProviderDescriptor descriptor() const noexcept override {
        return descriptorValue;
    }
    provider::DiscoveryResponse discoverInstalledGames() noexcept override {
        return {provider::ProviderResult::Success, 17u, {}, {}};
    }
    provider::AccountReferenceResponse listAccountReferences() noexcept override {
        return {provider::ProviderResult::Success, 17u,
                {{"fixture", "local-account"}}, {}};
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
        request.launchCorrelationId = "ui-" + selection.gameId;
        return {provider::ProviderResult::Success, std::move(request), {}};
    }
    provider::ProcessIdentificationResponse identifyProcesses(
        const provider::ProcessIdentificationQuery&) noexcept override {
        return {provider::ProviderResult::Success, 17u, {}, {}};
    }
};

profile::CompatibilityReference compatibility() {
    return {"compat-ui", "fixture", 4u};
}

profile::SeatConfigDocument seats() {
    profile::SeatConfigDocument document;
    profile::PersistedSeatConfig first;
    first.seatId = 1u;
    first.name = L"Seat 1";
    first.displayIds = {L"display-1"};
    first.primaryDisplayId = L"display-1";
    first.keyboardIds = {L"keyboard-1"};
    first.mouseIds = {L"mouse-1"};
    profile::PersistedSeatConfig second;
    second.seatId = 2u;
    second.name = L"Seat 2";
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
    value.validatedSeatCount = 2u;
    value.requirements.keyboard = true;
    value.requirements.mouse = true;
    value.compatibility = compatibility();
    return value;
}

void testNormalDifferentAndSameGameFlow() {
    FakeProvider provider;
    LauncherUiModel model;
    check(model.initialize(seats(), library(), {}, {{"fixture", &provider}},
                           {requirement("game-one"), requirement("game-two")})
              .succeeded(),
          "valid snapshots initialize the launcher UI model");

    std::string firstPlayer;
    std::string secondPlayer;
    check(model.createPlayer(L"Alex", "en-US", std::nullopt, firstPlayer).succeeded() &&
              model.createPlayer(L"Sam", "en-US", L"C:\\Avatars\\sam.png",
                                 secondPlayer).succeeded(),
          "two local Players can be created without JSON");
    check(model.setPlayerAccount(firstPlayer, "fixture", "local-account").succeeded(),
          "an opaque provider account reference can be selected for a Player");

    check(model.selectGame(1u, firstPlayer, "game-one").succeeded() &&
              model.selectGame(2u, secondPlayer, "game-two").succeeded(),
          "different games can be selected per Seat");
    auto preview = model.preview();
    check(preview.summary.canActivate && preview.compileResult.succeeded(),
          "different-game UI selection produces a validated Play plan");

    const auto setupNeeded = model.selectBoth("game-one", firstPlayer, secondPlayer);
    check(setupNeeded.result == UiResult::MissingSetup && !model.preview().summary.canActivate,
          "same-game selection visibly requires a two-player setup");

    const auto* selectedGame = &model.library().entries[0].game;
    setup::GenerateSetupInput input;
    input.game = selectedGame;
    input.setupId = "setup-ui";
    input.displayName = L"Two players";
    input.instances = {
        profile::InstanceRecipe{{L"--player=1"}, L"C:\\Games", L"C:\\Data\\One"},
        profile::InstanceRecipe{{L"--player=2"}, L"C:\\Games", L"C:\\Data\\Two"},
    };
    std::vector<setup::MutationIntent> mutations;
    check(model.createSetup(input, mutations).succeeded() && mutations.size() == 2u,
          "guided setup creation emits typed mutations and selects the setup");
    preview = model.preview();
    check(preview.summary.canActivate && preview.compileResult.succeeded() &&
              preview.compileResult.plan->seats[0].instanceIndex == 0u &&
              preview.compileResult.plan->seats[1].instanceIndex == 1u,
          "resolved same-game UI flow produces an immutable Play plan");

    check(model.removePlayer(firstPlayer).result == UiResult::PlayerInUse,
          "a Player selected by a Seat cannot be removed silently");
    check(model.recordActivatedPlan(*preview.compileResult.plan).succeeded() &&
              model.playerPresentation()[0].recentGameIds.front() == "game-one" &&
              model.playerPresentation()[0].preferredSeat == 1u,
          "successful Play updates recent game and Seat preference presentation data");
}

void testTransactionalRefreshAndRequirementEvidence() {
    FakeProvider provider;
    LauncherUiModel model;
    check(model.initialize(seats(), library(), {}, {{"fixture", &provider}},
                           {requirement("game-one"), requirement("game-two")})
              .succeeded(),
          "fixture model initializes");
    std::string player;
    check(model.createPlayer(L"Player", "en-US", std::nullopt, player).succeeded() &&
              model.selectGame(1u, player, "game-one").succeeded(),
          "fixture selection succeeds");
    check(model.clearSeat(2u).succeeded() && model.selection().bindings.size() == 1u &&
              model.selection().bindings.front().seatId == 1u,
          "clearing an already unused Seat is an idempotent UI action and preserves the other Seat");

    auto reduced = library();
    reduced.entries.erase(reduced.entries.begin());
    check(model.replaceLibrary(std::move(reduced)).result == UiResult::InvalidSelection &&
              model.library().entries.size() == 2u,
          "library refresh cannot silently remove a selected title");

    check(model.replaceRequirements({requirement("game-two")}).succeeded(),
          "requirement evidence snapshot can be refreshed");
    const auto preview = model.preview();
    check(!preview.summary.canActivate && !preview.compileResult.succeeded() &&
              preview.compileResult.issues.front().code ==
                  plan::PlanIssueCode::MissingRequirement,
          "missing compatibility evidence blocks Play instead of inventing capability");

    std::vector<plan::GameRuntimeRequirement> oversized(
        kMaximumRuntimeRequirements + 1u, requirement("game-two"));
    check(model.replaceRequirements(std::move(oversized)).result ==
              UiResult::InvalidRequirement && model.requirements().size() == 1u,
          "oversized requirement snapshots fail transactionally");

    std::string ignored;
    check(model.createPlayer(L"Bad avatar", "en-US", L"relative.png", ignored).result ==
              UiResult::InvalidPlayer,
          "optional avatar selection requires a bounded absolute local path");
}

void testSharedPlayerSnapshotInitialization() {
    FakeProvider provider;
    profile::PlayerProfileDocument players;
    players.players.push_back({"shared-player", L"Shared Player", "en-US", {}});
    std::vector<PlayerPresentation> presentation{
        {"shared-player", L"C:\\Avatars\\shared.png", {"game-two", "game-one"}, 2u}};
    LauncherUiModel model;
    check(model.initializeShared(
              seats(), library(), players, presentation, {}, {{"fixture", &provider}},
              {requirement("game-one"), requirement("game-two")}).succeeded() &&
              model.players() == players && model.playerPresentation() == presentation &&
              model.selectGame(2u, "shared-player", "game-two").succeeded() &&
              model.preview().summary.canActivate,
          "a validated shared Player/presentation snapshot uses the same selection and plan path");

    auto invalid = presentation;
    invalid.front().recentGameIds.push_back("missing-game");
    const auto before = model.players();
    check(model.initializeShared(
              seats(), library(), players, invalid, {}, {{"fixture", &provider}},
              {requirement("game-one"), requirement("game-two")}).result ==
                  UiResult::InvalidPlayer && model.players() == before,
          "invalid shared presentation fails without replacing the active model");
}

} // namespace

int main() {
    testNormalDifferentAndSameGameFlow();
    testTransactionalRefreshAndRequirementEvidence();
    testSharedPlayerSnapshotInitialization();
    if (failures != 0) {
        std::cerr << failures << " launcher UI model test(s) failed\n";
        return 1;
    }
    std::cout << "launcher UI model tests passed\n";
    return 0;
}
