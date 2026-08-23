#include "hydra/input_router.hpp"
#include "hydra/hardware_identity.hpp"
#include "hydra/hid_usage.hpp"
#include "hydra/raw_input_utils.hpp"

#include <iostream>

#ifdef _WIN32
#include <hidusage.h>
#endif

namespace hydra {

static InputRouter* g_routerInstance = nullptr;

InputRouter::InputRouter() {
    g_routerInstance = this;
}

InputRouter::~InputRouter() {
    stop();
    if (g_routerInstance == this) {
        g_routerInstance = nullptr;
    }
}

bool InputRouter::postInputToWindow(uint64_t hwndVal, const RawInputEvent& evt) {
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(hwndVal);
    if (!hwnd || !IsWindow(hwnd)) return false;

    if (evt.vkey > 0) {
        UINT msg = (evt.messageType == WM_KEYDOWN || evt.messageType == WM_SYSKEYDOWN) ? WM_KEYDOWN : WM_KEYUP;
        WPARAM wParam = static_cast<WPARAM>(evt.vkey);
        LPARAM lParam = (msg == WM_KEYDOWN) ? 0x00010001 : 0xC0010001;
        PostMessageW(hwnd, msg, wParam, lParam);
        return true;
    } else if (evt.deltaX != 0 || evt.deltaY != 0) {
        WPARAM wParam = 0;
        LPARAM lParam = MAKELPARAM(evt.deltaX, evt.deltaY);
        PostMessageW(hwnd, WM_MOUSEMOVE, wParam, lParam);
        return true;
    }
#else
    (void)hwndVal;
    (void)evt;
#endif

    return false;
}

#ifdef _WIN32
LRESULT CALLBACK InputRouter::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INPUT && g_routerInstance) {
        g_routerInstance->handleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void InputRouter::handleRawInput(HRAWINPUT hRawInput) {
    UINT dwSize = 0;
    if (GetRawInputData(hRawInput, RID_INPUT, NULL, &dwSize,
                        sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
        dwSize < sizeof(RAWINPUTHEADER)) {
        return;
    }

    std::vector<BYTE> lpb(dwSize);
    if (GetRawInputData(hRawInput, RID_INPUT, lpb.data(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
        return;
    }

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(lpb.data());
    RawInputEvent event{};
    event.deviceHandle = reinterpret_cast<uintptr_t>(raw->header.hDevice);
    event.rawDevType = raw->header.dwType;

    if (raw->header.hDevice != NULL) {
        if (const auto deviceName = win32::rawInputDeviceName(raw->header.hDevice)) {
            event.devicePath = *deviceName;
        }
    }

    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        event.messageType = raw->data.keyboard.Message;
        event.vkey = raw->data.keyboard.VKey;
        if (event.vkey == 0) {
            event.vkey = raw->data.keyboard.MakeCode;
        }
    } else if (raw->header.dwType == RIM_TYPEMOUSE) {
        event.messageType = WM_MOUSEMOVE;
        event.deltaX = raw->data.mouse.lLastX;
        event.deltaY = raw->data.mouse.lLastY;
        event.mouseButtons = raw->data.mouse.usButtonFlags;

        // Detect if this mouse event is actually from a touchpad
        // Windows Precision Touchpads route motion through RIM_TYPEMOUSE
        if (!event.devicePath.empty()) {
            event.isTouchpad = hardware::isLikelyTouchpadPath(event.devicePath);
        }

        // CRITICAL: Windows Precision Touchpads frequently send RIM_TYPEMOUSE
        // events with hDevice == NULL (handle = 0). Physical USB mice ALWAYS
        // have valid non-zero handles. So if handle is NULL and type is MOUSE,
        // this is almost certainly a touchpad event.
        if (raw->header.hDevice == NULL) {
            event.isTouchpad = true;
        }
    } else if (raw->header.dwType == RIM_TYPEHID) {
        const auto details = win32::rawInputDeviceInfo(raw->header.hDevice);
        if (!details || details->dwType != RIM_TYPEHID ||
            !hid::isMouseLikeCollection(
                details->hid.usUsagePage, details->hid.usUsage)) {
            return;
        }
        event.messageType = WM_MOUSEMOVE;
        event.isTouchpad = hid::classifyCollection(
                               details->hid.usUsagePage, details->hid.usUsage) ==
                           hid::CollectionKind::Touchpad;
    }

    // Trigger device specific callback if registered
    auto it = m_deviceCallbacks.find(event.deviceHandle);
    if (it != m_deviceCallbacks.end() && it->second) {
        it->second(event);
    }

    // Trigger global callback
    if (m_globalCallback) {
        m_globalCallback(event);
    }
}
#endif

bool InputRouter::initialize(uint64_t targetHwndVal) {
#ifdef _WIN32
    if (targetHwndVal != 0) {
        m_hwnd = reinterpret_cast<HWND>(targetHwndVal);
    } else {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = InputRouter::WndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"HydraSeatRawInputHost";

        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW, L"HydraSeatRawInputHost", L"HydraSeat Input Router",
            WS_POPUP, 0, 0, 0, 0,
            NULL, NULL, GetModuleHandle(NULL), NULL
        );

        if (!m_hwnd) {
            return false;
        }
    }
#else
    (void)targetHwndVal;
#endif

    m_running = true;
    return registerRawInputDevices(true);
}

bool InputRouter::registerRawInputDevices(bool backgroundSink) {
#ifdef _WIN32
    if (!m_hwnd) return false;

    DWORD flags = backgroundSink ? (RIDEV_INPUTSINK | RIDEV_DEVNOTIFY) : RIDEV_DEVNOTIFY;

    RAWINPUTDEVICE rid[3];

    // Keyboard
    rid[0].usUsagePage = 0x01; // Generic Desktop
    rid[0].usUsage = 0x06;     // Keyboard
    rid[0].dwFlags = flags;
    rid[0].hwndTarget = m_hwnd;

    // Mouse
    rid[1].usUsagePage = 0x01; // Generic Desktop
    rid[1].usUsage = 0x02;     // Mouse
    rid[1].dwFlags = flags;
    rid[1].hwndTarget = m_hwnd;

    // Touchpad / Precision Touchpad
    rid[2].usUsagePage = HID_USAGE_PAGE_DIGITIZER;
    rid[2].usUsage = HID_USAGE_DIGITIZER_TOUCH_PAD;
    rid[2].dwFlags = flags;
    rid[2].hwndTarget = m_hwnd;

    if (!RegisterRawInputDevices(rid, 3, sizeof(RAWINPUTDEVICE))) {
        return false;
    }
#else
    (void)backgroundSink;
#endif

    return true;
}

void InputRouter::subscribeDevice(uintptr_t deviceHandle, InputCallback callback) {
    m_deviceCallbacks[deviceHandle] = callback;
}

void InputRouter::setGlobalCallback(InputCallback callback) {
    m_globalCallback = callback;
}

void InputRouter::processMessages() {
#ifdef _WIN32
    MSG msg;
    while (PeekMessageW(&msg, m_hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
#endif
}

void InputRouter::stop() {
    m_running = false;
#ifdef _WIN32
    if (m_hwnd) {
        RAWINPUTDEVICE rid[3];
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x06; rid[0].dwFlags = RIDEV_REMOVE; rid[0].hwndTarget = NULL;
        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x02; rid[1].dwFlags = RIDEV_REMOVE; rid[1].hwndTarget = NULL;
        rid[2].usUsagePage = HID_USAGE_PAGE_DIGITIZER; rid[2].usUsage = HID_USAGE_DIGITIZER_TOUCH_PAD; rid[2].dwFlags = RIDEV_REMOVE; rid[2].hwndTarget = NULL;
        RegisterRawInputDevices(rid, 3, sizeof(RAWINPUTDEVICE));

        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
#endif
}

} // namespace hydra
