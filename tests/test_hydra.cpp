#include "hydra/display_manager.hpp"
#include "hydra/hardware_detector.hpp"
#include "hydra/hid_usage.hpp"
#include "hydra/input_router.hpp"
#include "hydra/workspace_manager.hpp"

#include <cstdlib>
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
    hydra::WorkspaceManager mgr;
    const uint32_t ws1 = mgr.createWorkspace(L"Player 1");
    const uint32_t ws2 = mgr.createWorkspace(L"Player 2");

    check(ws1 == 1, "first workspace ID is 1");
    check(ws2 == 2, "second workspace ID is 2");
    check(mgr.getAllWorkspaces().size() == 2, "two workspaces were created");

    check(mgr.assignDisplay(ws1, L"\\\\.\\DISPLAY1"), "display assignment succeeds");

    const auto* wsConfig = mgr.getWorkspace(ws1);
    check(wsConfig != nullptr, "created workspace can be retrieved");
    check(wsConfig->displayDeviceName == L"\\\\.\\DISPLAY1",
          "assigned display is retained");

    std::cout << "[Test] WorkspaceManager tests passed." << std::endl;
}

} // namespace

int main() {
    std::cout << "Running HydraSeat Engine Tests..." << std::endl;
    testHidUsageClassification();
    testHardwareDetector();
    testWorkspaceManager();
    std::cout << "All HydraSeat Engine Tests Passed!" << std::endl;
    return EXIT_SUCCESS;
}
