#include "hydra/seat_navigation_model.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace hydra;
using namespace hydra::seatui;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<SeatDisplayRegion> seatOneDisplays() {
    return {{"display-a", 0, 0, 1920, 1080}, {"display-b", 1920, 0, 3840, 1080}};
}

void testSeatLocalPointerCannotEscapeDisplayGroup() {
    SeatNavigationModel first(1u);
    SeatNavigationModel second(2u);
    std::string error;
    const std::vector<SeatDisplayRegion> seatTwo{{"display-c", 3840, 0, 5760, 1080}};
    check(first.configureDisplays(seatOneDisplays(), 7u, &error) &&
              second.configureDisplays(seatTwo, 7u, &error),
          "two Seat navigation models accept disjoint authoritative display groups");

    check(first.applyPointer({1u, "display-b", 2200, 400, 7u}, &error) &&
              first.state().mode == SeatNavigationMode::SeatLocalPointer &&
              first.state().pointerX == 2200,
          "own Seat-scoped pointer sample updates only local presentation state");
    const auto before = first.state();
    check(!first.applyPointer({2u, "display-c", 4000, 400, 7u}, &error) &&
              first.state().pointerX == before.pointerX,
          "cross-Seat pointer sample is rejected transactionally");
    check(!first.applyPointer({1u, "display-a", 3000, 400, 7u}, &error) &&
              first.state().pointerX == before.pointerX,
          "coordinates outside the named assigned display are rejected");
    check(!first.applyPointer({1u, "display-c", 4000, 400, 7u}, &error),
          "unassigned display identity cannot redirect Seat pointer state");
}

void testControllerOnlyNavigationAvoidsSystemCursorDependency() {
    SeatNavigationModel model(2u);
    std::string error;
    check(model.configureDisplays({{"display-2", 0, 0, 1280, 720}}, 3u, &error),
          "controller fixture display group configures");
    check(model.controllerStep(1, 4u, &error) && model.state().focusIndex == 1u &&
              model.state().mode == SeatNavigationMode::ControllerFocus,
          "controller focus moves forward without pointer state");
    check(model.controllerStep(-1, 4u, &error) && model.state().focusIndex == 0u,
          "controller focus moves backward deterministically");
    check(model.controllerStep(-1, 4u, &error) && model.state().focusIndex == 3u,
          "controller focus wraps within bounded launcher items");
    check(!model.controllerStep(1, kMaximumSeatNavigationItems + 1u, &error),
          "unbounded focus surface is rejected");
}

void testStaleTopologyFailsClosed() {
    SeatNavigationModel model(1u);
    std::string error;
    check(model.configureDisplays(seatOneDisplays(), 10u, &error),
          "fresh topology accepted");
    const auto displays = model.displays();
    const std::vector<SeatDisplayRegion> stale{{"old", 0, 0, 800, 600}};
    check(!model.configureDisplays(stale, 9u, &error) &&
              model.displays() == displays,
          "stale display generation leaves current Seat bounds unchanged");
    check(!model.applyPointer({1u, "display-a", 10, 10, 9u}, &error),
          "stale pointer authority cannot move local pointer state");
}

void testMalformedDisplayConfigurationIsTransactional() {
    SeatNavigationModel model(1u);
    std::string error;
    check(model.configureDisplays(seatOneDisplays(), 4u, &error), "baseline config accepted");
    const auto before = model.displays();
    const std::vector<SeatDisplayRegion> duplicate{
        {"dup", 0, 0, 100, 100}, {"dup", 100, 0, 200, 100}};
    const std::vector<SeatDisplayRegion> invalid{{"bad", 10, 10, 10, 20}};
    check(!model.configureDisplays(duplicate, 5u, &error) &&
              model.displays() == before,
          "duplicate display identity fails without replacing the previous group");
    check(!model.configureDisplays(invalid, 5u, &error) &&
              model.displays() == before,
          "empty display bounds fail transactionally");
}

} // namespace

int main() {
    testSeatLocalPointerCannotEscapeDisplayGroup();
    testControllerOnlyNavigationAvoidsSystemCursorDependency();
    testStaleTopologyFailsClosed();
    testMalformedDisplayConfigurationIsTransactional();
    if (failures != 0) {
        std::cerr << failures << " Seat navigation test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Seat navigation tests passed.\n";
    return EXIT_SUCCESS;
}
