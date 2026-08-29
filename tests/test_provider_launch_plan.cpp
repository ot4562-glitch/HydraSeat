#include "hydra/provider_launch_plan.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace hydra;
using namespace hydra::plan;
using namespace hydra::provider;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class FakeProvider final : public LauncherProviderAdapter {
public:
    ProviderDescriptor descriptorValue{
        "fake", ProviderAvailability::Available, 9u,
        {true, true, true, true, true}};

    ProviderDescriptor descriptor() const noexcept override { return descriptorValue; }
    DiscoveryResponse discoverInstalledGames() noexcept override {
        return {ProviderResult::Success, descriptorValue.metadataRevision, {}, {}};
    }
    AccountReferenceResponse listAccountReferences() noexcept override {
        return {ProviderResult::Success, descriptorValue.metadataRevision, {}, {}};
    }
    LaunchResponse buildLaunchRequest(const LaunchSelection& selection) noexcept override {
        ProviderLaunchRequest request;
        request.providerId = "fake";
        request.gameId = selection.gameId;
        request.providerAppId = selection.providerAppId;
        request.accountRef = selection.accountRef;
        request.metadataRevision = descriptorValue.metadataRevision;
        request.targetKind = LaunchTargetKind::ProviderUri;
        request.target = L"fake://launch/";
        if (selection.providerAppId) {
            for (const char ch : *selection.providerAppId) {
                request.target.push_back(static_cast<wchar_t>(ch));
            }
        }
        request.arguments = selection.instanceArguments;
        request.launchCorrelationId = "fake-" + selection.gameId;
        return {ProviderResult::Success, std::move(request), {}};
    }
    ProcessIdentificationResponse identifyProcesses(
        const ProcessIdentificationQuery&) noexcept override {
        return {ProviderResult::Success, descriptorValue.metadataRevision, {}, {}};
    }
};

profile::CompatibilityReference compatibility(std::string id = "compat-a") {
    return {std::move(id), "fixture", 3u};
}

profile::SeatConfigDocument seats() {
    profile::SeatConfigDocument document;
    document.managementSeatId = 1u;
    profile::PersistedSeatConfig first;
    first.seatId = 1u;
    first.name = L"Seat 1";
    first.displayIds = {L"display-1"};
    first.primaryDisplayId = L"display-1";
    first.keyboardIds = {L"keyboard-1"};
    first.mouseIds = {L"mouse-1"};
    first.controllerIds = {L"controller-1"};
    first.audioOutputEndpointId = L"audio-1";
    profile::PersistedSeatConfig second;
    second.seatId = 2u;
    second.name = L"Seat 2";
    second.displayIds = {L"display-2"};
    second.primaryDisplayId = L"display-2";
    second.keyboardIds = {L"keyboard-2"};
    second.mouseIds = {L"mouse-2"};
    second.controllerIds = {L"controller-2"};
    second.audioOutputEndpointId = L"audio-2";
    document.seats = {first, second};
    return document;
}

profile::PlayerProfileDocument players() {
    profile::PlayerProfileDocument document;
    document.players = {
        {"player-1", L"Player 1", "en-US", {{"fake", "account-1"}}},
        {"player-2", L"Player 2", "en-US", {{"fake", "account-2"}}},
    };
    return document;
}

profile::GameRecord game(std::string id, std::string app, std::wstring path) {
    profile::GameRecord record;
    record.gameId = std::move(id);
    record.providerId = "fake";
    record.providerAppId = std::move(app);
    record.title = L"Fixture";
    record.installRoot = L"C:\\Games";
    record.executableCandidates = {std::move(path)};
    record.compatibility = compatibility();
    record.origin = profile::GameOrigin::Discovered;
    return record;
}

profile::GameRecordDocument games() {
    profile::GameRecordDocument document;
    document.games = {
        game("game:a", "100", L"C:\\Games\\A.exe"),
        game("game:b", "200", L"C:\\Games\\B.exe"),
    };
    return document;
}

GameRuntimeRequirement requirement(std::string gameId) {
    GameRuntimeRequirement value;
    value.gameId = std::move(gameId);
    value.revision = 11u;
    value.requirements.display = true;
    value.requirements.keyboard = true;
    value.requirements.mouse = true;
    value.requirements.controller = false;
    value.requirements.audioOutput = true;
    value.requirements.windowOwnership = true;
    value.requirements.recovery = true;
    value.capabilities = {};
    value.compatibility = compatibility();
    return value;
}

std::vector<GameRuntimeRequirement> requirements() {
    return {requirement("game:a"), requirement("game:b")};
}

profile::RuntimeSessionSelection twoDifferentGames(bool reverse = false) {
    profile::RuntimeSessionSelection selection;
    selection.bindings = {
        {1u, "player-1", "game:a", std::nullopt, 0u},
        {2u, "player-2", "game:b", std::nullopt, 0u},
    };
    if (reverse) std::swap(selection.bindings[0], selection.bindings[1]);
    return selection;
}

void testDeterministicDifferentGamePlan() {
    FakeProvider fake;
    const std::vector<ProviderAdapterBinding> providers{{"fake", &fake}};
    const auto seatDocument = seats();
    const auto playerDocument = players();
    const auto gameDocument = games();
    const profile::TwoPlayerSetupDocument setupDocument;
    const auto requirementSet = requirements();

    const auto first = compileProviderAwareLaunchPlan(
        seatDocument, playerDocument, gameDocument, setupDocument,
        twoDifferentGames(false), providers, requirementSet);
    const auto second = compileProviderAwareLaunchPlan(
        seatDocument, playerDocument, gameDocument, setupDocument,
        twoDifferentGames(true), providers, requirementSet);

    check(first.succeeded() && second.succeeded(),
          "valid different-game selections compile");
    check(first.plan == second.plan && first.plan->fingerprint == second.plan->fingerprint,
          "equivalent binding order produces identical immutable plan and hash");
    check(first.plan->seats.size() == 2u && first.plan->seats[0].seatId == 1u &&
              first.plan->seats[1].seatId == 2u,
          "compiled Seat order is canonical");
    check(first.plan->seats[0].launchRequest.accountRef == "account-1" &&
              first.plan->seats[1].launchRequest.accountRef == "account-2",
          "Player provider account references resolve into typed launch requests");
}

void testSameGameRequiresAndPinsSetup() {
    FakeProvider fake;
    const std::vector<ProviderAdapterBinding> providers{{"fake", &fake}};
    auto seatDocument = seats();
    auto playerDocument = players();
    auto gameDocument = games();
    gameDocument.games.resize(1u);
    auto requirementSet = requirements();
    requirementSet.resize(1u);

    profile::RuntimeSessionSelection missing;
    missing.bindings = {
        {1u, "player-1", "game:a", std::nullopt, 0u},
        {2u, "player-2", "game:a", std::nullopt, 1u},
    };
    profile::TwoPlayerSetupDocument noSetups;
    auto rejected = compileProviderAwareLaunchPlan(
        seatDocument, playerDocument, gameDocument, noSetups, missing, providers,
        requirementSet);
    check(!rejected.succeeded(), "same-game selection without setup fails closed");

    profile::TwoPlayerSetup setup;
    setup.setupId = "setup-a";
    setup.gameId = "game:a";
    setup.displayName = L"Two players";
    setup.compatibility = compatibility();
    setup.instances = {
        {{L"--seat=1"}, L"C:\\Games\\A1", L"C:\\Data\\A1"},
        {{L"--seat=2"}, L"C:\\Games\\A2", L"C:\\Data\\A2"},
    };
    profile::TwoPlayerSetupDocument setupDocument;
    setupDocument.setups = {setup};
    profile::RuntimeSessionSelection selected;
    selected.bindings = {
        {1u, "player-1", "game:a", "setup-a", 0u},
        {2u, "player-2", "game:a", "setup-a", 1u},
    };
    const auto compiled = compileProviderAwareLaunchPlan(
        seatDocument, playerDocument, gameDocument, setupDocument, selected, providers,
        requirementSet);
    check(compiled.succeeded(), "same-game selection with exact setup compiles");
    check(compiled.plan->seats[0].instanceRecipe->arguments ==
              std::vector<std::wstring>{L"--seat=1"} &&
              compiled.plan->seats[1].instanceRecipe->arguments ==
              std::vector<std::wstring>{L"--seat=2"},
          "instance recipes are pinned per Seat");
}

void testMaterialStalenessAndHardwareBlockPlan() {
    FakeProvider fake;
    const std::vector<ProviderAdapterBinding> providers{{"fake", &fake}};
    auto seatDocument = seats();
    const auto playerDocument = players();
    const auto gameDocument = games();
    const profile::TwoPlayerSetupDocument setupDocument;
    auto requirementSet = requirements();

    requirementSet[0].compatibility = compatibility("different");
    auto stale = compileProviderAwareLaunchPlan(
        seatDocument, playerDocument, gameDocument, setupDocument,
        twoDifferentGames(), providers, requirementSet);
    check(!stale.succeeded() && stale.issues.front().code == PlanIssueCode::StaleCompatibility,
          "stale compatibility evidence prevents plan creation");

    requirementSet = requirements();
    requirementSet[0].requirements.controller = true;
    seatDocument.seats[0].controllerIds.clear();
    auto missingController = compileProviderAwareLaunchPlan(
        seatDocument, playerDocument, gameDocument, setupDocument,
        twoDifferentGames(), providers, requirementSet);
    check(!missingController.succeeded() &&
              missingController.issues.front().code == PlanIssueCode::MissingController,
          "requirement-aware hardware preflight fails before activation");

    requirementSet = requirements();
    seatDocument = seats();
    seatDocument.seats[1].displayIds = seatDocument.seats[0].displayIds;
    seatDocument.seats[1].primaryDisplayId = seatDocument.seats[0].primaryDisplayId;
    auto duplicate = compileProviderAwareLaunchPlan(
        seatDocument, playerDocument, gameDocument, setupDocument,
        twoDifferentGames(), providers, requirementSet);
    check(!duplicate.succeeded() &&
              duplicate.issues.front().code == PlanIssueCode::DuplicateExclusiveHardware,
          "cross-Seat exclusive hardware collision fails closed");
}

void testProviderAndAccountAmbiguityFailClosed() {
    FakeProvider fake;
    const std::vector<ProviderAdapterBinding> providers{{"fake", &fake}};
    const auto seatDocument = seats();
    auto playerDocument = players();
    const auto gameDocument = games();
    const profile::TwoPlayerSetupDocument setupDocument;
    const auto requirementSet = requirements();

    fake.descriptorValue.availability = ProviderAvailability::Offline;
    auto offline = compileProviderAwareLaunchPlan(
        seatDocument, playerDocument, gameDocument, setupDocument,
        twoDifferentGames(), providers, requirementSet);
    check(!offline.succeeded() &&
              offline.issues.front().code == PlanIssueCode::ProviderUnavailable,
          "offline provider cannot silently change the launch path");

    fake.descriptorValue.availability = ProviderAvailability::Available;
    playerDocument.players[0].providerAccounts.push_back({"fake", "account-other"});
    auto ambiguous = compileProviderAwareLaunchPlan(
        seatDocument, playerDocument, gameDocument, setupDocument,
        twoDifferentGames(), providers, requirementSet);
    check(!ambiguous.succeeded() &&
              ambiguous.issues.front().code == PlanIssueCode::InvalidPlayerDocument,
          "duplicate provider account references fail closed at the stable Player schema boundary");
}

void testApplicationScopedProviderBindings() {
    FakeProvider first;
    FakeProvider second;
    const std::vector<ProviderAdapterBinding> providers{
        {"fake", &first, "100"},
        {"fake", &second, "200"},
    };
    const auto compiled = compileProviderAwareLaunchPlan(
        seats(), players(), games(), {}, twoDifferentGames(), providers,
        requirements());
    check(compiled.succeeded(),
          "application-scoped adapters allow multiple definitions from one provider");

    const std::vector<ProviderAdapterBinding> ambiguous{
        {"fake", &first, "100"},
        {"fake", &second, "100"},
    };
    const auto rejected = compileProviderAwareLaunchPlan(
        seats(), players(), games(), {}, twoDifferentGames(), ambiguous,
        requirements());
    check(!rejected.succeeded() &&
              rejected.issues.front().code == PlanIssueCode::DuplicateProvider,
          "duplicate application-scoped adapters fail closed");

    const std::vector<ProviderAdapterBinding> nullScoped{
        {"fake", &first},
        {"fake", nullptr, "100"},
    };
    const auto nullRejected = compileProviderAwareLaunchPlan(
        seats(), players(), games(), {}, twoDifferentGames(), nullScoped,
        requirements());
    check(!nullRejected.succeeded() &&
              nullRejected.issues.front().code == PlanIssueCode::MissingProvider,
          "a null exact binding cannot fall back to a provider-wide adapter");
}

} // namespace

int main() {
    testDeterministicDifferentGamePlan();
    testSameGameRequiresAndPinsSetup();
    testMaterialStalenessAndHardwareBlockPlan();
    testProviderAndAccountAmbiguityFailClosed();
    testApplicationScopedProviderBindings();
    if (failures != 0) {
        std::cerr << failures << " provider launch plan test(s) failed\n";
        return 1;
    }
    std::cout << "provider launch plan tests passed\n";
    return 0;
}
