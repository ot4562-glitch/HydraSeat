#include "hydra/gui_win32.hpp"
#include "hydra/hid_usage.hpp"
#include "hydra/raw_input_utils.hpp"

#ifdef _WIN32
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <iterator>

#pragma comment(lib, "comctl32.lib")

namespace hydra {

// Diagnostic log file for debugging handle mapping
static std::wofstream g_diagLog;

static void openDiagLog() {
    if (!g_diagLog.is_open()) {
        g_diagLog.open("hydraseat_debug.log", std::ios::out | std::ios::trunc);
        if (g_diagLog.is_open()) {
            g_diagLog << L"=== HydraSeat Diagnostic Log ===" << std::endl;
        }
    }
}

static std::wstring_view stableIdCategory(DWORD rawDevType) {
    if (rawDevType == RIM_TYPEKEYBOARD) {
        return L"Keyboard";
    }
    if (rawDevType == RIM_TYPEMOUSE || rawDevType == RIM_TYPEHID) {
        return L"Mouse";
    }
    return {};
}

static std::string utf8FromWide(std::wstring_view value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                              value.data(), static_cast<int>(value.size()),
                                              nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                            value.data(), static_cast<int>(value.size()),
                            result.data(), required, nullptr, nullptr) != required) {
        return {};
    }
    return result;
}

static const wchar_t* runtimeModeText(control::RuntimeDisplayMode mode) noexcept {
    switch (mode) {
    case control::RuntimeDisplayMode::Unknown: return L"Unknown";
    case control::RuntimeDisplayMode::BackgroundIdle: return L"Idle / normal Windows";
    case control::RuntimeDisplayMode::SplitActive: return L"Split session Active";
    case control::RuntimeDisplayMode::Degraded: return L"Split session Degraded";
    case control::RuntimeDisplayMode::Transitioning: return L"Transitioning";
    case control::RuntimeDisplayMode::RecoveryRequired: return L"Recovery required";
    case control::RuntimeDisplayMode::HostExitRequested: return L"Background host exiting";
    }
    return L"Unknown";
}

static std::wstring seatGamePhaseText(runtime::SeatGamePhase phase) {
    const auto text = runtime::seatGamePhaseName(phase);
    return std::wstring(text.begin(), text.end());
}

// Check if a raw input device type is compatible with a tile's device category
static bool isTypeCompatible(DWORD rawDevType, gui::DeviceCategory tileCat) {
    if (rawDevType == RIM_TYPEKEYBOARD) {
        return tileCat == gui::DeviceCategory::Keyboard;
    }
    if (rawDevType == RIM_TYPEMOUSE) {
        return tileCat == gui::DeviceCategory::Mouse || tileCat == gui::DeviceCategory::Touchpad;
    }
    if (rawDevType == RIM_TYPEHID) {
        // HID collections can be touchpads, mice, or other
        return tileCat == gui::DeviceCategory::Touchpad || tileCat == gui::DeviceCategory::Mouse;
    }
    return false;
}

namespace gui {

static Win32App* g_appInstance = nullptr;

#define ID_BTN_REFRESH   1001
#define ID_BTN_LAUNCH    1003
#define ID_BTN_SAVE_PROF  1005
#define ID_BTN_LOAD_PROF  1006
#define ID_BTN_ISOLATION  1007
#define ID_BTN_GATE_C     1008
#define ID_BTN_START_SESSION 1009
#define ID_BTN_STOP_SESSION 1010
#define ID_BTN_RECONFIGURE 1011

#define TIMER_FLASH_RESET 2001
#define TIMER_HOST_REFRESH 2002
constexpr UINT WM_HYDRA_ACTIVATE_CONTROL = WM_APP + 17;

Win32App::Win32App() {
    g_appInstance = this;
}

Win32App::~Win32App() {
    m_hostClient.close();
    if (m_singleInstanceMutex != nullptr) {
        CloseHandle(m_singleInstanceMutex);
        m_singleInstanceMutex = nullptr;
    }
    if (g_appInstance == this) {
        g_appInstance = nullptr;
    }
}

LRESULT CALLBACK Win32App::DeviceTileProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    VisualDeviceTile* tile = reinterpret_cast<VisualDeviceTile*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (uMsg == WM_LBUTTONDOWN) {
        if (tile) {
            tile->isDragging = true;
            SetCapture(hwnd);
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
        }
        return 0;
    }

    if (uMsg == WM_MOUSEMOVE) {
        if (tile && tile->isDragging && g_appInstance) {
            POINT pt;
            GetCursorPos(&pt);
            HWND parentHwnd = GetParent(hwnd);
            ScreenToClient(parentHwnd, &pt);

            int width = (tile->type == DeviceCategory::Display) ? 145 : ((tile->type == DeviceCategory::Keyboard) ? 105 : 45);
            int height = (tile->type == DeviceCategory::Display) ? 80 : 42;

            SetWindowPos(hwnd, HWND_TOP, pt.x - width / 2, pt.y - height / 2, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        }
        return 0;
    }

    if (uMsg == WM_LBUTTONUP) {
        if (tile && tile->isDragging) {
            tile->isDragging = false;
            ReleaseCapture();
            SetCursor(LoadCursor(NULL, IDC_ARROW));

            POINT pt;
            GetCursorPos(&pt);
            if (g_appInstance) {
                g_appInstance->dropTileAtScreenPos(tile, pt);
            }
        }
        return 0;
    }

    if (uMsg == WM_RBUTTONUP) {
        if (tile && g_appInstance && tile->type == DeviceCategory::Display &&
            tile->owner != PartitionOwner::Pool) {
            for (auto& candidate : g_appInstance->m_deviceTiles) {
                if (candidate && candidate->type == DeviceCategory::Display &&
                    candidate->owner == tile->owner) {
                    candidate->isPrimaryDisplay = (candidate.get() == tile);
                    InvalidateRect(candidate->hwndControl, nullptr, FALSE);
                }
            }
        }
        return 0;
    }

    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rect;
        GetClientRect(hwnd, &rect);

        bool isFlashing = false;
        if (tile && tile->flashUntil > GetTickCount64()) {
            isFlashing = true;
        }

        COLORREF bgCol = RGB(24, 24, 27); // Dark Slate background
        COLORREF borderCol = RGB(59, 130, 246); // Default Blue Border

        if (tile && tile->owner == PartitionOwner::Pool) {
            borderCol = RGB(100, 116, 139); // Slate Gray for Pool
        }

        // AMBER YELLOW BORDER HIGHLIGHT ON RAW INPUT
        if (isFlashing) {
            borderCol = RGB(245, 158, 11); // ASTER Bright Amber Yellow (#F59E0B)
        }

        int penWidth = isFlashing ? 3 : 2;

        HBRUSH bgBrush = CreateSolidBrush(bgCol);
        HPEN borderPen = CreatePen(PS_SOLID, penWidth, borderCol);

        HGDIOBJ oldBrush = SelectObject(hdc, bgBrush);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);

        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);

        if (tile) {
            if (tile->type == DeviceCategory::Display) {
                RECT screenRect = { rect.left + 8, rect.top + 6, rect.right - 8, rect.bottom - 18 };
                HBRUSH screenBrush = CreateSolidBrush(RGB(37, 99, 235)); // ASTER Blue Screen
                HPEN screenPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));

                HGDIOBJ oldSBrush = SelectObject(hdc, screenBrush);
                HGDIOBJ oldSPen = SelectObject(hdc, screenPen);

                Rectangle(hdc, screenRect.left, screenRect.top, screenRect.right, screenRect.bottom);

                // Stand Base
                MoveToEx(hdc, rect.left + (rect.right - rect.left) / 2, screenRect.bottom, NULL);
                LineTo(hdc, rect.left + (rect.right - rect.left) / 2, rect.bottom - 8);
                MoveToEx(hdc, rect.left + (rect.right - rect.left) / 2 - 15, rect.bottom - 8, NULL);
                LineTo(hdc, rect.left + (rect.right - rect.left) / 2 + 15, rect.bottom - 8);

                SelectObject(hdc, oldSBrush);
                SelectObject(hdc, oldSPen);
                DeleteObject(screenBrush);
                DeleteObject(screenPen);

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));
                HFONT hFontS = CreateFontW(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                HGDIOBJ oldFontS = SelectObject(hdc, hFontS);

                RECT textRect = screenRect;
                textRect.right -= 6;
                textRect.bottom -= 4;
                const std::wstring monitorLabel = tile->isPrimaryDisplay
                                                      ? tile->displayLabel + L" PRIMARY"
                                                      : tile->displayLabel;
                DrawTextW(hdc, monitorLabel.c_str(), -1, &textRect, DT_SINGLELINE | DT_RIGHT | DT_BOTTOM);

                SelectObject(hdc, oldFontS);
                DeleteObject(hFontS);

            } else if (tile->type == DeviceCategory::Keyboard) {
                RECT kbdRect = { rect.left + 5, rect.top + 5, rect.right - 5, rect.bottom - 5 };
                HBRUSH kbdBrush = CreateSolidBrush(RGB(30, 41, 59));
                HPEN kbdPen = CreatePen(PS_SOLID, 1, RGB(203, 213, 225));

                HGDIOBJ oldKBrush = SelectObject(hdc, kbdBrush);
                HGDIOBJ oldKPen = SelectObject(hdc, kbdPen);

                RoundRect(hdc, kbdRect.left, kbdRect.top, kbdRect.right, kbdRect.bottom, 4, 4);

                HPEN keyLinePen = CreatePen(PS_SOLID, 1, RGB(148, 163, 184));
                SelectObject(hdc, keyLinePen);

                for (int y = kbdRect.top + 7; y < kbdRect.bottom - 4; y += 7) {
                    MoveToEx(hdc, kbdRect.left + 6, y, NULL);
                    LineTo(hdc, kbdRect.right - 6, y);
                }

                SelectObject(hdc, oldKBrush);
                SelectObject(hdc, oldKPen);
                DeleteObject(kbdBrush);
                DeleteObject(kbdPen);
                DeleteObject(keyLinePen);

            } else if (tile->type == DeviceCategory::Mouse) {
                HBRUSH mouseBrush = CreateSolidBrush(RGB(255, 255, 255));
                HPEN mousePen = CreatePen(PS_SOLID, 1, RGB(148, 163, 184));

                HGDIOBJ oldMBrush = SelectObject(hdc, mouseBrush);
                HGDIOBJ oldMPen = SelectObject(hdc, mousePen);

                RoundRect(hdc, rect.left + 10, rect.top + 8, rect.right - 10, rect.bottom - 6, 12, 12);

                MoveToEx(hdc, rect.left + (rect.right - rect.left) / 2, rect.top + 8, NULL);
                LineTo(hdc, rect.left + (rect.right - rect.left) / 2, rect.top + 20);

                MoveToEx(hdc, rect.left + (rect.right - rect.left) / 2, rect.top + 8, NULL);
                LineTo(hdc, rect.left + (rect.right - rect.left) / 2 - 3, rect.top + 3);

                SelectObject(hdc, oldMBrush);
                SelectObject(hdc, oldMPen);
                DeleteObject(mouseBrush);
                DeleteObject(mousePen);

            } else if (tile->type == DeviceCategory::Touchpad) {
                HBRUSH padBrush = CreateSolidBrush(RGB(30, 41, 59));
                HPEN padPen = CreatePen(PS_SOLID, 1, RGB(203, 213, 225));

                HGDIOBJ oldPBrush = SelectObject(hdc, padBrush);
                HGDIOBJ oldPPen = SelectObject(hdc, padPen);

                // Touchpad Surface Box
                RoundRect(hdc, rect.left + 8, rect.top + 8, rect.right - 8, rect.bottom - 8, 4, 4);

                // Touch Gesture Center Ring
                Ellipse(hdc, rect.left + 18, rect.top + 14, rect.right - 18, rect.bottom - 14);

                SelectObject(hdc, oldPBrush);
                SelectObject(hdc, oldPPen);
                DeleteObject(padBrush);
                DeleteObject(padPen);
            }
        }

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(bgBrush);
        DeleteObject(borderPen);

        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void Win32App::dropTileAtScreenPos(VisualDeviceTile* tile, POINT screenPt) {
    if (!configurationEditingAllowed()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (!tile || !m_hwnd) return;

    POINT clientPt = screenPt;
    ScreenToClient(m_hwnd, &clientPt);

    const PartitionOwner oldOwner = tile->owner;
    if (clientPt.x < 315) {
        tile->owner = PartitionOwner::Pool;
    } else if (clientPt.x >= 315 && clientPt.x < 635) {
        tile->owner = PartitionOwner::Player1;
    } else {
        tile->owner = PartitionOwner::Player2;
    }

    if (tile->type == DeviceCategory::Display) {
        if (tile->owner == PartitionOwner::Pool) {
            tile->isPrimaryDisplay = false;
        } else if (oldOwner != tile->owner) {
            bool ownerAlreadyHasPrimary = false;
            for (const auto& candidate : m_deviceTiles) {
                if (candidate && candidate.get() != tile &&
                    candidate->type == DeviceCategory::Display &&
                    candidate->owner == tile->owner && candidate->isPrimaryDisplay) {
                    ownerAlreadyHasPrimary = true;
                    break;
                }
            }
            tile->isPrimaryDisplay = !ownerAlreadyHasPrimary;
        }
    }

    layoutDeviceTiles();
}

bool Win32App::initialize(HINSTANCE hInstance, int nCmdShow) {
    m_singleInstanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\HydraSeat.ControlConsole.v2");
    if (m_singleInstanceMutex == nullptr) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = nullptr;
        for (int attempt = 0; attempt < 50 && existing == nullptr; ++attempt) {
            existing = FindWindowW(L"HydraSeatMainWindowClass", nullptr);
            if (existing == nullptr) Sleep(20);
        }
        if (existing != nullptr) {
            PostMessageW(existing, WM_HYDRA_ACTIVATE_CONTROL, 0, 0);
            m_duplicateLaunch = true;
            return true;
        }
    }
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wcTile = { sizeof(WNDCLASSEXW) };
    wcTile.lpfnWndProc = Win32App::DeviceTileProc;
    wcTile.hInstance = hInstance;
    wcTile.hCursor = LoadCursor(NULL, IDC_HAND);
    wcTile.hbrBackground = NULL;
    wcTile.lpszClassName = L"HydraSeatDeviceTileClass";
    RegisterClassExW(&wcTile);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = Win32App::WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(15, 23, 42)); // Dark Slate Background
    wc.lpszClassName = L"HydraSeatMainWindowClass";

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    m_hwnd = CreateWindowExW(
        0, L"HydraSeatMainWindowClass",
        L"HydraSeat - Seat Composition Control Center",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 750,
        NULL, NULL, hInstance, NULL
    );

    if (!m_hwnd) return false;

    setupUI();
    // Hook diagnostics before device-change notifications begin. The main UI
    // remains a configuration surface; it does not claim zero-bleed isolation.
    m_inputRouter.setGlobalCallback([this](const RawInputEvent& evt) {
        triggerDeviceFlash(evt.deviceHandle, evt.devicePath, evt.rawDevType, evt.isTouchpad);
    });
    if (!m_inputRouter.initialize(reinterpret_cast<uint64_t>(m_hwnd))) {
        return false;
    }
    m_inputRouter.setDeviceChangeCallback([this](const RawInputDeviceChange& change) {
        if (g_diagLog.is_open()) {
            g_diagLog << L"[DEVICE-CHANGE] "
                      << (change.kind == RawInputDeviceChangeKind::Arrival ? L"arrival" : L"removal")
                      << L" id=" << change.device.deviceId
                      << L" path=" << change.device.devicePath << std::endl;
        }
        refreshHardware();
    });
    refreshHardware();

    (void)initializeControlSurface();

    SetTimer(m_hwnd, TIMER_FLASH_RESET, 50, NULL);
    SetTimer(m_hwnd, TIMER_HOST_REFRESH, 1000, NULL);

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    return true;
}

void Win32App::triggerDeviceFlash(uintptr_t handle, const std::wstring& devPath, uint32_t rawDevType, bool isTouchpad) {
    uint64_t now = GetTickCount64();

    // Log every raw input event for debugging
    if (g_diagLog.is_open()) {
        static uint64_t lastLogTime = 0;
        if (now - lastLogTime > 100) { // Throttle logging to max 10/sec
            const wchar_t* typeStr = (rawDevType == RIM_TYPEKEYBOARD) ? L"KBD" :
                                     (rawDevType == RIM_TYPEMOUSE) ? L"MOUSE" :
                                     (rawDevType == RIM_TYPEHID) ? L"HID" : L"UNK";
            g_diagLog << L"[INPUT] handle=0x" << std::hex << handle << std::dec
                      << L" type=" << typeStr
                      << L" isTouchpad=" << isTouchpad
                      << L" mapped=" << (m_handleToTileIndex.count(handle) > 0 ? L"YES" : L"NO");
            if (m_handleToTileIndex.count(handle) > 0) {
                size_t tIdx = m_handleToTileIndex[handle];
                if (tIdx < m_deviceTiles.size() && m_deviceTiles[tIdx]) {
                    g_diagLog << L" -> tile[" << tIdx << L"] " << m_deviceTiles[tIdx]->displayLabel;
                }
            }
            g_diagLog << std::endl;
            lastLogTime = now;
        }
    }

    // 1. Direct Handle Lookup via m_handleToTileIndex
    if (handle != 0) {
        auto it = m_handleToTileIndex.find(handle);
        if (it != m_handleToTileIndex.end() && it->second < m_deviceTiles.size()) {
            m_deviceTiles[it->second]->flashUntil = now + 250;
            InvalidateRect(m_deviceTiles[it->second]->hwndControl, NULL, FALSE);
            return;
        }
    }

    // 2. Fallback: resolve the interface to the same stable ID used by HardwareDetector.
    if (!devPath.empty()) {
        const auto category = stableIdCategory(rawDevType);
        const auto incomingId = category.empty()
                                    ? std::wstring{}
                                    : win32::makeStableRawInputDeviceId(category, devPath);
        for (size_t tIdx = 0; tIdx < m_deviceTiles.size(); tIdx++) {
            auto& tilePtr = m_deviceTiles[tIdx];
            if (!tilePtr) continue;
            // TYPE CHECK: Only match tiles compatible with the raw device type
            if (!isTypeCompatible(rawDevType, tilePtr->type)) continue;
            if (!incomingId.empty() && incomingId == tilePtr->deviceId) {
                tilePtr->flashUntil = now + 250;
                InvalidateRect(tilePtr->hwndControl, NULL, FALSE);
                // Cache this handle for future O(1) lookups
                if (handle != 0) {
                    m_handleToTileIndex[handle] = tIdx;
                    if (g_diagLog.is_open()) {
                        g_diagLog << L"[CACHE] handle=0x" << std::hex << handle << std::dec
                                  << L" -> tile[" << tIdx << L"] " << tilePtr->displayLabel << std::endl;
                    }
                }
                return;
            }
        }
    }

    // Do not guess by device type: that can associate input with the wrong
    // physical keyboard or mouse on a multiseat system.
    if (g_diagLog.is_open()) {
        const wchar_t* typeStr = (rawDevType == RIM_TYPEKEYBOARD) ? L"KBD" :
                                 (rawDevType == RIM_TYPEMOUSE) ? L"MOUSE" :
                                 (rawDevType == RIM_TYPEHID) ? L"HID" : L"UNK";
        g_diagLog << L"[FALLBACK] handle=0x" << std::hex << handle << std::dec
                  << L" type=" << typeStr << L" isTouchpad=" << isTouchpad << std::endl;
    }
}

void Win32App::setupUI() {
    // Header Label
    HWND header = CreateWindowExW(0, L"STATIC", L"HydraSeat Multiseat Control Center",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 15, 420, 30, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

    HFONT hFontHeader = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(header, WM_SETFONT, (WPARAM)hFontHeader, TRUE);

    // Save Profile Button
    m_saveProfileBtn = CreateWindowExW(0, L"BUTTON", L"Save Profile",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        450, 15, 110, 32, m_hwnd, (HMENU)ID_BTN_SAVE_PROF, GetModuleHandle(NULL), NULL);

    // Load Profile Button
    m_loadProfileBtn = CreateWindowExW(0, L"BUTTON", L"Load Profile",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        570, 15, 110, 32, m_hwnd, (HMENU)ID_BTN_LOAD_PROF, GetModuleHandle(NULL), NULL);

    // Refresh Button
    m_refreshBtn = CreateWindowExW(0, L"BUTTON", L"Refresh",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        690, 15, 90, 32, m_hwnd, (HMENU)ID_BTN_REFRESH, GetModuleHandle(NULL), NULL);

    // Status Label
    m_deviceStatusLabel = CreateWindowExW(0, L"STATIC", L"Detecting connected hardware...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 82, 920, 22, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

    HFONT hFontNormal = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(m_deviceStatusLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(m_saveProfileBtn, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(m_loadProfileBtn, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(m_refreshBtn, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    m_runtimeStatusLabel = CreateWindowExW(
        0, L"STATIC", L"Runtime: host state unknown",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 52, 410, 24, m_hwnd, NULL, GetModuleHandle(NULL), NULL);
    m_startSessionBtn = CreateWindowExW(
        0, L"BUTTON", L"Start",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        450, 50, 100, 28, m_hwnd, (HMENU)ID_BTN_START_SESSION, GetModuleHandle(NULL), NULL);
    m_stopSessionBtn = CreateWindowExW(
        0, L"BUTTON", L"Stop / Return to Windows",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        560, 50, 190, 28, m_hwnd, (HMENU)ID_BTN_STOP_SESSION, GetModuleHandle(NULL), NULL);
    m_reconfigureBtn = CreateWindowExW(
        0, L"BUTTON", L"Reconfigure",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        760, 50, 120, 28, m_hwnd, (HMENU)ID_BTN_RECONFIGURE, GetModuleHandle(NULL), NULL);
    SendMessageW(m_runtimeStatusLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(m_startSessionBtn, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(m_stopSessionBtn, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(m_reconfigureBtn, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    // 3 PARTITIONS (ASTER DRAG-AND-DROP LAYOUT):
    m_poolGroup = CreateWindowExW(0, L"BUTTON", L"System Hardware Pool (Drag tile to assign)",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        20, 110, 290, 510, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

    m_p1Group = CreateWindowExW(0, L"BUTTON", L"Player 1 Seat",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        330, 110, 290, 510, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

    m_p2Group = CreateWindowExW(0, L"BUTTON", L"Player 2 Seat",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        640, 110, 290, 510, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

    HFONT hFontBold = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(m_poolGroup, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    SendMessageW(m_p1Group, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    SendMessageW(m_p2Group, WM_SETFONT, (WPARAM)hFontBold, TRUE);

    // Isolation Toggle Button
    m_isolationBtn = CreateWindowExW(0, L"BUTTON", L"Diagnostic Intent: OFF",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 642, 285, 42, m_hwnd, (HMENU)ID_BTN_ISOLATION, GetModuleHandle(NULL), NULL);

    // Phase 3 development harnesses.
    m_launchBtn = CreateWindowExW(0, L"BUTTON", L"Gate A/B Input Lab",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        322, 642, 285, 42, m_hwnd, (HMENU)ID_BTN_LAUNCH, GetModuleHandle(NULL), NULL);
    m_gateCBtn = CreateWindowExW(0, L"BUTTON", L"Gate C Process Lab",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        624, 642, 306, 42, m_hwnd, (HMENU)ID_BTN_GATE_C, GetModuleHandle(NULL), NULL);

    HFONT hFontBtn = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(m_isolationBtn, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
    SendMessageW(m_launchBtn, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
    SendMessageW(m_gateCBtn, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
}

void Win32App::layoutDeviceTiles() {
    auto layoutPartition = [this](PartitionOwner owner, int startX) {
        int dispX = startX + 15;
        int dispY = 140;

        int inputX = startX + 15;
        int inputY = 235;

        for (auto& tilePtr : m_deviceTiles) {
            if (!tilePtr || !tilePtr->hwndControl || tilePtr->owner != owner) continue;

            if (tilePtr->type == DeviceCategory::Display) {
                SetWindowPos(tilePtr->hwndControl, NULL, dispX, dispY, 145, 80, SWP_NOZORDER | SWP_SHOWWINDOW);
                dispX += 155;
                if (dispX > startX + 270) {
                    dispX = startX + 15;
                    dispY += 90;
                }
            } else if (tilePtr->type == DeviceCategory::Keyboard) {
                SetWindowPos(tilePtr->hwndControl, NULL, inputX, inputY, 105, 42, SWP_NOZORDER | SWP_SHOWWINDOW);
                inputX += 115;
                if (inputX > startX + 270) {
                    inputX = startX + 15;
                    inputY += 50;
                }
            } else {
                SetWindowPos(tilePtr->hwndControl, NULL, inputX, inputY, 45, 42, SWP_NOZORDER | SWP_SHOWWINDOW);
                inputX += 55;
                if (inputX > startX + 270) {
                    inputX = startX + 15;
                    inputY += 50;
                }
            }

            InvalidateRect(tilePtr->hwndControl, NULL, TRUE);
        }
    };

    layoutPartition(PartitionOwner::Pool, 20);
    layoutPartition(PartitionOwner::Player1, 330);
    layoutPartition(PartitionOwner::Player2, 640);
}

void Win32App::refreshHardware() {
    m_displays = m_hardwareDetector.detectDisplays();
    m_keyboards = m_hardwareDetector.detectKeyboards();
    m_mice = m_hardwareDetector.detectMice();
    m_controllers = m_hardwareDetector.detectControllers();

    std::wstring statusText = L"Connected Hardware: " + std::to_wstring(m_displays.size()) + L" Displays | " +
                              std::to_wstring(m_keyboards.size()) + L" Keyboards | " +
                              std::to_wstring(m_mice.size()) + L" Mice | " +
                              std::to_wstring(m_controllers.size()) + L" Gamepads";

    // Runtime authority is external to the UI. A failed read means "unknown",
    // never "stopped"; reopening/refreshing the UI starts from a full host snapshot.
    hydra::hostipc::HostControlClient hostClient;
    std::string hostError;
    if (hostClient.connect(hydra::hostipc::ClientRole::ReadOnly, 150u, &hostError)) {
        const auto snapshot = hostClient.getSnapshot(250u, &hostError);
        if (snapshot) {
            const auto hostPhase = hydra::runtime::hostLifecyclePhaseName(snapshot->hostPhase);
            const auto sessionPhase = hydra::runtime::seatSessionPhaseName(snapshot->sessionPhase);
            statusText += L" | Runtime Host: ";
            statusText.append(hostPhase.begin(), hostPhase.end());
            statusText += L" / ";
            statusText.append(sessionPhase.begin(), sessionPhase.end());
        } else {
            statusText += L" | Runtime Host: state unknown";
        }
    } else {
        statusText += L" | Runtime Host: unavailable (state unknown)";
    }

    SetWindowTextW(m_deviceStatusLabel, statusText.c_str());

    for (auto& tilePtr : m_deviceTiles) {
        if (tilePtr && tilePtr->hwndControl) {
            DestroyWindow(tilePtr->hwndControl);
        }
    }
    m_deviceTiles.clear();
    m_handleToTileIndex.clear();

    // Displays (Default to Pool)
    for (size_t i = 0; i < m_displays.size(); ++i) {
        auto tile = std::make_unique<VisualDeviceTile>();
        tile->name = m_displays[i].name;
        tile->displayLabel = L"1." + std::to_wstring(i + 1);
        tile->type = DeviceCategory::Display;
        tile->nativeHandle = m_displays[i].nativeHandle;
        tile->deviceId = m_displays[i].id;
        tile->devicePath = m_displays[i].devicePath;
        tile->owner = PartitionOwner::Pool;

        tile->hwndControl = CreateWindowExW(0, L"HydraSeatDeviceTileClass", L"",
            WS_CHILD | WS_VISIBLE,
            0, 0, 145, 80, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

        SetWindowLongPtrW(tile->hwndControl, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tile.get()));
        m_deviceTiles.push_back(std::move(tile));
    }

    // Keyboards (Default to Pool)
    for (size_t i = 0; i < m_keyboards.size(); ++i) {
        auto tile = std::make_unique<VisualDeviceTile>();
        tile->name = m_keyboards[i].name;
        tile->displayLabel = L"KBD " + std::to_wstring(i + 1);
        tile->type = DeviceCategory::Keyboard;
        tile->nativeHandle = m_keyboards[i].nativeHandle;
        tile->deviceId = m_keyboards[i].id;
        tile->devicePath = m_keyboards[i].devicePath;
        tile->owner = PartitionOwner::Pool;

        tile->hwndControl = CreateWindowExW(0, L"HydraSeatDeviceTileClass", L"",
            WS_CHILD | WS_VISIBLE,
            0, 0, 105, 42, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

        SetWindowLongPtrW(tile->hwndControl, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tile.get()));
        size_t idx = m_deviceTiles.size();
        uintptr_t handle = tile->nativeHandle;
        m_deviceTiles.push_back(std::move(tile));
        if (handle != 0) m_handleToTileIndex[handle] = idx;
    }

    // Mice / Touchpads (Default to Pool)
    for (size_t i = 0; i < m_mice.size(); ++i) {
        auto tile = std::make_unique<VisualDeviceTile>();
        tile->name = m_mice[i].name;
        tile->displayLabel = L"MOU " + std::to_wstring(i + 1);

        std::wstring nameUpper = tile->name;
        for (auto& c : nameUpper) c = ::towupper(c);

        if (nameUpper.find(L"TOUCHPAD") != std::wstring::npos) {
            tile->type = DeviceCategory::Touchpad;
        } else {
            tile->type = DeviceCategory::Mouse;
        }

        tile->nativeHandle = m_mice[i].nativeHandle;
        tile->deviceId = m_mice[i].id;
        tile->devicePath = m_mice[i].devicePath;
        tile->owner = PartitionOwner::Pool;

        tile->hwndControl = CreateWindowExW(0, L"HydraSeatDeviceTileClass", L"",
            WS_CHILD | WS_VISIBLE,
            0, 0, 45, 42, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

        SetWindowLongPtrW(tile->hwndControl, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tile.get()));
        size_t idx = m_deviceTiles.size();
        uintptr_t handle = tile->nativeHandle;
        m_deviceTiles.push_back(std::move(tile));
        if (handle != 0) m_handleToTileIndex[handle] = idx;
    }

    // ================================================================
    // Map every Raw Input handle to the matching physical input tile. Composite
    // collection paths are correlated through SetupAPI/ConfigMgr identities.
    // ================================================================
    openDiagLog();
    if (g_diagLog.is_open()) {
        g_diagLog << L"\n=== TILE REGISTRY ===" << std::endl;
        for (size_t i = 0; i < m_deviceTiles.size(); i++) {
            if (!m_deviceTiles[i]) continue;
            const wchar_t* catStr = (m_deviceTiles[i]->type == DeviceCategory::Display) ? L"DISPLAY" :
                                    (m_deviceTiles[i]->type == DeviceCategory::Keyboard) ? L"KEYBOARD" :
                                    (m_deviceTiles[i]->type == DeviceCategory::Mouse) ? L"MOUSE" :
                                    (m_deviceTiles[i]->type == DeviceCategory::Touchpad) ? L"TOUCHPAD" : L"OTHER";
            std::wstring cleanPath = m_deviceTiles[i]->devicePath;
            cleanPath.erase(std::remove(cleanPath.begin(), cleanPath.end(), L'\0'), cleanPath.end());
            g_diagLog << L"  tile[" << i << L"] " << m_deviceTiles[i]->displayLabel
                      << L" cat=" << catStr
                      << L" handle=0x" << std::hex << m_deviceTiles[i]->nativeHandle << std::dec
                      << L" id=" << m_deviceTiles[i]->deviceId
                      << L" path=" << cleanPath << std::endl;
        }
    }

    const auto allRawDevices = win32::enumerateRawInputDevices();
    if (allRawDevices) {
        if (g_diagLog.is_open()) {
            g_diagLog << L"\n=== ALL RAW INPUT DEVICE HANDLES ("
                      << allRawDevices.devices.size() << L") ===" << std::endl;
        }
        for (const auto& rawDev : allRawDevices.devices) {
            if (rawDev.dwType == RIM_TYPEHID) {
                const auto details = win32::rawInputDeviceInfo(rawDev.hDevice);
                if (!details || details->dwType != RIM_TYPEHID ||
                    !hid::isMouseLikeCollection(
                        details->hid.usUsagePage, details->hid.usUsage)) {
                    continue;
                }
            }
            uintptr_t rawHandle = reinterpret_cast<uintptr_t>(rawDev.hDevice);

            const auto rawPath = win32::rawInputDeviceName(rawDev.hDevice);
            if (!rawPath) {
                continue;
            }

            const wchar_t* typeStr = (rawDev.dwType == RIM_TYPEKEYBOARD) ? L"KBD" :
                                     (rawDev.dwType == RIM_TYPEMOUSE) ? L"MOUSE" :
                                     (rawDev.dwType == RIM_TYPEHID) ? L"HID" : L"UNK";

            // Skip if already mapped
            if (m_handleToTileIndex.count(rawHandle) > 0) {
                if (g_diagLog.is_open()) {
                    size_t tIdx = m_handleToTileIndex[rawHandle];
                    g_diagLog << L"  handle=0x" << std::hex << rawHandle << std::dec
                              << L" type=" << typeStr
                              << L" ALREADY -> tile[" << tIdx << L"]";
                    if (tIdx < m_deviceTiles.size() && m_deviceTiles[tIdx]) {
                        g_diagLog << L" " << m_deviceTiles[tIdx]->displayLabel;
                    }
                    g_diagLog << std::endl;
                }
                continue;
            }

            const auto category = stableIdCategory(rawDev.dwType);
            const auto rawStableId = category.empty()
                                         ? std::wstring{}
                                         : win32::makeStableRawInputDeviceId(category, *rawPath);
            if (rawStableId.empty()) continue;

            // Find the matching tile by resolved identity and compatible type.
            bool mapped = false;
            for (size_t tIdx = 0; tIdx < m_deviceTiles.size(); tIdx++) {
                auto& tile = m_deviceTiles[tIdx];
                if (!tile || tile->devicePath.empty()) continue;

                // TYPE CHECK: Only map to tiles compatible with the raw device type
                if (!isTypeCompatible(rawDev.dwType, tile->type)) continue;

                if (tile->deviceId == rawStableId) {
                    m_handleToTileIndex[rawHandle] = tIdx;
                    mapped = true;
                    if (g_diagLog.is_open()) {
                        std::wstring cleanRawPath = *rawPath;
                        cleanRawPath.erase(std::remove(cleanRawPath.begin(), cleanRawPath.end(), L'\0'), cleanRawPath.end());
                        g_diagLog << L"  handle=0x" << std::hex << rawHandle << std::dec
                                  << L" type=" << typeStr
                                  << L" MAPPED -> tile[" << tIdx << L"] " << tile->displayLabel
                                  << L" path=" << cleanRawPath << std::endl;
                    }
                    break;
                }
            }
            if (!mapped && g_diagLog.is_open()) {
                std::wstring cleanRawPath = *rawPath;
                cleanRawPath.erase(std::remove(cleanRawPath.begin(), cleanRawPath.end(), L'\0'), cleanRawPath.end());
                g_diagLog << L"  handle=0x" << std::hex << rawHandle << std::dec
                          << L" type=" << typeStr
                          << L" UNMAPPED id=" << rawStableId
                          << L" path=" << cleanRawPath << std::endl;
            }
        }
    } else if (g_diagLog.is_open()) {
        g_diagLog << L"Raw Input enumeration failed with Win32 error "
                  << allRawDevices.error << std::endl;
    }

    if (g_diagLog.is_open()) {
        g_diagLog << L"\n=== FINAL HANDLE MAP (" << m_handleToTileIndex.size() << L" entries) ===" << std::endl;
        for (auto& [h, idx] : m_handleToTileIndex) {
            if (idx < m_deviceTiles.size() && m_deviceTiles[idx]) {
                g_diagLog << L"  0x" << std::hex << h << std::dec
                          << L" -> tile[" << idx << L"] " << m_deviceTiles[idx]->displayLabel << std::endl;
            }
        }
        g_diagLog << L"\n=== READY FOR INPUT ===" << std::endl;
        g_diagLog.flush();
    }

    layoutDeviceTiles();
}

void Win32App::saveWorkspaceProfile() {
    if (!configurationEditingAllowed()) {
        MessageBoxW(m_hwnd,
            L"Configuration changes are disabled until the background host is connected and the session is verified Idle. Use Reconfigure first for an active session.",
            L"HydraSeat Runtime", MB_OK | MB_ICONWARNING);
        return;
    }
    const SeatId previousManagementSeat = m_workspaceManager.managementSeatId();
    m_workspaceManager = WorkspaceManager{};
    const SeatId seat1 = m_workspaceManager.createSeat(L"Player 1");
    const SeatId seat2 = m_workspaceManager.createSeat(L"Player 2");
    if (previousManagementSeat == seat2) {
        (void)m_workspaceManager.setManagementSeatId(seat2);
    }

    const auto assignTile = [this](SeatId seatId, const VisualDeviceTile& tile) {
        const std::wstring& stableId = tile.deviceId.empty() ? tile.devicePath : tile.deviceId;
        if (stableId.empty()) return false;
        switch (tile.type) {
        case DeviceCategory::Display:
            return m_workspaceManager.assignDisplay(seatId, stableId, tile.isPrimaryDisplay);
        case DeviceCategory::Keyboard:
            return m_workspaceManager.assignKeyboard(seatId, stableId);
        case DeviceCategory::Mouse:
        case DeviceCategory::Touchpad:
            return m_workspaceManager.assignMouse(seatId, stableId);
        case DeviceCategory::Gamepad:
            return m_workspaceManager.assignController(seatId, stableId);
        }
        return false;
    };

    bool valid = true;
    for (const auto& tilePtr : m_deviceTiles) {
        if (!tilePtr || tilePtr->owner == PartitionOwner::Pool) continue;
        const SeatId seatId = tilePtr->owner == PartitionOwner::Player1 ? seat1 : seat2;
        valid = assignTile(seatId, *tilePtr) && valid;
    }

    for (const SeatId seatId : {seat1, seat2}) {
        const auto* seat = m_workspaceManager.getSeat(seatId);
        if (seat && !seat->displayIds.empty() && !seat->primaryDisplayId) {
            valid = m_workspaceManager.setPrimaryDisplay(seatId, seat->displayIds.front()) && valid;
        }
    }

    if (!valid) {
        MessageBoxW(m_hwnd,
            L"One or more devices could not be assigned. A physical device may only belong to one Seat unless explicitly shareable.",
            L"HydraSeat Seat Manager", MB_OK | MB_ICONERROR);
        return;
    }

    if (m_workspaceManager.saveToFile("workspace_config.json")) {
        MessageBoxW(m_hwnd,
            L"Seat assignments saved to workspace_config.json.\n\nTip: right-click a display tile inside a Seat to mark it PRIMARY.",
            L"HydraSeat Seat Manager", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(m_hwnd, L"Could not save workspace_config.json.",
                    L"HydraSeat Seat Manager", MB_OK | MB_ICONERROR);
    }
}

void Win32App::applyWorkspaceProfileToTiles() {
    for (auto& tile : m_deviceTiles) {
        if (!tile) continue;
        tile->owner = PartitionOwner::Pool;
        tile->isPrimaryDisplay = false;
    }

    const auto containsId = [](const std::vector<std::wstring>& ids, const std::wstring& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };

    for (const auto& seat : m_workspaceManager.getAllSeats()) {
        const PartitionOwner owner = seat.seatId == 1 ? PartitionOwner::Player1
                                      : seat.seatId == 2 ? PartitionOwner::Player2
                                                         : PartitionOwner::Pool;
        if (owner == PartitionOwner::Pool) continue;
        for (auto& tile : m_deviceTiles) {
            if (!tile) continue;
            const std::wstring& stableId = tile->deviceId.empty() ? tile->devicePath : tile->deviceId;
            bool assigned = false;
            switch (tile->type) {
            case DeviceCategory::Display: assigned = containsId(seat.displayIds, stableId); break;
            case DeviceCategory::Keyboard: assigned = containsId(seat.keyboardIds, stableId); break;
            case DeviceCategory::Mouse:
            case DeviceCategory::Touchpad: assigned = containsId(seat.mouseIds, stableId); break;
            case DeviceCategory::Gamepad: assigned = containsId(seat.controllerIds, stableId); break;
            }
            if (assigned) {
                tile->owner = owner;
                tile->isPrimaryDisplay = tile->type == DeviceCategory::Display &&
                    seat.primaryDisplayId && *seat.primaryDisplayId == stableId;
            }
        }
    }
    layoutDeviceTiles();
}

void Win32App::loadWorkspaceProfile() {
    if (!configurationEditingAllowed()) {
        MessageBoxW(m_hwnd,
            L"Loading/editing configuration is disabled until the runtime is verified Idle. Use Reconfigure first for an active session.",
            L"HydraSeat Runtime", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!m_workspaceManager.loadFromFile("workspace_config.json")) {
        MessageBoxW(m_hwnd, L"Could not load workspace_config.json.",
                    L"HydraSeat Seat Manager", MB_OK | MB_ICONWARNING);
        return;
    }

    for (auto& tile : m_deviceTiles) {
        if (!tile) continue;
        tile->owner = PartitionOwner::Pool;
        tile->isPrimaryDisplay = false;
    }

    const auto containsId = [](const std::vector<std::wstring>& ids, const std::wstring& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };

    for (const auto& seat : m_workspaceManager.getAllSeats()) {
        const PartitionOwner owner = seat.seatId == 1 ? PartitionOwner::Player1
                                      : seat.seatId == 2 ? PartitionOwner::Player2
                                                         : PartitionOwner::Pool;
        if (owner == PartitionOwner::Pool) continue;
        for (auto& tile : m_deviceTiles) {
            if (!tile) continue;
            const std::wstring& stableId = tile->deviceId.empty() ? tile->devicePath : tile->deviceId;
            bool assigned = false;
            switch (tile->type) {
            case DeviceCategory::Display: assigned = containsId(seat.displayIds, stableId); break;
            case DeviceCategory::Keyboard: assigned = containsId(seat.keyboardIds, stableId); break;
            case DeviceCategory::Mouse:
            case DeviceCategory::Touchpad: assigned = containsId(seat.mouseIds, stableId); break;
            case DeviceCategory::Gamepad: assigned = containsId(seat.controllerIds, stableId); break;
            }
            if (assigned) {
                tile->owner = owner;
                tile->isPrimaryDisplay = tile->type == DeviceCategory::Display &&
                    seat.primaryDisplayId && *seat.primaryDisplayId == stableId;
            }
        }
    }

    layoutDeviceTiles();
    MessageBoxW(m_hwnd,
        L"Seat profile loaded successfully. Multiple displays per Seat and primary-display selection were restored.",
        L"HydraSeat Seat Manager", MB_OK | MB_ICONINFORMATION);
}

bool Win32App::configurationEditingAllowed() const noexcept {
    const auto& state = m_controlSurfaceModel.state();
    return state.hostConnected && state.runtimeStateKnown && state.sessionPhase &&
           *state.sessionPhase == runtime::SeatSessionPhase::Idle;
}

bool Win32App::initializeControlSurface() {
    if (m_workspaceManager.loadFromFile("workspace_config.json")) {
        applyWorkspaceProfileToTiles();
        std::string modelError;
        (void)m_controlSurfaceModel.setValidatedConfiguration(
            m_workspaceManager.managementSeatId(), m_workspaceManager.getAllSeats(),
            control::StartupMode::Manual, &modelError);
    }

    const SeatId requestedSeat = m_workspaceManager.getAllSeats().empty()
        ? 1u : m_workspaceManager.managementSeatId();
    m_controlSurfaceModel.setControlContext(requestedSeat, true, false);

    std::string error;
    if (m_hostClient.connectForSeat(hostipc::ClientRole::Control, requestedSeat, 750u, &error)) {
        m_controlSurfaceModel.setControlContext(requestedSeat, true, true);
    } else {
        m_hostClient.close();
        if (!m_hostClient.connect(hostipc::ClientRole::ReadOnly, 750u, &error)) {
            m_controlSurfaceModel.markHostDisconnected(error);
            m_sessionControlTransition.markHostDisconnected(error);
            updateControlSurfaceUi();
            applyManagementSeatPlacement();
            return true;
        }
    }

    const auto snapshot = m_hostClient.getSnapshot(1000u, &error);
    if (!snapshot) {
        m_hostClient.close();
        m_controlSurfaceModel.markHostDisconnected(error);
        m_sessionControlTransition.markHostDisconnected(error);
    } else {
        m_controlSurfaceModel.observeHostSnapshot(*snapshot);
        m_sessionControlTransition.observeSnapshot(*snapshot);
    }
    updateControlSurfaceUi();
    applyManagementSeatPlacement();
    return true;
}

void Win32App::refreshControlSurface() {
    if (!m_hostClient.connected()) {
        const SeatId requestedSeat = m_workspaceManager.getAllSeats().empty()
            ? 1u : m_workspaceManager.managementSeatId();
        std::string connectError;
        if (m_hostClient.connectForSeat(
                hostipc::ClientRole::Control, requestedSeat, 250u, &connectError)) {
            m_controlSurfaceModel.setControlContext(requestedSeat, true, true);
        } else {
            m_hostClient.close();
            m_controlSurfaceModel.setControlContext(requestedSeat, true, false);
            m_controlSurfaceModel.markHostDisconnected(connectError);
            m_sessionControlTransition.markHostDisconnected(connectError);
            updateControlSurfaceUi();
            return;
        }
    }

    std::string error;
    const auto snapshot = m_hostClient.getSnapshot(500u, &error);
    if (!snapshot) {
        m_hostClient.close();
        m_controlSurfaceModel.markHostDisconnected(error);
        m_sessionControlTransition.markHostDisconnected(error);
    } else {
        m_controlSurfaceModel.observeHostSnapshot(*snapshot);
        m_sessionControlTransition.observeSnapshot(*snapshot);
    }
    updateControlSurfaceUi();
}

void Win32App::updateControlSurfaceUi() {
    const auto& state = m_controlSurfaceModel.state();
    if (m_runtimeStatusLabel != nullptr) {
        std::wstring label = L"Runtime: ";
        label += runtimeModeText(state.runtimeMode);
        label += L" | Management Seat ";
        label += std::to_wstring(state.managementSeatId);
        for (const auto& seat : state.seats) {
            if (!seat.game) continue;
            label += L" | Seat ";
            label += std::to_wstring(seat.config.seatId);
            label += L": ";
            label += seatGamePhaseText(seat.game->phase);
        }
        if (state.wholeMachineReturnRequested) {
            label += L" | both Seat games ended";
        }
        if (!state.hostConnected) label += L" | disconnected";
        SetWindowTextW(m_runtimeStatusLabel, label.c_str());
    }
    if (m_startSessionBtn != nullptr) {
        EnableWindow(m_startSessionBtn, state.actions.start ? TRUE : FALSE);
    }
    if (m_stopSessionBtn != nullptr) {
        EnableWindow(m_stopSessionBtn,
                     state.actions.stopAndReturnToWindows ? TRUE : FALSE);
    }
    if (m_reconfigureBtn != nullptr) {
        EnableWindow(m_reconfigureBtn, state.actions.reconfigure ? TRUE : FALSE);
    }
    const BOOL editing = configurationEditingAllowed() ? TRUE : FALSE;
    if (m_saveProfileBtn != nullptr) EnableWindow(m_saveProfileBtn, editing);
    if (m_loadProfileBtn != nullptr) EnableWindow(m_loadProfileBtn, editing);
}

void Win32App::applyManagementSeatPlacement() {
    if (m_hwnd == nullptr) return;
    display::DisplayTopologyInventory inventory;
    const auto topology = inventory.refresh();
    if (!topology.querySucceeded) return;

    std::vector<SeatConfig> configs;
    const auto& state = m_controlSurfaceModel.state();
    if (!state.seats.empty()) {
        configs.reserve(state.seats.size());
        for (const auto& seat : state.seats) configs.push_back(seat.config);
    } else {
        configs = m_workspaceManager.getAllSeats();
    }

    std::vector<display::SeatDisplayRequest> requests;
    for (const auto& seat : configs) {
        if (seat.displayIds.empty()) continue;
        display::SeatDisplayRequest request;
        request.seatId = seat.seatId;
        request.missingOutputPolicy = display::MissingOutputPolicy::Degrade;
        for (const auto& outputId : seat.displayIds) {
            const auto stable = utf8FromWide(outputId);
            if (!stable.empty()) request.outputs.push_back({stable, true, false});
        }
        if (seat.primaryDisplayId) request.primaryOutputId = utf8FromWide(*seat.primaryDisplayId);
        if (!request.outputs.empty()) requests.push_back(std::move(request));
    }
    const auto layouts = display::buildSeatDisplayLayouts(topology, requests);
    control::ManagementSeatConfig placementConfig;
    placementConfig.managementSeatId = state.managementSeatId;
    placementConfig.preferredWidth = 980;
    placementConfig.preferredHeight = 750;
    const auto placement = control::resolveControlSurfacePlacement(
        placementConfig, layouts.groups, topology);
    if (!placement.valid) return;
    SetWindowPos(m_hwnd, nullptr, placement.windowRect.left, placement.windowRect.top,
                 placement.windowRect.width(), placement.windowRect.height(),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

bool Win32App::applyCurrentProfileToHost(bool showErrors) {
    if (!m_hostClient.connected() || m_hostClient.role() != hostipc::ClientRole::Control) {
        if (showErrors) {
            MessageBoxW(m_hwnd, L"The background host control channel is not available.",
                        L"HydraSeat Runtime", MB_OK | MB_ICONWARNING);
        }
        return false;
    }
    hostipc::ProfilePayload payload;
    payload.managementSeatId = m_workspaceManager.managementSeatId();
    payload.seats = m_workspaceManager.getAllSeats();
    std::string error;
    const auto applied = m_hostClient.applyProfile(payload, 2000u, &error);
    if (!applied || !applied->succeeded()) {
        if (showErrors) {
            const std::wstring message = L"The edited profile was not accepted by the background host. The active runtime was not changed.";
            MessageBoxW(m_hwnd, message.c_str(), L"HydraSeat Reconfigure",
                        MB_OK | MB_ICONERROR);
        }
        refreshControlSurface();
        return false;
    }
    std::string modelError;
    (void)m_controlSurfaceModel.setValidatedConfiguration(
        payload.managementSeatId, payload.seats, control::StartupMode::Manual, &modelError);
    m_controlSurfaceModel.setControlContext(payload.managementSeatId, true, true);
    m_controlSurfaceModel.observeHostSnapshot(applied->snapshot);
    m_sessionControlTransition.observeSnapshot(applied->snapshot);
    updateControlSurfaceUi();
    applyManagementSeatPlacement();
    return true;
}

void Win32App::startSession() {
    const auto& state = m_controlSurfaceModel.state();
    if (!state.actions.start || !m_hostClient.connected()) return;
    std::string error;
    std::optional<runtime::RuntimeCommandResult> current;
    if (state.sessionPhase && *state.sessionPhase == runtime::SeatSessionPhase::Idle) {
        current = m_hostClient.command(hostipc::MessageType::PlanSession, 2000u, &error);
        if (!current || !current->succeeded()) {
            MessageBoxW(m_hwnd, L"Session planning/preflight failed. No split session was started.",
                        L"HydraSeat Start", MB_OK | MB_ICONERROR);
            refreshControlSurface();
            return;
        }
        m_controlSurfaceModel.observeHostSnapshot(current->snapshot);
        m_sessionControlTransition.observeSnapshot(current->snapshot);
    }
    current = m_hostClient.command(hostipc::MessageType::StartSession, 3000u, &error);
    if (!current || !current->succeeded()) {
        MessageBoxW(m_hwnd, L"Session start failed or required recovery. Check runtime diagnostics.",
                    L"HydraSeat Start", MB_OK | MB_ICONERROR);
        refreshControlSurface();
        return;
    }
    m_controlSurfaceModel.observeHostSnapshot(current->snapshot);
    m_sessionControlTransition.observeSnapshot(current->snapshot);
    updateControlSurfaceUi();
}

void Win32App::stopSessionAndReturnToWindows() {
    if (!m_controlSurfaceModel.state().actions.stopAndReturnToWindows ||
        !m_hostClient.connected()) return;
    std::string error;
    const auto result = m_hostClient.command(
        hostipc::MessageType::StopAndReturnToWindows, 5000u, &error);
    if (!result || !m_sessionControlTransition.observeCommandResult(
                       control::SessionControlIntent::StopAndReturnToWindows, *result)) {
        MessageBoxW(m_hwnd,
            L"Return to Windows did not verify every rollback postcondition. Recovery may be required.",
            L"HydraSeat Stop", MB_OK | MB_ICONERROR);
    }
    if (result) m_controlSurfaceModel.observeHostSnapshot(result->snapshot);
    updateControlSurfaceUi();
}

void Win32App::beginReconfigure() {
    if (!m_controlSurfaceModel.state().actions.reconfigure ||
        !m_hostClient.connected()) return;
    std::string error;
    const auto result = m_hostClient.command(
        hostipc::MessageType::BeginReconfigure, 5000u, &error);
    if (!result || !m_sessionControlTransition.observeCommandResult(
                       control::SessionControlIntent::Reconfigure, *result)) {
        MessageBoxW(m_hwnd,
            L"Reconfigure could not reach verified normal-Windows state. Configuration editing remains blocked.",
            L"HydraSeat Reconfigure", MB_OK | MB_ICONERROR);
    }
    if (result) m_controlSurfaceModel.observeHostSnapshot(result->snapshot);
    updateControlSurfaceUi();
    applyManagementSeatPlacement();
}

void Win32App::toggleIsolationMode() {
    const bool current = m_inputRouter.isIsolationMode();
    m_inputRouter.setIsolationMode(!current);

    if (!current) {
        SetWindowTextW(m_isolationBtn, L"Diagnostic Intent: ON");
        MessageBoxW(
            m_hwnd,
            L"Diagnostic routing intent is ON.\n\nThis flag does not hide physical devices, suppress normal Windows input, virtualize game key state, or guarantee zero input bleed. Use the Phase 3 Input Lab to validate Gate A/B behavior.",
            L"HydraSeat Phase 3 Diagnostics", MB_OK | MB_ICONWARNING);
    } else {
        SetWindowTextW(m_isolationBtn, L"Diagnostic Intent: OFF");
    }
}

void Win32App::launchMultiseat() {
    wchar_t modulePath[MAX_PATH]{};
    const DWORD moduleLength = GetModuleFileNameW(
        nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (moduleLength == 0 || moduleLength >= std::size(modulePath)) {
        MessageBoxW(m_hwnd, L"Could not locate the HydraSeat executable directory.",
                    L"HydraSeat Phase 3 Input Lab", MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring labPath(modulePath, moduleLength);
    const auto separator = labPath.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        labPath.resize(separator + 1);
    } else {
        labPath.clear();
    }
    labPath += L"hydra_input_lab.exe";

    const DWORD attributes = GetFileAttributesW(labPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        MessageBoxW(
            m_hwnd,
            L"hydra_input_lab.exe was not found next to HydraSeat.exe. Build the Phase 3 Gate A/B target first.",
            L"HydraSeat Phase 3 Input Lab", MB_OK | MB_ICONWARNING);
        return;
    }

    wchar_t profilePath[MAX_PATH]{};
    const DWORD profileLength = GetFullPathNameW(
        L"workspace_config.json", static_cast<DWORD>(std::size(profilePath)),
        profilePath, nullptr);
    const std::wstring profile =
        profileLength > 0 && profileLength < std::size(profilePath)
            ? std::wstring(profilePath, profileLength)
            : L"workspace_config.json";

    std::wstring commandLine = L"\"" + labPath +
        L"\" --profile \"" + profile + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            labPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0,
            nullptr, nullptr, &startup, &process)) {
        const DWORD error = GetLastError();
        const std::wstring message =
            L"Could not start hydra_input_lab.exe. Win32 error: " +
            std::to_wstring(error);
        MessageBoxW(m_hwnd, message.c_str(), L"HydraSeat Phase 3 Input Lab",
                    MB_OK | MB_ICONERROR);
        return;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    MessageBoxW(
        m_hwnd,
        L"The Phase 3 Gate A/B Input Lab was launched.\n\nIt observes and diagnoses Seat routing only. Normal Windows input remains active, so this is not a zero-bleed game session.",
        L"HydraSeat Phase 3 Input Lab", MB_OK | MB_ICONINFORMATION);
}

void Win32App::launchGateCControlledLab() {
    wchar_t modulePath[MAX_PATH]{};
    const DWORD moduleLength = GetModuleFileNameW(
        nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (moduleLength == 0 || moduleLength >= std::size(modulePath)) {
        MessageBoxW(m_hwnd, L"Could not locate the HydraSeat executable directory.",
                    L"HydraSeat Gate C Process Lab", MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring directory(modulePath, moduleLength);
    const auto separator = directory.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        directory.resize(separator + 1);
    } else {
        directory.clear();
    }

    const std::wstring hostPath = directory + L"hydra_gate_c_host.exe";
    const std::wstring targetPath = directory + L"hydra_gate_c_target.exe";
    const std::wstring adapterPath = directory + L"hydra_gate_c_adapter.dll";
    for (const auto& required : {hostPath, targetPath, adapterPath}) {
        const DWORD attributes = GetFileAttributesW(required.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            const std::wstring message =
                required + L" was not found. Build the complete Gate C targets first.";
            MessageBoxW(m_hwnd, message.c_str(),
                        L"HydraSeat Gate C Process Lab",
                        MB_OK | MB_ICONWARNING);
            return;
        }
    }

    wchar_t profilePath[MAX_PATH]{};
    const DWORD profileLength = GetFullPathNameW(
        L"workspace_config.json", static_cast<DWORD>(std::size(profilePath)),
        profilePath, nullptr);
    const std::wstring profile =
        profileLength > 0 && profileLength < std::size(profilePath)
            ? std::wstring(profilePath, profileLength)
            : L"workspace_config.json";

    std::wstring commandLine = L"\"" + hostPath +
        L"\" --profile \"" + profile + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            hostPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
            CREATE_NEW_CONSOLE, nullptr, directory.c_str(), &startup, &process)) {
        const DWORD error = GetLastError();
        const std::wstring message =
            L"Could not start hydra_gate_c_host.exe. Win32 error: " +
            std::to_wstring(error);
        MessageBoxW(m_hwnd, message.c_str(),
                    L"HydraSeat Gate C Process Lab",
                    MB_OK | MB_ICONERROR);
        return;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    MessageBoxW(
        m_hwnd,
        L"Gate C launched two HydraSeat-owned controlled target processes.\n\n"
        L"They receive independent virtual keyboard, mouse, cursor, clip, foreground, and capture state through the adapter DLL. No commercial game is injected, normal Windows input is not suppressed, and zero-bleed game support is not yet claimed.",
        L"HydraSeat Gate C Process Lab",
        MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK Win32App::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_HYDRA_ACTIVATE_CONTROL && g_appInstance) {
        g_appInstance->applyManagementSeatPlacement();
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        return 0;
    }
    if (uMsg == WM_INPUT && g_appInstance) {
        g_appInstance->m_inputRouter.handleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    } else if (uMsg == WM_INPUT_DEVICE_CHANGE && g_appInstance) {
        g_appInstance->m_inputRouter.handleDeviceChange(
            wParam, reinterpret_cast<HANDLE>(lParam));
        return 0;
    } else if ((uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) && g_appInstance) {
        // Fallback for injected/synthetic keys (like Asus Gaming Keyboards) that bypass WM_INPUT
        uint64_t now = GetTickCount64();
        for (auto& tilePtr : g_appInstance->m_deviceTiles) {
            if (tilePtr && tilePtr->type == DeviceCategory::Keyboard) {
                tilePtr->flashUntil = now + 250;
                InvalidateRect(tilePtr->hwndControl, NULL, FALSE);
                break; // Flash the first keyboard (usually laptop) and stop
            }
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    } else if (uMsg == WM_TIMER && wParam == TIMER_FLASH_RESET && g_appInstance) {
        uint64_t now = GetTickCount64();
        for (auto& tilePtr : g_appInstance->m_deviceTiles) {
            if (tilePtr && tilePtr->flashUntil > 0 && now >= tilePtr->flashUntil) {
                tilePtr->flashUntil = 0;
                InvalidateRect(tilePtr->hwndControl, NULL, FALSE);
            }
        }
        return 0;
    } else if (uMsg == WM_TIMER && wParam == TIMER_HOST_REFRESH && g_appInstance) {
        g_appInstance->refreshControlSurface();
        return 0;
    } else if (uMsg == WM_COMMAND) {
        int wmId = LOWORD(wParam);
        if (wmId == ID_BTN_REFRESH && g_appInstance) {
            g_appInstance->refreshHardware();
        } else if (wmId == ID_BTN_SAVE_PROF && g_appInstance) {
            g_appInstance->saveWorkspaceProfile();
        } else if (wmId == ID_BTN_LOAD_PROF && g_appInstance) {
            g_appInstance->loadWorkspaceProfile();
        } else if (wmId == ID_BTN_ISOLATION && g_appInstance) {
            g_appInstance->toggleIsolationMode();
        } else if (wmId == ID_BTN_LAUNCH && g_appInstance) {
            g_appInstance->launchMultiseat();
        } else if (wmId == ID_BTN_GATE_C && g_appInstance) {
            g_appInstance->launchGateCControlledLab();
        } else if (wmId == ID_BTN_START_SESSION && g_appInstance) {
            g_appInstance->startSession();
        } else if (wmId == ID_BTN_STOP_SESSION && g_appInstance) {
            g_appInstance->stopSessionAndReturnToWindows();
        } else if (wmId == ID_BTN_RECONFIGURE && g_appInstance) {
            g_appInstance->beginReconfigure();
        }
    } else if (uMsg == WM_DESTROY) {
        KillTimer(hwnd, TIMER_FLASH_RESET);
        KillTimer(hwnd, TIMER_HOST_REFRESH);
        if (g_appInstance) g_appInstance->m_hostClient.close();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int Win32App::run() {
    if (m_duplicateLaunch) return 0;
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

} // namespace gui
} // namespace hydra
#endif
