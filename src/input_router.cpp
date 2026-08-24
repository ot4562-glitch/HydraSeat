#include "hydra/input_router.hpp"

#include "hydra/hardware_identity.hpp"
#include "hydra/hid_usage.hpp"
#include "hydra/raw_input_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

#ifdef _WIN32
#include <hidusage.h>
#endif

namespace hydra {
namespace {

#ifdef _WIN32

constexpr wchar_t kRouterWindowClass[] = L"HydraSeatRawInputHost";

std::wstring categoryForDevice(std::uint32_t rawDeviceType) {
    if (rawDeviceType == RIM_TYPEKEYBOARD) {
        return L"Keyboard";
    }
    if (rawDeviceType == RIM_TYPEMOUSE || rawDeviceType == RIM_TYPEHID) {
        return L"Mouse";
    }
    return L"RawInput";
}

std::wstring fallbackDeviceId(std::uint32_t rawDeviceType,
                              std::uintptr_t handle) {
    std::wostringstream out;
    out << categoryForDevice(rawDeviceType) << L":SessionHandle:0x"
        << std::hex << std::uppercase << handle;
    return out.str();
}

bool isKeyboardDownMessage(std::uint32_t message) noexcept {
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

bool isKeyboardUpMessage(std::uint32_t message) noexcept {
    return message == WM_KEYUP || message == WM_SYSKEYUP;
}

#endif

} // namespace

InputRouter::~InputRouter() {
    stop();
}

bool InputRouter::postInputToWindow(std::uint64_t hwndValue,
                                    const RawInputEvent& event) {
#ifdef _WIN32
    const HWND hwnd = reinterpret_cast<HWND>(hwndValue);
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return false;
    }

    bool posted = false;
    if (event.keyTransition != RawKeyTransition::None && event.vkey != 0) {
        UINT message = event.messageType;
        if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN &&
            message != WM_KEYUP && message != WM_SYSKEYUP) {
            message = event.keyTransition == RawKeyTransition::Down
                          ? WM_KEYDOWN
                          : WM_KEYUP;
        }

        LPARAM keyData = 1;
        keyData |= static_cast<LPARAM>(event.scanCode & 0xffu) << 16;
        if ((event.keyboardFlags & RI_KEY_E0) != 0) {
            keyData |= static_cast<LPARAM>(1) << 24;
        }
        if (event.keyTransition == RawKeyTransition::Up) {
            keyData |= static_cast<LPARAM>(1) << 30;
            keyData |= static_cast<LPARAM>(1) << 31;
        }
        posted = PostMessageW(hwnd, message,
                              static_cast<WPARAM>(event.vkey), keyData) != FALSE;
    }

    if (event.deltaX != 0 || event.deltaY != 0) {
        // The Phase 3 Gate B harness treats these as diagnostic relative
        // deltas. They are not a substitute for process-local cursor state.
        const auto clampToShort = [](std::int32_t value) {
            return static_cast<SHORT>(std::clamp(
                value,
                static_cast<std::int32_t>(std::numeric_limits<SHORT>::min()),
                static_cast<std::int32_t>(std::numeric_limits<SHORT>::max())));
        };
        posted = PostMessageW(
                     hwnd, WM_MOUSEMOVE, 0,
                     MAKELPARAM(clampToShort(event.deltaX),
                                clampToShort(event.deltaY))) != FALSE ||
                 posted;
    }

    return posted;
#else
    (void)hwndValue;
    (void)event;
    return false;
#endif
}

#ifdef _WIN32

std::uint64_t InputRouter::monotonicTimestampMicros() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

void InputRouter::recordError(std::wstring operation,
                              std::uint32_t systemError) {
    m_lastError = InputRouterError{std::move(operation), systemError};
}

void InputRouter::recordDroppedEvent() noexcept {
    ++m_statistics.droppedEvents;
}

LRESULT CALLBACK InputRouter::WndProc(HWND hwnd, UINT message,
                                      WPARAM wParam, LPARAM lParam) {
    InputRouter* router = reinterpret_cast<InputRouter*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        router = static_cast<InputRouter*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(router));
    }

    if (router != nullptr) {
        if (message == WM_INPUT) {
            router->handleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        if (message == WM_INPUT_DEVICE_CHANGE) {
            router->handleDeviceChange(wParam,
                                       reinterpret_cast<HANDLE>(lParam));
            return 0;
        }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

RawInputDeviceDescriptor InputRouter::describeDevice(
    HANDLE deviceHandle, std::uint32_t knownType) {
    RawInputDeviceDescriptor result;
    result.deviceHandle = reinterpret_cast<std::uintptr_t>(deviceHandle);
    result.rawDevType = knownType;
    result.online = true;

    if (deviceHandle == nullptr) {
        if (knownType == std::numeric_limits<std::uint32_t>::max()) {
            result.rawDevType = RIM_TYPEMOUSE;
        }
        result.deviceId = categoryForDevice(result.rawDevType) +
                          L":SystemAggregate";
        return result;
    }

    if (const auto details = win32::rawInputDeviceInfo(deviceHandle)) {
        result.rawDevType = details->dwType;
        if (details->dwType == RIM_TYPEHID) {
            const auto kind = hid::classifyCollection(
                details->hid.usUsagePage, details->hid.usUsage);
            if (kind != hid::CollectionKind::Mouse &&
                kind != hid::CollectionKind::Touchpad) {
                // This router registers only keyboard, mouse, and touchpad
                // classes. Controller HID collections belong to a separate
                // Phase 3 backend and must not be mislabeled as mice.
                return result;
            }
            result.isTouchpad = kind == hid::CollectionKind::Touchpad;
        } else if (details->dwType != RIM_TYPEKEYBOARD &&
                   details->dwType != RIM_TYPEMOUSE) {
            return result;
        }
    } else if (knownType == RIM_TYPEHID ||
               knownType == std::numeric_limits<std::uint32_t>::max()) {
        return result;
    }

    if (const auto path = win32::rawInputDeviceName(deviceHandle)) {
        result.devicePath = *path;
        if (result.rawDevType == RIM_TYPEMOUSE &&
            hardware::isLikelyTouchpadPath(*path)) {
            result.isTouchpad = true;
        }
        result.deviceId = win32::makeStableRawInputDeviceId(
            categoryForDevice(result.rawDevType), *path);
    }

    if (result.deviceId.empty()) {
        result.deviceId = fallbackDeviceId(result.rawDevType,
                                           result.deviceHandle);
    }
    return result;
}

RawInputDeviceDescriptor InputRouter::cachedOrDescribeDevice(
    HANDLE deviceHandle, std::uint32_t knownType) {
    const auto key = reinterpret_cast<std::uintptr_t>(deviceHandle);
    if (const auto existing = m_deviceCache.find(key);
        existing != m_deviceCache.end()) {
        return existing->second;
    }

    auto descriptor = describeDevice(deviceHandle, knownType);
    cacheDevice(descriptor);
    return descriptor;
}

void InputRouter::cacheDevice(const RawInputDeviceDescriptor& device) {
    m_deviceCache[device.deviceHandle] = device;
}

void InputRouter::handleRawInput(HRAWINPUT rawInputHandle) {
    UINT requiredBytes = 0;
    if (GetRawInputData(rawInputHandle, RID_INPUT, nullptr,
                        &requiredBytes, sizeof(RAWINPUTHEADER)) ==
            static_cast<UINT>(-1) ||
        requiredBytes < sizeof(RAWINPUTHEADER)) {
        recordDroppedEvent();
        return;
    }

    std::vector<std::byte> storage(requiredBytes);
    UINT availableBytes = requiredBytes;
    const UINT copied = GetRawInputData(
        rawInputHandle, RID_INPUT, storage.data(), &availableBytes,
        sizeof(RAWINPUTHEADER));
    if (copied == static_cast<UINT>(-1) || copied != requiredBytes) {
        recordDroppedEvent();
        return;
    }

    const auto* raw = reinterpret_cast<const RAWINPUT*>(storage.data());
    RawInputEvent event;
    event.sequence = m_nextSequence++;
    event.monotonicTimestampMicros = monotonicTimestampMicros();
    event.deviceHandle = reinterpret_cast<std::uintptr_t>(
        raw->header.hDevice);
    event.rawDevType = raw->header.dwType;

    const auto descriptor = cachedOrDescribeDevice(
        raw->header.hDevice, raw->header.dwType);
    event.deviceId = descriptor.deviceId;
    event.devicePath = descriptor.devicePath;
    event.isTouchpad = descriptor.isTouchpad;

    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        event.messageType = raw->data.keyboard.Message;
        event.vkey = raw->data.keyboard.VKey;
        event.scanCode = raw->data.keyboard.MakeCode;
        event.keyboardFlags = raw->data.keyboard.Flags;
        if (event.vkey == 0) {
            event.vkey = event.scanCode;
        }
        if (isKeyboardDownMessage(event.messageType)) {
            event.keyTransition = RawKeyTransition::Down;
        } else if (isKeyboardUpMessage(event.messageType)) {
            event.keyTransition = RawKeyTransition::Up;
        }
    } else if (raw->header.dwType == RIM_TYPEMOUSE) {
        event.messageType = WM_MOUSEMOVE;
        event.deltaX = raw->data.mouse.lLastX;
        event.deltaY = raw->data.mouse.lLastY;
        event.mouseFlags = raw->data.mouse.usFlags;
        event.mouseButtonFlags = raw->data.mouse.usButtonFlags;
        event.mouseButtonData = raw->data.mouse.usButtonData;
        event.isAbsoluteMouse =
            (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
        if ((event.mouseButtonFlags & RI_MOUSE_WHEEL) != 0 ||
            (event.mouseButtonFlags & RI_MOUSE_HWHEEL) != 0) {
            event.wheelDelta = static_cast<std::int16_t>(
                static_cast<SHORT>(event.mouseButtonData));
        }
    } else if (raw->header.dwType == RIM_TYPEHID) {
        const auto details = win32::rawInputDeviceInfo(raw->header.hDevice);
        if (!details || details->dwType != RIM_TYPEHID ||
            !hid::isMouseLikeCollection(details->hid.usUsagePage,
                                        details->hid.usUsage)) {
            recordDroppedEvent();
            return;
        }
        event.messageType = WM_MOUSEMOVE;
        event.isTouchpad = hid::classifyCollection(
                               details->hid.usUsagePage,
                               details->hid.usUsage) ==
                           hid::CollectionKind::Touchpad;
    } else {
        recordDroppedEvent();
        return;
    }

    ++m_statistics.decodedEvents;

    try {
        if (const auto callback = m_deviceCallbacks.find(event.deviceHandle);
            callback != m_deviceCallbacks.end() && callback->second) {
            callback->second(event);
        }
        if (m_globalCallback) {
            m_globalCallback(event);
        }
    } catch (...) {
        // User callbacks must not unwind through a Win32 window procedure.
        recordDroppedEvent();
    }
}

void InputRouter::handleDeviceChange(WPARAM change, HANDLE deviceHandle) {
    RawInputDeviceChange event;
    event.sequence = m_nextSequence++;
    event.monotonicTimestampMicros = monotonicTimestampMicros();

    const auto cacheKey = reinterpret_cast<std::uintptr_t>(deviceHandle);
    if (change == GIDC_ARRIVAL) {
        event.kind = RawInputDeviceChangeKind::Arrival;
        event.device = describeDevice(deviceHandle);
        if (event.device.deviceId.empty()) {
            return;
        }
        cacheDevice(event.device);
        ++m_statistics.deviceArrivals;
    } else if (change == GIDC_REMOVAL) {
        event.kind = RawInputDeviceChangeKind::Removal;
        if (const auto existing = m_deviceCache.find(cacheKey);
            existing != m_deviceCache.end()) {
            event.device = existing->second;
        } else {
            event.device = describeDevice(deviceHandle);
            if (event.device.deviceId.empty()) {
                return;
            }
        }
        event.device.online = false;
        m_deviceCache.erase(cacheKey);
        ++m_statistics.deviceRemovals;
    } else {
        return;
    }

    if (m_deviceChangeCallback) {
        try {
            m_deviceChangeCallback(event);
        } catch (...) {
            recordDroppedEvent();
        }
    }
}

#endif

bool InputRouter::initialize(std::uint64_t targetHwndValue) {
    if (m_running) {
        return true;
    }
    m_lastError.reset();
    m_statistics = {};
    m_nextSequence = 1;
    m_deviceCache.clear();

#ifdef _WIN32
    if (targetHwndValue != 0) {
        m_hwnd = reinterpret_cast<HWND>(targetHwndValue);
        if (!IsWindow(m_hwnd)) {
            recordError(L"IsWindow", ERROR_INVALID_WINDOW_HANDLE);
            m_hwnd = nullptr;
            return false;
        }
        m_ownsWindow = false;
    } else {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = InputRouter::WndProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kRouterWindowClass;

        if (RegisterClassExW(&windowClass) == 0) {
            const DWORD error = GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS) {
                recordError(L"RegisterClassExW", error);
                return false;
            }
        }

        m_hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kRouterWindowClass,
            L"HydraSeat Raw Input Host", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr,
            GetModuleHandleW(nullptr), this);
        if (m_hwnd == nullptr) {
            recordError(L"CreateWindowExW", GetLastError());
            return false;
        }
        m_ownsWindow = true;
    }
#else
    (void)targetHwndValue;
#endif

    if (!registerRawInputDevices(true)) {
        const auto failure = m_lastError;
        stop();
        m_lastError = failure;
        return false;
    }

    m_running = true;
    if (!refreshConnectedDevices(true)) {
        const auto failure = m_lastError;
        stop();
        m_lastError = failure;
        return false;
    }
    return true;
}

bool InputRouter::registerRawInputDevices(bool backgroundSink) {
#ifdef _WIN32
    if (m_hwnd == nullptr) {
        recordError(L"RegisterRawInputDevices", ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }

    const DWORD flags =
        (backgroundSink ? RIDEV_INPUTSINK : 0) | RIDEV_DEVNOTIFY;

    RAWINPUTDEVICE devices[3]{};
    devices[0] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_KEYBOARD,
                  flags, m_hwnd};
    devices[1] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE,
                  flags, m_hwnd};
    devices[2] = {HID_USAGE_PAGE_DIGITIZER,
                  HID_USAGE_DIGITIZER_TOUCH_PAD, flags, m_hwnd};

    if (!RegisterRawInputDevices(devices, 3,
                                 sizeof(RAWINPUTDEVICE))) {
        recordError(L"RegisterRawInputDevices", GetLastError());
        return false;
    }
#else
    (void)backgroundSink;
#endif
    return true;
}

bool InputRouter::refreshConnectedDevices(bool notifyExisting) {
#ifdef _WIN32
    const auto enumerated = win32::enumerateRawInputDevices();
    if (!enumerated) {
        recordError(L"GetRawInputDeviceList", enumerated.error);
        return false;
    }

    const auto previous = m_deviceCache;
    std::unordered_map<std::uintptr_t, RawInputDeviceDescriptor> refreshed;
    refreshed.reserve(enumerated.devices.size());

    for (const auto& rawDevice : enumerated.devices) {
        auto descriptor = describeDevice(rawDevice.hDevice,
                                         rawDevice.dwType);
        if (descriptor.deviceId.empty()) {
            continue;
        }
        const auto key = descriptor.deviceHandle;
        refreshed.emplace(key, descriptor);

        if (notifyExisting || !previous.contains(key)) {
            RawInputDeviceChange event;
            event.sequence = m_nextSequence++;
            event.monotonicTimestampMicros = monotonicTimestampMicros();
            event.kind = RawInputDeviceChangeKind::Arrival;
            event.device = descriptor;
            ++m_statistics.deviceArrivals;
            if (m_deviceChangeCallback) {
                try {
                    m_deviceChangeCallback(event);
                } catch (...) {
                    recordDroppedEvent();
                }
            }
        }
    }

    for (const auto& [handle, oldDevice] : previous) {
        if (refreshed.contains(handle)) {
            continue;
        }
        RawInputDeviceChange event;
        event.sequence = m_nextSequence++;
        event.monotonicTimestampMicros = monotonicTimestampMicros();
        event.kind = RawInputDeviceChangeKind::Removal;
        event.device = oldDevice;
        event.device.online = false;
        ++m_statistics.deviceRemovals;
        if (m_deviceChangeCallback) {
            try {
                m_deviceChangeCallback(event);
            } catch (...) {
                recordDroppedEvent();
            }
        }
    }

    m_deviceCache = std::move(refreshed);
#else
    (void)notifyExisting;
    m_deviceCache.clear();
#endif
    return true;
}

std::vector<RawInputDeviceDescriptor> InputRouter::connectedDevices() const {
    std::vector<RawInputDeviceDescriptor> result;
    result.reserve(m_deviceCache.size());
    for (const auto& [handle, device] : m_deviceCache) {
        (void)handle;
        result.push_back(device);
    }
    std::sort(result.begin(), result.end(),
              [](const RawInputDeviceDescriptor& left,
                 const RawInputDeviceDescriptor& right) {
                  if (left.deviceId != right.deviceId) {
                      return left.deviceId < right.deviceId;
                  }
                  return left.deviceHandle < right.deviceHandle;
              });
    return result;
}

void InputRouter::subscribeDevice(std::uintptr_t deviceHandle,
                                  InputCallback callback) {
    m_deviceCallbacks[deviceHandle] = std::move(callback);
}

void InputRouter::unsubscribeDevice(std::uintptr_t deviceHandle) {
    m_deviceCallbacks.erase(deviceHandle);
}

void InputRouter::setGlobalCallback(InputCallback callback) {
    m_globalCallback = std::move(callback);
}

void InputRouter::setDeviceChangeCallback(DeviceChangeCallback callback) {
    m_deviceChangeCallback = std::move(callback);
}

void InputRouter::processMessages() {
#ifdef _WIN32
    MSG message{};
    while (m_hwnd != nullptr &&
           PeekMessageW(&message, m_hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
#endif
}

void InputRouter::stop() {
#ifdef _WIN32
    if (m_hwnd != nullptr) {
        RAWINPUTDEVICE devices[3]{};
        devices[0] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_KEYBOARD,
                      RIDEV_REMOVE, nullptr};
        devices[1] = {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE,
                      RIDEV_REMOVE, nullptr};
        devices[2] = {HID_USAGE_PAGE_DIGITIZER,
                      HID_USAGE_DIGITIZER_TOUCH_PAD,
                      RIDEV_REMOVE, nullptr};
        if (!RegisterRawInputDevices(devices, 3,
                                     sizeof(RAWINPUTDEVICE))) {
            recordError(L"RegisterRawInputDevices(RIDEV_REMOVE)",
                        GetLastError());
        }

        if (m_ownsWindow) {
            DestroyWindow(m_hwnd);
        }
        m_hwnd = nullptr;
        m_ownsWindow = false;
    }
#endif
    m_running = false;
    m_deviceCache.clear();
}

} // namespace hydra
