#include "hydra/seat_launch_flow.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

using namespace hydra;
using namespace hydra::launcher_ui;
using namespace hydra::runtime;
using namespace hydra::seatui;

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
        return {"fixture", provider::ProviderAvailability::Available, 31u,
                {true, true, true, true, true}};
    }
    provider::DiscoveryResponse discoverInstalledGames() noexcept override {
        return {provider::ProviderResult::Success, 31u, {}, {}};
    }
    provider::AccountReferenceResponse listAccountReferences() noexcept override {
        return {provider::ProviderResult::Success, 31u, {}, {}};
    }
    provider::LaunchResponse buildLaunchRequest(
        const provider::LaunchSelection& selection) noexcept override {
        provider::ProviderLaunchRequest request;
        request.providerId = "fixture";
        request.gameId = selection.gameId;
        request.providerAppId = selection.providerAppId;
        request.accountRef = selection.accountRef;
        request.metadataRevision = 31u;
        request.targetKind = provider::LaunchTargetKind::ProviderUri;
        request.target = L"fixture://seat-launch/";
        request.arguments = selection.instanceArguments;
        request.launchCorrelationId = "seat-flow-" + selection.gameId;
        return {provider::ProviderResult::Success, std::move(request), {}};
    }
    provider::ProcessIdentificationResponse identifyProcesses(
        const provider::ProcessIdentificationQuery&) noexcept override {
        return {provider::ProviderResult::Success, 31u, {}, {}};
    }
};

profile::CompatibilityReference compatibility() {
    return {"compat-seat-flow", "controlled", 9u};
}

profile::SeatConfigDocument seats() {
    profile::SeatConfigDocument value;
    for (SeatId id : {SeatId{1}, SeatId{2}}) {
        profile::PersistedSeatConfig seat;
        seat.seatId = id;
        seat.name = L"Seat " + std::to_wstring(id);
        seat.displayIds = {L"display-" + std::to_wstring(id)};
        seat.primaryDisplayId = seat.displayIds.front();
        seat.keyboardIds = {L"keyboard-" + std::to_wstring(id)};
        seat.mouseIds = {L"mouse-" + std::to_wstring(id)};
        value.seats.push_back(std::move(seat));
    }
    return value;
}

profile::GameRecord game(std::string id, std::wstring title) {
    profile::GameRecord value;
    value.gameId = id;
    value.providerId = "fixture";
    value.providerAppId = id;
    value.title = std::move(title);
    value.installRoot = L"C:\\ControlledGames";
    value.executableCandidates = {L"C:\\ControlledGames\\game.exe"};
    value.compatibility = compatibility();
    return value;
}

catalog::LocalGameCatalog library() {
    catalog::LocalGameCatalog value;
    value.entries = {
        {game("game-one", L"Game One"), std::nullopt,
         catalog::ExecutableArchitecture::X64, catalog::CatalogStaleness::Current, 1u},
        {game("game-two", L"Game Two"), std::nullopt,
         catalog::ExecutableArchitecture::X64, catalog::CatalogStaleness::Current, 1u}};
    return value;
}

plan::GameRuntimeRequirement requirement(std::string gameId) {
    plan::GameRuntimeRequirement value;
    value.gameId = std::move(gameId);
    value.revision = 12u;
    value.validatedSeatCount = 2u;
    value.requirements.keyboard = true;
    value.requirements.mouse = true;
    value.compatibility = compatibility();
    return value;
}

profile::TwoPlayerSetupDocument sameGameSetup() {
    profile::TwoPlayerSetupDocument value;
    profile::TwoPlayerSetup setup;
    setup.setupId = "setup-game-one";
    setup.gameId = "game-one";
    setup.displayName = L"Controlled two-player setup";
    setup.compatibility = compatibility();
    setup.instances = {
        {{L"--instance=1"}, L"C:\\ControlledGames", L"C:\\SeatData\\One"},
        {{L"--instance=2"}, L"C:\\ControlledGames", L"C:\\SeatData\\Two"}};
    value.setups.push_back(std::move(setup));
    return value;
}

SeatLaunchContext context(FakeProvider& provider, bool includeSetup = true) {
    SeatLaunchContext value;
    value.seats = seats();
    value.library = library();
    value.players.players = {
        {"player-one", L"Player One", "en-US", {}},
        {"player-two", L"Player Two", "en-US", {}}};
    value.playerPresentation = {
        {"player-one", std::nullopt, {"game-one"}, 1u},
        {"player-two", std::nullopt, {"game-two"}, 2u}};
    if (includeSetup) value.setups = sameGameSetup();
    value.providers = {{"fixture", &provider}};
    value.requirements = {requirement("game-one"), requirement("game-two")};
    return value;
}

HostRuntimeSnapshot hostSnapshot(std::string firstGame = "game-one") {
    HostRuntimeSnapshot value;
    value.schemaVersion = 3;
    value.hostPhase = HostLifecyclePhase::Running;
    value.sessionPhase = SeatSessionPhase::Active;
    value.sessionId.bytes[0] = 0x72u;
    value.generation = 5;
    value.transitionSequence = 10;
    value.profileLoaded = true;
    value.managementSeatId = 1;
    for (const auto& persisted : seats().seats) {
        value.configuredSeats.push_back(profile::makeRuntimeSeatConfig(persisted));
        value.seats.push_back({persisted.seatId, SeatSessionPhase::Active, {}});
    }
    value.seatGames = {
        {1u, SeatGamePhase::Playing,
         SeatGameBinding{"player-one", std::move(firstGame)}, 20u, {}},
        {2u, SeatGamePhase::Idle, std::nullopt, 30u, {}}};
    return value;
}

class FakeHost final : public ISeatLaunchHost {
public:
    HostRuntimeSnapshot state{hostSnapshot()};
    bool failAssign{false};
    bool failStart{false};
    int assignCalls{0};
    int startCalls{0};
    int stopCalls{0};

    std::optional<HostRuntimeSnapshot> resnapshot(std::string&) override {
        return state;
    }
    std::optional<SeatGameCommandResult> assign(
        const SeatGameBinding& binding, std::string& error) override {
        ++assignCalls;
        if (failAssign) {
            error = "controlled assign failure";
            return SeatGameCommandResult{SeatGameResultCode::BackendFailure,
                                         state.seatGames, false, error};
        }
        auto& own = state.seatGames[1];
        own.phase = SeatGamePhase::Planning;
        own.binding = binding;
        ++own.generation;
        ++state.generation;
        ++state.transitionSequence;
        return SeatGameCommandResult{SeatGameResultCode::Ok, state.seatGames, false, {}};
    }
    std::optional<SeatGameCommandResult> start(std::string& error) override {
        ++startCalls;
        if (failStart) {
            error = "controlled start failure";
            return SeatGameCommandResult{SeatGameResultCode::BackendFailure,
                                         state.seatGames, false, error};
        }
        auto& own = state.seatGames[1];
        own.phase = SeatGamePhase::Playing;
        ++own.generation;
        ++state.generation;
        ++state.transitionSequence;
        return SeatGameCommandResult{SeatGameResultCode::Ok, state.seatGames, false, {}};
    }
    std::optional<SeatGameCommandResult> stop(std::string&) override {
        ++stopCalls;
        auto& own = state.seatGames[1];
        own.phase = SeatGamePhase::Idle;
        own.binding.reset();
        ++own.generation;
        ++state.generation;
        ++state.transitionSequence;
        return SeatGameCommandResult{SeatGameResultCode::Ok, state.seatGames, true, {}};
    }
};

class FakeInstaller final : public ISeatLaunchPlanInstaller {
public:
    bool failInstall{false};
    int installCalls{0};
    int rollbackCalls{0};
    std::optional<plan::ProviderAwareLaunchPlan> installedFullPlan;
    std::optional<plan::SeatProviderLaunchPlan> installedSeatPlan;

    bool install(SeatId seatId, const plan::ProviderAwareLaunchPlan& fullPlan,
                 const plan::SeatProviderLaunchPlan& seatPlan,
                 std::string& error) override {
        ++installCalls;
        if (seatId != 2u || seatPlan.seatId != seatId) {
            error = "installer received another Seat";
            return false;
        }
        installedFullPlan = fullPlan;
        installedSeatPlan = seatPlan;
        if (failInstall) {
            error = "controlled install failure";
            return false;
        }
        return true;
    }
    bool rollback(SeatId seatId, std::string&) noexcept override {
        ++rollbackCalls;
        if (seatId != 2u) return false;
        installedFullPlan.reset();
        installedSeatPlan.reset();
        return true;
    }
};

void testDifferentGameActivationKeepsOtherSeatExact() {
    FakeProvider provider;
    SeatLaunchFlow flow(2u);
    FakeHost host;
    FakeInstaller installer;
    const auto otherBefore = host.state.seatGames[0];
    check(flow.initialize(context(provider)).succeeded() &&
              flow.sync(host.state).succeeded() &&
              flow.select("player-two", "game-two").succeeded(),
          "idle Seat uses shared context and preflight for a different game");
    const auto selected = flow.preview();
    check(selected.sharedPreview.summary.canActivate && selected.seatPlan &&
              selected.seatPlan->seatId == 2u &&
              selected.seatPlan->launchRequest.gameId == "game-two",
          "preview exposes the exact immutable Seat plan from the shared compiler");
    check(flow.activate(host, installer).succeeded() &&
              host.state.seatGames[1].phase == SeatGamePhase::Playing &&
              host.state.seatGames[0] == otherBefore &&
              host.assignCalls == 1 && host.startCalls == 1 && host.stopCalls == 0,
          "idle Seat returns to Playing through only its assign/start operations");
    check(installer.installedFullPlan && installer.installedSeatPlan &&
              flow.state().phase == SeatLauncherPhase::Playing &&
              flow.sharedModel().playerPresentation()[1].recentGameIds.front() ==
                  "game-two" &&
              flow.sharedModel().playerPresentation()[0].recentGameIds ==
                  std::vector<std::string>{"game-one"},
          "activation records only the newly started Seat presentation state");
}

void testSameGameUsesSharedSetupResolver() {
    FakeProvider provider;
    SeatLaunchFlow flow(2u);
    FakeHost host;
    FakeInstaller installer;
    const auto otherBefore = host.state.seatGames[0];
    check(flow.initialize(context(provider, true)).succeeded() &&
              flow.sync(host.state).succeeded() &&
              flow.select("player-two", "game-one").succeeded(),
          "same-game idle selection resolves through shared setup data");
    const auto selected = flow.preview();
    check(selected.sharedPreview.compileResult.plan &&
              selected.sharedPreview.compileResult.plan->seats.size() == 2u &&
              selected.sharedPreview.compileResult.plan->seats[0].setupId ==
                  std::optional<std::string>{"setup-game-one"} &&
              selected.sharedPreview.compileResult.plan->seats[1].setupId ==
                  std::optional<std::string>{"setup-game-one"} &&
              selected.sharedPreview.compileResult.plan->seats[0].instanceIndex == 0u &&
              selected.sharedPreview.compileResult.plan->seats[1].instanceIndex == 1u,
          "same-game preview pins the exact two-instance setup for both Seats");
    check(flow.activate(host, installer).succeeded() &&
              host.state.seatGames[0] == otherBefore &&
              host.state.seatGames[1].binding ==
                  std::optional<SeatGameBinding>{
                      SeatGameBinding{"player-two", "game-one"}},
          "only the idle Seat activates from the shared same-game plan");
}

void testPreflightAndSnapshotChangesBlockBeforeMutation() {
    FakeProvider provider;
    auto blockedContext = context(provider, false);
    blockedContext.requirements[1].requirements.controller = true;
    SeatLaunchFlow blocked(2u);
    FakeHost host;
    FakeInstaller installer;
    check(blocked.initialize(std::move(blockedContext)).succeeded() &&
              blocked.sync(host.state).succeeded() &&
              blocked.select("player-two", "game-two").result ==
                  SeatLaunchResult::PreflightBlocked &&
              !blocked.preview().sharedPreview.summary.canActivate &&
              installer.installCalls == 0 && host.assignCalls == 0,
          "missing Seat device requirement blocks through shared preflight without mutation");

    SeatLaunchFlow stale(2u);
    check(stale.initialize(context(provider)).succeeded() &&
              stale.sync(host.state).succeeded() &&
              stale.select("player-two", "game-two").succeeded(),
          "stale-snapshot fixture reaches a valid preview");
    host.state.seatGames[0].generation++;
    check(stale.activate(host, installer).result == SeatLaunchResult::SnapshotChanged &&
              installer.installCalls == 0 && host.assignCalls == 0,
          "other Seat change between preview and activation fails before plan installation");

    FakeHost generationHost;
    SeatLaunchFlow generationChanged(2u);
    check(generationChanged.initialize(context(provider)).succeeded() &&
              generationChanged.sync(generationHost.state).succeeded() &&
              generationChanged.select("player-two", "game-two").succeeded(),
          "authority-generation fixture reaches a valid preview");
    ++generationHost.state.generation;
    check(generationChanged.activate(generationHost, installer).result ==
              SeatLaunchResult::SnapshotChanged && generationHost.assignCalls == 0,
          "authority generation change invalidates the immutable preview before mutation");

    SeatLaunchFlow missingSetup(2u);
    FakeHost sameGameHost;
    check(missingSetup.initialize(context(provider, false)).succeeded() &&
              missingSetup.sync(sameGameHost.state).succeeded() &&
              missingSetup.select("player-two", "game-one").result ==
                  SeatLaunchResult::PreflightBlocked,
          "same-game selection cannot bypass the shared TwoPlayerSetup resolver");
}

void testStartFailureRollsBackOnlyIdleSeat() {
    FakeProvider provider;
    SeatLaunchFlow flow(2u);
    FakeHost host;
    host.failStart = true;
    FakeInstaller installer;
    const auto otherBefore = host.state.seatGames[0];
    check(flow.initialize(context(provider)).succeeded() &&
              flow.sync(host.state).succeeded() &&
              flow.select("player-two", "game-two").succeeded(),
          "failure fixture reaches a valid activation preview");
    const auto result = flow.activate(host, installer);
    check(result.result == SeatLaunchResult::StartFailed &&
              host.stopCalls == 1 && installer.rollbackCalls == 1 &&
              host.state.seatGames[1].phase == SeatGamePhase::Idle &&
              host.state.seatGames[0] == otherBefore,
          "failed start rolls back the selected Seat plan/binding without touching the other Seat");
}

} // namespace

int main() {
    testDifferentGameActivationKeepsOtherSeatExact();
    testSameGameUsesSharedSetupResolver();
    testPreflightAndSnapshotChangesBlockBeforeMutation();
    testStartFailureRollsBackOnlyIdleSeat();
    if (failures != 0) {
        std::cerr << failures << " Seat launch flow test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Seat launch flow tests passed.\n";
    return EXIT_SUCCESS;
}
