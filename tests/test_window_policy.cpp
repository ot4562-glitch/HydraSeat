#include "hydra/window_policy.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace hydra;
using namespace hydra::display;
using namespace hydra::windowing;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

TrackedWindow makeWindow(SeatId seatId = 1) {
    TrackedWindow window;
    window.identity.nativeHandle = 0x100u;
    window.identity.threadId = 7u;
    window.identity.trackerGeneration = 3u;
    window.identity.process.processId = 42u;
    window.identity.process.creationTime100ns = 99u;
    window.identity.process.executablePath = L"C:\\HydraSeatTest\\game.exe";
    window.seatId = seatId;
    window.role = WindowRole::PrimaryGame;
    window.visible = true;
    window.bounds = {100, 100, 900, 700};
    return window;
}

SeatDisplayOutput makeOutput(std::string id, DisplayRect bounds, std::uint32_t dpi = 96) {
    SeatDisplayOutput output;
    output.outputId = std::move(id);
    output.globalBounds = bounds;
    output.dpiX = dpi;
    output.dpiY = dpi;
    return output;
}

SeatDisplayGroup makeGroup() {
    SeatDisplayGroup group;
    group.seatId = 1;
    group.primaryOutputId = "display-left";
    group.primaryOriginX = -1920;
    group.primaryOriginY = 0;
    group.globalBounds = {-1920, 0, 2560, 1440};
    group.outputs = {
        makeOutput("display-left", {-1920, 0, 0, 1080}, 96),
        makeOutput("display-right", {0, 0, 2560, 1440}, 144),
    };
    return group;
}

void testPolicyValidationBounds() {
    WindowPlacementPolicy policy;
    std::string error;
    check(validateWindowPlacementPolicy(policy, &error), "default placement policy is valid");

    policy.retryCount = 9u;
    check(!validateWindowPlacementPolicy(policy, &error), "retry count is bounded");
    policy.retryCount = 2u;
    policy.initialDelayMs = 10001u;
    check(!validateWindowPlacementPolicy(policy, &error), "initial delay is bounded");
    policy.initialDelayMs = 0u;
    policy.retryDelayMs = 2001u;
    check(!validateWindowPlacementPolicy(policy, &error), "retry delay is bounded");
    policy.retryDelayMs = 50u;
    policy.placementTolerancePixels = 65u;
    check(!validateWindowPlacementPolicy(policy, &error), "placement tolerance is bounded");

    policy = {};
    policy.mode = WindowPlacementMode::BorderlessOnSelectedOutput;
    check(!validateWindowPlacementPolicy(policy, &error),
          "borderless policy requires selected stable output ID");

    policy = {};
    policy.mode = WindowPlacementMode::RestoreLastSeatLocalRect;
    check(!validateWindowPlacementPolicy(policy, &error),
          "restore-last policy requires a saved Seat-local rectangle");
    policy.lastSeatLocalRect = CoordinateRect{0.0, 0.0, 0.0, 20.0};
    check(!validateWindowPlacementPolicy(policy, &error),
          "restore-last policy rejects empty rectangles");
}

void testPlacementModes() {
    const auto window = makeWindow();
    const auto group = makeGroup();

    WindowPlacementPolicy policy;
    auto plan = computeWindowPlacementPlan(window, group, policy);
    check(plan.valid && plan.leaveNative && !plan.actionable,
          "leave-native produces a valid non-mutating plan");

    policy.mode = WindowPlacementMode::PlaceOnPrimaryOutput;
    plan = computeWindowPlacementPlan(window, group, policy);
    check(plan.valid && plan.actionable && plan.targetOutputId == "display-left" &&
              plan.desiredRect == DisplayRect{-1920, 0, -1120, 600},
          "primary-output placement preserves window size and uses Seat primary origin");

    policy.mode = WindowPlacementMode::SpanSeatGroup;
    plan = computeWindowPlacementPlan(window, group, policy);
    check(plan.valid && plan.actionable && plan.desiredRect == group.globalBounds,
          "span policy targets the complete Seat display union");

    policy.mode = WindowPlacementMode::BorderlessOnSelectedOutput;
    policy.selectedOutputId = "display-right";
    plan = computeWindowPlacementPlan(window, group, policy);
    check(plan.valid && plan.actionable && plan.borderless &&
              plan.desiredRect == DisplayRect{0, 0, 2560, 1440},
          "borderless policy targets only the selected Seat output");

    policy.selectedOutputId = "foreign-output";
    plan = computeWindowPlacementPlan(window, group, policy);
    check(!plan.valid && !plan.actionable,
          "borderless policy rejects outputs outside the Seat group");
}

void testRestoreRoleAndSeatIsolation() {
    const auto window = makeWindow();
    const auto group = makeGroup();

    WindowPlacementPolicy restore;
    restore.mode = WindowPlacementMode::RestoreLastSeatLocalRect;
    restore.lastSeatLocalRect = CoordinateRect{100.0, 40.0, 900.0, 640.0};
    auto plan = computeWindowPlacementPlan(window, group, restore);
    check(plan.valid && plan.actionable &&
              plan.desiredRect == DisplayRect{-1820, 40, -1020, 640},
          "Seat-local restore converts through the Seat primary origin");

    WindowPlacementPolicy rolePolicy;
    rolePolicy.mode = WindowPlacementMode::SpanSeatGroup;
    rolePolicy.stableWindowRole = WindowRole::Dialog;
    plan = computeWindowPlacementPlan(window, group, rolePolicy);
    check(plan.valid && plan.leaveNative && !plan.actionable,
          "stable-window selector leaves nonmatching roles untouched");

    auto otherSeat = makeWindow(2);
    plan = computeWindowPlacementPlan(otherSeat, group, rolePolicy);
    check(!plan.valid, "window from another Seat is never planned into this Seat display group");

    auto invalid = window;
    invalid.identity.trackerGeneration = 0;
    plan = computeWindowPlacementPlan(invalid, group, {});
    check(!plan.valid, "invalid/stale window identity is rejected before placement planning");
}

void testExclusiveFullscreenFailClosed() {
    const auto window = makeWindow();
    const auto group = makeGroup();

    WindowPlacementPolicy policy;
    policy.mode = WindowPlacementMode::ExclusiveFullscreen;
    policy.exclusiveFullscreen = ExclusiveFullscreenPolicy::Unsupported;
    auto plan = computeWindowPlacementPlan(window, group, policy);
    check(!plan.valid && plan.degraded,
          "unsupported exclusive fullscreen fails closed with degradation visible");

    policy.exclusiveFullscreen = ExclusiveFullscreenPolicy::Block;
    plan = computeWindowPlacementPlan(window, group, policy);
    check(!plan.valid && !plan.degraded,
          "explicitly blocked exclusive fullscreen is rejected");

    policy.exclusiveFullscreen = ExclusiveFullscreenPolicy::AllowNative;
    plan = computeWindowPlacementPlan(window, group, policy);
    check(plan.valid && plan.leaveNative && plan.degraded,
          "allowed native exclusive fullscreen remains an explicit degraded capability");
}

} // namespace

int main() {
    testPolicyValidationBounds();
    testPlacementModes();
    testRestoreRoleAndSeatIsolation();
    testExclusiveFullscreenFailClosed();

    if (failures != 0) {
        std::cerr << failures << " window-policy test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "window policy tests passed\n";
    return EXIT_SUCCESS;
}
