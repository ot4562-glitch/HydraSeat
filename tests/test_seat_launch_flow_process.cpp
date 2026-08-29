#include "hydra/process_launcher.hpp"
#include "hydra/runtime_host.hpp"
#include "hydra/seat_launch_flow.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace hydra;
using namespace hydra::process;
using namespace hydra::runtime;
using namespace hydra::seatui;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

#ifdef _WIN32

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::uint64_t creationTime100ns(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(process, &created, &exited, &kernel, &user) == FALSE) return 0;
    ULARGE_INTEGER value{};
    value.LowPart = created.dwLowDateTime;
    value.HighPart = created.dwHighDateTime;
    return value.QuadPart;
}

bool exactProcessRunning(const ProcessIdentity& identity) {
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE, identity.processId);
    if (process == nullptr) return false;
    const bool running = creationTime100ns(process) == identity.creationTime100ns &&
                         WaitForSingleObject(process, 0u) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
}

bool waitUntil(const auto& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

class ControlledProvider final : public provider::LauncherProviderAdapter {
public:
    explicit ControlledProvider(std::filesystem::path target)
        : target_(std::move(target)) {}

    provider::ProviderDescriptor descriptor() const noexcept override {
        return {"controlled", provider::ProviderAvailability::Available, 41u,
                {true, true, true, true, true}};
    }
    provider::DiscoveryResponse discoverInstalledGames() noexcept override {
        return {provider::ProviderResult::Success, 41u, {}, {}};
    }
    provider::AccountReferenceResponse listAccountReferences() noexcept override {
        return {provider::ProviderResult::Success, 41u, {}, {}};
    }
    provider::LaunchResponse buildLaunchRequest(
        const provider::LaunchSelection& selection) noexcept override {
        provider::ProviderLaunchRequest request;
        request.providerId = "controlled";
        request.gameId = selection.gameId;
        request.providerAppId = selection.providerAppId;
        request.metadataRevision = 41u;
        request.targetKind = provider::LaunchTargetKind::Executable;
        request.target = target_.wstring();
        request.arguments = {L"--depth", L"1", L"--sleep-ms", L"30000",
                             L"--descendant-sleep-ms", L"30000"};
        request.workingDirectory = target_.parent_path().wstring();
        request.launchCorrelationId = "controlled-" + selection.gameId;
        return {provider::ProviderResult::Success, std::move(request), {}};
    }
    provider::ProcessIdentificationResponse identifyProcesses(
        const provider::ProcessIdentificationQuery&) noexcept override {
        return {provider::ProviderResult::Success, 41u, {}, {}};
    }

private:
    std::filesystem::path target_;
};

profile::CompatibilityReference compatibility() {
    return {"compat-process-flow", "controlled-process", 3u};
}

profile::SeatConfigDocument seatDocument() {
    profile::SeatConfigDocument document;
    for (SeatId id : {SeatId{1}, SeatId{2}}) {
        profile::PersistedSeatConfig seat;
        seat.seatId = id;
        seat.name = L"Process Seat " + std::to_wstring(id);
        seat.displayIds = {L"display-" + std::to_wstring(id)};
        seat.primaryDisplayId = seat.displayIds.front();
        document.seats.push_back(std::move(seat));
    }
    return document;
}

catalog::LocalGameCatalog gameLibrary() {
    catalog::LocalGameCatalog library;
    for (const auto& pair : {
             std::pair<std::string, std::wstring>{"process-game-one", L"Process Game One"},
             std::pair<std::string, std::wstring>{"process-game-two", L"Process Game Two"}}) {
        profile::GameRecord game;
        game.gameId = pair.first;
        game.providerId = "controlled";
        game.providerAppId = pair.first;
        game.title = pair.second;
        game.installRoot = L"C:\\Controlled";
        game.executableCandidates = {L"C:\\Controlled\\target.exe"};
        game.compatibility = compatibility();
        library.entries.push_back(
            {std::move(game), std::nullopt,
             sizeof(void*) == 8u ? catalog::ExecutableArchitecture::X64
                                  : catalog::ExecutableArchitecture::X86,
             catalog::CatalogStaleness::Current, 1u});
    }
    return library;
}

SeatLaunchContext launchContext(ControlledProvider& provider) {
    SeatLaunchContext context;
    context.seats = seatDocument();
    context.library = gameLibrary();
    context.players.players = {
        {"process-player-one", L"Process Player One", "en-US", {}},
        {"process-player-two", L"Process Player Two", "en-US", {}}};
    context.playerPresentation = {
        {"process-player-one", std::nullopt, {"process-game-one"}, 1u},
        {"process-player-two", std::nullopt, {"process-game-two"}, 2u}};
    context.providers = {{"controlled", &provider}};
    for (const auto& gameId : {std::string{"process-game-one"},
                               std::string{"process-game-two"}}) {
        plan::GameRuntimeRequirement requirement;
        requirement.gameId = gameId;
        requirement.revision = 5u;
        requirement.compatibility = compatibility();
        context.requirements.push_back(std::move(requirement));
    }
    return context;
}

class ControlledInstance final : public ISeatGameInstance {
public:
    ControlledInstance(SeatId seatId, plan::SeatProviderLaunchPlan plan)
        : seatId_(seatId), plan_(std::move(plan)) {}

    bool start(const SeatGameBinding& binding, std::string& error) override {
        if (binding.playerId != plan_.playerId || binding.gameId != plan_.gameId ||
            plan_.launchRequest.targetKind != provider::LaunchTargetKind::Executable) {
            error = "binding does not match installed typed plan";
            return false;
        }
        ProcessLaunchSpec spec;
        spec.seatId = seatId_;
        spec.executablePath = plan_.launchRequest.target;
        spec.arguments = plan_.launchRequest.arguments;
        if (plan_.launchRequest.workingDirectory) {
            spec.workingDirectory = *plan_.launchRequest.workingDirectory;
        }
        spec.architecture = sizeof(void*) == 8u
            ? ProcessArchitecture::X64 : ProcessArchitecture::X86;
        auto launched = ProcessLauncher::launch(spec, &error);
        if (!launched.group) return false;
        identity_ = launched.root;
        group_ = std::move(launched.group);
        return true;
    }
    bool stop(std::string& error) noexcept override {
        if (!group_) return true;
        ProcessStopPolicy policy;
        policy.gracefulTimeoutMs = 50u;
        policy.forcedTimeoutMs = 3000u;
        return group_->stop(policy, &error);
    }
    bool verifyStopped(std::string& error) noexcept override {
        if (!group_ || group_->waitForEmpty(0u)) return true;
        error = "controlled process group remains live";
        return false;
    }
    bool running() const noexcept override {
        return group_ && group_->snapshot().runningCount() != 0u;
    }
    ProcessIdentity identity() const noexcept { return identity_; }
    std::size_t runningCount() const {
        return group_ ? group_->snapshot().runningCount() : 0u;
    }

private:
    SeatId seatId_{0};
    plan::SeatProviderLaunchPlan plan_;
    ProcessIdentity identity_;
    std::unique_ptr<SeatProcessGroup> group_;
};

class DynamicFactory final : public ISeatGameInstanceFactory {
public:
    std::unique_ptr<ISeatGameInstance> create(SeatId seatId,
                                               std::string& error) override {
        const auto found = plans.find(seatId);
        if (found == plans.end()) {
            error = "no typed plan installed for Seat";
            return {};
        }
        auto instance = std::make_unique<ControlledInstance>(seatId, found->second);
        created[seatId].push_back(instance.get());
        return instance;
    }

    std::map<SeatId, plan::SeatProviderLaunchPlan> plans;
    std::map<SeatId, std::vector<ControlledInstance*>> created;
};

class DynamicInstaller final : public ISeatLaunchPlanInstaller {
public:
    explicit DynamicInstaller(DynamicFactory& factory) : factory_(factory) {}
    bool install(SeatId seatId, const plan::ProviderAwareLaunchPlan& fullPlan,
                 const plan::SeatProviderLaunchPlan& seatPlan,
                 std::string& error) override {
        const auto exact = std::find(fullPlan.seats.begin(), fullPlan.seats.end(), seatPlan);
        if (seatId == 0 || seatPlan.seatId != seatId || exact == fullPlan.seats.end() ||
            seatPlan.launchRequest.targetKind != provider::LaunchTargetKind::Executable) {
            error = "installer rejected mismatched typed plan";
            return false;
        }
        factory_.plans[seatId] = seatPlan;
        return true;
    }
    bool rollback(SeatId seatId, std::string&) noexcept override {
        factory_.plans.erase(seatId);
        return true;
    }

private:
    DynamicFactory& factory_;
};

class RuntimeHostAdapter final : public ISeatLaunchHost {
public:
    RuntimeHostAdapter(RuntimeHost& host, SeatId seatId)
        : host_(host), seatId_(seatId) {}
    std::optional<HostRuntimeSnapshot> resnapshot(std::string&) override {
        return host_.snapshot();
    }
    std::optional<SeatGameCommandResult> assign(
        const SeatGameBinding& binding, std::string&) override {
        return host_.assignSeatGame(seatId_, binding, correlation_++);
    }
    std::optional<SeatGameCommandResult> start(std::string&) override {
        return host_.startSeatGame(seatId_, correlation_++);
    }
    std::optional<SeatGameCommandResult> stop(std::string&) override {
        return host_.stopSeatGame(seatId_, correlation_++);
    }

private:
    RuntimeHost& host_;
    SeatId seatId_{0};
    std::uint64_t correlation_{1000u};
};

void testIdleSeatStartsWhileOtherExactTreeSurvives() {
    const auto child = executableDirectory() / L"hydra_process_tree_child.exe";
    ControlledProvider provider(child);
    auto factory = std::make_shared<DynamicFactory>();
    DynamicInstaller installer(*factory);

    launcher_ui::LauncherUiModel initialPlanModel;
    auto initialContext = launchContext(provider);
    check(initialPlanModel.initializeShared(
              initialContext.seats, initialContext.library, initialContext.players,
              initialContext.playerPresentation, initialContext.setups,
              initialContext.providers, initialContext.requirements).succeeded() &&
              initialPlanModel.selectGame(
                  1u, "process-player-one", "process-game-one").succeeded(),
          "controlled Seat 1 initial plan uses the shared P6 compiler path");
    const auto initialPreview = initialPlanModel.preview();
    check(initialPreview.summary.canActivate && initialPreview.compileResult.plan &&
              initialPreview.compileResult.plan->seats.size() == 1u,
          "controlled Seat 1 initial plan is validated");
    if (!initialPreview.compileResult.plan) return;
    std::string error;
    check(installer.install(1u, *initialPreview.compileResult.plan,
                            initialPreview.compileResult.plan->seats.front(), error),
          "controlled Seat 1 typed plan installs");

    RuntimeHost host({}, factory);
    std::vector<SeatConfig> runtimeSeats;
    for (const auto& seat : seatDocument().seats) {
        runtimeSeats.push_back(profile::makeRuntimeSeatConfig(seat));
    }
    check(host.loadProfile(runtimeSeats, 1u, 1u).succeeded() &&
              host.plan(2u).succeeded() && host.prepare(3u).succeeded() &&
              host.start(4u).succeeded() &&
              host.assignSeatGame(
                  1u, {"process-player-one", "process-game-one"}, 5u).succeeded() &&
              host.startSeatGame(1u, 6u).succeeded(),
          "controlled Seat 1 enters Playing with an owned process tree");
    if (factory->created[1u].empty()) return;
    auto* firstInstance = factory->created[1u].front();
    check(waitUntil([&] { return firstInstance->runningCount() >= 2u; },
                    std::chrono::milliseconds(2500)),
          "Seat 1 root and descendant are contained and live");
    const auto firstIdentity = firstInstance->identity();
    check(firstIdentity.valid() && exactProcessRunning(firstIdentity),
          "Seat 1 exact PID and creation time are live before Seat 2 selection");

    SeatLaunchFlow flow(2u);
    RuntimeHostAdapter hostAdapter(host, 2u);
    check(flow.initialize(launchContext(provider)).succeeded() &&
              flow.sync(host.snapshot()).succeeded() &&
              flow.select("process-player-two", "process-game-two").succeeded() &&
              flow.activate(hostAdapter, installer).succeeded(),
          "idle Seat 2 selects and reaches Playing through the shared flow");
    check(!factory->created[2u].empty(),
          "Seat 2 activation creates its exact owned process instance");
    if (factory->created[2u].empty()) return;
    auto* secondInstance = factory->created[2u].front();
    check(waitUntil([&] { return secondInstance->runningCount() >= 2u; },
                    std::chrono::milliseconds(2500)) &&
              exactProcessRunning(firstIdentity) &&
              firstInstance->identity().sameInstance(firstIdentity) &&
              host.snapshot().seatGames[0].phase == SeatGamePhase::Playing &&
              host.snapshot().seatGames[1].phase == SeatGamePhase::Playing,
          "Seat 2 start preserves Seat 1 exact process identity and Playing state");

    const auto secondIdentity = secondInstance->identity();
    check(host.stopSeatGame(2u, 2000u).succeeded() &&
              !exactProcessRunning(secondIdentity) && exactProcessRunning(firstIdentity),
          "Seat 2 stop cleans only Seat 2 while Seat 1 remains live");
    check(host.stopSeatGame(1u, 2001u).succeeded() &&
              host.stopAndReturnToWindows(2002u).succeeded() &&
              !exactProcessRunning(firstIdentity) && !exactProcessRunning(secondIdentity),
          "controlled final rollback leaves no owned process orphan");
}

#endif

} // namespace

int main() {
#ifdef _WIN32
    testIdleSeatStartsWhileOtherExactTreeSurvives();
#else
    std::cout << "Seat launch flow process test is Windows-only.\n";
#endif
    if (failures != 0) {
        std::cerr << failures << " Seat launch process test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Seat launch flow process tests passed.\n";
    return EXIT_SUCCESS;
}
