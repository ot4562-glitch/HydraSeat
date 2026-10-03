#include "hydra/gui_win32.hpp"
#include "hydra/controller_inventory.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>

#ifdef _WIN32
#include <commdlg.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <unordered_set>

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

// Extract clean hardware base ID key (strips HID sub-collections like &Col01, &Col02)
static std::wstring getGuiHardwareDeviceKey(const std::wstring& devPath) {
    std::wstring pathUpper = devPath;
    for (auto& c : pathUpper) c = ::towupper(c);

    // Remove any embedded null characters that come from Win32 API
    pathUpper.erase(std::remove(pathUpper.begin(), pathUpper.end(), L'\0'), pathUpper.end());

    // Find the VID&PID or ACPI device identifier portion
    size_t start = pathUpper.find(L"HID#");
    if (start == std::wstring::npos) start = pathUpper.find(L"ACPI#");
    if (start != std::wstring::npos) {
        size_t firstHash = pathUpper.find(L"#", start);
        if (firstHash != std::wstring::npos) {
            size_t secondHash = pathUpper.find(L"#", firstHash + 1);
            if (secondHash != std::wstring::npos) {
                std::wstring key = pathUpper.substr(firstHash + 1, secondHash - firstHash - 1);
                // Strip sub-collection suffix (&COL01, &COL02, etc.)
                size_t colPos = key.find(L"&COL");
                if (colPos != std::wstring::npos) {
                    key.erase(colPos);
                }
                return key;
            }
        }
    }
    return pathUpper;
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

#define TIMER_FLASH_RESET 2001

static std::optional<std::string> wideToUtf8(std::wstring_view value) {
    if (value.empty()) return std::string{};
    if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }
    const int sourceLength = static_cast<int>(value.size());
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), sourceLength,
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return std::nullopt;
    std::string result(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), sourceLength,
        result.data(), required, nullptr, nullptr);
    if (written != required) return std::nullopt;
    return result;
}

Win32App::Win32App() {
    g_appInstance = this;
}

Win32App::~Win32App() {
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

            int width = (tile->type == DeviceCategory::Display) ? 145 :
                        ((tile->type == DeviceCategory::Keyboard) ? 105 :
                        ((tile->type == DeviceCategory::Gamepad) ? 90 : 45));
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
                DrawTextW(hdc, tile->displayLabel.c_str(), -1, &textRect, DT_SINGLELINE | DT_RIGHT | DT_BOTTOM);

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
            } else if (tile->type == DeviceCategory::Gamepad) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(226, 232, 240));
                HFONT padFont = CreateFontW(
                    14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                HGDIOBJ oldPadFont = SelectObject(hdc, padFont);
                RECT labelRect = rect;
                DrawTextW(
                    hdc,
                    tile->displayLabel.c_str(),
                    -1,
                    &labelRect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldPadFont);
                DeleteObject(padFont);
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
    if (!tile || !m_hwnd) return;

    POINT clientPt = screenPt;
    ScreenToClient(m_hwnd, &clientPt);

    if (clientPt.x < 315) {
        tile->owner = PartitionOwner::Pool;
    } else if (clientPt.x >= 315 && clientPt.x < 635) {
        tile->owner = PartitionOwner::Player1;
    } else {
        tile->owner = PartitionOwner::Player2;
    }

    layoutDeviceTiles();
}

bool Win32App::initialize(HINSTANCE hInstance, int nCmdShow) {
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
        L"HydraSeat - Multiseat Control Center",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 750,
        NULL, NULL, hInstance, NULL
    );

    if (!m_hwnd) return false;

    setupUI();
    m_inputRouter.initialize(reinterpret_cast<uint64_t>(m_hwnd));
    refreshHardware();
    loadWorkspaceProfile();

    // Hook global raw input events to trigger live YELLOW BORDER highlights on device tiles
    m_inputRouter.setGlobalCallback([this](const RawInputEvent& evt) {
        triggerDeviceFlash(evt.deviceHandle, evt.devicePath, evt.rawDevType, evt.isTouchpad);
    });

    SetTimer(m_hwnd, TIMER_FLASH_RESET, 50, NULL);

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

    // 2. Fallback: match by device path base hardware ID + DEVICE TYPE
    if (!devPath.empty()) {
        std::wstring incomingKey = getGuiHardwareDeviceKey(devPath);
        for (size_t tIdx = 0; tIdx < m_deviceTiles.size(); tIdx++) {
            auto& tilePtr = m_deviceTiles[tIdx];
            if (!tilePtr) continue;
            // TYPE CHECK: Only match tiles compatible with the raw device type
            if (!isTypeCompatible(rawDevType, tilePtr->type)) continue;
            std::wstring tileKey = getGuiHardwareDeviceKey(tilePtr->devicePath);
            if (!incomingKey.empty() && incomingKey == tileKey) {
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

    // 3. Last resort fallback for unmapped handles with no path match
    if (g_diagLog.is_open()) {
        const wchar_t* typeStr = (rawDevType == RIM_TYPEKEYBOARD) ? L"KBD" :
                                 (rawDevType == RIM_TYPEMOUSE) ? L"MOUSE" :
                                 (rawDevType == RIM_TYPEHID) ? L"HID" : L"UNK";
        g_diagLog << L"[FALLBACK] handle=0x" << std::hex << handle << std::dec
                  << L" type=" << typeStr << L" isTouchpad=" << isTouchpad << std::endl;
    }
    for (size_t tIdx = 0; tIdx < m_deviceTiles.size(); tIdx++) {
        auto& tilePtr = m_deviceTiles[tIdx];
        if (!tilePtr) continue;
        bool matched = false;
        if (rawDevType == RIM_TYPEKEYBOARD && tilePtr->type == DeviceCategory::Keyboard) {
            matched = true;
        } else if (rawDevType == RIM_TYPEMOUSE) {
            if (isTouchpad && tilePtr->type == DeviceCategory::Touchpad) {
                matched = true;
            } else if (!isTouchpad && tilePtr->type == DeviceCategory::Mouse) {
                matched = true;
            }
        } else if (rawDevType == RIM_TYPEHID && tilePtr->type == DeviceCategory::Touchpad) {
            matched = true;
        }
        if (matched) {
            tilePtr->flashUntil = now + 250;
            InvalidateRect(tilePtr->hwndControl, NULL, FALSE);
            // Cache non-zero handles for future O(1) lookups
            if (handle != 0) {
                m_handleToTileIndex[handle] = tIdx;
                if (g_diagLog.is_open()) {
                    g_diagLog << L"[FALLBACK-CACHE] handle=0x" << std::hex << handle << std::dec
                              << L" -> tile[" << tIdx << L"] " << tilePtr->displayLabel << std::endl;
                }
            }
            break;
        }
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

    // Canonical host configuration actions.
    m_saveProfileBtn = CreateWindowExW(0, L"BUTTON", L"Apply Assignments",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        430, 15, 135, 32, m_hwnd, (HMENU)ID_BTN_SAVE_PROF, GetModuleHandle(NULL), NULL);

    m_loadProfileBtn = CreateWindowExW(0, L"BUTTON", L"Reload Assignments",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        575, 15, 135, 32, m_hwnd, (HMENU)ID_BTN_LOAD_PROF, GetModuleHandle(NULL), NULL);

    // Refresh Button
    m_refreshBtn = CreateWindowExW(0, L"BUTTON", L"Refresh",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        720, 15, 90, 32, m_hwnd, (HMENU)ID_BTN_REFRESH, GetModuleHandle(NULL), NULL);

    // Status Label
    m_deviceStatusLabel = CreateWindowExW(0, L"STATIC", L"Detecting connected hardware...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 50, 920, 22, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

    HFONT hFontNormal = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(m_deviceStatusLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(m_saveProfileBtn, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(m_loadProfileBtn, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(m_refreshBtn, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    // 3 PARTITIONS (ASTER DRAG-AND-DROP LAYOUT):
    m_poolGroup = CreateWindowExW(0, L"BUTTON", L"System Hardware Pool (Drag tile to assign)",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        20, 80, 290, 540, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

    m_p1Group = CreateWindowExW(0, L"BUTTON", L"Player 1 Workspace",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        330, 80, 290, 540, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

    m_p2Group = CreateWindowExW(0, L"BUTTON", L"Player 2 Workspace",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        640, 80, 290, 540, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

    HFONT hFontBold = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(m_poolGroup, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    SendMessageW(m_p1Group, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    SendMessageW(m_p2Group, WM_SETFONT, (WPARAM)hFontBold, TRUE);

    auto* p1ControllerLabel = CreateWindowExW(
        0, L"STATIC", L"XInput slot:", WS_CHILD | WS_VISIBLE,
        350, 574, 88, 22, m_hwnd, NULL, GetModuleHandle(NULL), NULL);
    m_p1ControllerSlotCombo = CreateWindowExW(
        0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        440, 570, 160, 160, m_hwnd, NULL, GetModuleHandle(NULL), NULL);
    auto* p2ControllerLabel = CreateWindowExW(
        0, L"STATIC", L"XInput slot:", WS_CHILD | WS_VISIBLE,
        660, 574, 88, 22, m_hwnd, NULL, GetModuleHandle(NULL), NULL);
    m_p2ControllerSlotCombo = CreateWindowExW(
        0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        750, 570, 160, 160, m_hwnd, NULL, GetModuleHandle(NULL), NULL);
    SendMessageW(p1ControllerLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(p2ControllerLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(m_p1ControllerSlotCombo, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessageW(m_p2ControllerSlotCombo, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    // The canonical host owns isolation. These buttons start/stop one Seat at a
    // time and never launch a local fallback process.
    m_isolationBtn = CreateWindowExW(0, L"BUTTON", L"Seat 1: Launch / Stop",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 642, 440, 42, m_hwnd, (HMENU)ID_BTN_ISOLATION, GetModuleHandle(NULL), NULL);

    m_launchBtn = CreateWindowExW(0, L"BUTTON", L"Seat 2: Launch / Stop",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        490, 642, 440, 42, m_hwnd, (HMENU)ID_BTN_LAUNCH, GetModuleHandle(NULL), NULL);

    HFONT hFontBtn = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(m_isolationBtn, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
    SendMessageW(m_launchBtn, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
}

void Win32App::layoutDeviceTiles() {
    auto layoutPartition = [this](PartitionOwner owner, int startX) {
        int dispX = startX + 15;
        int dispY = 110;

        int inputX = startX + 15;
        int inputY = 205;

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
            } else if (tilePtr->type == DeviceCategory::Gamepad) {
                SetWindowPos(tilePtr->hwndControl, NULL, inputX, inputY, 90, 42, SWP_NOZORDER | SWP_SHOWWINDOW);
                inputX += 100;
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
    std::unordered_map<std::wstring, PartitionOwner> preservedOwners;
    for (const auto& tile : m_deviceTiles) {
        if (tile && !tile->stableId.empty()) {
            preservedOwners[tile->stableId] = tile->owner;
        }
    }

    const auto ownerFor = [&](const std::wstring& stableId) {
        const auto found = preservedOwners.find(stableId);
        return found != preservedOwners.end()
            ? found->second
            : PartitionOwner::Pool;
    };

    m_displays = m_hardwareDetector.detectDisplays();
    m_keyboards = m_hardwareDetector.detectKeyboards();
    m_mice = m_hardwareDetector.detectMice();
    m_controllers = m_hardwareDetector.detectControllers();

    const auto controllerInventory = hydra::controller::scanControllerSources();
    const auto populateControllerSlots = [&](HWND combo) {
        if (!combo) return;
        const LRESULT previousIndex = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        LRESULT previousData = -1;
        if (previousIndex != CB_ERR) {
            previousData = SendMessageW(combo, CB_GETITEMDATA, previousIndex, 0);
        }
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        const LRESULT noneIndex = SendMessageW(
            combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(L"No XInput controller"));
        if (noneIndex != CB_ERR) {
            SendMessageW(combo, CB_SETITEMDATA, noneIndex, static_cast<LPARAM>(-1));
        }
        LRESULT selectedIndex = noneIndex;
        if (controllerInventory.authoritative) {
            for (const auto& source : controllerInventory.sources) {
                if (source.api != hydra::controller::ApiSurface::XInput ||
                    !source.connected || !source.runtimeXInputSlot) {
                    continue;
                }
                const auto slot = *source.runtimeXInputSlot;
                const std::wstring label =
                    L"XInput slot " + std::to_wstring(slot) + L" (connected)";
                const LRESULT item = SendMessageW(
                    combo, CB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(label.c_str()));
                if (item == CB_ERR) continue;
                SendMessageW(combo, CB_SETITEMDATA, item, static_cast<LPARAM>(slot));
                if (previousData == static_cast<LRESULT>(slot)) {
                    selectedIndex = item;
                }
            }
        }
        if (selectedIndex != CB_ERR) {
            SendMessageW(combo, CB_SETCURSEL, selectedIndex, 0);
        }
    };
    populateControllerSlots(m_p1ControllerSlotCombo);
    populateControllerSlots(m_p2ControllerSlotCombo);

    std::wstring statusText = L"Connected Hardware: " + std::to_wstring(m_displays.size()) + L" Displays | " +
                              std::to_wstring(m_keyboards.size()) + L" Keyboards | " +
                              std::to_wstring(m_mice.size()) + L" Mice | " +
                              std::to_wstring(m_controllers.size()) + L" Gamepads";

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
        tile->stableId = m_displays[i].id;
        tile->displayLabel = L"1." + std::to_wstring(i + 1);
        tile->type = DeviceCategory::Display;
        tile->nativeHandle = m_displays[i].nativeHandle;
        tile->devicePath = m_displays[i].devicePath;
        tile->owner = ownerFor(tile->stableId);

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
        tile->stableId = m_keyboards[i].id;
        tile->displayLabel = L"KBD " + std::to_wstring(i + 1);
        tile->type = DeviceCategory::Keyboard;
        tile->nativeHandle = m_keyboards[i].nativeHandle;
        tile->devicePath = m_keyboards[i].devicePath;
        tile->owner = ownerFor(tile->stableId);

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
        tile->stableId = m_mice[i].id;
        tile->displayLabel = L"MOU " + std::to_wstring(i + 1);

        std::wstring nameUpper = tile->name;
        for (auto& c : nameUpper) c = ::towupper(c);

        std::wstring pathUpper = m_mice[i].devicePath;
        for (auto& c : pathUpper) c = ::towupper(c);

        if (nameUpper.find(L"TOUCHPAD") != std::wstring::npos || pathUpper.find(L"ACPI") != std::wstring::npos || pathUpper.find(L"MSFT0001") != std::wstring::npos || pathUpper.find(L"SYN") != std::wstring::npos || pathUpper.find(L"ELAN") != std::wstring::npos || pathUpper.find(L"ITE5570") != std::wstring::npos || pathUpper.find(L"PNP0C50") != std::wstring::npos) {
            tile->type = DeviceCategory::Touchpad;
        } else {
            tile->type = DeviceCategory::Mouse;
        }

        tile->nativeHandle = m_mice[i].nativeHandle;
        tile->devicePath = m_mice[i].devicePath;
        tile->owner = ownerFor(tile->stableId);

        tile->hwndControl = CreateWindowExW(0, L"HydraSeatDeviceTileClass", L"",
            WS_CHILD | WS_VISIBLE,
            0, 0, 45, 42, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

        SetWindowLongPtrW(tile->hwndControl, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tile.get()));
        size_t idx = m_deviceTiles.size();
        uintptr_t handle = tile->nativeHandle;
        m_deviceTiles.push_back(std::move(tile));
        if (handle != 0) m_handleToTileIndex[handle] = idx;
    }

    // Physical controllers are stable-ID tiles. Pairing to the separate,
    // explicitly selected XInput runtime slot happens only at Apply/Launch.
    for (size_t i = 0; i < m_controllers.size(); ++i) {
        auto tile = std::make_unique<VisualDeviceTile>();
        tile->name = m_controllers[i].name;
        tile->stableId = m_controllers[i].id;
        tile->displayLabel = L"PAD " + std::to_wstring(i + 1);
        tile->type = DeviceCategory::Gamepad;
        tile->nativeHandle = 0;
        tile->devicePath = m_controllers[i].devicePath;
        tile->owner = ownerFor(tile->stableId);
        tile->hwndControl = CreateWindowExW(
            0, L"HydraSeatDeviceTileClass", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 90, 42, m_hwnd, NULL, GetModuleHandle(NULL), NULL);
        SetWindowLongPtrW(
            tile->hwndControl,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(tile.get()));
        m_deviceTiles.push_back(std::move(tile));
    }

    // ================================================================
    // CRITICAL FIX: Map ALL raw input device handles to their correct tiles.
    // Composite USB devices have MULTIPLE HID sub-collection handles
    // (Col01, Col02, Col03...). Raw input events can arrive on ANY handle.
    // We match by BOTH base hardware ID AND device type compatibility.
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
                      << L" baseKey=" << getGuiHardwareDeviceKey(m_deviceTiles[i]->devicePath)
                      << L" path=" << cleanPath << std::endl;
        }
    }

    UINT totalRawDevices = 0;
    GetRawInputDeviceList(NULL, &totalRawDevices, sizeof(RAWINPUTDEVICELIST));
    if (totalRawDevices > 0) {
        std::vector<RAWINPUTDEVICELIST> allRawDevices(totalRawDevices);
        if (GetRawInputDeviceList(allRawDevices.data(), &totalRawDevices, sizeof(RAWINPUTDEVICELIST)) != (UINT)-1) {
            if (g_diagLog.is_open()) {
                g_diagLog << L"\n=== ALL RAW INPUT DEVICE HANDLES (" << totalRawDevices << L") ===" << std::endl;
            }
            for (const auto& rawDev : allRawDevices) {
                uintptr_t rawHandle = reinterpret_cast<uintptr_t>(rawDev.hDevice);

                // Get this raw device's path
                UINT nameSize = 0;
                GetRawInputDeviceInfoW(rawDev.hDevice, RIDI_DEVICENAME, NULL, &nameSize);
                if (nameSize == 0) continue;
                std::wstring rawPath(nameSize, L'\0');
                if (GetRawInputDeviceInfoW(rawDev.hDevice, RIDI_DEVICENAME, rawPath.data(), &nameSize) == (UINT)-1) {
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

                // Compute base hardware ID for this raw device
                std::wstring rawBaseKey = getGuiHardwareDeviceKey(rawPath);
                if (rawBaseKey.empty()) continue;

                // Find matching tile by BOTH base hardware ID AND device type
                bool mapped = false;
                for (size_t tIdx = 0; tIdx < m_deviceTiles.size(); tIdx++) {
                    auto& tile = m_deviceTiles[tIdx];
                    if (!tile || tile->devicePath.empty()) continue;

                    // TYPE CHECK: Only map to tiles compatible with the raw device type
                    if (!isTypeCompatible(rawDev.dwType, tile->type)) continue;

                    std::wstring tileBaseKey = getGuiHardwareDeviceKey(tile->devicePath);
                    if (tileBaseKey == rawBaseKey) {
                        m_handleToTileIndex[rawHandle] = tIdx;
                        mapped = true;
                        if (g_diagLog.is_open()) {
                            std::wstring cleanRawPath = rawPath;
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
                    std::wstring cleanRawPath = rawPath;
                    cleanRawPath.erase(std::remove(cleanRawPath.begin(), cleanRawPath.end(), L'\0'), cleanRawPath.end());
                    g_diagLog << L"  handle=0x" << std::hex << rawHandle << std::dec
                              << L" type=" << typeStr
                              << L" UNMAPPED baseKey=" << rawBaseKey
                              << L" path=" << cleanRawPath << std::endl;
                }
            }
        }
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
    std::string error;
    if (!commitAssignments(&error)) {
        const std::wstring message(error.begin(), error.end());
        MessageBoxW(
            m_hwnd,
            message.empty() ? L"Seat assignments could not be committed." : message.c_str(),
            L"HydraSeat assignment failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    MessageBoxW(
        m_hwnd,
        L"Seat assignments were committed to the canonical host and persisted.",
        L"HydraSeat",
        MB_OK | MB_ICONINFORMATION);
}

void Win32App::loadWorkspaceProfile() {
    std::string error;
    const auto first = m_hostControl.seatHardware(1u, &error);
    const auto second = m_hostControl.seatHardware(2u, &error);
    if (!first || !second) {
        const std::wstring message(error.begin(), error.end());
        MessageBoxW(
            m_hwnd,
            message.empty() ? L"Persisted Seat assignments are unavailable." : message.c_str(),
            L"HydraSeat reload failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    for (auto& tile : m_deviceTiles) {
        if (tile) tile->owner = PartitionOwner::Pool;
    }

    const auto applyOwner = [this](
        const hydra::hostipc::SeatHardwareAssignment& assignment,
        PartitionOwner owner) {
        for (auto& tile : m_deviceTiles) {
            if (!tile) continue;
            const auto stable = wideToUtf8(tile->stableId);
            if (!stable) continue;
            if (*stable == assignment.displayIdUtf8 ||
                *stable == assignment.keyboardIdUtf8 ||
                *stable == assignment.mouseIdUtf8) {
                tile->owner = owner;
            }
        }
    };

    applyOwner(*first, PartitionOwner::Player1);
    applyOwner(*second, PartitionOwner::Player2);
    layoutDeviceTiles();
}

bool Win32App::applySeatAssignment(
    std::uint32_t seatId,
    std::string* error) {
    if (seatId != 1u && seatId != 2u) {
        if (error) *error = "invalid Seat id";
        return false;
    }

    const auto owner =
        seatId == 1u ? PartitionOwner::Player1 : PartitionOwner::Player2;
    std::optional<std::wstring> display;
    std::optional<std::wstring> keyboard;
    std::optional<std::wstring> mouse;
    std::optional<std::wstring> controller;

    for (const auto& tile : m_deviceTiles) {
        if (!tile || tile->owner != owner) continue;

        const auto assignUnique = [&](
            std::optional<std::wstring>& target,
            std::string_view label) {
            if (target) {
                if (error) {
                    *error =
                        "Seat " + std::to_string(seatId) +
                        " has more than one assigned " + std::string(label) +
                        "; v1 supports exactly one per type";
                }
                return false;
            }
            if (tile->stableId.empty()) {
                if (error) {
                    *error =
                        "Seat " + std::to_string(seatId) +
                        " contains hardware without a stable identity";
                }
                return false;
            }
            target = tile->stableId;
            return true;
        };

        if (tile->type == DeviceCategory::Display) {
            if (!assignUnique(display, "display")) return false;
        } else if (tile->type == DeviceCategory::Keyboard) {
            if (!assignUnique(keyboard, "keyboard")) return false;
        } else if (
            tile->type == DeviceCategory::Mouse ||
            tile->type == DeviceCategory::Touchpad) {
            if (!assignUnique(mouse, "mouse")) return false;
        } else if (tile->type == DeviceCategory::Gamepad) {
            if (!assignUnique(controller, "controller")) return false;
        }
    }

    std::optional<std::uint8_t> controllerSlot;
    if (controller) {
        HWND combo =
            seatId == 1u ? m_p1ControllerSlotCombo : m_p2ControllerSlotCombo;
        const LRESULT index =
            combo ? SendMessageW(combo, CB_GETCURSEL, 0, 0) : CB_ERR;
        const LRESULT data =
            index != CB_ERR
                ? SendMessageW(combo, CB_GETITEMDATA, index, 0)
                : CB_ERR;
        if (data == CB_ERR || data < 0 ||
            data >= hydra::controller::kXInputSlotCount) {
            if (error) {
                *error =
                    "Assign exactly one connected XInput slot to Seat " +
                    std::to_string(seatId) +
                    " for the selected physical controller";
            }
            return false;
        }
        controllerSlot = static_cast<std::uint8_t>(data);
    }

    const auto displayUtf8 = wideToUtf8(display.value_or(L""));
    const auto keyboardUtf8 = wideToUtf8(keyboard.value_or(L""));
    const auto mouseUtf8 = wideToUtf8(mouse.value_or(L""));
    const auto controllerUtf8 = wideToUtf8(controller.value_or(L""));
    if (!displayUtf8 || !keyboardUtf8 || !mouseUtf8 || !controllerUtf8) {
        if (error) *error = "assigned hardware identity is not valid Unicode";
        return false;
    }

    const auto before = m_hostControl.seatHardware(seatId, error);
    if (!before) return false;

    const auto snapshot = m_hostControl.snapshot(error);
    if (!snapshot || seatId > snapshot->seats.size()) return false;
    if (snapshot->seats[seatId - 1u].gameLeaseActive) {
        if (error) {
            *error =
                "Stop the Seat game before changing its hardware or controller pairing";
        }
        return false;
    }

    // A controller binding is generation/lease scoped. Release our inactive UI
    // lease before reconfiguration so removing or changing a controller cannot
    // leave a stale binding behind.
    if (m_hostControl.ownsUiLease(seatId) &&
        !m_hostControl.releaseUiLease(seatId, error)) {
        return false;
    }

    if (!m_hostControl.assignSeatHardware(
            seatId,
            *displayUtf8,
            *keyboardUtf8,
            *mouseUtf8,
            error)) {
        return false;
    }

    if (!controller) return true;

    if (m_hostControl.pairController(
            seatId,
            *controllerUtf8,
            *controllerSlot,
            error)) {
        return true;
    }

    // Pairing failed after the durable hardware mutation. Restore the previous
    // durable assignment and discard the fresh lease/binding generation.
    std::string ignored;
    if (m_hostControl.ownsUiLease(seatId)) {
        (void)m_hostControl.releaseUiLease(seatId, &ignored);
    }
    const bool restored = m_hostControl.assignSeatHardware(
        seatId,
        before->displayIdUtf8,
        before->keyboardIdUtf8,
        before->mouseIdUtf8,
        &ignored).has_value();
    if (!restored && error) {
        *error += "; previous Seat hardware assignment could not be restored";
    }
    return false;
}

bool Win32App::commitAssignments(std::string* error) {
    const auto snapshot = m_hostControl.snapshot(error);
    if (!snapshot) return false;
    for (const auto& seat : snapshot->seats) {
        if (seat.gameLeaseActive) {
            if (error) {
                *error =
                    "Stop both Seat games before changing hardware assignments";
            }
            return false;
        }
    }

    const auto previousFirst = m_hostControl.seatHardware(1u, error);
    const auto previousSecond = m_hostControl.seatHardware(2u, error);
    if (!previousFirst || !previousSecond) return false;

    // Release inactive UI leases/controller bindings before rebuilding the
    // two-Seat assignment. This starts the reconfiguration from a clean
    // authority generation while preserving the running-host process.
    m_hostControl.close();

    const auto restore = [&]() {
        m_hostControl.close();
        std::string ignored;
        (void)m_hostControl.assignSeatHardware(1u, "", "", "", &ignored);
        (void)m_hostControl.assignSeatHardware(2u, "", "", "", &ignored);
        const bool firstRestored = m_hostControl.assignSeatHardware(
            1u,
            previousFirst->displayIdUtf8,
            previousFirst->keyboardIdUtf8,
            previousFirst->mouseIdUtf8,
            &ignored).has_value();
        const bool secondRestored = m_hostControl.assignSeatHardware(
            2u,
            previousSecond->displayIdUtf8,
            previousSecond->keyboardIdUtf8,
            previousSecond->mouseIdUtf8,
            &ignored).has_value();
        return firstRestored && secondRestored;
    };

    if (!m_hostControl.assignSeatHardware(1u, "", "", "", error) ||
        !m_hostControl.assignSeatHardware(2u, "", "", "", error)) {
        (void)restore();
        return false;
    }

    std::string applyError;
    if (!applySeatAssignment(1u, &applyError) ||
        !applySeatAssignment(2u, &applyError)) {
        const bool restored = restore();
        if (error) {
            *error = applyError.empty()
                ? "failed to apply Seat assignments"
                : std::move(applyError);
            if (!restored) {
                *error += "; previous assignments could not be fully restored";
            }
        }
        return false;
    }
    return true;
}

std::optional<std::wstring> Win32App::chooseExecutable(
    std::uint32_t seatId) {
    std::array<wchar_t, 32768> fileName{};
    const std::wstring title =
        L"Select executable for Seat " + std::to_wstring(seatId);

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = m_hwnd;
    dialog.lpstrFilter =
        L"Windows applications (*.exe)\0*.exe\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.lpstrTitle = title.c_str();
    dialog.Flags =
        OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;

    if (!GetOpenFileNameW(&dialog)) {
        return std::nullopt;
    }
    return std::wstring(fileName.data());
}

void Win32App::toggleSeatGame(std::uint32_t seatId) {
    std::string error;
    const auto snapshot = m_hostControl.snapshot(&error);
    if (!snapshot || seatId == 0u || seatId > snapshot->seats.size()) {
        const std::wstring message(error.begin(), error.end());
        MessageBoxW(
            m_hwnd,
            message.empty() ? L"Canonical host is unavailable." : message.c_str(),
            L"HydraSeat host unavailable",
            MB_OK | MB_ICONERROR);
        return;
    }

    if (snapshot->seats[seatId - 1u].gameLeaseActive) {
        if (!m_hostControl.stopGame(seatId, &error)) {
            const std::wstring message(error.begin(), error.end());
            MessageBoxW(
                m_hwnd,
                message.empty() ? L"The Seat game could not be stopped safely." : message.c_str(),
                L"HydraSeat stop failed",
                MB_OK | MB_ICONERROR);
            return;
        }
        MessageBoxW(
            m_hwnd,
            L"Seat game stopped safely.",
            L"HydraSeat",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    const bool anotherSeatActive = std::any_of(
        snapshot->seats.begin(),
        snapshot->seats.end(),
        [](const auto& seat) { return seat.gameLeaseActive; });
    const bool assignmentReady = anotherSeatActive
        ? applySeatAssignment(seatId, &error)
        : commitAssignments(&error);
    if (!assignmentReady) {
        const std::wstring message(error.begin(), error.end());
        MessageBoxW(
            m_hwnd,
            message.empty() ? L"Seat hardware assignment failed." : message.c_str(),
            L"HydraSeat assignment failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    const auto executable = chooseExecutable(seatId);
    if (!executable) return;

    const std::filesystem::path executablePath(*executable);
    const auto titleUtf8 = wideToUtf8(executablePath.filename().wstring());
    const auto executableUtf8 = wideToUtf8(executablePath.wstring());
    const auto workingUtf8 =
        wideToUtf8(executablePath.parent_path().wstring());
    if (!titleUtf8 || !executableUtf8 || !workingUtf8) {
        MessageBoxW(
            m_hwnd,
            L"Selected executable path is not valid Unicode.",
            L"HydraSeat",
            MB_OK | MB_ICONERROR);
        return;
    }

    if (!m_hostControl.launchGame(
            seatId,
            *titleUtf8,
            *executableUtf8,
            "",
            *workingUtf8,
            &error)) {
        const std::wstring message(error.begin(), error.end());
        MessageBoxW(
            m_hwnd,
            message.empty() ? L"The canonical host rejected the launch." : message.c_str(),
            L"HydraSeat launch failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    MessageBoxW(
        m_hwnd,
        L"Seat game launched under canonical host authority.",
        L"HydraSeat",
        MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK Win32App::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_INPUT && g_appInstance) {
        g_appInstance->m_inputRouter.handleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
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
    } else if (uMsg == WM_COMMAND) {
        int wmId = LOWORD(wParam);
        if (wmId == ID_BTN_REFRESH && g_appInstance) {
            g_appInstance->refreshHardware();
        } else if (wmId == ID_BTN_SAVE_PROF && g_appInstance) {
            g_appInstance->saveWorkspaceProfile();
        } else if (wmId == ID_BTN_LOAD_PROF && g_appInstance) {
            g_appInstance->loadWorkspaceProfile();
        } else if (wmId == ID_BTN_ISOLATION && g_appInstance) {
            g_appInstance->toggleSeatGame(1u);
        } else if (wmId == ID_BTN_LAUNCH && g_appInstance) {
            g_appInstance->toggleSeatGame(2u);
        }
    } else if (uMsg == WM_DESTROY) {
        KillTimer(hwnd, TIMER_FLASH_RESET);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int Win32App::run() {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (g_appInstance) {
            g_appInstance->m_inputRouter.processMessages();
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

} // namespace gui
} // namespace hydra
#endif
