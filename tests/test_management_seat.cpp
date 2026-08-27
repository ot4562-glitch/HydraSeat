#include "hydra/management_seat.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace hydra;
using namespace hydra::control;
using namespace hydra::display;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

DisplayOutput makeOutput(std::uint32_t targetId, const wchar_t* path,
                         DisplayRect bounds, bool primary = false, bool active = true) {
    DisplayOutput output;
    output.identity.adapterLuid = {1u, 0};
    output.identity.targetId = targetId;
    output.identity.monitorDevicePath = path;
    output.desktopBounds = bounds;
    output.primary = primary;
    output.active = active;
    output.attached = active;
    return output;
}

SeatDisplayGroup groupFor(SeatId seatId, const DisplayOutput& output) {
    SeatDisplayOutput selected;
    selected.outputId = output.identity.stableKey();
    selected.identity = output.identity;
    selected.globalBounds = output.desktopBounds;
    SeatDisplayGroup group;
    group.seatId = seatId;
    group.outputs.push_back(selected);
    group.primaryOutputId = selected.outputId;
    group.globalBounds = selected.globalBounds;
    group.primaryOriginX = selected.globalBounds.left;
    group.primaryOriginY = selected.globalBounds.top;
    return group;
}

DisplayTopologySnapshot topologyOf(std::vector<DisplayOutput> outputs) {
    DisplayTopologySnapshot topology;
    topology.querySucceeded = true;
    topology.generation = 1;
    topology.outputs = std::move(outputs);
    return topology;
}

bool inside(const DisplayRect& outer, const DisplayRect& inner) {
    return inner.left >= outer.left && inner.top >= outer.top &&
           inner.right <= outer.right && inner.bottom <= outer.bottom &&
           inner.right > inner.left && inner.bottom > inner.top;
}

void testManagementSeatPrimaryPlacement() {
    const auto seat1Output = makeOutput(1, L"MON#S1", {0, 0, 1920, 1080}, true);
    const auto seat2Output = makeOutput(2, L"MON#S2", {1920, 0, 3840, 1080});
    const auto topology = topologyOf({seat1Output, seat2Output});
    const std::vector<SeatDisplayGroup> groups{groupFor(1, seat1Output), groupFor(2, seat2Output)};

    ManagementSeatConfig config;
    config.managementSeatId = 2;
    const auto placement = resolveControlSurfacePlacement(config, groups, topology);
    check(placement.valid && !placement.degraded &&
              placement.targetOutputId == seat2Output.identity.stableKey(),
          "control console targets the configured Management Seat primary output");
    check(inside(placement.workArea, placement.windowRect),
          "default control console placement is wholly visible inside the work area");
}

void testSavedSeatLocalRectAndOffscreenRejection() {
    const auto output = makeOutput(3, L"MON#NEG", {-1920, 0, 0, 1080}, true);
    const auto topology = topologyOf({output});
    const std::vector<SeatDisplayGroup> groups{groupFor(1, output)};

    ManagementSeatConfig config;
    const auto restored = resolveControlSurfacePlacement(
        config, groups, topology, CoordinateRect{100.0, 50.0, 800.0, 650.0});
    check(restored.valid && restored.restoredSavedRect &&
              restored.windowRect.left == -1820 && restored.windowRect.top == 50,
          "saved control-console rectangle is interpreted in Management Seat-local coordinates");

    const auto rejected = resolveControlSurfacePlacement(
        config, groups, topology, CoordinateRect{5000.0, 5000.0, 5900.0, 5600.0});
    check(rejected.valid && !rejected.restoredSavedRect &&
              inside(rejected.workArea, rejected.windowRect) && !rejected.diagnostics.empty(),
          "stale off-screen saved rectangle is rejected in favor of a visible placement");
}

void testVisibleFallbackWhenManagementDisplayMissing() {
    auto management = makeOutput(4, L"MON#MISSING", {1920, 0, 3840, 1080});
    management.active = false;
    management.attached = false;
    const auto windowsPrimary = makeOutput(5, L"MON#WIN", {0, 0, 1920, 1080}, true);
    const auto topology = topologyOf({management, windowsPrimary});
    const std::vector<SeatDisplayGroup> groups{groupFor(2, management)};

    ManagementSeatConfig config;
    config.managementSeatId = 2;
    const auto placement = resolveControlSurfacePlacement(config, groups, topology);
    check(placement.valid && placement.degraded &&
              placement.fallback == ControlSurfaceFallback::WindowsPrimary &&
              placement.targetOutputId == windowsPrimary.identity.stableKey(),
          "missing Management Seat display uses an explicit degraded Windows-primary fallback");
}

void testGlobalControlPermission() {
    GlobalControlPermission permission;
    permission.managementSeatId = 2;
    permission.callerSeatId = 2;
    permission.sameWindowsUserSession = true;
    permission.authenticatedControlRole = true;
    check(permission.permitsGlobalMutation(),
          "Management Seat control client in the authenticated Windows session may mutate global state");

    permission.callerSeatId = 1;
    check(!permission.permitsGlobalMutation(),
          "another Seat cannot issue global runtime mutations");
    permission.callerSeatId = 2;
    permission.sameWindowsUserSession = false;
    check(!permission.permitsGlobalMutation(),
          "cross-session client cannot gain global authority from Seat identity alone");
}

} // namespace

int main() {
    testManagementSeatPrimaryPlacement();
    testSavedSeatLocalRectAndOffscreenRejection();
    testVisibleFallbackWhenManagementDisplayMissing();
    testGlobalControlPermission();
    if (failures != 0) {
        std::cerr << failures << " Management Seat test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Management Seat tests passed\n";
    return EXIT_SUCCESS;
}
