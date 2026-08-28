#include "hydra/process_launcher.hpp"
#include "hydra/host_protocol.hpp"
#include "hydra/host_transport.hpp"
#include "hydra/runtime_host.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace hydra::process;
using namespace hydra::runtime;

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

class ProcessInstance final : public ISeatGameInstance {
public:
    ProcessInstance(hydra::SeatId seatId, std::filesystem::path executable)
        : seatId_(seatId), executable_(std::move(executable)) {}

    bool start(const SeatGameBinding&, std::string& error) override {
        ProcessLaunchSpec spec;
        spec.seatId = seatId_;
        spec.executablePath = executable_.wstring();
        spec.workingDirectory = executable_.parent_path().wstring();
        spec.arguments = {L"--depth", L"1", L"--sleep-ms", L"30000",
                          L"--descendant-sleep-ms", L"30000"};
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
        error = "exact Seat process group remains live";
        return false;
    }

    bool running() const noexcept override {
        return group_ && group_->snapshot().runningCount() != 0u;
    }

    ProcessIdentity identity() const { return identity_; }
    std::size_t runningCount() const {
        return group_ ? group_->snapshot().runningCount() : 0u;
    }
    bool crash(std::string& error) {
        if (!group_) return false;
        ProcessStopPolicy policy;
        policy.gracefulTimeoutMs = 0u;
        policy.forcedTimeoutMs = 3000u;
        return group_->stop(policy, &error);
    }

private:
    hydra::SeatId seatId_{0};
    std::filesystem::path executable_;
    std::unique_ptr<SeatProcessGroup> group_;
    ProcessIdentity identity_;
};

class ProcessFactory final : public ISeatGameInstanceFactory {
public:
    explicit ProcessFactory(std::filesystem::path executable)
        : executable_(std::move(executable)) {}

    std::unique_ptr<ISeatGameInstance> create(hydra::SeatId seatId,
                                               std::string&) override {
        auto instance = std::make_unique<ProcessInstance>(seatId, executable_);
        created.push_back(instance.get());
        return instance;
    }

    std::vector<ProcessInstance*> created;

private:
    std::filesystem::path executable_;
};

bool waitUntil(const auto& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
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

std::vector<hydra::SeatConfig> controlledSeats() {
    hydra::SeatConfig first;
    first.seatId = 1;
    first.name = L"Seat 1";
    hydra::SeatConfig second;
    second.seatId = 2;
    second.name = L"Seat 2";
    return {first, second};
}

int runCrashHostFixture(const std::filesystem::path& readyFile) {
    const auto child = executableDirectory() / L"hydra_process_tree_child.exe";
    auto factory = std::make_shared<ProcessFactory>(child);
    RuntimeHost host({}, factory);
    if (!host.loadProfile(controlledSeats(), 700).succeeded() ||
        !host.plan(701).succeeded() || !host.prepare(702).succeeded() ||
        !host.start(703).succeeded() ||
        !host.assignSeatGame(1, {"crash-player-a", "crash-game-a"}, 704).succeeded() ||
        !host.startSeatGame(1, 705).succeeded() ||
        !host.assignSeatGame(2, {"crash-player-b", "crash-game-b"}, 706).succeeded() ||
        !host.startSeatGame(2, 707).succeeded() || factory->created.size() != 2u ||
        !waitUntil([&] { return factory->created[0]->runningCount() >= 2u &&
                                factory->created[1]->runningCount() >= 2u; },
                   std::chrono::milliseconds(2500))) {
        return 2;
    }
    std::ofstream ready(readyFile, std::ios::trunc);
    if (!ready) return 3;
    for (const auto* instance : factory->created) {
        const auto identity = instance->identity();
        ready << identity.processId << ' ' << identity.creationTime100ns << '\n';
    }
    ready.close();
    for (;;) Sleep(1000u);
}

int runUiClientFixture(const std::filesystem::path& readyFile) {
    hydra::hostipc::HostControlClient client;
    std::string error;
    if (!client.connect(hydra::hostipc::ClientRole::ReadOnly, 5000u, &error)) return 4;
    const auto snapshot = client.getSnapshot(5000u, &error);
    if (!snapshot || snapshot->seatGames.size() != 2u ||
        snapshot->seatGames[0].phase != SeatGamePhase::Playing ||
        snapshot->seatGames[1].phase != SeatGamePhase::Playing) {
        return 5;
    }
    std::ofstream ready(readyFile, std::ios::trunc);
    if (!ready) return 6;
    ready << "both-playing\n";
    ready.close();
    for (;;) Sleep(1000u);
}

std::vector<ProcessIdentity> readCrashFixtureIdentities(
    const std::filesystem::path& readyFile) {
    std::ifstream ready(readyFile);
    std::vector<ProcessIdentity> identities;
    ProcessIdentity identity;
    while (ready >> identity.processId >> identity.creationTime100ns) {
        identity.executablePath = L"controlled-crash-fixture";
        identities.push_back(identity);
        identity = {};
    }
    return identities;
}

void testIndependentOwnedProcessTrees() {
    const auto child = executableDirectory() / L"hydra_process_tree_child.exe";
    auto factory = std::make_shared<ProcessFactory>(child);
    RuntimeHost host({}, factory);
    hydra::SeatConfig firstSeat;
    firstSeat.seatId = 1;
    firstSeat.name = L"Seat 1";
    hydra::SeatConfig secondSeat;
    secondSeat.seatId = 2;
    secondSeat.name = L"Seat 2";
    check(host.loadProfile({firstSeat, secondSeat}, 1).succeeded() &&
              host.plan(2).succeeded() && host.prepare(3).succeeded() &&
              host.start(4).succeeded(),
          "authoritative host prepares shared runtime before controlled Seat games");

    check(host.assignSeatGame(1, {"player-a", "controlled-a"}, 10).succeeded() &&
              host.startSeatGame(1, 11).succeeded(),
          "Seat 1 controlled process tree starts");
    check(host.assignSeatGame(2, {"player-b", "controlled-b"}, 12).succeeded() &&
              host.startSeatGame(2, 13).succeeded(),
          "Seat 2 controlled process tree starts independently");
    if (factory->created.size() != 2u) return;

    const auto reconnectedSnapshot = hydra::hostipc::decodeSnapshot(
        hydra::hostipc::encodeSnapshot(host.snapshot()));
    check(reconnectedSnapshot && reconnectedSnapshot->seatGames.size() == 2u &&
              reconnectedSnapshot->seatGames[0].phase == SeatGamePhase::Playing &&
              reconnectedSnapshot->seatGames[1].phase == SeatGamePhase::Playing,
          "UI-style reconnect snapshot retains both authoritative Playing Seat states");

    auto* first = factory->created[0];
    auto* second = factory->created[1];
    check(waitUntil([&] { return first->runningCount() >= 2u &&
                                 second->runningCount() >= 2u; },
                    std::chrono::milliseconds(2500)),
          "both exact Job Object trees track their controlled descendants");
    const auto firstIdentity = first->identity();
    check(firstIdentity.valid(), "Seat 1 exact process creation identity is captured");

    const auto stoppedSecond = host.stopSeatGame(2, 14);
    check(stoppedSecond.succeeded() && first->running() &&
              first->identity().sameInstance(firstIdentity),
          "Seat 2 stop leaves Seat 1 exact process instance alive and unchanged");

    check(host.assignSeatGame(2, {"player-c", "controlled-c"}, 15).succeeded() &&
              host.startSeatGame(2, 16).succeeded() && factory->created.size() == 3u,
          "Seat 2 starts another controlled game without restarting Seat 1");
    check(first->running() && first->identity().sameInstance(firstIdentity),
          "Seat 1 remains unchanged across Seat 2 restart");

    auto* crashingSecond = factory->created.back();
    std::string crashError;
    check(crashingSecond->crash(crashError), "controlled Seat 2 crash fixture exits exactly owned tree");
    const auto degraded = host.observeSeatGameExit(2, false, "controlled Seat 2 crash", 17);
    check(degraded.code == SeatGameResultCode::BackendFailure &&
              degraded.seats[1].phase == SeatGamePhase::Degraded &&
              first->running() && first->identity().sameInstance(firstIdentity),
          "Seat 2 crash degrades only Seat 2 while Seat 1 exact tree remains unchanged");
    check(host.stopSeatGame(2, 18).succeeded(),
          "verified crashed Seat acknowledges back to Idle without global Stop");

    std::uint64_t correlation = 30;
    for (int cycle = 0; cycle < 10; ++cycle) {
        check(host.assignSeatGame(2, {"player-cycle", "controlled-cycle"}, correlation++).succeeded() &&
                  host.startSeatGame(2, correlation++).succeeded() &&
                  host.stopSeatGame(2, correlation++).succeeded() && first->running(),
              "repeated Seat 2 start/stop cycle preserves Seat 1");
    }

    check(host.assignSeatGame(2, {"player-final", "controlled-final"}, correlation++).succeeded() &&
              host.startSeatGame(2, correlation++).succeeded(),
          "final Seat 2 tree starts for forced global recovery");
    const auto finalSecondIdentity = factory->created.back()->identity();
    const auto reset = host.reset(correlation++);
    check(reset.succeeded() && host.snapshot().sessionPhase == SeatSessionPhase::Idle &&
              !exactProcessRunning(firstIdentity) && !exactProcessRunning(finalSecondIdentity),
          "forced global reset cleans both exact Seat trees and restores Idle postcondition");
}

void testAuthoritativeHostTeardownLeavesNoOwnedOrphans() {
    const auto child = executableDirectory() / L"hydra_process_tree_child.exe";
    auto factory = std::make_shared<ProcessFactory>(child);
    auto host = std::make_unique<RuntimeHost>(
        std::vector<std::shared_ptr<IRuntimeBackend>>{}, factory);
    hydra::SeatConfig first;
    first.seatId = 1;
    first.name = L"Seat 1";
    hydra::SeatConfig second;
    second.seatId = 2;
    second.name = L"Seat 2";
    check(host->loadProfile({first, second}, 100).succeeded() &&
              host->plan(101).succeeded() && host->prepare(102).succeeded() &&
              host->start(103).succeeded() &&
              host->assignSeatGame(1, {"player-a", "teardown-a"}, 104).succeeded() &&
              host->startSeatGame(1, 105).succeeded() &&
              host->assignSeatGame(2, {"player-b", "teardown-b"}, 106).succeeded() &&
              host->startSeatGame(2, 107).succeeded(),
          "host teardown fixture starts two exact Seat process trees");
    if (factory->created.size() != 2u) return;
    const auto firstIdentity = factory->created[0]->identity();
    const auto secondIdentity = factory->created[1]->identity();
    host.reset();
    check(!exactProcessRunning(firstIdentity) && !exactProcessRunning(secondIdentity),
          "authoritative host teardown leaves zero owned Seat process orphans");
}

void testForcedHostCrashClosesBothSeatJobs() {
    wchar_t temporaryDirectory[MAX_PATH + 1]{};
    const DWORD temporaryLength = GetTempPathW(MAX_PATH, temporaryDirectory);
    check(temporaryLength != 0u && temporaryLength <= MAX_PATH,
          "forced Host crash fixture resolves temporary directory");
    if (temporaryLength == 0u || temporaryLength > MAX_PATH) return;
    const auto readyFile = std::filesystem::path(temporaryDirectory) /
        (L"hydraseat-p4-host-crash-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()) + L".txt");
    const auto executable = executableDirectory() /
        L"seat_game_process_lifecycle_tests.exe";
    std::wstring command = L"\"" + executable.wstring() +
        L"\" --crash-host-fixture \"" + readyFile.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(
        executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(),
        &startup, &process);
    check(launched != FALSE, "separate authoritative Host crash fixture launches");
    if (launched == FALSE) return;

    std::vector<ProcessIdentity> identities;
    const bool ready = waitUntil([&] {
        identities = readCrashFixtureIdentities(readyFile);
        return identities.size() == 2u;
    }, std::chrono::milliseconds(5000));
    check(ready && exactProcessRunning(identities[0]) &&
              exactProcessRunning(identities[1]),
          "crash fixture publishes two exact live Seat roots");
    if (ready) {
        check(TerminateProcess(process.hProcess, 91u) != FALSE,
              "authoritative Host fixture is forcibly terminated");
        (void)WaitForSingleObject(process.hProcess, 5000u);
        check(waitUntil([&] { return !exactProcessRunning(identities[0]) &&
                                     !exactProcessRunning(identities[1]); },
                        std::chrono::milliseconds(5000)),
              "forced Host crash closes both Seat Job Objects without exact root orphans");
    } else {
        (void)TerminateProcess(process.hProcess, 92u);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::error_code ignored;
    (void)std::filesystem::remove(readyFile, ignored);
}

void testUiCrashAndReconnectKeepsAuthoritativeSeatState() {
    const auto child = executableDirectory() / L"hydra_process_tree_child.exe";
    auto factory = std::make_shared<ProcessFactory>(child);
    RuntimeHost host({}, factory);
    check(host.loadProfile(controlledSeats(), 800).succeeded() &&
              host.plan(801).succeeded() && host.prepare(802).succeeded() &&
              host.start(803).succeeded() &&
              host.assignSeatGame(1, {"ui-player-a", "ui-game-a"}, 804).succeeded() &&
              host.startSeatGame(1, 805).succeeded() &&
              host.assignSeatGame(2, {"ui-player-b", "ui-game-b"}, 806).succeeded() &&
              host.startSeatGame(2, 807).succeeded(),
          "UI reconnect fixture starts both authoritative Seat trees");

    hydra::hostipc::HostControlServer server(host);
    std::string serverError;
    std::thread serverThread([&] { (void)server.serve(&serverError); });

    wchar_t temporaryDirectory[MAX_PATH + 1]{};
    const DWORD temporaryLength = GetTempPathW(MAX_PATH, temporaryDirectory);
    if (temporaryLength == 0u || temporaryLength > MAX_PATH) {
        check(false, "UI crash fixture resolves temporary directory");
        server.requestStop();
        serverThread.join();
        return;
    }
    const auto readyFile = std::filesystem::path(temporaryDirectory) /
        (L"hydraseat-p4-ui-crash-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()) + L".txt");
    const auto executable = executableDirectory() /
        L"seat_game_process_lifecycle_tests.exe";
    std::wstring command = L"\"" + executable.wstring() +
        L"\" --ui-client-fixture \"" + readyFile.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(
        executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(),
        &startup, &process);
    check(launched != FALSE, "separate read-only UI fixture connects to authoritative Host");
    const bool ready = launched != FALSE && waitUntil(
        [&] { return std::filesystem::exists(readyFile); },
        std::chrono::milliseconds(5000));
    check(ready, "UI fixture observes both Seats Playing before forced crash");
    if (launched != FALSE) {
        (void)TerminateProcess(process.hProcess, 93u);
        (void)WaitForSingleObject(process.hProcess, 5000u);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }

    hydra::hostipc::HostControlClient reopened;
    std::string reconnectError;
    const bool connected = reopened.connect(
        hydra::hostipc::ClientRole::ReadOnly, 5000u, &reconnectError);
    const auto snapshot = connected
        ? reopened.getSnapshot(5000u, &reconnectError) : std::nullopt;
    check(snapshot && snapshot->seatGames.size() == 2u &&
              snapshot->seatGames[0].phase == SeatGamePhase::Playing &&
              snapshot->seatGames[1].phase == SeatGamePhase::Playing &&
              factory->created.size() == 2u && factory->created[0]->running() &&
              factory->created[1]->running(),
          "UI crash/reopen resnapshots both Playing Seats without owning their lifetime");
    reopened.close();
    server.requestStop();
    serverThread.join();
    check(serverError.empty(), "UI crash/reconnect server exits without transport failure");
    std::error_code ignored;
    (void)std::filesystem::remove(readyFile, ignored);
}

void testRepeatedWholeMachineReturnCyclesLeaveNoRoots() {
    const auto child = executableDirectory() / L"hydra_process_tree_child.exe";
    auto factory = std::make_shared<ProcessFactory>(child);
    RuntimeHost host({}, factory);
    std::uint64_t correlation = 900;
    check(host.loadProfile(controlledSeats(), correlation++).succeeded(),
          "repeated global-return fixture loads the two-Seat profile once");
    for (int cycle = 0; cycle < 3; ++cycle) {
        check(host.plan(correlation++).succeeded() &&
                  host.prepare(correlation++).succeeded() &&
                  host.start(correlation++).succeeded(),
              "repeated global-return cycle activates shared runtime");
        const auto beforeCreate = factory->created.size();
        check(host.assignSeatGame(1, {"return-player-a", "return-game-a"},
                                  correlation++).succeeded() &&
                  host.startSeatGame(1, correlation++).succeeded() &&
                  host.assignSeatGame(2, {"return-player-b", "return-game-b"},
                                      correlation++).succeeded() &&
                  host.startSeatGame(2, correlation++).succeeded() &&
                  factory->created.size() == beforeCreate + 2u,
              "repeated global-return cycle starts two fresh exact Seat roots");
        if (factory->created.size() != beforeCreate + 2u) return;
        const auto first = factory->created[beforeCreate]->identity();
        const auto second = factory->created[beforeCreate + 1u]->identity();
        check(host.stopAndReturnToWindows(correlation++).succeeded() &&
                  host.snapshot().sessionPhase == SeatSessionPhase::Idle &&
                  !exactProcessRunning(first) && !exactProcessRunning(second),
              "explicit whole-machine return removes both exact roots on every cycle");
    }
}

#endif

} // namespace

int wmain(int argc, wchar_t* argv[]) {
#ifdef _WIN32
    if (argc == 3 && std::wstring_view(argv[1]) == L"--crash-host-fixture") {
        return runCrashHostFixture(argv[2]);
    }
    if (argc == 3 && std::wstring_view(argv[1]) == L"--ui-client-fixture") {
        return runUiClientFixture(argv[2]);
    }
    testIndependentOwnedProcessTrees();
    testAuthoritativeHostTeardownLeavesNoOwnedOrphans();
    testForcedHostCrashClosesBothSeatJobs();
    testUiCrashAndReconnectKeepsAuthoritativeSeatState();
    testRepeatedWholeMachineReturnCyclesLeaveNoRoots();
#endif
    if (failures != 0) {
        std::cerr << failures << " Seat game process lifecycle test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Seat game process lifecycle tests passed.\n";
    return EXIT_SUCCESS;
}
