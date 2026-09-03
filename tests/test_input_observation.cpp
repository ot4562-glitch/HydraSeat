#include "hydra/input_observation.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

hydra::RawInputEvent keyboardEvent(std::uint64_t sequence,
                                   std::wstring deviceId,
                                   std::uint32_t vkey,
                                   hydra::RawKeyTransition transition) {
    hydra::RawInputEvent event;
    event.sequence = sequence;
    event.monotonicTimestampMicros = sequence * 100;
    event.deviceHandle = static_cast<std::uintptr_t>(0x3001);
    event.deviceId = std::move(deviceId);
    event.devicePath = L"\\\\?\\HID#TEST_KEYBOARD";
    event.rawDevType = 1;
    event.vkey = vkey;
    event.keyTransition = transition;
    return event;
}

hydra::RawInputEvent mouseEvent(std::uint64_t sequence,
                                std::wstring deviceId,
                                std::int32_t deltaX,
                                std::int32_t deltaY,
                                std::uint16_t buttonFlags = 0,
                                std::int16_t wheelDelta = 0) {
    hydra::RawInputEvent event;
    event.sequence = sequence;
    event.monotonicTimestampMicros = sequence * 100;
    event.deviceHandle = static_cast<std::uintptr_t>(0x3002);
    event.deviceId = std::move(deviceId);
    event.devicePath = L"\\\\?\\HID#TEST_MOUSE";
    event.rawDevType = 0;
    event.deltaX = deltaX;
    event.deltaY = deltaY;
    event.mouseButtonFlags = buttonFlags;
    event.wheelDelta = wheelDelta;
    return event;
}

hydra::RawInputDeviceChange deviceChange(
    std::uint64_t sequence,
    hydra::RawInputDeviceChangeKind kind,
    std::wstring deviceId,
    std::uint32_t rawType) {
    hydra::RawInputDeviceChange change;
    change.sequence = sequence;
    change.monotonicTimestampMicros = sequence * 100;
    change.kind = kind;
    change.device.deviceHandle = rawType == 1
                                     ? static_cast<std::uintptr_t>(0x3001)
                                     : static_cast<std::uintptr_t>(0x3002);
    change.device.deviceId = std::move(deviceId);
    change.device.devicePath = L"\\\\?\\HID#TEST_DEVICE";
    change.device.rawDevType = rawType;
    change.device.online = kind == hydra::RawInputDeviceChangeKind::Arrival;
    return change;
}

hydra::InputIdentificationRequest identificationRequest(
    hydra::InputIdentificationKind kind,
    std::uint64_t minimumSequenceExclusive,
    std::uint64_t startedAtMicros,
    std::uint64_t timeoutMicros = 5'000) {
    hydra::InputIdentificationRequest request;
    request.kind = kind;
    request.minimumSequenceExclusive = minimumSequenceExclusive;
    request.startedAtMicros = startedAtMicros;
    request.timeoutMicros = timeoutMicros;
    return request;
}

void testKeyboardIdentificationRequiresIntentionalPress() {
    hydra::InputIdentificationCapture capture;
    const auto& begun = capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Keyboard, 10, 1'000));
    check(begun.state == hydra::InputIdentificationState::Waiting &&
              !begun.terminal(),
          "keyboard identification begins in deterministic waiting state");

    capture.observeInput(mouseEvent(11, L"Mouse:Noise", 9, -4));
    check(capture.snapshot().state == hydra::InputIdentificationState::Waiting,
          "unrelated mouse motion cannot identify a keyboard");
    capture.observeInput(keyboardEvent(
        12, L"Keyboard:Exact", 0x41, hydra::RawKeyTransition::Up));
    check(capture.snapshot().state == hydra::InputIdentificationState::Waiting,
          "key-up without a post-begin press is not an intentional keyboard capture");

    capture.observeInput(keyboardEvent(
        13, L"Keyboard:Exact", 0x41, hydra::RawKeyTransition::Down));
    const auto identified = capture.snapshot();
    check(identified.state == hydra::InputIdentificationState::Identified &&
              identified.failure == hydra::InputIdentificationFailure::None &&
              identified.candidate &&
              identified.candidate->kind == hydra::InputIdentificationKind::Keyboard &&
              identified.candidate->deviceId == L"Keyboard:Exact" &&
              identified.candidate->sequence == 13,
          "post-begin key-down returns only the exact stable keyboard identity");

    capture.cancel();
    capture.observeInput(keyboardEvent(
        14, L"Keyboard:Other", 0x42, hydra::RawKeyTransition::Down));
    check(capture.snapshot() == identified,
          "identified result is terminal until an explicit new begin");
}

void testMouseIdentificationIgnoresMotionWheelAndRelease() {
    hydra::InputIdentificationCapture capture;
    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Mouse, 20, 2'000));

    capture.observeInput(keyboardEvent(
        21, L"Keyboard:Noise", 0x41, hydra::RawKeyTransition::Down));
    capture.observeInput(mouseEvent(22, L"Mouse:Exact", 5, -3));
    capture.observeInput(mouseEvent(23, L"Mouse:Exact", 0, 0, 0, 120));
    capture.observeInput(mouseEvent(24, L"Mouse:Exact", 0, 0, 0x0002));
    check(capture.snapshot().state == hydra::InputIdentificationState::Waiting,
          "mouse motion, wheel and button-up noise never identify a mouse");

    capture.observeInput(mouseEvent(25, L"Mouse:Exact", 0, 0, 0x0004));
    const auto identified = capture.snapshot();
    check(identified.state == hydra::InputIdentificationState::Identified &&
              identified.candidate &&
              identified.candidate->kind == hydra::InputIdentificationKind::Mouse &&
              identified.candidate->deviceId == L"Mouse:Exact" &&
              identified.candidate->sequence == 25,
          "mouse button-down returns the exact stable mouse identity");
}

void testIdentificationNoiseCannotPoisonOrdering() {
    hydra::InputIdentificationCapture capture;
    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Keyboard, 100, 10'000));

    capture.observeInput(keyboardEvent(
        100, L"Keyboard:QueuedRelease", 0x41, hydra::RawKeyTransition::Up));
    capture.observeInput(mouseEvent(99, L"Mouse:OldMotion", 4, -2));
    check(capture.snapshot().state == hydra::InputIdentificationState::Waiting,
          "stale key-up and unrelated pointer noise do not poison keyboard identification ordering");

    capture.observeInput(keyboardEvent(
        101, L"Keyboard:FreshPress", 0x41, hydra::RawKeyTransition::Down));
    check(capture.snapshot().state == hydra::InputIdentificationState::Identified &&
              capture.snapshot().candidate &&
              capture.snapshot().candidate->deviceId == L"Keyboard:FreshPress",
          "fresh key-down remains identifiable after stale non-intentional noise");

    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Mouse, 200, 20'000));
    capture.observeInput(mouseEvent(200, L"Mouse:QueuedMotion", 3, 1));
    capture.observeInput(mouseEvent(199, L"Mouse:QueuedWheel", 0, 0, 0, 120));
    capture.observeInput(mouseEvent(198, L"Mouse:QueuedRelease", 0, 0, 0x0002));
    check(capture.snapshot().state == hydra::InputIdentificationState::Waiting,
          "stale mouse motion, wheel and release remain ignorable noise");

    capture.observeInput(mouseEvent(201, L"Mouse:FreshClick", 0, 0, 0x0001));
    check(capture.snapshot().state == hydra::InputIdentificationState::Identified &&
              capture.snapshot().candidate &&
              capture.snapshot().candidate->deviceId == L"Mouse:FreshClick",
          "fresh mouse button-down remains identifiable after stale pointer noise");
}

void testIdentificationCancelTimeoutAndInvalidRequest() {
    hydra::InputIdentificationCapture capture;
    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Keyboard, 30, 3'000, 500));
    capture.cancel();
    check(capture.snapshot().state == hydra::InputIdentificationState::Cancelled &&
              capture.snapshot().terminal(),
          "explicit cancellation is terminal and deterministic");
    capture.observeInput(keyboardEvent(
        31, L"Keyboard:Late", 0x41, hydra::RawKeyTransition::Down));
    check(capture.snapshot().state == hydra::InputIdentificationState::Cancelled,
          "cancelled capture ignores later input");

    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Keyboard, 31, 3'100, 500));
    capture.advanceTime(3'599);
    check(capture.snapshot().state == hydra::InputIdentificationState::Waiting,
          "capture remains waiting strictly before its deadline");
    capture.advanceTime(3'600);
    check(capture.snapshot().state == hydra::InputIdentificationState::TimedOut &&
              capture.snapshot().terminal(),
          "capture times out exactly at its monotonic deadline");

    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Mouse, 0, 0, 0));
    check(capture.snapshot().state == hydra::InputIdentificationState::Rejected &&
              capture.snapshot().failure ==
                  hydra::InputIdentificationFailure::InvalidRequest,
          "zero-duration identification request fails closed");

    capture.reset();
    check(capture.snapshot().state == hydra::InputIdentificationState::Idle &&
              capture.snapshot().failure == hydra::InputIdentificationFailure::None &&
              !capture.snapshot().candidate && !capture.snapshot().terminal(),
          "explicit reset clears stale identification feedback before a new hardware view");
}

void testIdentificationRejectsStaleAndOutOfOrderObservations() {
    hydra::InputIdentificationCapture capture;
    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Keyboard, 40, 4'000));
    capture.observeInput(keyboardEvent(
        40, L"Keyboard:Stale", 0x41, hydra::RawKeyTransition::Down));
    check(capture.snapshot().state == hydra::InputIdentificationState::Rejected &&
              capture.snapshot().failure ==
                  hydra::InputIdentificationFailure::StaleSequence,
          "queued intentional event at the begin sequence cannot identify a device");

    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Mouse, 50, 5'000));
    capture.observeDeviceChange(deviceChange(
        50, hydra::RawInputDeviceChangeKind::Removal, L"Mouse:StaleChange", 0));
    check(capture.snapshot().state == hydra::InputIdentificationState::Rejected &&
              capture.snapshot().failure ==
                  hydra::InputIdentificationFailure::StaleSequence,
          "stale target-device change cannot mutate a fresh identification attempt");

    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Mouse, 0, 1'000));
    auto staleTimestamp = mouseEvent(11, L"Mouse:A", 0, 0, 0x0001);
    staleTimestamp.monotonicTimestampMicros = 999;
    capture.observeInput(staleTimestamp);
    check(capture.snapshot().state == hydra::InputIdentificationState::Rejected &&
              capture.snapshot().failure ==
                  hydra::InputIdentificationFailure::StaleTimestamp,
          "intentional click predating the identification start fails closed");
}

void testIdentificationRejectsRemovedAmbiguousAndUnstableDevices() {
    hydra::InputIdentificationCapture capture;
    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Mouse, 50, 5'000));
    capture.observeDeviceChange(deviceChange(
        51, hydra::RawInputDeviceChangeKind::Removal, L"Mouse:Removed", 0));
    check(capture.snapshot().state == hydra::InputIdentificationState::Rejected &&
              capture.snapshot().failure ==
                  hydra::InputIdentificationFailure::DeviceRemoved,
          "target-device removal immediately ends identification so the UI can ask for a retry");
    capture.observeInput(mouseEvent(52, L"mouse:removed", 0, 0, 0x0001));
    check(capture.snapshot().failure == hydra::InputIdentificationFailure::DeviceRemoved,
          "input arriving after removal cannot revive a rejected stale capture");

    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Mouse, 60, 6'000));
    capture.observeDeviceChange(deviceChange(
        61, hydra::RawInputDeviceChangeKind::Removal, L"Mouse:Returned", 0));
    capture.observeDeviceChange(deviceChange(
        62, hydra::RawInputDeviceChangeKind::Arrival, L"MOUSE:RETURNED", 0));
    check(capture.snapshot().state == hydra::InputIdentificationState::Rejected &&
              capture.snapshot().failure ==
                  hydra::InputIdentificationFailure::DeviceRemoved,
          "arrival cannot silently resume an identification attempt invalidated by removal");
    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Mouse, 62, 6'200));
    capture.observeInput(mouseEvent(63, L"Mouse:Returned", 0, 0, 0x0001));
    check(capture.snapshot().state == hydra::InputIdentificationState::Identified &&
              capture.snapshot().candidate &&
              capture.snapshot().candidate->deviceId == L"Mouse:Returned",
          "a fresh retry after the newer arrival can identify the returned device");

    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Keyboard, 70, 7'000));
    capture.observeInput(
        keyboardEvent(71, L"Keyboard:Shared", 0x41,
                      hydra::RawKeyTransition::Down),
        hydra::InputIdentificationDeviceStatus::AmbiguousShared);
    check(capture.snapshot().state == hydra::InputIdentificationState::Rejected &&
              capture.snapshot().failure ==
                  hydra::InputIdentificationFailure::AmbiguousSharedDevice,
          "shared ambiguous stable ID cannot be selected by first-event guessing");

    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Keyboard, 75, 7'500));
    capture.observeInput(
        keyboardEvent(76, L"Keyboard:Filtered", 0x41,
                      hydra::RawKeyTransition::Down),
        hydra::InputIdentificationDeviceStatus::Unavailable);
    check(capture.snapshot().state == hydra::InputIdentificationState::Rejected &&
              capture.snapshot().failure ==
                  hydra::InputIdentificationFailure::DeviceUnavailable,
          "an input identity absent from the current physical tile inventory cannot be guessed into a Seat");

    capture.begin(identificationRequest(
        hydra::InputIdentificationKind::Keyboard, 80, 8'000));
    capture.observeInput(keyboardEvent(
        81, L"", 0x41, hydra::RawKeyTransition::Down));
    check(capture.snapshot().state == hydra::InputIdentificationState::Rejected &&
              capture.snapshot().failure ==
                  hydra::InputIdentificationFailure::MissingStableDeviceId,
          "device path/handle fallback cannot masquerade as stable identification");
}

void testLedgerTracksHotplugAndState() {
    hydra::InputObservationLedger ledger;

    ledger.observeDeviceChange(deviceChange(
        1, hydra::RawInputDeviceChangeKind::Arrival,
        L"Keyboard:A", 1));
    ledger.observeInput(keyboardEvent(
        2, L"Keyboard:A", 0x41, hydra::RawKeyTransition::Down));

    auto keyboard = ledger.device(L"keyboard:a");
    check(keyboard.has_value(), "keyboard snapshot is case-insensitively addressable");
    check(keyboard->online, "arrival marks the keyboard online");
    check(keyboard->arrivals == 1 && keyboard->eventCount == 1,
          "arrival and input counters are independent");
    check(keyboard->pressedKeys == std::vector<std::uint32_t>{0x41},
          "key-down state is tracked per physical device");

    ledger.observeInput(keyboardEvent(
        3, L"Keyboard:A", 0x41, hydra::RawKeyTransition::Up));
    keyboard = ledger.device(L"Keyboard:A");
    check(keyboard && keyboard->pressedKeys.empty(),
          "key-up removes the per-device pressed-key state");

    ledger.observeDeviceChange(deviceChange(
        4, hydra::RawInputDeviceChangeKind::Removal,
        L"Keyboard:A", 1));
    keyboard = ledger.device(L"Keyboard:A");
    check(keyboard && !keyboard->online && keyboard->removals == 1,
          "removal marks the device offline and increments diagnostics");

    // Win32 Raw Input mouse flag values: left down = 0x0001,
    // left up = 0x0002. The tracker intentionally treats these as normalized
    // observations without requiring Win32 headers in pure tests.
    ledger.observeDeviceChange(deviceChange(
        5, hydra::RawInputDeviceChangeKind::Arrival,
        L"Mouse:A", 0));
    ledger.observeInput(mouseEvent(6, L"Mouse:A", 4, -2, 0x0001, 120));
    auto mouse = ledger.device(L"mouse:a");
    check(mouse && mouse->online, "mouse arrival is recorded");
    check(mouse->totalDeltaX == 4 && mouse->totalDeltaY == -2 &&
              mouse->totalWheelDelta == 120,
          "mouse movement and wheel totals are accumulated");
    check((mouse->mouseButtonsDown & 1u) != 0,
          "mouse button-down state is tracked");

    ledger.observeInput(mouseEvent(7, L"Mouse:A", 0, 0, 0x0002));
    mouse = ledger.device(L"Mouse:A");
    check(mouse && mouse->mouseButtonsDown == 0,
          "mouse button-up clears tracked state");

    check(ledger.totalInputEvents() == 4,
          "ledger counts all decoded input observations");
    check(ledger.totalDeviceChanges() == 3,
          "ledger counts all hot-plug observations");

    hydra::RawInputDeviceChange collectionOne = deviceChange(
        8, hydra::RawInputDeviceChangeKind::Arrival,
        L"Keyboard:Composite", 1);
    collectionOne.device.deviceHandle = 0x5001;
    auto collectionTwo = collectionOne;
    collectionTwo.sequence = 9;
    collectionTwo.device.deviceHandle = 0x5002;
    ledger.observeDeviceChange(collectionOne);
    ledger.observeDeviceChange(collectionTwo);
    collectionOne.sequence = 10;
    collectionOne.kind = hydra::RawInputDeviceChangeKind::Removal;
    collectionOne.device.online = false;
    ledger.observeDeviceChange(collectionOne);
    auto composite = ledger.device(L"Keyboard:Composite");
    check(composite && composite->online,
          "removing one HID collection does not mark a composite device offline");
    collectionTwo.sequence = 11;
    collectionTwo.kind = hydra::RawInputDeviceChangeKind::Removal;
    collectionTwo.device.online = false;
    ledger.observeDeviceChange(collectionTwo);
    composite = ledger.device(L"Keyboard:Composite");
    check(composite && !composite->online,
          "composite device becomes offline after its last collection is removed");

    const auto devices = ledger.devices();
    check(devices.size() == 3 && devices[0].deviceId == L"Keyboard:A" &&
              devices[1].deviceId == L"Keyboard:Composite" &&
              devices[2].deviceId == L"Mouse:A",
          "device snapshots have deterministic stable-ID order");
}

void testExclusiveSeatRouting() {
    hydra::WorkspaceManager seats;
    const auto seat1 = seats.createSeat(L"Seat 1");
    const auto seat2 = seats.createSeat(L"Seat 2");
    check(seats.assignKeyboard(seat1, L"Keyboard:A"),
          "Seat 1 keyboard assignment succeeds");
    check(seats.assignMouse(seat1, L"Mouse:A"),
          "Seat 1 mouse assignment succeeds");
    check(seats.assignKeyboard(seat2, L"Keyboard:B"),
          "Seat 2 keyboard assignment succeeds");
    check(seats.assignMouse(seat2, L"Mouse:B"),
          "Seat 2 mouse assignment succeeds");
    check(seats.assignTargetWindow(seat1, 0x1111),
          "Seat 1 target assignment succeeds");
    check(seats.assignTargetWindow(seat2, 0x2222),
          "Seat 2 target assignment succeeds");

    hydra::SeatRoutingPolicy policy;
    std::vector<std::uint64_t> dispatchedTargets;
    hydra::InputObservationSession session(
        seats, policy,
        [&](const hydra::RawInputEvent&,
            const hydra::InputRouteDecision& decision) {
            dispatchedTargets.push_back(decision.targetHwnd);
            return true;
        });

    const auto bindings = session.rebuildBindings();
    check(bindings.boundDevices == 4 &&
              bindings.ambiguousSharedDevices.empty(),
          "exclusive Seat devices bind without ambiguity");

    const auto routeA = session.processInput(keyboardEvent(
        10, L"keyboard:a", 0x41, hydra::RawKeyTransition::Down));
    const auto routeB = session.processInput(mouseEvent(
        11, L"MOUSE:B", 3, 2));
    const auto unassigned = session.processInput(keyboardEvent(
        12, L"Keyboard:Unknown", 0x42,
        hydra::RawKeyTransition::Down));

    check(routeA.disposition == hydra::InputRouteDisposition::Routed &&
              routeA.seatId == seat1 && routeA.targetHwnd == 0x1111,
          "Keyboard A routes only to Seat 1's target");
    check(routeB.disposition == hydra::InputRouteDisposition::Routed &&
              routeB.seatId == seat2 && routeB.targetHwnd == 0x2222,
          "Mouse B routes only to Seat 2's target");
    check(unassigned.disposition ==
              hydra::InputRouteDisposition::UnassignedDevice,
          "unassigned devices fail closed instead of guessing a Seat");
    check(dispatchedTargets == std::vector<std::uint64_t>{0x1111, 0x2222},
          "dispatcher receives one explicit target per owned device");

    const auto seat1Metrics = session.seat(seat1);
    const auto seat2Metrics = session.seat(seat2);
    check(seat1Metrics && seat1Metrics->routedEvents == 1 &&
              seat1Metrics->keyboardEvents == 1,
          "Seat 1 metrics contain only its routed keyboard event");
    check(seat2Metrics && seat2Metrics->routedEvents == 1 &&
              seat2Metrics->mouseEvents == 1,
          "Seat 2 metrics contain only its routed mouse event");
    check(session.unassignedEvents() == 1,
          "unassigned route diagnostics are counted");
}

void testSharedDevicesAreAmbiguousForGateB() {
    hydra::WorkspaceManager seats;
    const auto seat1 = seats.createSeat(L"Seat 1");
    const auto seat2 = seats.createSeat(L"Seat 2");
    check(seats.assignKeyboard(seat1, L"Keyboard:Shared", true),
          "first explicit shared assignment succeeds");
    check(seats.assignKeyboard(seat2, L"Keyboard:Shared"),
          "second shared assignment succeeds");
    check(seats.assignTargetWindow(seat1, 1), "Seat 1 target exists");
    check(seats.assignTargetWindow(seat2, 2), "Seat 2 target exists");

    hydra::SeatRoutingPolicy policy;
    bool dispatched = false;
    hydra::InputObservationSession session(
        seats, policy,
        [&](const hydra::RawInputEvent&,
            const hydra::InputRouteDecision&) {
            dispatched = true;
            return true;
        });

    const auto report = session.rebuildBindings();
    check(report.boundDevices == 0 &&
              report.ambiguousSharedDevices.size() == 1,
          "shared input is reported as ambiguous for one-target Gate B routing");

    const auto route = session.processInput(keyboardEvent(
        20, L"Keyboard:Shared", 0x43,
        hydra::RawKeyTransition::Down));
    check(route.disposition ==
              hydra::InputRouteDisposition::AmbiguousSharedDevice,
          "ambiguous shared input is not silently routed to one Seat");
    check(!dispatched && session.ambiguousEvents() == 1,
          "ambiguous input never reaches the dispatcher");
}

void testInactiveMissingAndFailedTargets() {
    hydra::WorkspaceManager seats;
    const auto inactive = seats.createSeat(L"Inactive");
    const auto missing = seats.createSeat(L"Missing Target");
    const auto failing = seats.createSeat(L"Failing Dispatch");

    check(seats.assignKeyboard(inactive, L"Keyboard:Inactive"),
          "inactive keyboard assignment succeeds");
    check(seats.setActive(inactive, false), "Seat can be disabled");
    check(seats.assignTargetWindow(inactive, 1), "inactive target exists");

    check(seats.assignKeyboard(missing, L"Keyboard:Missing"),
          "missing-target keyboard assignment succeeds");

    check(seats.assignKeyboard(failing, L"Keyboard:Fail"),
          "failing keyboard assignment succeeds");
    check(seats.assignTargetWindow(failing, 3), "failing target exists");

    hydra::SeatRoutingPolicy policy;
    hydra::InputObservationSession session(
        seats, policy,
        [](const hydra::RawInputEvent&,
           const hydra::InputRouteDecision&) { return false; });
    session.rebuildBindings();

    check(session.processInput(keyboardEvent(
              30, L"Keyboard:Inactive", 1,
              hydra::RawKeyTransition::Down)).disposition ==
              hydra::InputRouteDisposition::InactiveSeat,
          "inactive Seats reject their owned input");
    check(session.processInput(keyboardEvent(
              31, L"Keyboard:Missing", 1,
              hydra::RawKeyTransition::Down)).disposition ==
              hydra::InputRouteDisposition::MissingTargetWindow,
          "missing target windows are explicit failures");
    check(session.processInput(keyboardEvent(
              32, L"Keyboard:Fail", 1,
              hydra::RawKeyTransition::Down)).disposition ==
              hydra::InputRouteDisposition::DispatchFailed,
          "dispatcher failure is visible");

    check(session.inactiveSeatEvents() == 1 &&
              session.missingTargetEvents() == 1,
          "inactive and missing-target counters are independent");
    const auto failingMetrics = session.seat(failing);
    check(failingMetrics && failingMetrics->dispatchFailures == 1,
          "per-Seat dispatch failures are recorded");
}

void testTraceWriterProducesJsonLines() {
    const std::string tracePath = "hydra_input_observation_test.jsonl";
    hydra::InputTraceWriter trace(tracePath);
    check(trace.isOpen(), "trace writer opens a JSONL file");

    const auto event = keyboardEvent(
        40, L"Keyboard:한글", 0x41,
        hydra::RawKeyTransition::Down);
    hydra::InputRouteRecord route;
    route.sequence = event.sequence;
    route.deviceId = event.deviceId;
    route.seatId = 1;
    route.targetHwnd = 0x1111;
    route.disposition = hydra::InputRouteDisposition::Routed;

    check(trace.writeInput(event, route),
          "input trace record is written");
    check(trace.writeDeviceChange(deviceChange(
              41, hydra::RawInputDeviceChangeKind::Removal,
              L"Keyboard:한글", 1)),
          "device-change trace record is written");
    trace.flush();

    std::ifstream input(tracePath, std::ios::binary);
    std::string firstLine;
    std::string secondLine;
    std::getline(input, firstLine);
    std::getline(input, secondLine);
    check(firstLine.find("\"record\":\"input\"") != std::string::npos &&
              firstLine.find("\"route\":\"Routed\"") != std::string::npos,
          "input trace line exposes route disposition");
    check(firstLine.find("native_os_input_not_suppressed") != std::string::npos,
          "trace never implies that Gate B is real OS isolation");
    check(firstLine.find("\"vkey\":null") != std::string::npos &&
              firstLine.find("\"key_code_redacted\":true") != std::string::npos,
          "trace redacts key identifiers by default");
    check(secondLine.find("\"record\":\"device_change\"") !=
              std::string::npos &&
              secondLine.find("\"change\":\"Removal\"") !=
                  std::string::npos,
          "device-change trace line exposes hot-plug state");
    check(firstLine.find('\n') == std::string::npos &&
              secondLine.find('\n') == std::string::npos,
          "each diagnostic record remains one JSONL line");

    std::remove(tracePath.c_str());

    const std::string diagnosticPath =
        "hydra_input_observation_diagnostic_test.jsonl";
    hydra::InputTraceWriter diagnostic(
        diagnosticPath, hydra::InputTracePrivacyMode::DiagnosticKeyIds);
    check(diagnostic.isOpen() && diagnostic.writeInput(event, route),
          "diagnostic trace writes with explicit key-ID opt-in");
    diagnostic.flush();
    std::ifstream diagnosticInput(diagnosticPath, std::ios::binary);
    std::string diagnosticLine;
    std::getline(diagnosticInput, diagnosticLine);
    check(diagnosticLine.find("\"vkey\":65") != std::string::npos &&
              diagnosticLine.find("\"key_code_redacted\":false") !=
                  std::string::npos,
          "diagnostic trace exposes key identifier only after explicit opt-in");
    diagnosticInput.close();
    std::remove(diagnosticPath.c_str());
}

} // namespace

int main() {
    testKeyboardIdentificationRequiresIntentionalPress();
    testMouseIdentificationIgnoresMotionWheelAndRelease();
    testIdentificationNoiseCannotPoisonOrdering();
    testIdentificationCancelTimeoutAndInvalidRequest();
    testIdentificationRejectsStaleAndOutOfOrderObservations();
    testIdentificationRejectsRemovedAmbiguousAndUnstableDevices();
    testLedgerTracksHotplugAndState();
    testExclusiveSeatRouting();
    testSharedDevicesAreAmbiguousForGateB();
    testInactiveMissingAndFailedTargets();
    testTraceWriterProducesJsonLines();

    std::cout << "Input observation and identification tests passed.\n";
    return EXIT_SUCCESS;
}
