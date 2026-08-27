#include "hydra/runtime_host.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace hydra::runtime;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct FakeBackend final : IRuntimeBackend {
    RuntimeBackendKind backendKind{RuntimeBackendKind::Hardware};
    std::string backendName;
    std::vector<std::string>* log{nullptr};
    bool failPrepare{false};
    bool failStart{false};
    bool failRollback{false};
    bool failVerify{false};
    bool blockPrepare{false};
    bool prepared{false};
    bool started{false};
    std::mutex blockMutex;
    std::condition_variable blockCv;
    bool prepareEntered{false};
    bool prepareReleased{false};

    RuntimeBackendKind kind() const noexcept override { return backendKind; }
    std::string_view name() const noexcept override { return backendName; }

    bool prepare(const RuntimeSessionId&, std::span<const hydra::SeatConfig> seats,
                 std::string& error) override {
        if (log) log->push_back("prepare:" + backendName);
        if (seats.size() != 2u) {
            error = "unexpected Seat count";
            return false;
        }
        if (blockPrepare) {
            std::unique_lock lock(blockMutex);
            prepareEntered = true;
            blockCv.notify_all();
            blockCv.wait(lock, [&] { return prepareReleased; });
        }
        if (failPrepare) {
            error = "injected prepare failure";
            return false;
        }
        prepared = true;
        return true;
    }

    bool start(std::string& error) override {
        if (log) log->push_back("start:" + backendName);
        if (!prepared || failStart) {
            error = "injected start failure";
            return false;
        }
        started = true;
        return true;
    }

    bool rollback(std::string& error) noexcept override {
        if (log) log->push_back("rollback:" + backendName);
        if (failRollback) {
            error = "injected rollback failure";
            return false;
        }
        prepared = false;
        started = false;
        return true;
    }

    bool verifySafe(std::string& error) noexcept override {
        if (log) log->push_back("verify:" + backendName);
        if (failVerify || prepared || started) {
            error = "safe state not verified";
            return false;
        }
        return true;
    }

    void waitForPrepare() {
        std::unique_lock lock(blockMutex);
        blockCv.wait(lock, [&] { return prepareEntered; });
    }

    void releasePrepare() {
        std::lock_guard lock(blockMutex);
        prepareReleased = true;
        blockCv.notify_all();
    }
};

std::vector<hydra::SeatConfig> validSeats() {
    hydra::SeatConfig first;
    first.seatId = 1;
    first.name = L"Seat 1";
    first.displayIds = {L"display-a", L"display-b"};
    first.primaryDisplayId = L"display-a";
    hydra::SeatConfig second;
    second.seatId = 2;
    second.name = L"Seat 2";
    second.displayIds = {L"display-c"};
    second.primaryDisplayId = L"display-c";
    return {first, second};
}

void loadAndPlan(RuntimeHost& host) {
    check(host.loadProfile(validSeats(), 1).succeeded(), "valid profile loads");
    check(host.plan(2).succeeded(), "idle profile plans");
}

void testDefaultAndProfileValidation() {
    RuntimeHost host;
    const auto initial = host.snapshot();
    check(initial.hostPhase == HostLifecyclePhase::Running, "host starts running");
    check(initial.sessionPhase == SeatSessionPhase::Idle, "session starts idle");
    check(!initial.profileLoaded, "profile is initially absent");
    check(initial.managementSeatId == 1,
          "runtime authority defaults to Management Seat 1 before profile load");
    check(host.start(1).code == RuntimeResultCode::InvalidState,
          "start without preparation is rejected");

    auto duplicate = validSeats();
    duplicate[1].seatId = duplicate[0].seatId;
    check(host.loadProfile(duplicate, 2).code == RuntimeResultCode::InvalidProfile,
          "duplicate Seat ids are rejected");
    auto badPrimary = validSeats();
    badPrimary[0].primaryDisplayId = L"not-owned";
    check(host.loadProfile(badPrimary, 3).code == RuntimeResultCode::InvalidProfile,
          "unowned primary display is rejected");
    check(!host.snapshot().profileLoaded,
          "invalid profile leaves prior runtime profile unchanged");

    RuntimeHost managementHost;
    const auto managementLoaded = managementHost.loadProfile(validSeats(), 2, 4);
    check(managementLoaded.succeeded() && managementLoaded.snapshot.managementSeatId == 2,
          "explicit Management Seat 2 becomes host authority");
    const auto invalidManagement = managementHost.loadProfile(validSeats(), 99, 5);
    check(invalidManagement.code == RuntimeResultCode::InvalidProfile &&
              managementHost.snapshot().managementSeatId == 2,
          "unknown Management Seat is rejected without replacing prior authority");
}

void testLifecycleAndUiIndependence() {
    std::vector<std::string> log;
    auto hardware = std::make_shared<FakeBackend>();
    hardware->backendName = "hardware";
    hardware->log = &log;
    auto input = std::make_shared<FakeBackend>();
    input->backendKind = RuntimeBackendKind::Input;
    input->backendName = "input";
    input->log = &log;
    RuntimeHost host({hardware, input});
    loadAndPlan(host);
    check(!host.snapshot().sessionId.empty(), "planning creates session identity");
    check(host.prepare(3).succeeded(), "backends prepare");
    check(host.prepare(4).code == RuntimeResultCode::AlreadySatisfied,
          "prepare is idempotent");
    check(host.start(5).succeeded(), "prepared session starts");
    check(host.start(6).code == RuntimeResultCode::AlreadySatisfied,
          "start is idempotent");
    host.controlClientConnected();
    host.controlClientDisconnected();
    check(host.snapshot().sessionPhase == SeatSessionPhase::Active,
          "closing UI client leaves active session unchanged");
    check(host.exitHostWhenIdle(7).code == RuntimeResultCode::InvalidState,
          "active host exit is rejected");
    check(host.markDegraded("display missing", 8).succeeded(),
          "active session may become degraded");
    check(host.stopAndReturnToWindows(9).succeeded(),
          "stop verifies rollback and returns idle");
    check(log.size() >= 8u && log[4] == "rollback:input" &&
              log[5] == "rollback:hardware",
          "rollback executes prepared backends in reverse order");
    check(host.stopAndReturnToWindows(10).code == RuntimeResultCode::AlreadySatisfied,
          "repeated stop is safe");
    check(host.exitHostWhenIdle(11).succeeded(), "idle host may request exit");
}

void testReconfigureIsDistinctVerifiedStop() {
    auto backend = std::make_shared<FakeBackend>();
    backend->backendName = "reconfigure";
    RuntimeHost host({backend});
    loadAndPlan(host);
    check(host.prepare(3).succeeded(), "reconfigure session prepares");
    check(host.start(4).succeeded(), "reconfigure session starts");

    const auto reconfigure = host.beginReconfigure(5);
    check(reconfigure.succeeded() &&
              reconfigure.snapshot.sessionPhase == SeatSessionPhase::Idle &&
              reconfigure.snapshot.lastTransition &&
              reconfigure.snapshot.lastTransition->command == RuntimeCommand::BeginReconfigure,
          "BeginReconfigure performs verified rollback and records a distinct transition");
    check(!backend->prepared && !backend->started,
          "configuration editing is exposed only after backend safe state is restored");

    const auto repeated = host.beginReconfigure(6);
    check(repeated.code == RuntimeResultCode::AlreadySatisfied &&
              repeated.snapshot.sessionPhase == SeatSessionPhase::Idle &&
              repeated.snapshot.lastTransition &&
              repeated.snapshot.lastTransition->command == RuntimeCommand::BeginReconfigure,
          "repeated BeginReconfigure remains idempotent without collapsing into ordinary Stop");

    check(host.loadProfile(validSeats(), 2, 7).succeeded() &&
              host.snapshot().managementSeatId == 2,
          "fresh edited profile may replace authority only after reconfigure reaches Idle");
    check(host.plan(8).succeeded(), "edited profile receives a fresh immutable plan");
    check(host.prepare(9).succeeded() && host.start(10).succeeded(),
          "edited profile can restart through a fresh prepare/start sequence");
    check(host.stopAndReturnToWindows(11).succeeded(),
          "reconfigured session still uses ordinary verified Stop afterwards");
}

void testPartialFailureAndRecoveryRequired() {
    std::vector<std::string> log;
    auto first = std::make_shared<FakeBackend>();
    first->backendName = "first";
    first->log = &log;
    auto second = std::make_shared<FakeBackend>();
    second->backendName = "second";
    second->log = &log;
    second->failPrepare = true;
    RuntimeHost host({first, second});
    loadAndPlan(host);
    const auto failed = host.prepare(3);
    check(failed.code == RuntimeResultCode::BackendFailure,
          "partial prepare failure is explicit");
    check(failed.snapshot.sessionPhase == SeatSessionPhase::Idle,
          "successful partial rollback returns idle");
    check(!first->prepared, "previously prepared backend rolled back");
    check(log.size() >= 6u && log[2] == "rollback:second" &&
              log[3] == "rollback:first",
          "backend that reports prepare failure is also rolled back first");

    second->failPrepare = false;
    check(host.plan(4).succeeded(), "session may replan after safe failure");
    check(host.prepare(5).succeeded(), "second prepare succeeds after repair");
    second->failStart = true;
    first->failRollback = true;
    const auto recovery = host.start(6);
    check(recovery.code == RuntimeResultCode::RollbackFailure,
          "failed rollback is explicit");
    check(recovery.snapshot.sessionPhase == SeatSessionPhase::RecoveryRequired,
          "failed rollback enters RecoveryRequired");
    check(host.stopAndReturnToWindows(7).code == RuntimeResultCode::RecoveryRequired,
          "ordinary stop cannot forge recovery success");
    first->failRollback = false;
    second->failStart = false;
    check(host.reset(8).succeeded(), "verified reset retries unresolved rollback");
    check(host.snapshot().sessionPhase == SeatSessionPhase::Idle,
          "successful reset restores idle");
}

void testDiagnosticBounds() {
    RuntimeHost host;
    loadAndPlan(host);
    check(host.prepare(3).succeeded(), "diagnostic-bound session prepares");
    check(host.start(4).succeeded(), "diagnostic-bound session starts");

    const std::string oversized(4096u, 'x');
    const auto degraded = host.markDegraded(oversized, 5);
    check(degraded.succeeded(), "oversized degraded diagnostic is accepted in bounded form");
    check(degraded.diagnostic.size() == 2048u &&
              degraded.snapshot.diagnostic.size() == 2048u &&
              degraded.snapshot.lastTransition &&
              degraded.snapshot.lastTransition->diagnostic.size() == 2048u,
          "runtime and transition diagnostics are bounded to 2 KiB");
    bool seatsBounded = !degraded.snapshot.seats.empty();
    for (const auto& seat : degraded.snapshot.seats) {
        seatsBounded = seatsBounded && seat.diagnostic.size() == 2048u;
    }
    check(seatsBounded, "per-Seat diagnostics use the same 2 KiB bound");
    check(host.stopAndReturnToWindows(6).succeeded(),
          "diagnostic-bound session rolls back cleanly");
}

void testConcurrentMutationRejectionAndReaders() {
    auto blocking = std::make_shared<FakeBackend>();
    blocking->backendName = "blocking";
    blocking->blockPrepare = true;
    RuntimeHost host({blocking});
    loadAndPlan(host);
    RuntimeCommandResult prepareResult;
    std::thread worker([&] { prepareResult = host.prepare(3); });
    blocking->waitForPrepare();

    HostRuntimeSnapshot snapshotDuringPrepare;
    std::mutex snapshotMutex;
    std::condition_variable snapshotCv;
    bool snapshotReady = false;
    std::thread snapshotReader([&] {
        const auto snapshot = host.snapshot();
        {
            std::lock_guard lock(snapshotMutex);
            snapshotDuringPrepare = snapshot;
            snapshotReady = true;
        }
        snapshotCv.notify_one();
    });
    {
        std::unique_lock lock(snapshotMutex);
        check(snapshotCv.wait_for(lock, std::chrono::seconds(1),
                                  [&] { return snapshotReady; }),
              "snapshot remains readable while a backend prepare call is blocked");
    }
    if (snapshotReady) {
        check(snapshotDuringPrepare.sessionPhase == SeatSessionPhase::Planning,
              "in-flight prepare snapshot preserves the published session phase");
        check(snapshotDuringPrepare.mutationInProgress,
              "in-flight prepare snapshot reports mutation in progress");
        check(snapshotDuringPrepare.profileLoaded &&
                  snapshotDuringPrepare.seats.size() == 2u &&
                  !snapshotDuringPrepare.sessionId.empty(),
              "in-flight prepare snapshot preserves authoritative runtime identity");
    }

    const auto concurrent = host.start(4);
    check(concurrent.code == RuntimeResultCode::Busy,
          "concurrent mutation is rejected without waiting");
    check(concurrent.snapshot.sessionPhase == SeatSessionPhase::Planning &&
              concurrent.snapshot.profileLoaded &&
              concurrent.snapshot.seats.size() == 2u &&
              !concurrent.snapshot.sessionId.empty() &&
              concurrent.snapshot.mutationInProgress,
          "busy result carries the current authoritative runtime snapshot");
    blocking->releasePrepare();
    worker.join();
    snapshotReader.join();
    check(prepareResult.succeeded(), "original serialized mutation completes");

    std::atomic<bool> readersValid{true};
    std::vector<std::thread> readers;
    for (int index = 0; index < 4; ++index) {
        readers.emplace_back([&] {
            for (int count = 0; count < 100; ++count) {
                const auto snapshot = host.snapshot();
                if (snapshot.schemaVersion != 2 || snapshot.seats.size() != 2u ||
                    snapshot.managementSeatId != 1) {
                    readersValid.store(false, std::memory_order_relaxed);
                }
            }
        });
    }
    check(host.start(5).succeeded(), "session starts while readers resnapshot");
    check(host.stopAndReturnToWindows(6).succeeded(),
          "session stops while readers resnapshot");
    for (auto& reader : readers) reader.join();
    check(readersValid.load(std::memory_order_relaxed),
          "snapshot readers observe coherent bounded state");
}

} // namespace

int main() {
    testDefaultAndProfileValidation();
    testLifecycleAndUiIndependence();
    testReconfigureIsDistinctVerifiedStop();
    testPartialFailureAndRecoveryRequired();
    testDiagnosticBounds();
    testConcurrentMutationRejectionAndReaders();

    if (failures != 0) {
        std::cerr << failures << " runtime host test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Runtime host tests passed.\n";
    return EXIT_SUCCESS;
}
