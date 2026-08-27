#include "hydra/display_manager.hpp"
#include "hydra/hardware_detector.hpp"
#include "hydra/hid_usage.hpp"
#include "hydra/input_router.hpp"
#include "hydra/input_isolation.hpp"
#include "hydra/workspace_manager.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>

static_assert(hydra::hid::classifyCollection(0x01, 0x02) == hydra::hid::CollectionKind::Mouse);
static_assert(hydra::hid::classifyCollection(0x0D, 0x05) == hydra::hid::CollectionKind::Touchpad);
static_assert(hydra::hid::classifyCollection(0x01, 0x06) == hydra::hid::CollectionKind::Keyboard);
static_assert(hydra::hid::classifyCollection(0x01, 0x04) == hydra::hid::CollectionKind::Joystick);
static_assert(hydra::hid::classifyCollection(0x01, 0x05) == hydra::hid::CollectionKind::Gamepad);
static_assert(hydra::hid::classifyCollection(0x0C, 0x01) == hydra::hid::CollectionKind::Other);
static_assert(hydra::hid::isMouseLikeCollection(0x01, 0x02));
static_assert(hydra::hid::isMouseLikeCollection(0x0D, 0x05));
static_assert(!hydra::hid::isMouseLikeCollection(0x01, 0x05));

namespace {

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void checkDetectorQuery(const hydra::HardwareDetector& detector, std::string_view category) {
    if (detector.lastError()) {
        std::cerr << "[FAIL] HardwareDetector query failed for " << category
                  << " with Win32 error " << detector.lastError()->systemError << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void testHidUsageClassification() {
    std::cout << "[Test] HID usage classification tests passed." << std::endl;
}

void testHardwareDetector() {
    hydra::HardwareDetector detector;

    const auto displays = detector.detectDisplays();
    checkDetectorQuery(detector, "displays");
    std::cout << "[Test] Displays detected: " << displays.size() << std::endl;

    const auto keyboards = detector.detectKeyboards();
    checkDetectorQuery(detector, "keyboards");
    std::cout << "[Test] Keyboards detected: " << keyboards.size() << std::endl;
    for (std::size_t i = 0; i < keyboards.size(); ++i) {
        std::wcout << L"  KBD [" << i << L"]: handle=0x" << std::hex << keyboards[i].nativeHandle
                   << L", id=" << keyboards[i].id
                   << L", name=" << keyboards[i].name
                   << L", path=" << keyboards[i].devicePath << std::dec << std::endl;
    }

    const auto mice = detector.detectMice();
    checkDetectorQuery(detector, "mice/touchpads");
    std::cout << "[Test] Mice/touchpads detected: " << mice.size() << std::endl;
    for (std::size_t i = 0; i < mice.size(); ++i) {
        std::wcout << L"  MOU [" << i << L"]: handle=0x" << std::hex << mice[i].nativeHandle
                   << L", id=" << mice[i].id
                   << L", name=" << mice[i].name
                   << L", path=" << mice[i].devicePath << std::dec << std::endl;
    }

    const auto controllers = detector.detectControllers();
    checkDetectorQuery(detector, "controllers");
    std::cout << "[Test] Controllers detected: " << controllers.size() << std::endl;
}

void testWorkspaceManager() {
    const std::string roundTripPath = "hydra_seat_roundtrip_test.json";
    const std::string malformedPath = "hydra_seat_malformed_test.json";

    hydra::WorkspaceManager mgr;
    const auto seat1 = mgr.createSeat(L"민성");
    const auto seat2 = mgr.createSeat(L"Player 2");
    check(seat1 == 1 && seat2 == 2, "seat IDs are deterministic");
    check(mgr.managementSeatId() == 1, "Management Seat defaults deterministically to Seat 1");
    check(!mgr.setManagementSeatId(99), "Management Seat cannot reference an unknown Seat");
    check(mgr.setManagementSeatId(seat2), "Management Seat can be explicitly moved to Seat 2");

    check(mgr.assignDisplay(seat1, L"Display:LG", true), "first display assignment succeeds");
    check(mgr.assignDisplay(seat1, L"Display:Samsung"), "second display can belong to one seat");
    check(mgr.setPrimaryDisplay(seat1, L"Display:Samsung"), "primary display can be changed explicitly");
    check(!mgr.assignDisplay(seat2, L"Display:LG"), "display ownership is exclusive by default");

    check(mgr.assignKeyboard(seat1, L"Keyboard:A"), "keyboard assignment succeeds");
    check(!mgr.assignKeyboard(seat2, L"keyboard:a"), "keyboard ownership is case-insensitively exclusive");
    check(mgr.assignMouse(seat1, L"Mouse:A"), "mouse assignment succeeds");
    check(!mgr.assignMouse(seat2, L"MOUSE:A"), "mouse ownership is exclusive");
    check(mgr.assignController(seat1, L"Controller:XInput:0"), "controller assignment succeeds");
    check(!mgr.assignController(seat2, L"Controller:XInput:0"), "controller ownership is exclusive");

    check(mgr.assignKeyboard(seat1, L"Keyboard:Shared", true), "shareable keyboard assignment succeeds");
    check(mgr.assignKeyboard(seat2, L"Keyboard:Shared"), "explicitly shareable device can belong to two seats");

    check(mgr.assignAudioOutput(seat1, L"Audio:Headset"), "audio output assignment succeeds");
    check(mgr.assignAudioInput(seat1, L"Audio:Mic"), "audio input assignment succeeds");

    const auto* config = mgr.getSeat(seat1);
    check(config != nullptr, "created seat can be retrieved");
    check(config->displayIds.size() == 2, "seat retains multiple displays");
    check(config->primaryDisplayId && *config->primaryDisplayId == L"Display:Samsung",
          "explicit primary display is retained");

    check(mgr.saveToFile(roundTripPath), "seat profile saves as JSON");
    hydra::WorkspaceManager loaded;
    check(loaded.loadFromFile(roundTripPath), "saved seat profile loads successfully");
    check(loaded.getAllSeats() == mgr.getAllSeats(), "save/load preserves all seat configuration");
    check(loaded.managementSeatId() == seat2,
          "save/load preserves the explicit Management Seat");
    check(loaded.isDeviceShareable(hydra::SeatDeviceType::Keyboard, L"Keyboard:Shared"),
          "save/load preserves shareable-device policy");
    check(loaded.createSeat(L"After Load") == 3, "next seat ID advances after load");

    const auto beforeMalformed = loaded.getAllSeats();
    {
        std::ofstream malformed(malformedPath, std::ios::binary | std::ios::trunc);
        malformed << "{ this is not valid json";
    }
    check(!loaded.loadFromFile(malformedPath), "malformed JSON is rejected");
    check(loaded.getAllSeats() == beforeMalformed, "failed load is transactional");

    check(mgr.unassignKeyboard(seat1, L"Keyboard:A"), "device can be unassigned");
    check(mgr.assignKeyboard(seat2, L"Keyboard:A"), "unassignment releases exclusive ownership");
    check(mgr.removeSeat(seat2), "seat removal succeeds");
    check(mgr.managementSeatId() == seat1,
          "removing the Management Seat deterministically transfers control to the lowest remaining Seat");
    check(!mgr.findKeyboardOwner(L"Keyboard:A"), "removing a seat releases its device ownership");

    std::remove(roundTripPath.c_str());
    std::remove(malformedPath.c_str());
    std::cout << "[Test] Seat/WorkspaceManager tests passed." << std::endl;
}

void testInputIsolationSkeleton() {
    hydra::WorkspaceManager seats;
    const auto seat1 = seats.createSeat(L"Seat 1");
    const auto seat2 = seats.createSeat(L"Seat 2");
    check(seats.assignTargetWindow(seat1, 0x1111), "seat 1 target window assignment succeeds");
    check(seats.assignTargetWindow(seat2, 0x2222), "seat 2 target window assignment succeeds");

    hydra::SeatRoutingPolicy routing;
    check(routing.bindDevice(L"Keyboard:A", seat1), "routing policy binds keyboard A");
    check(routing.bindDevice(L"Keyboard:B", seat2), "routing policy binds keyboard B");

    const auto routeA = routing.route(L"keyboard:a", seats, true);
    const auto routeB = routing.route(L"Keyboard:B", seats, false);
    check(routeA.seatId && *routeA.seatId == seat1 && routeA.targetHwnd == 0x1111,
          "routing policy resolves seat 1 target window");
    check(routeA.consumePhysicalInput, "isolation request is represented in route decision");
    check(routeB.seatId && *routeB.seatId == seat2 && routeB.targetHwnd == 0x2222,
          "routing policy resolves seat 2 target window");
    check(!routeB.consumePhysicalInput, "non-isolated route does not request physical suppression");

    hydra::UnsupportedIsolationBackend backend;
    check(backend.start(), "placeholder isolation backend can initialize safely");
    check(!backend.applyRoute(L"Keyboard:A", routeA),
          "placeholder backend explicitly refuses to claim real isolation");
    check(!hydra::hasCapability(backend.capabilities(),
                                hydra::InputIsolationCapability::PhysicalDeviceSuppression),
          "Phase 3 skeleton does not claim physical device suppression");
    backend.stop();

    routing.clearSeat(seat1);
    check(!routing.ownerOf(L"Keyboard:A"), "routing policy removes bindings for deleted seat");
    std::cout << "[Test] Phase 3 input-isolation skeleton tests passed." << std::endl;
}

} // namespace

int main() {
    std::cout << "Running HydraSeat Engine Tests..." << std::endl;
    testHidUsageClassification();
    testHardwareDetector();
    testWorkspaceManager();
    testInputIsolationSkeleton();
    std::cout << "All HydraSeat Engine Tests Passed!" << std::endl;
    return EXIT_SUCCESS;
}
