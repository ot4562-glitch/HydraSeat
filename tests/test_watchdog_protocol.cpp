#include "hydra/rollback_registry.hpp"
#include "hydra/watchdog_protocol.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using namespace hydra::watchdog;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

SessionId session() {
    SessionId value{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        value[index] = static_cast<std::uint8_t>(index + 1);
    }
    return value;
}

RollbackPlanManifest samplePlan() {
    RollbackPlanManifest manifest;
    manifest.lease.sessionId = session();
    manifest.lease.generation = 7;
    manifest.lease.timeoutMilliseconds = 500;
    manifest.rollbackTimeoutMilliseconds = 2'000;

    RollbackActionDescriptor process;
    process.actionId = 10;
    process.kind = RollbackActionKind::TerminateOwnedProcess;
    process.activationOrdinal = 1;
    process.timeoutMilliseconds = 500;
    process.generation = 1;
    process.process = {1234, 5678};

    RollbackActionDescriptor overlay;
    overlay.actionId = 20;
    overlay.kind = RollbackActionKind::ReleaseOverlayState;
    overlay.activationOrdinal = 3;
    overlay.timeoutMilliseconds = 250;
    overlay.generation = 2;
    overlay.resourceId = 0x1001;

    RollbackActionDescriptor backend;
    backend.actionId = 30;
    backend.kind = RollbackActionKind::ClearOptionalBackendState;
    backend.activationOrdinal = 2;
    backend.timeoutMilliseconds = 250;
    backend.generation = 3;
    backend.resourceId = 0x2001;

    manifest.actions = {process, overlay, backend};
    return manifest;
}

class FakeExecutor final : public RollbackExecutor {
public:
    std::vector<std::uint32_t> calls;
    std::vector<std::uint32_t> timeouts;
    std::unordered_map<std::uint32_t, RollbackActionResult> results;

    RollbackActionOutcome terminateOwnedProcess(
        const RollbackActionDescriptor& action,
        std::uint32_t timeoutMilliseconds) override {
        return run(action, timeoutMilliseconds);
    }
    RollbackActionOutcome closeOwnedSession(
        const RollbackActionDescriptor& action,
        std::uint32_t timeoutMilliseconds) override {
        return run(action, timeoutMilliseconds);
    }
    RollbackActionOutcome clearOptionalBackendState(
        const RollbackActionDescriptor& action,
        std::uint32_t timeoutMilliseconds) override {
        return run(action, timeoutMilliseconds);
    }
    RollbackActionOutcome releaseOverlayState(
        const RollbackActionDescriptor& action,
        std::uint32_t timeoutMilliseconds) override {
        return run(action, timeoutMilliseconds);
    }
    RollbackActionOutcome restoreSnapshotState(
        const RollbackActionDescriptor& action,
        std::uint32_t timeoutMilliseconds) override {
        return run(action, timeoutMilliseconds);
    }
    RollbackActionOutcome writeSafeModeResult(
        const RollbackActionDescriptor& action,
        std::uint32_t timeoutMilliseconds) override {
        return run(action, timeoutMilliseconds);
    }

private:
    RollbackActionOutcome run(const RollbackActionDescriptor& action,
                              std::uint32_t timeoutMilliseconds) {
        calls.push_back(action.actionId);
        timeouts.push_back(timeoutMilliseconds);
        const auto found = results.find(action.actionId);
        const auto result = found == results.end()
            ? RollbackActionResult::Success
            : found->second;
        return {action.actionId, action.kind, result,
                result == RollbackActionResult::Failed ? 55u : 0u};
    }
};

void testPlanRoundTripAndBounds() {
    const auto manifest = samplePlan();
    std::string error;
    check(validateRollbackPlan(manifest, &error), "sample rollback plan validates");

    const auto encoded = encodeRegisterPlan(1, manifest);
    check(!encoded.empty() && encoded.size() <= kWatchdogMaxFrameBytes,
          "register-plan frame is bounded");
    const auto frame = decodeWatchdogFrame(encoded);
    check(frame && frame.frame->type == WatchdogMessageType::RegisterPlan &&
              frame.frame->sequence == 1,
          "register-plan frame header round trips");

    RollbackPlanManifest decoded;
    check(decodeRegisterPlan(*frame.frame, decoded, &error) && decoded == manifest,
          "register-plan payload round trips exactly");
}

void testMalformedFramesFailClosed() {
    const auto manifest = samplePlan();
    auto encoded = encodeRegisterPlan(4, manifest);
    check(!encoded.empty(), "baseline frame encodes");

    auto badMagic = encoded;
    badMagic[0] = std::byte{0};
    check(!decodeWatchdogFrame(badMagic), "bad magic is rejected");

    auto badVersion = encoded;
    badVersion[4] = std::byte{2};
    check(!decodeWatchdogFrame(badVersion), "future protocol version is rejected");

    auto badReserved = encoded;
    badReserved[12] = std::byte{1};
    check(!decodeWatchdogFrame(badReserved), "nonzero frame reserved field is rejected");

    auto truncated = encoded;
    truncated.pop_back();
    check(!decodeWatchdogFrame(truncated), "truncated payload is rejected");

    auto oversizedPayload = std::vector<std::byte>(kWatchdogMaxPayloadBytes + 1);
    check(encodeWatchdogFrame(WatchdogMessageType::Status, 1,
                              oversizedPayload).empty(),
          "oversized payload cannot be encoded");
}

void testManifestValidationRejectsAmbiguity() {
    std::string error;

    auto zeroSession = samplePlan();
    zeroSession.lease.sessionId = {};
    check(!validateRollbackPlan(zeroSession, &error),
          "zero session id is rejected");

    auto duplicateId = samplePlan();
    duplicateId.actions[1].actionId = duplicateId.actions[0].actionId;
    check(!validateRollbackPlan(duplicateId, &error),
          "duplicate action id is rejected");

    auto duplicateOrdinal = samplePlan();
    duplicateOrdinal.actions[1].activationOrdinal =
        duplicateOrdinal.actions[0].activationOrdinal;
    check(!validateRollbackPlan(duplicateOrdinal, &error),
          "duplicate activation ordinal is rejected");

    auto unknown = samplePlan();
    unknown.actions[0].kind = static_cast<RollbackActionKind>(999);
    check(!validateRollbackPlan(unknown, &error),
          "unknown rollback action kind is rejected");

    auto reusedPid = samplePlan();
    reusedPid.actions[0].process.creationTime100ns = 0;
    check(!validateRollbackPlan(reusedPid, &error),
          "process action requires creation identity");

    auto resourceWithPid = samplePlan();
    resourceWithPid.actions[1].process = {42, 99};
    check(!validateRollbackPlan(resourceWithPid, &error),
          "resource action cannot smuggle a process identity");

    auto tooMany = samplePlan();
    tooMany.actions.clear();
    for (std::size_t index = 0; index < kWatchdogMaxRollbackActions + 1; ++index) {
        RollbackActionDescriptor action;
        action.actionId = static_cast<std::uint32_t>(index + 1);
        action.kind = RollbackActionKind::ReleaseOverlayState;
        action.activationOrdinal = static_cast<std::uint32_t>(index + 1);
        action.timeoutMilliseconds = 100;
        action.generation = 1;
        action.resourceId = static_cast<std::uint64_t>(index + 1);
        tooMany.actions.push_back(action);
    }
    check(!validateRollbackPlan(tooMany, &error),
          "action count above hard maximum is rejected");
}

void testLeaseAndStatusRoundTrip() {
    const WatchdogLease lease{session(), 77, 750};
    auto frame = decodeWatchdogFrame(encodeLeaseRenewal(9, lease));
    WatchdogLease decodedLease;
    std::string error;
    check(frame && decodeLeaseRenewal(*frame.frame, decodedLease, &error) &&
              decodedLease == lease,
          "lease renewal round trips");

    frame = decodeWatchdogFrame(encodeDisarm(10, lease));
    check(frame && decodeDisarm(*frame.frame, decodedLease, &error) &&
              decodedLease == lease,
          "disarm round trips");

    WatchdogStatus status;
    status.sessionId = session();
    status.generation = 77;
    status.state = WatchdogRunState::RecoveryRequired;
    status.reason = WatchdogTriggerReason::HostExited;
    status.completedActions = 2;
    status.totalActions = 3;
    status.failedActionId = 30;
    status.systemError = 5;
    frame = decodeWatchdogFrame(encodeWatchdogStatus(11, status));
    WatchdogStatus decodedStatus;
    check(frame && decodeWatchdogStatus(*frame.frame, decodedStatus, &error) &&
              decodedStatus == status,
          "watchdog status round trips");
}

void testRegistryReverseOrderAndIdempotency() {
    RollbackRegistry registry;
    const auto manifest = samplePlan();
    std::string error;
    check(registry.registerPlan(manifest, &error), "registry accepts valid plan");
    check(registry.registerPlan(manifest, &error),
          "registering identical plan is idempotent");

    FakeExecutor executor;
    const auto first = registry.execute(executor);
    check(first.allSatisfied && !first.recoveryRequired,
          "successful rollback satisfies the plan");
    check(executor.calls == std::vector<std::uint32_t>{20, 30, 10},
          "rollback executes reverse activation order");

    executor.calls.clear();
    executor.timeouts.clear();
    const auto second = registry.execute(executor);
    check(second.allSatisfied && executor.calls.empty(),
          "second rollback performs no already-completed mutation");
    check(second.outcomes.size() == manifest.actions.size(),
          "idempotent replay still reports every action");
    for (const auto& outcome : second.outcomes) {
        check(outcome.result == RollbackActionResult::AlreadySatisfied,
              "idempotent replay reports already-satisfied");
    }
}

void testPartialFailureContinuesAndCanRetry() {
    RollbackRegistry registry;
    const auto manifest = samplePlan();
    check(registry.registerPlan(manifest), "partial-failure plan registers");

    FakeExecutor executor;
    executor.results[30] = RollbackActionResult::Failed;
    const auto first = registry.execute(executor);
    check(first.recoveryRequired && !first.allSatisfied &&
              first.firstFailedActionId == 30 && first.firstSystemError == 55,
          "partial failure enters recovery-required with exact action");
    check(executor.calls == std::vector<std::uint32_t>{20, 30, 10},
          "partial failure does not prevent later independent cleanup");

    executor.calls.clear();
    executor.results[30] = RollbackActionResult::Success;
    const auto retry = registry.execute(executor);
    check(retry.allSatisfied && !retry.recoveryRequired,
          "failed action can be retried to completion");
    check(executor.calls == std::vector<std::uint32_t>{30},
          "retry invokes only the previously unsatisfied action");
}

void testDifferentPlanCannotReplaceArmedRegistry() {
    RollbackRegistry registry;
    auto first = samplePlan();
    auto second = first;
    second.lease.generation += 1;
    std::string error;
    check(registry.registerPlan(first, &error), "first plan registers");
    check(!registry.registerPlan(second, &error),
          "different plan cannot replace armed registry");
    registry.clear();
    check(registry.registerPlan(second, &error),
          "explicit clear permits a new generation");
}

void testRestartedRegistryCanTreatExternalCleanupAsSatisfied() {
    const auto manifest = samplePlan();
    RollbackRegistry restarted;
    check(restarted.registerPlan(manifest),
          "reconstructed registry accepts the same bounded manifest");
    FakeExecutor executor;
    executor.results[20] = RollbackActionResult::AlreadySatisfied;
    executor.results[30] = RollbackActionResult::AlreadySatisfied;
    executor.results[10] = RollbackActionResult::AlreadySatisfied;
    const auto result = restarted.execute(executor);
    check(result.allSatisfied && !result.recoveryRequired,
          "reloaded plan is safe when prior rollback already completed");
}

} // namespace

int main() {
    testPlanRoundTripAndBounds();
    testMalformedFramesFailClosed();
    testManifestValidationRejectsAmbiguity();
    testLeaseAndStatusRoundTrip();
    testRegistryReverseOrderAndIdempotency();
    testPartialFailureContinuesAndCanRetry();
    testDifferentPlanCannotReplaceArmedRegistry();
    testRestartedRegistryCanTreatExternalCleanupAsSatisfied();

    std::cout << "Watchdog protocol/registry tests passed.\n";
    return EXIT_SUCCESS;
}
