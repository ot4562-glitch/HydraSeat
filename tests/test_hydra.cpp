#include "hydra/hardware_detector.hpp"
#include "hydra/display_manager.hpp"
#include "hydra/workspace_manager.hpp"
#include "hydra/input_router.hpp"
#include "hydra/runtime_authority.hpp"

#include <iostream>
#include <cassert>

void testControllerIdentity();

void testHardwareDetector() {
    hydra::HardwareDetector detector;
    auto displays = detector.detectDisplays();
    std::cout << "[Test] Displays detected: " << displays.size() << std::endl;

    auto keyboards = detector.detectKeyboards();
    std::cout << "[Test] Keyboards detected: " << keyboards.size() << std::endl;
    auto mice = detector.detectMice();
    std::cout << "[Test] Mice detected: " << mice.size() << std::endl;
}

void testWorkspaceManager() {
    hydra::WorkspaceManager mgr;
    uint32_t ws1 = mgr.createWorkspace(L"Player 1");
    uint32_t ws2 = mgr.createWorkspace(L"Player 2");
    assert(ws1 == 1);
    assert(ws2 == 2);
    assert(mgr.getAllWorkspaces().size() == 2);
    assert(mgr.assignDisplay(ws1, L"\\\\.\\DISPLAY1"));
    const auto* wsConfig = mgr.getWorkspace(ws1);
    assert(wsConfig != nullptr);
    assert(wsConfig->displayDeviceName == L"\\\\.\\DISPLAY1");
    std::cout << "[Test] WorkspaceManager tests passed." << std::endl;
}

void testRuntimeAuthority() {
    hydra::runtime::SessionController controller;

    const auto first = controller.beginSeatActivation(1);
    assert(first.valid());
    const hydra::runtime::ProcessIdentity process{4242, 1001};
    assert(controller.publishProcess(first, process));
    assert(controller.bindTargetWindow(first, process, 0x100));

    const hydra::controller::SeatBinding seat1Controller{
        1, hydra::controller::ApiSurface::GameInput, "gameinput:pad-a",
        std::wstring{L"container-a"}, std::nullopt};
    assert(controller.bindController(first, seat1Controller));

    auto snapshot = controller.snapshot(1);
    assert(snapshot.has_value());
    assert(snapshot->active);
    assert(snapshot->process == process);
    assert(snapshot->targetHwnd == 0x100);
    assert(snapshot->controllerBinding == seat1Controller);

    // Never overwrite a live activation.
    assert(!controller.beginSeatActivation(1).valid());

    const auto otherSeat = controller.beginSeatActivation(2);
    assert(otherSeat.valid());
    assert(!controller.publishProcess(otherSeat, process));

    const hydra::runtime::ProcessIdentity otherProcess{6262, 3003};
    assert(controller.publishProcess(otherSeat, otherProcess));
    assert(!controller.bindTargetWindow(otherSeat, otherProcess, 0x100));
    assert(controller.bindTargetWindow(otherSeat, otherProcess, 0x200));

    // Same physical controller cannot be owned by both Seats even if the
    // runtime-facing key differs.
    const hydra::controller::SeatBinding duplicateController{
        2, hydra::controller::ApiSurface::GameInput, "gameinput:pad-a-second-view",
        std::wstring{L"CONTAINER-A"}, std::nullopt};
    assert(!controller.bindController(otherSeat, duplicateController));

    const hydra::controller::SeatBinding seat2Controller{
        2, hydra::controller::ApiSurface::XInput, "xinput-slot:0",
        std::nullopt, std::uint8_t{0}};
    assert(controller.bindController(otherSeat, seat2Controller));

    // GameInput is not silently downgraded to XInput.
    const auto unsupportedPoll = controller.pollController(first);
    assert(unsupportedPoll.status == hydra::controller::IoStatus::UnsupportedApi);

    const auto seat2Poll = controller.pollController(otherSeat);
    const auto seat2Vibration = controller.setControllerVibration(
        otherSeat, std::uint16_t{0}, std::uint16_t{0});
#ifdef _WIN32
    assert(seat2Poll.status == hydra::controller::IoStatus::Ok ||
           seat2Poll.status == hydra::controller::IoStatus::Disconnected);
    assert(seat2Vibration == hydra::controller::IoStatus::Ok ||
           seat2Vibration == hydra::controller::IoStatus::Disconnected);
#else
    assert(seat2Poll.status == hydra::controller::IoStatus::PlatformUnavailable);
    assert(seat2Vibration == hydra::controller::IoStatus::PlatformUnavailable);
#endif

    assert(controller.endSeatActivation(first));
    snapshot = controller.snapshot(1);
    assert(snapshot.has_value());
    assert(!snapshot->active);
    assert(!snapshot->process.has_value());
    assert(snapshot->targetHwnd == 0);
    assert(!snapshot->controllerBinding.has_value());

    const auto restarted = controller.beginSeatActivation(1);
    assert(restarted.valid());
    assert(restarted.generation > first.generation);
    assert(!controller.publishProcess(first, process));
    assert(!controller.bindTargetWindow(first, process, 0x300));
    assert(!controller.bindController(first, seat1Controller));
    assert(!controller.endSeatActivation(first));

    assert(controller.endSeatActivation(restarted));
    assert(controller.endSeatActivation(otherSeat));
    assert(controller.pollController(otherSeat).status ==
           hydra::controller::IoStatus::InvalidBinding);
    assert(controller.setControllerVibration(
               otherSeat, std::uint16_t{0}, std::uint16_t{0}) ==
           hydra::controller::IoStatus::InvalidBinding);
    assert(!controller.beginSeatActivation(3).valid());

    std::cout << "[Test] RuntimeAuthority tests passed." << std::endl;
}

int main() {
    std::cout << "Running HydraSeat Engine Tests..." << std::endl;
    testHardwareDetector();
    testWorkspaceManager();
    testRuntimeAuthority();
    testControllerIdentity();
    std::cout << "All HydraSeat Engine Tests Passed!" << std::endl;
    return 0;
}
