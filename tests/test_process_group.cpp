#include "hydra/process_launcher.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
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

std::filesystem::path childExecutable() {
    return executableDirectory() / L"hydra_process_tree_child.exe";
}

std::filesystem::path tempCaseDirectory(const wchar_t* name) {
    const auto directory = std::filesystem::temp_directory_path() /
        (std::wstring(L"hydraseat-p4-proc-") + name + L"-" +
         std::to_wstring(GetCurrentProcessId()));
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    return directory;
}

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

std::vector<std::uint32_t> readPids(const std::filesystem::path& path) {
    std::ifstream stream(path);
    std::vector<std::uint32_t> result;
    std::uint64_t value = 0;
    while (stream >> value) {
        if (value != 0 && value <= 0xffffffffull) {
            result.push_back(static_cast<std::uint32_t>(value));
        }
    }
    return result;
}

std::uint64_t creationTime100ns(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(process, &created, &exited, &kernel, &user) == FALSE) return 0;
    ULARGE_INTEGER value{};
    value.LowPart = created.dwLowDateTime;
    value.HighPart = created.dwHighDateTime;
    return value.QuadPart;
}

struct FixtureIdentity {
    std::uint32_t processId{0};
    std::uint64_t creationTime{0};
};

FixtureIdentity captureFixtureIdentity(std::uint32_t processId) {
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE, processId);
    if (process == nullptr) return {};
    FixtureIdentity identity{processId, creationTime100ns(process)};
    CloseHandle(process);
    return identity;
}

bool processAlive(const FixtureIdentity& identity) {
    if (identity.processId == 0 || identity.creationTime == 0) return false;
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE, identity.processId);
    if (process == nullptr) return false;
    const bool same = creationTime100ns(process) == identity.creationTime;
    const bool alive = same && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

void terminateFixtureProcess(const FixtureIdentity& identity) {
    if (identity.processId == 0 || identity.creationTime == 0) return;
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION |
                                     PROCESS_TERMINATE,
                                 FALSE, identity.processId);
    if (process == nullptr) return;
    if (creationTime100ns(process) == identity.creationTime &&
        WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
        (void)TerminateProcess(process, 0x50345453u); // "P4TS"
        (void)WaitForSingleObject(process, 2000u);
    }
    CloseHandle(process);
}

ProcessLaunchSpec baseSpec(hydra::SeatId seatId) {
    ProcessLaunchSpec spec;
    spec.seatId = seatId;
    spec.executablePath = childExecutable().wstring();
    spec.workingDirectory = executableDirectory().wstring();
    spec.architecture = sizeof(void*) == 8u
        ? ProcessArchitecture::X64 : ProcessArchitecture::X86;
    return spec;
}

void testParentChildGrandchildAndRootExit() {
    const auto directory = tempCaseDirectory(L"tree");
    const auto pidFile = directory / L"pids.txt";
    auto spec = baseSpec(11);
    spec.arguments = {L"--depth", L"2", L"--sleep-ms", L"250",
                      L"--descendant-sleep-ms", L"2500",
                      L"--pid-file", pidFile.wstring()};

    std::string error;
    auto launched = ProcessLauncher::launch(spec, &error);
    check(launched.group != nullptr, "full Job Object process group launches");
    if (!launched.group) return;
    check(launched.group->capability() == ChildTrackingCapability::FullJobObject,
          "strict launch reports full Job Object capability");
    check(launched.root.valid(), "root process has PID plus creation identity");

    const bool trackedThree = waitUntil([&] {
        return launched.group->snapshot().processes.size() >= 3u;
    }, std::chrono::milliseconds(1500));
    check(trackedThree, "parent, child, and grandchild are tracked by completion events");

    const auto initial = launched.group->snapshot();
    std::set<std::uint32_t> ids;
    bool childOfRoot = false;
    bool grandchild = false;
    for (const auto& record : initial.processes) {
        ids.insert(record.identity.processId);
        if (!record.root && record.parentProcessId == launched.root.processId) childOfRoot = true;
    }
    for (const auto& record : initial.processes) {
        if (!record.root && record.parentProcessId != launched.root.processId &&
            ids.contains(record.parentProcessId)) {
            grandchild = true;
        }
    }
    check(ids.size() == initial.processes.size(), "tracked process PIDs are unique while alive");
    check(childOfRoot && grandchild, "process snapshot preserves parent/child/grandchild topology");

    const bool rootExitedFirst = waitUntil([&] {
        const auto snapshot = launched.group->snapshot();
        bool rootExited = false;
        for (const auto& record : snapshot.processes) {
            if (record.root) rootExited = record.exited;
        }
        return rootExited && snapshot.runningCount() >= 1u;
    }, std::chrono::milliseconds(1800));
    check(rootExitedFirst, "root exit does not lose still-running descendants");

    ProcessStopPolicy stop;
    stop.gracefulTimeoutMs = 20;
    stop.forcedTimeoutMs = 2500;
    check(launched.group->stop(stop, &error), "owned descendant tree force-cleans after graceful timeout");
    check(launched.group->snapshot().runningCount() == 0u,
          "no Job Object test process remains after cleanup");

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

void testStrictBreakawayIsRejected() {
    const auto directory = tempCaseDirectory(L"strict-breakaway");
    const auto pidFile = directory / L"pids.txt";
    auto spec = baseSpec(12);
    spec.arguments = {L"--depth", L"1", L"--sleep-ms", L"300",
                      L"--descendant-sleep-ms", L"2500", L"--breakaway-child",
                      L"--pid-file", pidFile.wstring()};

    std::string error;
    auto launched = ProcessLauncher::launch(spec, &error);
    check(launched.group != nullptr, "strict breakaway fixture root launches");
    if (!launched.group) return;
    check(launched.group->waitForEmpty(1800u),
          "strict Job Object root exits without an escaped child");
    const auto pids = readPids(pidFile);
    check(pids.size() == 1u && pids.front() == launched.root.processId,
          "CREATE_BREAKAWAY_FROM_JOB is rejected when capability is strict");

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

void testAllowedBreakawayIsExplicitAndUnowned() {
    const auto directory = tempCaseDirectory(L"allowed-breakaway");
    const auto pidFile = directory / L"pids.txt";
    auto spec = baseSpec(13);
    spec.containment = ProcessContainmentPolicy::AllowBreakawayChildren;
    spec.arguments = {L"--depth", L"1", L"--sleep-ms", L"300",
                      L"--descendant-sleep-ms", L"3000", L"--breakaway-child",
                      L"--pid-file", pidFile.wstring()};

    std::string error;
    auto launched = ProcessLauncher::launch(spec, &error);
    check(launched.group != nullptr, "breakaway-capable process group launches");
    if (!launched.group) return;
    check(launched.group->capability() == ChildTrackingCapability::JobObjectBreakawayAllowed,
          "weaker breakaway tracking capability is explicit");

    const bool childReported = waitUntil([&] { return readPids(pidFile).size() >= 2u; },
                                         std::chrono::milliseconds(1200));
    check(childReported, "breakaway child fixture reports its independent PID");
    const auto pids = readPids(pidFile);
    std::uint32_t escapedPid = 0;
    for (const auto pid : pids) {
        if (pid != launched.root.processId) escapedPid = pid;
    }
    check(escapedPid != 0, "breakaway child PID differs from owned root");
    check(launched.group->waitForEmpty(1800u),
          "Job Object becomes empty after root exits while breakaway child is external");
    const auto escapedIdentity = captureFixtureIdentity(escapedPid);
    check(processAlive(escapedIdentity),
          "Seat process group does not terminate an explicitly unowned breakaway child");
    check(launched.group->snapshot().processes.size() == 1u,
          "escaped child is not forged into owned process snapshot");

    terminateFixtureProcess(escapedIdentity);
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

void testExplicitRootOnlyCapability() {
    const auto directory = tempCaseDirectory(L"root-only");
    const auto pidFile = directory / L"pids.txt";
    auto spec = baseSpec(14);
    spec.containment = ProcessContainmentPolicy::RootOnly;
    spec.arguments = {L"--depth", L"1", L"--sleep-ms", L"250",
                      L"--descendant-sleep-ms", L"3000",
                      L"--pid-file", pidFile.wstring()};

    std::string error;
    auto launched = ProcessLauncher::launch(spec, &error);
    check(launched.group != nullptr, "explicit root-only process launches");
    if (!launched.group) return;
    check(launched.group->capability() == ChildTrackingCapability::RootOnly,
          "profiles can explicitly declare root-only ownership capability");
    const bool childReported = waitUntil([&] { return readPids(pidFile).size() >= 2u; },
                                         std::chrono::milliseconds(1200));
    check(childReported, "root-only fixture creates an untracked descendant");
    const auto pids = readPids(pidFile);
    std::uint32_t childPid = 0;
    for (const auto pid : pids) {
        if (pid != launched.root.processId) childPid = pid;
    }
    check(launched.group->waitForEmpty(1800u), "root-only group tracks exact root exit");
    check(launched.group->snapshot().processes.size() == 1u,
          "root-only snapshot never claims descendant ownership");
    const auto childIdentity = captureFixtureIdentity(childPid);
    check(processAlive(childIdentity),
          "root-only cleanup leaves unowned descendant untouched");
    terminateFixtureProcess(childIdentity);

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

void testGracefulWindowStop() {
    auto spec = baseSpec(15);
    spec.arguments = {L"--depth", L"0", L"--sleep-ms", L"5000", L"--window"};

    std::string error;
    auto launched = ProcessLauncher::launch(spec, &error);
    check(launched.group != nullptr, "windowed owned process launches for graceful stop");
    if (!launched.group) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    ProcessStopPolicy stop;
    stop.gracefulTimeoutMs = 2000;
    stop.forcedTimeoutMs = 1000;
    stop.forceTerminate = false;
    check(launched.group->stop(stop, &error),
          "exact-identity WM_CLOSE performs graceful owned-process stop");
    check(launched.group->snapshot().runningCount() == 0u,
          "graceful stop leaves no owned process running");
}

void testEnvironmentArchitectureAndForcedTermination() {
    auto spec = baseSpec(16);
    spec.environmentOverrides = {{L"HYDRA_PROCESS_TEST", L"present"}};
    spec.arguments = {L"--depth", L"0", L"--sleep-ms", L"5000",
                      L"--require-env", L"HYDRA_PROCESS_TEST", L"present"};

    std::string error;
    auto launched = ProcessLauncher::launch(spec, &error);
    check(launched.group != nullptr, "explicit environment and architecture launch succeeds");
    if (!launched.group) return;
    check(waitUntil([&] { return launched.group->snapshot().runningCount() == 1u; },
                    std::chrono::milliseconds(500)),
          "root process is observable before forced stop");

    ProcessStopPolicy gracefulOnly;
    gracefulOnly.gracefulTimeoutMs = 25;
    gracefulOnly.forcedTimeoutMs = 25;
    gracefulOnly.forceTerminate = false;
    error.clear();
    check(!launched.group->stop(gracefulOnly, &error) &&
              launched.group->snapshot().runningCount() == 1u,
          "graceful timeout without force permission preserves the owned process");

    ProcessStopPolicy stop;
    stop.gracefulTimeoutMs = 25;
    stop.forcedTimeoutMs = 2000;
    stop.forcedExitCode = 0x13572468u;
    check(launched.group->stop(stop, &error),
          "graceful timeout escalates only the owned process group when force is allowed");
    const auto stopped = launched.group->snapshot();
    check(stopped.runningCount() == 0u, "forced policy leaves no owned process running");
    bool sawForcedCode = false;
    for (const auto& record : stopped.processes) {
        if (record.root && record.exited && record.exitCode == stop.forcedExitCode) {
            sawForcedCode = true;
        }
    }
    check(sawForcedCode, "forced termination exit code is retained in deterministic snapshot");

    auto mismatch = baseSpec(17);
    mismatch.architecture = sizeof(void*) == 8u
        ? ProcessArchitecture::X86 : ProcessArchitecture::X64;
    mismatch.arguments = {L"--sleep-ms", L"5000"};
    error.clear();
    auto rejected = ProcessLauncher::launch(mismatch, &error);
    check(rejected.group == nullptr && !error.empty(),
          "architecture mismatch is rejected before target resume");
}

#endif

void testPidReuseIdentityGuard() {
    ProcessIdentity oldProcess;
    oldProcess.processId = 4242;
    oldProcess.creationTime100ns = 100;
    oldProcess.executablePath = L"old.exe";
    ProcessIdentity reusedPid = oldProcess;
    reusedPid.creationTime100ns = 200;
    reusedPid.executablePath = L"new.exe";
    check(!oldProcess.sameInstance(reusedPid),
          "same PID with a different creation time is never the same process instance");
    check(oldProcess.sameInstance(oldProcess),
          "exact PID plus creation identity matches itself");
}

} // namespace

int main() {
    testPidReuseIdentityGuard();
#ifdef _WIN32
    testParentChildGrandchildAndRootExit();
    testStrictBreakawayIsRejected();
    testAllowedBreakawayIsExplicitAndUnowned();
    testExplicitRootOnlyCapability();
    testGracefulWindowStop();
    testEnvironmentArchitectureAndForcedTermination();
#else
    std::cout << "Process group integration tests are Windows-only.\n";
#endif

    if (failures != 0) {
        std::cerr << failures << " process group test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Process group tests passed.\n";
    return EXIT_SUCCESS;
}
