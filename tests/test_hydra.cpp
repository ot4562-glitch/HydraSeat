#include "hydra/hardware_detector.hpp"
#include "hydra/hid_usage.hpp"
#include "hydra/input_router.hpp"
#include "hydra/input_isolation.hpp"
#include "hydra/workspace_manager.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

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

std::string readFileBytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input), "test fixture file can be opened for reading");
    std::ostringstream bytes;
    bytes << input.rdbuf();
    check(static_cast<bool>(bytes), "test fixture file can be read completely");
    return bytes.str();
}

void checkDetectorQuery(const hydra::HardwareDetector& detector, std::string_view category) {
    if (detector.lastError()) {
        std::cerr << "[FAIL] HardwareDetector query failed for " << category
                  << " with Win32 error " << detector.lastError()->systemError << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

#ifdef _WIN32
struct StartupWindowProbe {
    DWORD processId{0};
    HWND visibleWindow{nullptr};
};

BOOL CALLBACK findVisibleProcessWindow(HWND hwnd, LPARAM parameter) {
    auto* probe = reinterpret_cast<StartupWindowProbe*>(parameter);
    if (probe == nullptr || !IsWindowVisible(hwnd)) return TRUE;

    DWORD windowProcessId = 0;
    GetWindowThreadProcessId(hwnd, &windowProcessId);
    if (windowProcessId != probe->processId) return TRUE;

    probe->visibleWindow = hwnd;
    return FALSE;
}

HWND visibleProcessWindow(DWORD processId) {
    StartupWindowProbe probe;
    probe.processId = processId;
    EnumWindows(findVisibleProcessWindow, reinterpret_cast<LPARAM>(&probe));
    return probe.visibleWindow;
}

int runStartupWindowProbe(const char* executableArgument) {
    constexpr DWORD kFirstWindowDeadlineMs = 5000u;
    constexpr DWORD kGracefulExitDeadlineMs = 5000u;

    std::error_code pathError;
    const auto executable = std::filesystem::absolute(executableArgument, pathError);
    check(!pathError && std::filesystem::is_regular_file(executable, pathError) && !pathError,
          "startup probe executable exists as a regular file");

    std::wstring commandLine = L"\"" + executable.wstring() + L"\"";
    const std::wstring workingDirectory = executable.parent_path().wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0,
                         nullptr, workingDirectory.c_str(), &startup, &process) != FALSE,
          "startup probe launches the real HydraSeat executable");
    CloseHandle(process.hThread);

    const ULONGLONG startedAt = GetTickCount64();
    HWND firstWindow = nullptr;
    while (GetTickCount64() - startedAt < kFirstWindowDeadlineMs) {
        firstWindow = visibleProcessWindow(process.dwProcessId);
        if (firstWindow != nullptr) break;
        if (WaitForSingleObject(process.hProcess, 25u) == WAIT_OBJECT_0) break;
    }

    if (firstWindow == nullptr) {
        DWORD exitCode = STILL_ACTIVE;
        (void)GetExitCodeProcess(process.hProcess, &exitCode);
        if (exitCode != STILL_ACTIVE) {
            CloseHandle(process.hProcess);
            const std::string message =
                "HydraSeat exited before exposing any visible top-level startup window (exit code " +
                std::to_string(exitCode) + ")";
            check(false, message);
        }

        (void)TerminateProcess(process.hProcess, 0xEEu);
        (void)WaitForSingleObject(process.hProcess, kGracefulExitDeadlineMs);
        CloseHandle(process.hProcess);
        check(false,
              "HydraSeat remained alive without a visible top-level window for 5000 ms");
    }

    DWORD_PTR responsivenessResult = 0;
    if (SendMessageTimeoutW(firstWindow, WM_NULL, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 500u,
                            &responsivenessResult) == 0) {
        (void)TerminateProcess(process.hProcess, 0xEDu);
        (void)WaitForSingleObject(process.hProcess, kGracefulExitDeadlineMs);
        CloseHandle(process.hProcess);
        check(false, "HydraSeat exposed a visible startup window but its UI thread was unresponsive");
    }

    wchar_t className[128]{};
    wchar_t title[256]{};
    (void)GetClassNameW(firstWindow, className, static_cast<int>(std::size(className)));
    (void)GetWindowTextW(firstWindow, title, static_cast<int>(std::size(title)));
    const ULONGLONG firstWindowMs = GetTickCount64() - startedAt;
    std::wcout << L"[Test] HydraSeat first visible window in " << firstWindowMs
               << L" ms | class=" << className << L" | title=" << title << std::endl;

    (void)PostMessageW(firstWindow, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(process.hProcess, kGracefulExitDeadlineMs) != WAIT_OBJECT_0) {
        (void)TerminateProcess(process.hProcess, 0xEFu);
        (void)WaitForSingleObject(process.hProcess, kGracefulExitDeadlineMs);
        CloseHandle(process.hProcess);
        check(false, "HydraSeat did not exit after its probed startup window was closed");
    }
    CloseHandle(process.hProcess);
    std::cout << "[Test] HydraSeat startup-window process probe passed." << std::endl;
    return EXIT_SUCCESS;
}
#endif

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
    const std::string legacyRuntimePath = "hydra_seat_legacy_runtime_test.json";
    const std::string deterministicAPath = "hydra_seat_deterministic_a_test.json";
    const std::string deterministicBPath = "hydra_seat_deterministic_b_test.json";
    const std::string oversizedPath = "hydra_seat_oversized_test.json";
    const std::string exhaustedIdPath = "hydra_seat_exhausted_id_test.json";
    const std::string atomicPath = "hydra_seat_atomic_test.json";

    hydra::WorkspaceManager defaults;
    const auto defaultSeat = defaults.createSeat();
    check(defaults.getSeat(defaultSeat) != nullptr &&
              defaults.getSeat(defaultSeat)->name == L"Seat 1",
          "default Seat names do not conflate Seat hardware with Player identity");

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
    check(mgr.assignTargetWindow(seat1, 0x12345678u),
          "runtime target window can be associated before persistence");

    const auto* config = mgr.getSeat(seat1);
    check(config != nullptr, "created seat can be retrieved");
    check(config->displayIds.size() == 2, "seat retains multiple displays");
    check(config->primaryDisplayId && *config->primaryDisplayId == L"Display:Samsung",
          "explicit primary display is retained");
    check(config->targetHwnd == 0x12345678u,
          "runtime target window remains available before persistence");

    check(mgr.saveToFile(roundTripPath), "seat profile saves as JSON");
    hydra::WorkspaceManager loaded;
    check(loaded.loadFromFile(roundTripPath), "saved seat profile loads successfully");
    auto expectedPersistedSeats = mgr.getAllSeats();
    for (auto& seat : expectedPersistedSeats) seat.targetHwnd = 0u;
    check(loaded.getAllSeats() == expectedPersistedSeats,
          "save/load preserves stable seat configuration while discarding runtime HWND identity");
    check(loaded.getSeat(seat1) != nullptr && loaded.getSeat(seat1)->targetHwnd == 0u,
          "saved runtime target HWND is never restored from the profile");
    check(loaded.managementSeatId() == seat2,
          "save/load preserves the explicit Management Seat");
    check(loaded.isDeviceShareable(hydra::SeatDeviceType::Keyboard, L"Keyboard:Shared"),
          "save/load preserves shareable-device policy");
    check(loaded.createSeat(L"After Load") == 3, "next seat ID advances after load");

    {
        std::ofstream legacy(legacyRuntimePath, std::ios::binary | std::ios::trunc);
        legacy << R"json({
  "schema_version": 2,
  "management_seat_id": 1,
  "shareable_resources": [],
  "seats": [
    {
      "id": 1,
      "name": "Legacy Seat",
      "active": true,
      "target_hwnd": 305419896,
      "displays": [],
      "primary_display": null,
      "keyboards": [],
      "mice": [],
      "controllers": [],
      "audio_output": null,
      "audio_input": null
    }
  ]
})json";
    }
    hydra::WorkspaceManager legacyLoaded;
    check(legacyLoaded.loadFromFile(legacyRuntimePath),
          "historical schema-v2 profile with a nonzero HWND still parses");
    check(legacyLoaded.getSeat(1) != nullptr && legacyLoaded.getSeat(1)->targetHwnd == 0u,
          "historical persisted HWND is discarded instead of becoming live ownership state");

    const auto beforeMalformed = loaded.getAllSeats();
    {
        std::ofstream malformed(malformedPath, std::ios::binary | std::ios::trunc);
        malformed << "{ this is not valid json";
    }
    check(!loaded.loadFromFile(malformedPath), "malformed JSON is rejected");
    check(loaded.getAllSeats() == beforeMalformed, "failed load is transactional");

    hydra::WorkspaceManager deterministicA;
    hydra::WorkspaceManager deterministicB;
    check(deterministicA.createSeat() == 1 && deterministicB.createSeat() == 1,
          "deterministic profile fixtures create the same Seat identity");
    check(deterministicA.setDeviceShareable(
              hydra::SeatDeviceType::Mouse, L"Mouse:Z", true) &&
              deterministicA.setDeviceShareable(
                  hydra::SeatDeviceType::Keyboard, L"Keyboard:A", true),
          "first deterministic fixture accepts shareable resources");
    check(deterministicB.setDeviceShareable(
              hydra::SeatDeviceType::Keyboard, L"Keyboard:A", true) &&
              deterministicB.setDeviceShareable(
                  hydra::SeatDeviceType::Mouse, L"Mouse:Z", true),
          "second deterministic fixture accepts the same resources in reverse order");
    check(deterministicA.saveToFile(deterministicAPath) &&
              deterministicB.saveToFile(deterministicBPath),
          "logically equivalent Seat profiles save successfully");
    check(readFileBytes(deterministicAPath) == readFileBytes(deterministicBPath),
          "profile serialization is deterministic regardless of unordered insertion order");

    const auto beforeBoundedFailure = loaded.getAllSeats();
    const auto managementBeforeBoundedFailure = loaded.managementSeatId();
    {
        std::ofstream oversized(oversizedPath, std::ios::binary | std::ios::trunc);
        oversized.seekp(static_cast<std::streamoff>(1024u * 1024u));
        oversized.put('x');
    }
    check(!loaded.loadFromFile(oversizedPath),
          "profile larger than the bounded input limit is rejected before parsing");
    check(loaded.getAllSeats() == beforeBoundedFailure &&
              loaded.managementSeatId() == managementBeforeBoundedFailure,
          "oversized profile rejection leaves the live Seat state untouched");

    {
        std::ofstream exhausted(exhaustedIdPath, std::ios::binary | std::ios::trunc);
        exhausted << R"json({
  "schema_version": 2,
  "management_seat_id": 4294967295,
  "shareable_resources": [],
  "seats": [
    {
      "id": 4294967295,
      "name": "Exhausted Seat",
      "active": true,
      "target_hwnd": 0,
      "displays": [],
      "primary_display": null,
      "keyboards": [],
      "mice": [],
      "controllers": [],
      "audio_output": null,
      "audio_input": null
    }
  ]
})json";
    }
    check(!loaded.loadFromFile(exhaustedIdPath),
          "profile that would exhaust and wrap Seat IDs is rejected");
    check(loaded.getAllSeats() == beforeBoundedFailure &&
              loaded.managementSeatId() == managementBeforeBoundedFailure,
          "Seat ID exhaustion rejection remains transactional");

    hydra::WorkspaceManager atomic;
    const auto atomicSeat = atomic.createSeat(L"Original Seat");
    check(atomicSeat == 1 && atomic.saveToFile(atomicPath),
          "baseline profile for atomic-save failure test is committed");
    const auto originalAtomicBytes = readFileBytes(atomicPath);
    const std::filesystem::path blockedStaging = atomicPath + ".tmp";
    std::filesystem::create_directories(blockedStaging);
    {
        std::ofstream blocker(blockedStaging / "keep", std::ios::binary | std::ios::trunc);
        blocker << "prevent stale-stage cleanup";
    }
    check(atomic.renameSeat(atomicSeat, L"Changed Seat"),
          "atomic-save fixture mutates in-memory state before the injected failure");
    check(!atomic.saveToFile(atomicPath),
          "save fails closed when its staging path cannot be safely replaced");
    check(readFileBytes(atomicPath) == originalAtomicBytes,
          "failed staged save preserves the previously committed profile bytes");
    std::error_code cleanupError;
    std::filesystem::remove_all(blockedStaging, cleanupError);
    check(!cleanupError, "atomic-save test staging directory is cleaned up");

    check(mgr.unassignKeyboard(seat1, L"Keyboard:A"), "device can be unassigned");
    check(mgr.assignKeyboard(seat2, L"Keyboard:A"), "unassignment releases exclusive ownership");
    check(mgr.removeSeat(seat2), "seat removal succeeds");
    check(mgr.managementSeatId() == seat1,
          "removing the Management Seat deterministically transfers control to the lowest remaining Seat");
    check(!mgr.findKeyboardOwner(L"Keyboard:A"), "removing a seat releases its device ownership");

    std::remove(roundTripPath.c_str());
    std::remove(malformedPath.c_str());
    std::remove(legacyRuntimePath.c_str());
    std::remove(deterministicAPath.c_str());
    std::remove(deterministicBPath.c_str());
    std::remove(oversizedPath.c_str());
    std::remove(exhaustedIdPath.c_str());
    std::remove(atomicPath.c_str());
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

int main(int argc, char* argv[]) {
#ifdef _WIN32
    if (argc == 3 && std::string_view(argv[1]) == "--startup-window-probe") {
        return runStartupWindowProbe(argv[2]);
    }
#else
    (void)argc;
    (void)argv;
#endif
    std::cout << "Running HydraSeat Engine Tests..." << std::endl;
    testHidUsageClassification();
    testHardwareDetector();
    testWorkspaceManager();
    testInputIsolationSkeleton();
    std::cout << "All HydraSeat Engine Tests Passed!" << std::endl;
    return EXIT_SUCCESS;
}
