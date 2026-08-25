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
    testLedgerTracksHotplugAndState();
    testExclusiveSeatRouting();
    testSharedDevicesAreAmbiguousForGateB();
    testInactiveMissingAndFailedTargets();
    testTraceWriterProducesJsonLines();

    std::cout << "Input observation and Gate B routing tests passed.\n";
    return EXIT_SUCCESS;
}
