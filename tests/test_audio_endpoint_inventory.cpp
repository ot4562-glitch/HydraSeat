#include "hydra/audio_endpoint_inventory.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace hydra;
using namespace hydra::audio;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class FakeSource final : public EndpointSource {
public:
    std::vector<EndpointRecord> records;
    std::uint64_t generation{1};
    bool notifications{true};
    bool fail{false};
    bool changeDuringEveryRead{false};
    unsigned reads{0};

    bool enumerate(std::vector<EndpointRecord>& endpoints,
                   std::string& error) noexcept override {
        ++reads;
        if (fail) {
            error = "injected audio enumeration failure";
            return false;
        }
        endpoints = records;
        if (changeDuringEveryRead) ++generation;
        error.clear();
        return true;
    }

    std::uint64_t changeGeneration() const noexcept override {
        return generation;
    }
    bool notificationsAvailable() const noexcept override {
        return notifications;
    }
};

EndpointRecord render(std::wstring id,
                      std::uint32_t state = kEndpointStateActive,
                      std::uint8_t roles = 0) {
    return {std::move(id), L"Render", DataFlow::Render, state, roles};
}

EndpointRecord capture(std::wstring id,
                       std::uint32_t state = kEndpointStateActive,
                       std::uint8_t roles = 0) {
    return {std::move(id), L"Capture", DataFlow::Capture, state, roles};
}

void testDeterministicSnapshotAndNotificationGeneration() {
    auto source = std::make_shared<FakeSource>();
    source->records = {
        render(L"{B}", kEndpointStateActive, kDefaultRoleMultimedia),
        capture(L"{C}"),
        render(L"{a}", kEndpointStateDisabled, kDefaultRoleConsole),
    };

    EndpointInventory inventory(source);
    std::string error;
    check(inventory.refresh(&error), "initial audio inventory refresh succeeds");
    check(inventory.current().has_value(), "refresh publishes an audio snapshot");
    if (!inventory.current()) return;

    const auto& snapshot = *inventory.current();
    check(snapshot.sourceGeneration == 1 && snapshot.notificationsAvailable,
          "snapshot records source generation and notification availability");
    check(snapshot.endpoints.size() == 3 &&
              snapshot.endpoints[0].flow == DataFlow::Render &&
              snapshot.endpoints[0].endpointId == L"{a}" &&
              snapshot.endpoints[1].endpointId == L"{B}" &&
              snapshot.endpoints[2].flow == DataFlow::Capture,
          "inventory order is deterministic by flow and stable endpoint ID, not enumeration order");
    check(!inventory.needsRefresh(), "unchanged notification generation is current");

    ++source->generation;
    check(inventory.needsRefresh(), "endpoint notification generation marks snapshot stale");
    std::reverse(source->records.begin(), source->records.end());
    check(inventory.refresh(&error) && !inventory.needsRefresh(),
          "refresh after notification accepts a reordered source");
    check(inventory.current()->endpoints[0].endpointId == L"{a}" &&
              inventory.current()->endpoints[1].endpointId == L"{B}",
          "reordered source produces the same canonical endpoint ordering");
}

void testBoundedRefreshAndMalformedSource() {
    auto source = std::make_shared<FakeSource>();
    source->records = {render(L"{A}")};
    source->changeDuringEveryRead = true;
    EndpointInventory inventory(source);
    std::string error;
    check(!inventory.refresh(&error) && source->reads == 3,
          "continuously changing notification generation is bounded to three attempts");

    source = std::make_shared<FakeSource>();
    source->records = {render(L"{A}"), render(L"{a}")};
    EndpointInventory duplicate(source);
    check(!duplicate.refresh(&error),
          "case-equivalent duplicate endpoint stable IDs fail closed");

    source = std::make_shared<FakeSource>();
    source->records = {render(L"{A}", 0)};
    EndpointInventory badState(source);
    check(!badState.refresh(&error), "empty endpoint state mask is rejected");

    source = std::make_shared<FakeSource>();
    source->notifications = false;
    source->records = {render(L"{A}")};
    EndpointInventory pollingOnly(source);
    check(pollingOnly.refresh(&error) && !pollingOnly.needsRefresh(),
          "read-only inventory remains usable when native notifications are unavailable");
}

void testOptionalSeatAssignmentsAndAvailability() {
    EndpointSnapshot snapshot;
    snapshot.endpoints = {
        render(L"OUT-1", kEndpointStateActive),
        render(L"OUT-2", kEndpointStateUnplugged),
        capture(L"MIC-1", kEndpointStateActive),
    };

    SeatConfig seat1;
    seat1.seatId = 1;
    seat1.active = true;
    seat1.audioOutputEndpointId = L"out-1";
    seat1.audioInputEndpointId = L"MIC-1";

    SeatConfig seat2;
    seat2.seatId = 2;
    seat2.active = true;
    seat2.audioOutputEndpointId = L"OUT-2";

    const std::vector<SeatConfig> seats{seat2, seat1};
    const auto validation = validateSeatAssignments(seats, snapshot);
    check(validation.configurationValid,
          "active and unplugged configured endpoints remain structurally valid assignments");
    check(!validation.configuredEndpointsReady,
          "an unplugged endpoint makes the active configured-audio set not ready");
    check(validation.seats.size() == 2 && validation.seats[0].seatId == 1 &&
              validation.seats[0].outputAvailable && validation.seats[0].inputAvailable &&
              validation.seats[1].seatId == 2 && !validation.seats[1].outputAvailable,
          "Seat assignment result is canonical and separates configured from currently available");
    check(validation.issues.size() == 1 &&
              validation.issues[0].code == AssignmentIssueCode::OutputEndpointUnavailable &&
              !validation.issues[0].configurationError,
          "unplugged endpoint is a readiness issue rather than corrupt saved configuration");

    SeatConfig optional;
    optional.seatId = 1;
    optional.active = true;
    const std::vector<SeatConfig> optionalSeats{optional};
    const auto optionalResult = validateSeatAssignments(optionalSeats, snapshot);
    check(optionalResult.configurationValid && optionalResult.configuredEndpointsReady &&
              optionalResult.issues.empty(),
          "missing audio is valid because Seat audio can be Set later");
}

void testAssignmentFailuresAndV1Limit() {
    EndpointSnapshot snapshot;
    snapshot.endpoints = {
        render(L"OUT"),
        capture(L"MIC"),
    };

    SeatConfig wrongFlow;
    wrongFlow.seatId = 1;
    wrongFlow.audioOutputEndpointId = L"MIC";
    wrongFlow.audioInputEndpointId = L"MISSING";
    const std::vector<SeatConfig> wrongFlowSeats{wrongFlow};
    const auto wrong = validateSeatAssignments(wrongFlowSeats, snapshot);
    check(!wrong.configurationValid && !wrong.configuredEndpointsReady &&
              wrong.issues.size() == 2,
          "wrong-flow and missing endpoint references fail configuration validation");

    SeatConfig first;
    first.seatId = 1;
    SeatConfig second;
    second.seatId = 2;
    SeatConfig third;
    third.seatId = 3;
    const std::vector<SeatConfig> three{first, second, third};
    const auto limited = validateSeatAssignments(three, snapshot);
    check(!limited.configurationValid && !limited.configuredEndpointsReady,
          "three active Seats fail the v1 audio assignment boundary");
    check(std::any_of(limited.issues.begin(), limited.issues.end(),
                      [](const AssignmentIssue& issue) {
                          return issue.code == AssignmentIssueCode::V1SeatLimitExceeded;
                      }),
          "v1 Seat-limit violation remains explicit in audio assignment evidence");

    second.seatId = 1;
    second.active = false;
    const std::vector<SeatConfig> duplicate{first, second};
    const auto duplicateResult = validateSeatAssignments(duplicate, snapshot);
    check(!duplicateResult.configurationValid,
          "duplicate Seat identity fails even when only one duplicate is active");
}

} // namespace

int main() {
    testDeterministicSnapshotAndNotificationGeneration();
    testBoundedRefreshAndMalformedSource();
    testOptionalSeatAssignmentsAndAvailability();
    testAssignmentFailuresAndV1Limit();

    if (failures != 0) {
        std::cerr << failures << " audio endpoint inventory test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Audio endpoint inventory tests passed.\n";
    return EXIT_SUCCESS;
}
