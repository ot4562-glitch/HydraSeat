#include "hydra/hidhide_session_backend.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

using namespace hydra;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class FakePlatform final : public HidHideSessionPlatform {
public:
    HidHideSessionSnapshot state;
    bool supportsMutation{true};
    bool supportsSessionBlacklist{true};
    bool failRead{false};
    bool failWrite{false};
    bool failSessionAdd{false};
    bool failSessionClear{false};
    bool partialWriteFailure{false};
    bool corruptAfterWrite{false};
    unsigned reads{0};
    unsigned writes{0};
    unsigned sessionAdds{0};
    unsigned sessionClears{0};
    std::vector<std::wstring> sessionDevices;

    bool readState(HidHideSessionSnapshot& snapshot,
                   std::string& error) noexcept override {
        ++reads;
        if (failRead) {
            error = "injected read failure";
            return false;
        }
        snapshot = state;
        error.clear();
        return true;
    }

    bool writeState(const HidHideSessionSnapshot& snapshot,
                    std::string& error) noexcept override {
        ++writes;
        if (partialWriteFailure) {
            state = snapshot;
            if (!state.blockedDeviceInstanceIds.empty()) {
                state.blockedDeviceInstanceIds.pop_back();
            }
            error = "injected partial write failure";
            return false;
        }
        if (failWrite) {
            error = "injected write failure";
            return false;
        }
        state = snapshot;
        if (corruptAfterWrite) {
            state.blockedDeviceInstanceIds.push_back(L"HID\\EXTERNAL_DRIFT");
        }
        error.clear();
        return true;
    }

    bool addSessionBlacklist(
        std::span<const std::wstring> deviceInstanceIds,
        std::string& error) noexcept override {
        ++sessionAdds;
        if (failSessionAdd) {
            error = "injected session blacklist add failure";
            return false;
        }
        sessionDevices.assign(deviceInstanceIds.begin(), deviceInstanceIds.end());
        error.clear();
        return true;
    }

    bool clearSessionBlacklist(std::string& error) noexcept override {
        ++sessionClears;
        if (failSessionClear) {
            error = "injected session blacklist clear failure";
            return false;
        }
        sessionDevices.clear();
        error.clear();
        return true;
    }

    bool mutationSupported() const noexcept override {
        return supportsMutation;
    }
    bool sessionBlacklistSupported() const noexcept override {
        return supportsSessionBlacklist;
    }
};

HidHideSessionRequest request() {
    HidHideSessionRequest value;
    value.deviceInstanceIds = {
        L"hid\\vid_1111&pid_0001\\a",
        L"hid\\vid_2222&pid_0002\\b",
    };
    value.allowedApplications = {
        L"\\Device\\HarddiskVolume3\\HydraSeat\\hydra_host.exe",
        L"\\Device\\HarddiskVolume3\\HydraSeat\\hydra_gate_c_host.exe",
    };
    value.replacementPathVerified = true;
    value.recoveryReady = true;
    value.spareRecoveryInputPresent = true;
    value.expiryMilliseconds = 30'000;
    value.generation = 7;
    return value;
}

HidHideSessionSnapshot initialState() {
    HidHideSessionSnapshot value;
    value.active = false;
    value.inverseWhitelist = false;
    value.blockedDeviceInstanceIds = {L"HID\\EXISTING"};
    value.allowedApplications = {L"C:\\EXISTING\\APP.EXE"};
    return value;
}

void testValidationAndPlanAreReadOnly() {
    auto platform = std::make_shared<FakePlatform>();
    platform->state = initialState();
    HidHideSessionTransaction transaction(platform);

    auto invalid = request();
    invalid.replacementPathVerified = false;
    const auto rejected = transaction.prepare(invalid, 1'000);
    check(rejected.code == HidHideSessionResultCode::InvalidRequest &&
              platform->reads == 0 && platform->writes == 0,
          "unverified replacement path is rejected before backend access");

    auto duplicate = request();
    duplicate.deviceInstanceIds[1] = L"HID\\VID_1111&PID_0001\\A";
    const auto duplicateResult = transaction.prepare(duplicate, 1'000);
    check(duplicateResult.code == HidHideSessionResultCode::InvalidRequest,
          "case-normalized duplicate device IDs are rejected");

    auto shortRecovery = request();
    shortRecovery.spareRecoveryInputPresent = false;
    shortRecovery.expiryMilliseconds = 10'001;
    check(transaction.prepare(shortRecovery, 1'000).code ==
              HidHideSessionResultCode::InvalidRequest,
          "no spare recovery input enforces the ten-second expiry bound");

    const auto prepared = transaction.prepare(request(), 1'000);
    check(prepared.succeeded() && prepared.phase == HidHideSessionPhase::Prepared &&
              prepared.plan.has_value(),
          "valid request produces a prepared transaction");
    check(platform->reads == 1 && platform->writes == 0,
          "prepare captures one snapshot and performs no mutation");
    check(prepared.plan->applied.active &&
              prepared.plan->applied.blockedDeviceInstanceIds.size() == 1 &&
              prepared.plan->applied.allowedApplications.size() == 3,
          "prepared state preserves the persistent blacklist and merges only allowed applications");
    check(prepared.plan->expiryAtMilliseconds == 31'000,
          "prepared plan carries an explicit bounded expiry deadline");
}

void testPhysicalGateAndApprovalBlockMutation() {
    auto platform = std::make_shared<FakePlatform>();
    platform->state = initialState();
    HidHideSessionTransaction transaction(platform);

    check(transaction.prepare(request(), 10).succeeded(),
          "gate test plan prepares");
    const auto physical = transaction.activate(20);
    check(physical.code == HidHideSessionResultCode::PhysicalGateRequired &&
              platform->writes == 0 && platform->sessionAdds == 0,
          "missing physical evidence blocks persistent and session mutation with zero writes");

    auto approved = request();
    approved.physicalAcceptanceRecorded = true;
    check(transaction.rollback().succeeded(), "prepared plan can be cancelled read-only");
    check(transaction.prepare(approved, 100).succeeded(),
          "physically eligible plan can prepare");
    const auto approval = transaction.activate(200);
    check(approval.code == HidHideSessionResultCode::NativeMutationDisabled &&
              platform->writes == 0 && platform->sessionAdds == 0,
          "missing explicit high-risk approval blocks native mutation");

    check(transaction.rollback().succeeded(), "second prepared plan cancels cleanly");
    approved.nativeMutationApproved = true;
    platform->supportsMutation = false;
    check(transaction.prepare(approved, 300).succeeded(),
          "unsupported-native test prepares");
    const auto unsupported = transaction.activate(400);
    check(unsupported.code == HidHideSessionResultCode::NativeMutationDisabled &&
              platform->writes == 0 && platform->sessionAdds == 0,
          "a read-only platform cannot be promoted into native mutation");

    check(transaction.rollback().succeeded(), "unsupported-native plan cancels cleanly");
    platform->supportsMutation = true;
    platform->supportsSessionBlacklist = false;
    check(transaction.prepare(approved, 500).succeeded(),
          "session-support test prepares");
    const auto noSessionApi = transaction.activate(600);
    check(noSessionApi.code == HidHideSessionResultCode::UnsupportedState &&
              platform->writes == 0 && platform->sessionAdds == 0,
          "persistent mutation is not attempted without verified process-lifetime session blacklist support");
}

void testActivationRollbackAndExpiry() {
    auto platform = std::make_shared<FakePlatform>();
    platform->state = initialState();
    auto gated = request();
    gated.physicalAcceptanceRecorded = true;
    gated.nativeMutationApproved = true;

    HidHideSessionTransaction transaction(platform);
    check(transaction.prepare(gated, 1'000).succeeded(),
          "approved fake plan prepares");
    const auto activated = transaction.activate(2'000);
    check(activated.succeeded() && activated.phase == HidHideSessionPhase::Active &&
              platform->writes == 1 && platform->state.active &&
              platform->sessionAdds == 1 && platform->sessionDevices.size() == 2,
          "fake backend activation applies persistent state then owns only process-lifetime session device entries");

    const auto notExpired = transaction.expireIfNeeded(30'999);
    check(notExpired.code == HidHideSessionResultCode::AlreadySatisfied &&
              transaction.phase() == HidHideSessionPhase::Active,
          "active session remains live before its exact deadline");

    const auto expired = transaction.expireIfNeeded(31'000);
    check(expired.code == HidHideSessionResultCode::Expired &&
              expired.phase == HidHideSessionPhase::Idle &&
              platform->state == initialState() && platform->writes == 2 &&
              platform->sessionClears == 1 && platform->sessionDevices.empty(),
          "expiry clears process-lifetime entries, restores the exact prior snapshot, and returns Idle");

    check(transaction.rollback().code == HidHideSessionResultCode::AlreadySatisfied,
          "rollback is idempotent after expiry");
}

void testSessionBlacklistFailurePaths() {
    auto addPlatform = std::make_shared<FakePlatform>();
    addPlatform->state = initialState();
    addPlatform->failSessionAdd = true;
    auto gated = request();
    gated.physicalAcceptanceRecorded = true;
    gated.nativeMutationApproved = true;

    HidHideSessionTransaction addFailure(addPlatform);
    check(addFailure.prepare(gated, 1'000).succeeded(),
          "session-add failure test prepares");
    const auto addResult = addFailure.activate(1'100);
    check(addResult.code == HidHideSessionResultCode::BackendFailure &&
              addResult.phase == HidHideSessionPhase::Idle &&
              addPlatform->state == initialState() && addPlatform->writes == 2 &&
              addPlatform->sessionAdds == 1 && addPlatform->sessionDevices.empty(),
          "session blacklist add failure restores persistent state and returns Idle");

    auto clearPlatform = std::make_shared<FakePlatform>();
    clearPlatform->state = initialState();
    clearPlatform->failSessionClear = true;
    HidHideSessionTransaction clearFailure(clearPlatform);
    check(clearFailure.prepare(gated, 2'000).succeeded() &&
              clearFailure.activate(2'100).succeeded(),
          "session-clear failure test activates");
    const auto clearResult = clearFailure.rollback();
    check(clearResult.code == HidHideSessionResultCode::RecoveryRequired &&
              clearResult.phase == HidHideSessionPhase::RecoveryRequired &&
              clearPlatform->state == initialState() && clearPlatform->writes == 2 &&
              clearPlatform->sessionClears == 1 && !clearPlatform->sessionDevices.empty(),
          "session blacklist clear failure restores persistent state but remains RecoveryRequired");
}

void testExternalDriftFailsClosed() {
    auto platform = std::make_shared<FakePlatform>();
    platform->state = initialState();
    auto gated = request();
    gated.physicalAcceptanceRecorded = true;
    gated.nativeMutationApproved = true;

    HidHideSessionTransaction transaction(platform);
    check(transaction.prepare(gated, 1'000).succeeded() &&
              transaction.activate(1'100).succeeded(),
          "drift test activates");
    platform->state.allowedApplications.push_back(L"C:\\OTHER\\TOOL.EXE");
    const auto writesBeforeRollback = platform->writes;
    const auto rollback = transaction.rollback();
    check(rollback.code == HidHideSessionResultCode::RecoveryRequired &&
              rollback.phase == HidHideSessionPhase::RecoveryRequired,
          "third-party HidHide drift enters RecoveryRequired");
    check(platform->writes == writesBeforeRollback && platform->sessionClears == 1 &&
              platform->sessionDevices.empty(),
          "session entries are cleared but stale persistent snapshot is never written over external HidHide drift");
}

void testPartialMutationFailureRetainsRecoveryTruth() {
    auto platform = std::make_shared<FakePlatform>();
    platform->state = initialState();
    platform->partialWriteFailure = true;
    auto gated = request();
    gated.physicalAcceptanceRecorded = true;
    gated.nativeMutationApproved = true;

    HidHideSessionTransaction transaction(platform);
    check(transaction.prepare(gated, 1'000).succeeded(),
          "partial-write test prepares");
    const auto activation = transaction.activate(1'100);
    check(activation.code == HidHideSessionResultCode::RecoveryRequired &&
              activation.phase == HidHideSessionPhase::RecoveryRequired,
          "unverifiable partial mutation cannot be downgraded to an ordinary backend failure");
    check(transaction.plan().has_value(),
          "recovery-required state retains the exact before/applied snapshots");
}

void testInverseModeAndWatchdogDescriptor() {
    auto platform = std::make_shared<FakePlatform>();
    platform->state = initialState();
    platform->state.inverseWhitelist = true;
    HidHideSessionTransaction transaction(platform);
    const auto inverse = transaction.prepare(request(), 1'000);
    check(inverse.code == HidHideSessionResultCode::UnsupportedState &&
              platform->writes == 0,
          "inverse HidHide application-list mode fails closed without mutation");

    const auto action = makeHidHideSessionRollbackAction(44, 6, 2'500, 9, 0x1234);
    check(action.actionId == 44 &&
              action.kind == watchdog::RollbackActionKind::RestoreSnapshotState &&
              action.activationOrdinal == 6 &&
              action.timeoutMilliseconds == 2'500 &&
              action.generation == 9 && action.resourceId == 0x1234,
          "session transaction emits a narrow watchdog restore-snapshot descriptor");
}

} // namespace

int main() {
    testValidationAndPlanAreReadOnly();
    testPhysicalGateAndApprovalBlockMutation();
    testActivationRollbackAndExpiry();
    testSessionBlacklistFailurePaths();
    testExternalDriftFailsClosed();
    testPartialMutationFailureRetainsRecoveryTruth();
    testInverseModeAndWatchdogDescriptor();

    if (failures != 0) {
        std::cerr << failures << " HidHide session backend test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "HidHide session backend tests passed.\n";
    return EXIT_SUCCESS;
}
