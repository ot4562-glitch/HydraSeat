#include "hydra/gui_win32.hpp"
#include "hydra/game_runtime_requirement_resolver.hpp"
#include "hydra/hid_usage.hpp"
#include "hydra/launcher_user_state.hpp"
#include "hydra/launcher_win32.hpp"
#include "hydra/raw_input_utils.hpp"
#include "hydra/seat_assignment_projection.hpp"
#include "hydra/ui_localization.hpp"

#ifdef _WIN32
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <iterator>

#pragma comment(lib, "comctl32.lib")

namespace hydra {

// Diagnostic log file for debugging handle mapping
static std::wofstream g_diagLog;
constexpr std::streamoff kMaximumDiagnosticLogBytes = 1024 * 1024;

static bool diagnosticLoggingRequested() {
    wchar_t value[8]{};
    const DWORD length = GetEnvironmentVariableW(
        L"HYDRASEAT_DIAGNOSTICS", value, static_cast<DWORD>(std::size(value)));
    return length == 1u && value[0] == L'1';
}

static bool diagnosticBudgetAvailable() {
    if (!g_diagLog.is_open()) return false;
    const auto position = g_diagLog.tellp();
    if (position >= 0 && position < kMaximumDiagnosticLogBytes) return true;
    g_diagLog << L"[TRUNCATED] Diagnostic retention limit reached.\n";
    g_diagLog.close();
    return false;
}

static void openDiagLog() {
    if (!g_diagLog.is_open()) {
        g_diagLog.open("hydraseat_debug.log", std::ios::out | std::ios::trunc);
        if (g_diagLog.is_open()) {
            g_diagLog << L"=== HydraSeat explicit redacted diagnostic log ===" << std::endl;
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

static std::wstring wideFromUtf8(std::string_view value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                              value.data(), static_cast<int>(value.size()),
                                              nullptr, 0);
    if (required <= 0) return L"Runtime operation failed.";
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            value.data(), static_cast<int>(value.size()),
                            result.data(), required) != required) {
        return L"Runtime operation failed.";
    }
    return result;
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

static bool activateFocusedButtonWithEnter(HWND root, const MSG& message) noexcept {
    if (message.message != WM_KEYDOWN || message.wParam != VK_RETURN || root == nullptr) {
        return false;
    }
    HWND focused = GetFocus();
    if (focused == nullptr || (focused != root && IsChild(root, focused) == FALSE) ||
        IsWindowEnabled(focused) == FALSE) {
        return false;
    }
    wchar_t className[16]{};
    if (GetClassNameW(focused, className, static_cast<int>(std::size(className))) == 0 ||
        _wcsicmp(className, L"Button") != 0) {
        return false;
    }
    SendMessageW(focused, BM_CLICK, 0, 0);
    return true;
}

static std::wstring localizedText(ui::TextId id) {
    return std::wstring(ui::text(id, ui::systemLocale()));
}

static std::optional<std::filesystem::path> workspaceProfilePath(
    bool ensureWritableRoot, std::string& error) {
    const auto root = launcher_state::defaultUserStateRoot();
    if (!root) {
        error = "Local App Data is unavailable";
        return std::nullopt;
    }
    if (ensureWritableRoot) {
        const auto ready = launcher_state::ensureUserStateRoot(*root);
        if (!ready.succeeded()) {
            error = ready.message;
            return std::nullopt;
        }
    }
    return launcher_state::workspaceProfilePath(*root);
}

static std::wstring deviceCategoryText(DeviceCategory type) {
    switch (type) {
    case DeviceCategory::Display: return localizedText(ui::TextId::DeviceDisplay);
    case DeviceCategory::Keyboard: return localizedText(ui::TextId::DeviceKeyboard);
    case DeviceCategory::Mouse: return localizedText(ui::TextId::DeviceMouse);
    case DeviceCategory::Touchpad: return localizedText(ui::TextId::DeviceTouchpad);
    case DeviceCategory::Gamepad: return localizedText(ui::TextId::DeviceController);
    }
    return localizedText(ui::TextId::AvailableHardware);
}

static std::wstring partitionOwnerText(PartitionOwner owner) {
    const auto locale = ui::systemLocale();
    switch (owner) {
    case PartitionOwner::Pool:
        return std::wstring(ui::text(ui::TextId::AvailableHardware, locale));
    case PartitionOwner::Player1:
        return ui::formatOne(ui::TextId::SeatLabel, locale, L"1");
    case PartitionOwner::Player2:
        return ui::formatOne(ui::TextId::SeatLabel, locale, L"2");
    }
    return std::wstring(ui::text(ui::TextId::AvailableHardware, locale));
}

static std::wstring tileFriendlyName(const VisualDeviceTile& tile) {
    if (!tile.name.empty()) return tile.name;
    return tile.displayLabel.empty() ? deviceCategoryText(tile.type)
                                     : tile.displayLabel;
}

static std::wstring displayPresentationName(
    const DeviceInfo& device, std::size_t ordinal,
    const display::DisplayTopologySnapshot& topology) {
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const bool hasMonitorInfo = device.nativeHandle != 0 &&
        GetMonitorInfoW(reinterpret_cast<HMONITOR>(device.nativeHandle), &monitorInfo) != FALSE;

    const display::DisplayOutput* output = nullptr;
    if (hasMonitorInfo) {
        const auto sameGdiDevice = [&](const auto& candidate) {
            return _wcsicmp(candidate.gdiDeviceName.c_str(), monitorInfo.szDevice) == 0;
        };
        const auto found = std::find_if(
            topology.outputs.begin(), topology.outputs.end(), [&](const auto& candidate) {
                return candidate.active && candidate.attached && sameGdiDevice(candidate);
            });
        // Hardware Setup must never borrow friendly-name/mode metadata from a
        // stale or disabled path merely because it once used the same GDI name.
        // EnumDisplayMonitors already proves this HMONITOR is currently active;
        // if DisplayConfig is racing, use live MONITORINFOEX bounds instead.
        if (found != topology.outputs.end()) output = &*found;
    }

    std::wstring label;
    if (output != nullptr && !output->friendlyName.empty()) {
        label = output->friendlyName;
    } else if (!device.name.empty()) {
        label = device.name;
    } else {
        label = deviceCategoryText(DeviceCategory::Display) + L" " +
                std::to_wstring(ordinal + 1u);
    }

    if (hasMonitorInfo && monitorInfo.szDevice[0] != L'\0') {
        std::wstring displayNumber = monitorInfo.szDevice;
        constexpr std::wstring_view prefix = L"\\\\.\\";
        if (displayNumber.starts_with(prefix)) {
            displayNumber.erase(0, prefix.size());
        }
        label += L" · ";
        label += displayNumber;
    }

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool primary = false;
    if (output != nullptr) {
        width = output->mode.width != 0u
            ? output->mode.width
            : static_cast<std::uint32_t>((std::max)(0, output->desktopBounds.width()));
        height = output->mode.height != 0u
            ? output->mode.height
            : static_cast<std::uint32_t>((std::max)(0, output->desktopBounds.height()));
        primary = output->primary;
    } else if (hasMonitorInfo) {
        const LONG liveWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
        const LONG liveHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
        width = liveWidth > 0 ? static_cast<std::uint32_t>(liveWidth) : 0u;
        height = liveHeight > 0 ? static_cast<std::uint32_t>(liveHeight) : 0u;
        primary = (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0;
    }
    if (width != 0u && height != 0u) {
        label += L" · ";
        label += std::to_wstring(width);
        label += L"×";
        label += std::to_wstring(height);
    }
    if (primary) {
        label += L" · ";
        label += localizedText(ui::TextId::PrimaryDisplay);
    }
    return label;
}

static void updateTileAccessibleName(VisualDeviceTile& tile) {
    if (tile.hwndControl == nullptr) return;
    std::wstring label = deviceCategoryText(tile.type);
    label += L": ";
    label += tileFriendlyName(tile);
    label += L", ";
    label += partitionOwnerText(tile.owner);
    if (tile.isPrimaryDisplay) {
        label += L", ";
        label += localizedText(ui::TextId::PrimaryDisplay);
    }
    SetWindowTextW(tile.hwndControl, label.c_str());
    SendMessageW(tile.hwndControl, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

#define ID_BTN_REFRESH   1001
#define ID_BTN_LAUNCH    1003
#define ID_BTN_SAVE_PROF  1005
#define ID_BTN_LOAD_PROF  1006
#define ID_BTN_ISOLATION  1007
#define ID_BTN_GATE_C     1008
#define ID_BTN_STOP_SESSION 1010
#define ID_BTN_RECONFIGURE 1011
#define ID_BTN_GAME_LIBRARY 1012
#define ID_BTN_ASSIGN_SEAT1 1013
#define ID_BTN_ASSIGN_SEAT2 1014
#define ID_BTN_UNASSIGN_DEVICE 1015
#define ID_BTN_IDENTIFY_KEYBOARD 1016
#define ID_BTN_IDENTIFY_MOUSE 1017
#define ID_BTN_MAKE_PRIMARY 1018

#define TIMER_IDENTIFICATION 2001
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

void Win32App::selectDeviceTile(VisualDeviceTile* tile) {
    m_selectedDeviceTile = tile;
    if (tile != nullptr && tile->hwndControl != nullptr) {
        SetFocus(tile->hwndControl);
    }
    updateDeviceAssignmentUi();
}

void Win32App::assignSelectedDevice(PartitionOwner owner) {
    if (m_selectedDeviceTile == nullptr) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    (void)assignTileToOwner(m_selectedDeviceTile, owner);
}

bool Win32App::assignTileToOwner(VisualDeviceTile* tile, PartitionOwner owner) {
    if (tile == nullptr || !configurationEditingAllowed()) {
        MessageBeep(MB_ICONWARNING);
        return false;
    }

    const PartitionOwner oldOwner = tile->owner;
    const bool wasPrimary = tile->isPrimaryDisplay;
    if (oldOwner == owner) {
        layoutDeviceTiles();
        updateDeviceAssignmentUi();
        return true;
    }

    tile->owner = owner;
    if (tile->type == DeviceCategory::Display) {
        tile->isPrimaryDisplay = false;

        if (wasPrimary && oldOwner != PartitionOwner::Pool) {
            for (auto& candidate : m_deviceTiles) {
                if (candidate && candidate.get() != tile &&
                    candidate->type == DeviceCategory::Display &&
                    candidate->owner == oldOwner) {
                    candidate->isPrimaryDisplay = true;
                    updateTileAccessibleName(*candidate);
                    InvalidateRect(candidate->hwndControl, nullptr, FALSE);
                    break;
                }
            }
        }

        if (owner != PartitionOwner::Pool) {
            const bool ownerAlreadyHasPrimary = std::any_of(
                m_deviceTiles.begin(), m_deviceTiles.end(),
                [&](const auto& candidate) {
                    return candidate && candidate.get() != tile &&
                           candidate->type == DeviceCategory::Display &&
                           candidate->owner == owner && candidate->isPrimaryDisplay;
                });
            tile->isPrimaryDisplay = !ownerAlreadyHasPrimary;
        }
    }

    updateTileAccessibleName(*tile);
    m_profileOutOfSync = true;
    layoutDeviceTiles();
    updateDeviceAssignmentUi();
    updateControlSurfaceUi();
    return true;
}

void Win32App::makeSelectedDisplayPrimary() {
    auto* tile = m_selectedDeviceTile;
    if (tile == nullptr || tile->type != DeviceCategory::Display ||
        tile->owner == PartitionOwner::Pool || !configurationEditingAllowed()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (tile->isPrimaryDisplay) return;

    for (auto& candidate : m_deviceTiles) {
        if (candidate && candidate->type == DeviceCategory::Display &&
            candidate->owner == tile->owner) {
            candidate->isPrimaryDisplay = (candidate.get() == tile);
            updateTileAccessibleName(*candidate);
        }
    }
    m_profileOutOfSync = true;
    updateDeviceAssignmentUi();
    updateControlSurfaceUi();
}

void Win32App::updateDeviceAssignmentUi() {
    const bool editing = configurationEditingAllowed();
    const auto* tile = m_selectedDeviceTile;
    const auto locale = ui::systemLocale();
    const auto& identification = m_identificationCapture.snapshot();
    const bool identifying = identification.state == InputIdentificationState::Waiting;

    // The two destination columns are screens, not abstract internal Seat ids.
    // Once a display is assigned, use the display information users can actually
    // recognize as the column title and place its input devices underneath it.
    const auto updateDisplayGroup = [&](HWND group, PartitionOwner owner) {
        if (group == nullptr) return;
        const auto display = std::find_if(
            m_deviceTiles.begin(), m_deviceTiles.end(),
            [&](const auto& candidate) {
                return candidate && candidate->type == DeviceCategory::Display &&
                       candidate->owner == owner;
            });
        std::wstring label{ui::text(ui::TextId::DeviceDisplay, locale)};
        label += owner == PartitionOwner::Player1 ? L" 1: " : L" 2: ";
        if (display == m_deviceTiles.end()) {
            label += ui::text(ui::TextId::None, locale);
        } else {
            label += tileFriendlyName(**display);
        }
        SetWindowTextW(group, label.c_str());
    };
    updateDisplayGroup(m_p1Group, PartitionOwner::Player1);
    updateDisplayGroup(m_p2Group, PartitionOwner::Player2);

    if (m_selectedDeviceLabel != nullptr) {
        if (identifying) {
            const auto id = m_identificationKind == InputIdentificationKind::Keyboard
                ? ui::TextId::IdentificationWaitingKeyboard
                : ui::TextId::IdentificationWaitingMouse;
            SetWindowTextW(m_selectedDeviceLabel, ui::text(id, locale).data());
        } else if (identification.state == InputIdentificationState::TimedOut) {
            SetWindowTextW(
                m_selectedDeviceLabel,
                ui::text(ui::TextId::IdentificationTimedOut, locale).data());
        } else if (identification.state == InputIdentificationState::Rejected) {
            if (identification.failure == InputIdentificationFailure::AmbiguousSharedDevice ||
                identification.failure == InputIdentificationFailure::DeviceUnavailable ||
                identification.failure == InputIdentificationFailure::DeviceRemoved ||
                identification.failure == InputIdentificationFailure::MissingStableDeviceId) {
                std::wstring message{ui::text(ui::TextId::IdentificationNotMapped, locale)};
                message += L" ";
                message += ui::text(ui::TextId::IdentificationTimedOut, locale);
                SetWindowTextW(m_selectedDeviceLabel, message.c_str());
            } else {
                SetWindowTextW(
                    m_selectedDeviceLabel,
                    ui::text(ui::TextId::IdentificationTimedOut, locale).data());
            }
        } else if (tile == nullptr) {
            const std::wstring hint{ui::text(ui::TextId::AvailableHardwareHint, locale)};
            SetWindowTextW(m_selectedDeviceLabel, hint.c_str());
        } else {
            std::wstring label = tileFriendlyName(*tile);
            label += L" · ";
            label += partitionOwnerText(tile->owner);
            SetWindowTextW(m_selectedDeviceLabel, label.c_str());
        }
    }

    if (m_identifyKeyboardBtn != nullptr) {
        SetWindowTextW(m_identifyKeyboardBtn, ui::text(
            identifying ? ui::TextId::CancelIdentification : ui::TextId::IdentifyKeyboard,
            locale).data());
        EnableWindow(m_identifyKeyboardBtn, TRUE);
    }
    if (m_identifyMouseBtn != nullptr) {
        EnableWindow(m_identifyMouseBtn, identifying ? FALSE : TRUE);
    }
    if (m_assignSeat1Btn != nullptr) {
        EnableWindow(m_assignSeat1Btn,
                     editing && !identifying && tile != nullptr &&
                         tile->owner != PartitionOwner::Player1);
    }
    if (m_assignSeat2Btn != nullptr) {
        EnableWindow(m_assignSeat2Btn,
                     editing && !identifying && tile != nullptr &&
                         tile->owner != PartitionOwner::Player2);
    }
    if (m_unassignDeviceBtn != nullptr) {
        EnableWindow(m_unassignDeviceBtn,
                     editing && !identifying && tile != nullptr &&
                         tile->owner != PartitionOwner::Pool);
    }
    if (m_makePrimaryDisplayBtn != nullptr) {
        EnableWindow(m_makePrimaryDisplayBtn,
                     editing && !identifying && tile != nullptr &&
                         tile->type == DeviceCategory::Display &&
                         tile->owner != PartitionOwner::Pool && !tile->isPrimaryDisplay);
    }
}

void Win32App::beginInputIdentification(InputIdentificationKind kind) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    InputIdentificationRequest request;
    request.kind = kind;
    request.minimumSequenceExclusive = m_lastInputSequence;
    request.startedAtMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    request.timeoutMicros = 10'000'000u;
    m_identificationKind = kind;
    const auto& snapshot = m_identificationCapture.begin(request);
    if (snapshot.state != InputIdentificationState::Waiting) {
        updateDeviceAssignmentUi();
        return;
    }

    // Waiting for an intentional press/click is a new selection operation. Clear
    // the previous tile so stale assignment buttons cannot suggest that an older
    // device was just identified.
    selectDeviceTile(nullptr);
}

void Win32App::observeIdentificationInput(const RawInputEvent& event) {
    m_lastInputSequence = std::max(m_lastInputSequence, event.sequence);
    if (m_identificationCapture.snapshot().state != InputIdentificationState::Waiting) return;

    std::size_t matchingTiles = 0;
    VisualDeviceTile* matchedTile = nullptr;
    for (const auto& tile : m_deviceTiles) {
        if (!tile || tile->deviceId != event.deviceId ||
            !isTypeCompatible(event.rawDevType, tile->type)) {
            continue;
        }
        ++matchingTiles;
        matchedTile = tile.get();
    }

    const auto deviceStatus = matchingTiles == 0
        ? InputIdentificationDeviceStatus::Unavailable
        : (matchingTiles == 1 ? InputIdentificationDeviceStatus::Unique
                              : InputIdentificationDeviceStatus::AmbiguousShared);
    m_identificationCapture.observeInput(event, deviceStatus);
    const auto& snapshot = m_identificationCapture.snapshot();
    if (!snapshot.terminal()) return;

    if (snapshot.state == InputIdentificationState::Identified && snapshot.candidate &&
        matchingTiles == 1 && matchedTile != nullptr &&
        matchedTile->deviceId == snapshot.candidate->deviceId) {
        selectDeviceTile(matchedTile);
        return;
    }
    updateDeviceAssignmentUi();
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

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = Win32App::WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = L"HydraSeatMainWindowClass";

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    std::wstring windowTitle = L"HydraSeat - ";
    windowTitle += ui::text(ui::TextId::SeatHardwareSetup, ui::systemLocale());
    m_hwnd = CreateWindowExW(
        0, L"HydraSeatMainWindowClass",
        windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 860, 620,
        NULL, NULL, hInstance, NULL
    );

    if (!m_hwnd) return false;

    setupUI();
    // Hook diagnostics before device-change notifications begin. The main UI
    // remains a configuration surface; it does not claim zero-bleed isolation.
    m_inputRouter.setGlobalCallback([this](const RawInputEvent& evt) {
        observeIdentificationInput(evt);
    });
    if (!m_inputRouter.initialize(reinterpret_cast<uint64_t>(m_hwnd))) {
        return false;
    }
    if (diagnosticLoggingRequested()) openDiagLog();
    m_inputRouter.setDeviceChangeCallback([this](const RawInputDeviceChange& change) {
        if (diagnosticBudgetAvailable()) {
            g_diagLog << L"[DEVICE-CHANGE] "
                      << (change.kind == RawInputDeviceChangeKind::Arrival ? L"arrival" : L"removal")
                      << L" raw-type=" << change.device.rawDevType
                      << std::endl;
        }
        m_lastInputSequence = std::max(m_lastInputSequence, change.sequence);
        m_identificationCapture.observeDeviceChange(change);
        refreshHardware(true);
    });
    refreshHardware();

    (void)initializeControlSurface();

    SetTimer(m_hwnd, TIMER_IDENTIFICATION, 50, NULL);
    SetTimer(m_hwnd, TIMER_HOST_REFRESH, 1000, NULL);

    (void)nCmdShow;
    // The legacy device-composition console is an explicit Setup/Diagnostics
    // surface. HydraSeat.exe opens the game-first launcher from run().
    ShowWindow(m_hwnd, SW_HIDE);
    return true;
}

void Win32App::setupUI() {
    const auto locale = ui::systemLocale();
    const auto text = [locale](ui::TextId id) {
        return std::wstring(ui::text(id, locale));
    };
    const auto module = GetModuleHandleW(nullptr);
    const auto create = [&](const wchar_t* klass, const std::wstring& label, DWORD style,
                            int id, bool visible = true) {
        HWND control = CreateWindowExW(
            0, klass, label.c_str(), WS_CHILD | (visible ? WS_VISIBLE : 0) | style,
            0, 0, 1, 1, m_hwnd,
            id == 0 ? nullptr : reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            module, nullptr);
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return control;
    };

    m_headerLabel = create(L"STATIC", text(ui::TextId::SeatHardwareSetup), SS_LEFT, 0);
    m_saveProfileBtn = create(L"BUTTON", text(ui::TextId::ApplySetup),
                              WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_SAVE_PROF);
    m_loadProfileBtn = create(L"BUTTON", text(ui::TextId::ReloadSetup),
                              WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_LOAD_PROF);
    m_refreshBtn = create(L"BUTTON", text(ui::TextId::Refresh),
                          WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_REFRESH);
    m_gameLibraryBtn = create(L"BUTTON", text(ui::TextId::BackToGames),
                              WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_GAME_LIBRARY);

    m_runtimeStatusLabel = create(L"STATIC", L"", SS_LEFT, 0, false);
    m_stopSessionBtn = create(L"BUTTON", text(ui::TextId::ReturnToWindows),
                              WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_STOP_SESSION, false);
    m_reconfigureBtn = create(L"BUTTON", text(ui::TextId::Reconfigure),
                              WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_RECONFIGURE, false);

    m_deviceStatusLabel = create(L"STATIC", text(ui::TextId::DetectingHardware), SS_LEFT, 0);
    m_selectedDeviceLabel = create(L"STATIC", text(ui::TextId::AvailableHardwareHint), SS_LEFT, 0);
    m_assignSeat1Btn = create(L"BUTTON", ui::formatOne(ui::TextId::AssignToSeat, locale, L"1"),
                              WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_ASSIGN_SEAT1);
    m_assignSeat2Btn = create(L"BUTTON", ui::formatOne(ui::TextId::AssignToSeat, locale, L"2"),
                              WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_ASSIGN_SEAT2);
    m_unassignDeviceBtn = create(L"BUTTON", text(ui::TextId::UnassignDevice),
                                 WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_UNASSIGN_DEVICE);
    m_makePrimaryDisplayBtn = create(L"BUTTON", text(ui::TextId::PrimaryDisplay),
                                     WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_MAKE_PRIMARY);
    m_identifyKeyboardBtn = create(L"BUTTON", text(ui::TextId::IdentifyKeyboard),
                                   WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_IDENTIFY_KEYBOARD);
    m_identifyMouseBtn = create(L"BUTTON", text(ui::TextId::IdentifyMouse),
                                WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_IDENTIFY_MOUSE);

    m_poolGroup = create(L"BUTTON", text(ui::TextId::AvailableHardware), BS_GROUPBOX, 0);
    m_p1Group = create(L"BUTTON", ui::formatOne(ui::TextId::SeatHardwareLabel, locale, L"1"),
                       BS_GROUPBOX, 0);
    m_p2Group = create(L"BUTTON", ui::formatOne(ui::TextId::SeatHardwareLabel, locale, L"2"),
                       BS_GROUPBOX, 0);

    layoutHardwareSetup();
    updateDeviceAssignmentUi();
}

void Win32App::layoutHardwareSetup() {
    if (m_hwnd == nullptr || m_headerLabel == nullptr) return;
    RECT client{};
    GetClientRect(m_hwnd, &client);
    const int width = std::max(0, static_cast<int>(client.right - client.left));
    const int height = std::max(0, static_cast<int>(client.bottom - client.top));
    const int margin = 20;
    const int gap = 10;
    const int topButtonWidth = std::max(92, (width - margin * 2 - 420 - gap * 3) / 4);
    const int topButtonsWidth = topButtonWidth * 4 + gap * 3;
    const int topButtonsX = std::max(margin, width - margin - topButtonsWidth);
    const int titleWidth = std::max(180, topButtonsX - margin - gap);
    MoveWindow(m_headerLabel, margin, 14, titleWidth, 34, TRUE);
    MoveWindow(m_saveProfileBtn, topButtonsX, 14, topButtonWidth, 36, TRUE);
    MoveWindow(m_loadProfileBtn, topButtonsX + (topButtonWidth + gap), 14,
               topButtonWidth, 36, TRUE);
    MoveWindow(m_refreshBtn, topButtonsX + (topButtonWidth + gap) * 2, 14,
               topButtonWidth, 36, TRUE);
    MoveWindow(m_gameLibraryBtn, topButtonsX + (topButtonWidth + gap) * 3, 14,
               topButtonWidth, 36, TRUE);

    MoveWindow(m_runtimeStatusLabel, margin, 54, width - margin * 2, 24, TRUE);
    MoveWindow(m_deviceStatusLabel, margin, 80, width - margin * 2, 24, TRUE);
    MoveWindow(m_selectedDeviceLabel, margin, 106, width - margin * 2, 24, TRUE);

    const int actionCount = 6;
    const int actionWidth = std::max(80, (width - margin * 2 - gap * (actionCount - 1)) /
                                             actionCount);
    const HWND actions[actionCount] = {m_assignSeat1Btn, m_assignSeat2Btn,
                                       m_unassignDeviceBtn, m_makePrimaryDisplayBtn,
                                       m_identifyKeyboardBtn, m_identifyMouseBtn};
    for (int index = 0; index < actionCount; ++index) {
        MoveWindow(actions[index], margin + index * (actionWidth + gap), 134,
                   actionWidth, 38, TRUE);
    }

    const int groupTop = 184;
    const int groupWidth = std::max(120, (width - margin * 2 - gap * 2) / 3);
    const int groupHeight = std::max(120, height - groupTop - margin);
    MoveWindow(m_poolGroup, margin, groupTop, groupWidth, groupHeight, TRUE);
    MoveWindow(m_p1Group, margin + groupWidth + gap, groupTop,
               groupWidth, groupHeight, TRUE);
    MoveWindow(m_p2Group, margin + (groupWidth + gap) * 2, groupTop,
               groupWidth, groupHeight, TRUE);
    layoutDeviceTiles();
}

void Win32App::layoutDeviceTiles() {
    const auto layoutPartition = [this](PartitionOwner owner, HWND group) {
        constexpr int kInner = 12;
        constexpr int kRowHeight = 30;
        constexpr int kGap = 6;
        RECT groupRect{};
        if (group == nullptr || !GetWindowRect(group, &groupRect)) return;
        POINT origin{groupRect.left, groupRect.top};
        ScreenToClient(m_hwnd, &origin);
        const int width = std::max(80, static_cast<int>(groupRect.right - groupRect.left) - kInner * 2);
        int y = origin.y + 24;
        for (auto& tile : m_deviceTiles) {
            if (!tile || tile->hwndControl == nullptr || tile->owner != owner) continue;
            MoveWindow(tile->hwndControl, origin.x + kInner, y, width, kRowHeight, TRUE);
            y += kRowHeight + kGap;
        }
    };

    layoutPartition(PartitionOwner::Pool, m_poolGroup);
    layoutPartition(PartitionOwner::Player1, m_p1Group);
    layoutPartition(PartitionOwner::Player2, m_p2Group);
}

void Win32App::refreshHardware(bool preserveIdentification) {
    if (!preserveIdentification) {
        m_identificationCapture.reset();
    }

    WorkspaceManager pendingProfile;
    const bool restorePendingProfile = m_profileOutOfSync && !m_deviceTiles.empty() &&
                                       captureWorkspaceFromTiles(pendingProfile);

    m_displays = m_hardwareDetector.detectDisplays();
    m_keyboards = m_hardwareDetector.detectKeyboards();
    m_mice = m_hardwareDetector.detectMice();
    m_controllers = m_hardwareDetector.detectControllers();
    display::DisplayTopologyInventory displayInventory;
    const auto displayTopology = displayInventory.refresh();

    const auto connectedDeviceCount = m_displays.size() + m_keyboards.size() +
                                      m_mice.size() + m_controllers.size();
    const auto statusText = ui::formatOne(
        ui::TextId::ConnectedHardwareCount, ui::systemLocale(),
        std::to_wstring(connectedDeviceCount));
    SetWindowTextW(m_deviceStatusLabel, statusText.c_str());

    m_selectedDeviceTile = nullptr;
    for (auto& tilePtr : m_deviceTiles) {
        if (tilePtr && tilePtr->hwndControl) {
            DestroyWindow(tilePtr->hwndControl);
        }
    }
    m_deviceTiles.clear();

    // Displays (Default to Pool)
    for (size_t i = 0; i < m_displays.size(); ++i) {
        auto tile = std::make_unique<VisualDeviceTile>();
        tile->name = displayPresentationName(m_displays[i], i, displayTopology);
        tile->displayLabel = deviceCategoryText(DeviceCategory::Display) +
                             L" " + std::to_wstring(i + 1);
        tile->type = DeviceCategory::Display;
        tile->nativeHandle = m_displays[i].nativeHandle;
        tile->deviceId = m_displays[i].id;
        tile->devicePath = m_displays[i].devicePath;
        tile->owner = PartitionOwner::Pool;

        tile->hwndControl = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 260, 64, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

        updateTileAccessibleName(*tile);
        m_deviceTiles.push_back(std::move(tile));
    }

    // Keyboards (Default to Pool)
    for (size_t i = 0; i < m_keyboards.size(); ++i) {
        auto tile = std::make_unique<VisualDeviceTile>();
        tile->name = m_keyboards[i].name;
        tile->displayLabel = deviceCategoryText(DeviceCategory::Keyboard) +
                             L" " + std::to_wstring(i + 1);
        tile->type = DeviceCategory::Keyboard;
        tile->nativeHandle = m_keyboards[i].nativeHandle;
        tile->deviceId = m_keyboards[i].id;
        tile->devicePath = m_keyboards[i].devicePath;
        tile->owner = PartitionOwner::Pool;

        tile->hwndControl = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 126, 54, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

        updateTileAccessibleName(*tile);
        m_deviceTiles.push_back(std::move(tile));
    }

    // Mice / Touchpads (Default to Pool)
    for (size_t i = 0; i < m_mice.size(); ++i) {
        auto tile = std::make_unique<VisualDeviceTile>();
        tile->name = m_mice[i].name;

        std::wstring nameUpper = tile->name;
        for (auto& c : nameUpper) c = ::towupper(c);

        if (nameUpper.find(L"TOUCHPAD") != std::wstring::npos) {
            tile->type = DeviceCategory::Touchpad;
            tile->displayLabel = deviceCategoryText(DeviceCategory::Touchpad) +
                                 L" " + std::to_wstring(i + 1);
        } else {
            tile->type = DeviceCategory::Mouse;
            tile->displayLabel = deviceCategoryText(DeviceCategory::Mouse) +
                                 L" " + std::to_wstring(i + 1);
        }

        tile->nativeHandle = m_mice[i].nativeHandle;
        tile->deviceId = m_mice[i].id;
        tile->devicePath = m_mice[i].devicePath;
        tile->owner = PartitionOwner::Pool;

        tile->hwndControl = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 126, 54, m_hwnd, NULL, GetModuleHandle(NULL), NULL);

        updateTileAccessibleName(*tile);
        m_deviceTiles.push_back(std::move(tile));
    }

    // Controllers remain optional Seat hardware, but they use the same explicit
    // click/keyboard assignment path as displays and input devices.
    for (size_t i = 0; i < m_controllers.size(); ++i) {
        auto tile = std::make_unique<VisualDeviceTile>();
        tile->name = m_controllers[i].name;
        tile->displayLabel = deviceCategoryText(DeviceCategory::Gamepad) +
                             L" " + std::to_wstring(i + 1);
        tile->type = DeviceCategory::Gamepad;
        tile->nativeHandle = m_controllers[i].nativeHandle;
        tile->deviceId = m_controllers[i].id;
        tile->devicePath = m_controllers[i].devicePath;
        tile->owner = PartitionOwner::Pool;
        tile->hwndControl = CreateWindowExW(
            0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 126, 54, m_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        updateTileAccessibleName(*tile);
        m_deviceTiles.push_back(std::move(tile));
    }

    if (restorePendingProfile) {
        // Preserve unsaved local edits across Refresh/hot-plug without claiming
        // that those edits have already been committed to the host or disk.
        applyWorkspaceProfileToTiles(&pendingProfile);
    } else if (m_workspaceManager.getAllSeats().empty()) {
        layoutDeviceTiles();
    } else {
        // Rebuilds caused by Refresh or hot-plug must not erase the persisted
        // Seat presentation. Newly discovered devices remain in the Pool.
        applyWorkspaceProfileToTiles();
    }
    updateDeviceAssignmentUi();
}

bool Win32App::captureWorkspaceFromTiles(WorkspaceManager& candidate) const {
    std::vector<VisibleSeatDeviceAssignment> visible;
    visible.reserve(m_deviceTiles.size());
    for (const auto& tile : m_deviceTiles) {
        if (!tile) continue;
        VisibleSeatDeviceAssignment assignment;
        switch (tile->type) {
        case DeviceCategory::Display: assignment.type = SeatDeviceType::Display; break;
        case DeviceCategory::Keyboard: assignment.type = SeatDeviceType::Keyboard; break;
        case DeviceCategory::Mouse:
        case DeviceCategory::Touchpad: assignment.type = SeatDeviceType::Mouse; break;
        case DeviceCategory::Gamepad: assignment.type = SeatDeviceType::Controller; break;
        }
        assignment.stableId = tile->deviceId.empty() ? tile->devicePath : tile->deviceId;
        assignment.seatId = tile->owner == PartitionOwner::Player1 ? 1u
                          : tile->owner == PartitionOwner::Player2 ? 2u : 0u;
        assignment.primaryDisplay = tile->type == DeviceCategory::Display &&
                                    tile->isPrimaryDisplay;
        visible.push_back(std::move(assignment));
    }
    return projectVisibleSeatAssignments(m_workspaceManager, visible, candidate);
}

void Win32App::saveWorkspaceProfile() {
    if (!configurationEditingAllowed()) {
        MessageBoxW(m_hwnd,
            L"Configuration changes are disabled until the background host is connected and the session is verified Idle. Use Reconfigure first for an active session.",
            L"HydraSeat Runtime", MB_OK | MB_ICONWARNING);
        return;
    }
    WorkspaceManager candidate;
    if (!captureWorkspaceFromTiles(candidate)) {
        MessageBoxW(m_hwnd,
            L"One or more devices could not be assigned. A physical device may only belong to one Seat unless explicitly shareable.",
            L"HydraSeat Seat Manager", MB_OK | MB_ICONERROR);
        return;
    }

    std::string profileError;
    const auto profilePath = workspaceProfilePath(true, profileError);
    if (!profilePath || !candidate.saveToFile(*profilePath)) {
        const auto message = localizedText(ui::TextId::WorkspaceProfileSaveFailed);
        const auto title = localizedText(ui::TextId::SeatHardwareSetup);
        MessageBoxW(m_hwnd, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
        return;
    }

    m_workspaceManager = std::move(candidate);
    applyWorkspaceProfileToTiles();
    const bool hostCanAcceptProfile = m_hostClient.connected() &&
                                      m_hostClient.role() == hostipc::ClientRole::Control;
    if (hostCanAcceptProfile && !applyCurrentProfileToHost(false)) {
        const auto message = localizedText(ui::TextId::WorkspaceProfileApplyFailed);
        const auto title = localizedText(ui::TextId::SeatHardwareSetup);
        MessageBoxW(m_hwnd, message.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    if (!hostCanAcceptProfile) {
        m_profileOutOfSync = true;
        updateControlSurfaceUi();
    }

    const auto message = localizedText(hostCanAcceptProfile
        ? ui::TextId::WorkspaceProfileSaved
        : ui::TextId::WorkspaceProfileSavedNeedsReconfigure);
    const auto title = localizedText(ui::TextId::SeatHardwareSetup);
    MessageBoxW(m_hwnd, message.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

void Win32App::applyWorkspaceProfileToTiles(const WorkspaceManager* source) {
    const WorkspaceManager& profile = source == nullptr ? m_workspaceManager : *source;
    for (auto& tile : m_deviceTiles) {
        if (!tile) continue;
        tile->owner = PartitionOwner::Pool;
        tile->isPrimaryDisplay = false;
    }

    const auto equalStableId = [](std::wstring_view left, std::wstring_view right) {
        return left.size() == right.size() &&
               std::equal(left.begin(), left.end(), right.begin(),
                   [](wchar_t a, wchar_t b) {
                       return std::towlower(a) == std::towlower(b);
                   });
    };
    const auto containsId = [&](const std::vector<std::wstring>& ids, std::wstring_view id) {
        return std::any_of(ids.begin(), ids.end(),
            [&](const std::wstring& candidate) { return equalStableId(candidate, id); });
    };

    for (const auto& seat : profile.getAllSeats()) {
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
                    seat.primaryDisplayId && equalStableId(*seat.primaryDisplayId, stableId);
            }
        }
    }
    for (auto& tile : m_deviceTiles) {
        if (tile) updateTileAccessibleName(*tile);
    }
    layoutDeviceTiles();
    updateDeviceAssignmentUi();
}

void Win32App::loadWorkspaceProfile() {
    if (!configurationEditingAllowed()) {
        MessageBoxW(m_hwnd,
            L"Loading/editing configuration is disabled until the runtime is verified Idle. Use Reconfigure first for an active session.",
            L"HydraSeat Runtime", MB_OK | MB_ICONWARNING);
        return;
    }
    std::string profileError;
    const auto profilePath = workspaceProfilePath(false, profileError);
    if (!profilePath || !m_workspaceManager.loadFromFile(*profilePath)) {
        const auto message = localizedText(ui::TextId::WorkspaceProfileLoadFailed);
        const auto title = localizedText(ui::TextId::SeatHardwareSetup);
        MessageBoxW(m_hwnd, message.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    applyWorkspaceProfileToTiles();

    const bool hostCanAcceptProfile = m_hostClient.connected() &&
                                      m_hostClient.role() == hostipc::ClientRole::Control;
    if (hostCanAcceptProfile && !applyCurrentProfileToHost(false)) {
        const auto message = localizedText(ui::TextId::WorkspaceProfileLoadApplyFailed);
        const auto title = localizedText(ui::TextId::SeatHardwareSetup);
        MessageBoxW(m_hwnd, message.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    if (!hostCanAcceptProfile) {
        m_profileOutOfSync = true;
        updateControlSurfaceUi();
    }

    const auto message = localizedText(hostCanAcceptProfile
        ? ui::TextId::WorkspaceProfileLoaded
        : ui::TextId::WorkspaceProfileLoadedNeedsReconfigure);
    const auto title = localizedText(ui::TextId::SeatHardwareSetup);
    MessageBoxW(m_hwnd, message.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

bool Win32App::configurationEditingAllowed() const noexcept {
    const auto& state = m_controlSurfaceModel.state();
    return state.hostConnected && state.runtimeStateKnown && state.sessionPhase &&
           *state.sessionPhase == runtime::SeatSessionPhase::Idle;
}

bool Win32App::bootstrapBackgroundHost(std::string& error) const {
    wchar_t modulePath[32768]{};
    const DWORD moduleLength = GetModuleFileNameW(
        nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (moduleLength == 0u || moduleLength >= std::size(modulePath)) {
        error = "unable to resolve HydraSeat executable directory";
        return false;
    }

    std::wstring directory(modulePath, moduleLength);
    const auto separator = directory.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        error = "HydraSeat executable directory has no parent separator";
        return false;
    }
    directory.resize(separator + 1u);
    const std::wstring hostPath = directory + L"hydra_host.exe";
    const DWORD attributes = GetFileAttributesW(hostPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
        error = "hydra_host.exe is not installed next to HydraSeat.exe";
        return false;
    }

    // Start an empty authority and apply the validated local profile over IPC.
    // Feeding the profile path to the child here would make one malformed/stale
    // file prevent first-run recovery and configuration entirely.
    std::wstring commandLine = L"\"" + hostPath + L"\" --serve";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(hostPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process) == FALSE) {
        error = "unable to start hydra_host.exe (Win32 error " +
                std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool Win32App::connectBackgroundHost(SeatId requestedSeat, bool allowBootstrap,
                                     std::string& error) {
    const auto tryControl = [&](SeatId seatId, std::uint32_t timeoutMs) {
        m_hostClient.close();
        return m_hostClient.connectForSeat(
            hostipc::ClientRole::Control, seatId, timeoutMs, &error);
    };

    if (tryControl(requestedSeat, 300u)) return true;

    // If a host exists but the local Management Seat hint is stale, discover the
    // authoritative seat read-only and retry Control instead of spawning another host.
    m_hostClient.close();
    if (m_hostClient.connect(hostipc::ClientRole::ReadOnly, 300u, &error)) {
        const auto snapshot = m_hostClient.getSnapshot(300u, &error);
        if (snapshot) {
            const SeatId authoritySeat = snapshot->managementSeatId;
            if (tryControl(authoritySeat, 300u)) return true;
            m_hostClient.close();
            if (m_hostClient.connect(hostipc::ClientRole::ReadOnly, 300u, &error)) return true;
        } else {
            m_hostClient.close();
        }
        return false;
    }

    if (!allowBootstrap || !bootstrapBackgroundHost(error)) return false;

    // A newly bootstrapped empty host starts with Management Seat 1. Try the
    // persisted hint first, then Seat 1 so a saved Management Seat 2 profile can
    // still be applied transactionally after connection.
    constexpr unsigned kAttempts = 20u;
    for (unsigned attempt = 0u; attempt < kAttempts; ++attempt) {
        Sleep(100u);
        if (tryControl(requestedSeat, 150u)) return true;
        if (requestedSeat != 1u && tryControl(1u, 150u)) return true;
    }
    error = "background host was started but did not accept a control connection";
    m_hostClient.close();
    return false;
}

bool Win32App::initializeControlSurface() {
    std::string profileError;
    const auto profilePath = workspaceProfilePath(false, profileError);
    if (profilePath && m_workspaceManager.loadFromFile(*profilePath)) {
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
    if (!connectBackgroundHost(requestedSeat, true, error)) {
        m_controlSurfaceModel.markHostDisconnected(error);
        m_sessionControlTransition.markHostDisconnected(error);
        updateControlSurfaceUi();
        applyManagementSeatPlacement();
        return true;
    }

    const auto snapshot = m_hostClient.getSnapshot(1000u, &error);
    if (!snapshot) {
        m_hostClient.close();
        m_controlSurfaceModel.markHostDisconnected(error);
        m_sessionControlTransition.markHostDisconnected(error);
    } else {
        m_controlSurfaceModel.setControlContext(
            snapshot->managementSeatId, true,
            m_hostClient.role() == hostipc::ClientRole::Control);
        m_controlSurfaceModel.observeHostSnapshot(*snapshot);
        m_sessionControlTransition.observeSnapshot(*snapshot);

        if (m_hostClient.role() == hostipc::ClientRole::Control &&
            !snapshot->profileLoaded && !m_workspaceManager.getAllSeats().empty()) {
            (void)applyCurrentProfileToHost(false);
        }
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
        if (!connectBackgroundHost(requestedSeat, false, connectError)) {
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
        m_controlSurfaceModel.setControlContext(
            snapshot->managementSeatId, true,
            m_hostClient.role() == hostipc::ClientRole::Control);
        m_controlSurfaceModel.observeHostSnapshot(*snapshot);
        m_sessionControlTransition.observeSnapshot(*snapshot);
    }
    updateControlSurfaceUi();
}

void Win32App::updateControlSurfaceUi() {
    const auto& state = m_controlSurfaceModel.state();
    if (m_runtimeStatusLabel != nullptr) {
        // Host/control internals are diagnostics, not hardware-setup concepts.
        // Keep them off this user-facing screen; actionable runtime state is
        // represented by the recovery/reconfigure buttons when those actions exist.
        ShowWindow(m_runtimeStatusLabel, SW_HIDE);
    }
    if (m_stopSessionBtn != nullptr) {
        const BOOL available = state.actions.stopAndReturnToWindows ? TRUE : FALSE;
        EnableWindow(m_stopSessionBtn, available);
        ShowWindow(m_stopSessionBtn, available ? SW_SHOW : SW_HIDE);
    }
    if (m_reconfigureBtn != nullptr) {
        const BOOL available = state.actions.reconfigure ? TRUE : FALSE;
        EnableWindow(m_reconfigureBtn, available);
        ShowWindow(m_reconfigureBtn, available ? SW_SHOW : SW_HIDE);
    }
    const BOOL editing = configurationEditingAllowed() ? TRUE : FALSE;
    if (m_saveProfileBtn != nullptr) EnableWindow(m_saveProfileBtn, editing);
    if (m_loadProfileBtn != nullptr) EnableWindow(m_loadProfileBtn, editing);
    updateDeviceAssignmentUi();
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
    placementConfig.preferredWidth = 860;
    placementConfig.preferredHeight = 620;
    const auto placement = control::resolveControlSurfacePlacement(
        placementConfig, layouts.groups, topology);
    if (!placement.valid) return;
    SetWindowPos(m_hwnd, nullptr, placement.windowRect.left, placement.windowRect.top,
                 placement.windowRect.width(), placement.windowRect.height(),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

bool Win32App::applyCurrentProfileToHost(bool showErrors) {
    if (!m_hostClient.connected() || m_hostClient.role() != hostipc::ClientRole::Control) {
        m_profileOutOfSync = true;
        updateControlSurfaceUi();
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
        m_profileOutOfSync = true;
        if (showErrors) {
            const std::wstring message = L"The edited profile was not accepted by the background host. The active runtime was not changed.";
            MessageBoxW(m_hwnd, message.c_str(), L"HydraSeat Reconfigure",
                        MB_OK | MB_ICONERROR);
        }
        refreshControlSurface();
        return false;
    }
    m_profileOutOfSync = false;
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

launcher_ui::LauncherActivationResult Win32App::activateLauncherPlan(
    const plan::ProviderAwareLaunchPlan& plan,
    const hostipc::ProfilePayload& profile) {
    using Status = launcher_ui::LauncherActivationStatus;
    const auto failure = [](Status status, std::wstring message) {
        return launcher_ui::LauncherActivationResult{status, std::move(message)};
    };
    const auto runtimeStatus = [](runtime::RuntimeResultCode code) {
        return (code == runtime::RuntimeResultCode::RecoveryRequired ||
                code == runtime::RuntimeResultCode::RollbackFailure)
            ? Status::RecoveryRequired : Status::Failed;
    };
    const auto seatStatus = [](runtime::SeatGameResultCode code) {
        return code == runtime::SeatGameResultCode::RecoveryRequired
            ? Status::RecoveryRequired : Status::Failed;
    };

    if (plan.seats.empty() || plan.seats.size() > runtime::kV1MaximumActiveSeats ||
        profile.seats.empty() || profile.seats.size() > runtime::kV1MaximumActiveSeats) {
        return failure(Status::InvalidData, L"The launch plan or Seat profile is outside the v1 two-Seat contract.");
    }
    std::vector<SeatId> planSeats;
    planSeats.reserve(plan.seats.size());
    for (const auto& seatPlan : plan.seats) {
        if (seatPlan.seatId == 0u ||
            std::find(planSeats.begin(), planSeats.end(), seatPlan.seatId) != planSeats.end() ||
            std::none_of(profile.seats.begin(), profile.seats.end(),
                         [&](const auto& seat) { return seat.seatId == seatPlan.seatId; })) {
            return failure(Status::InvalidData, L"The immutable launch plan does not match the current Seat profile.");
        }
        planSeats.push_back(seatPlan.seatId);
    }

    std::string error;
    if (!connectBackgroundHost(profile.managementSeatId, true, error) ||
        m_hostClient.role() != hostipc::ClientRole::Control) {
        return failure(Status::Unavailable,
                       error.empty() ? L"The background runtime control channel is unavailable."
                                     : wideFromUtf8(error));
    }

    const auto before = m_hostClient.getSnapshot(1000u, &error);
    if (!before) {
        return failure(Status::TimedOut,
                       error.empty() ? L"The background runtime did not return a fresh snapshot."
                                     : wideFromUtf8(error));
    }
    if (before->sessionPhase == runtime::SeatSessionPhase::RecoveryRequired) {
        return failure(Status::RecoveryRequired,
                       L"Runtime recovery is required before another game can start.");
    }
    if (before->sessionPhase != runtime::SeatSessionPhase::Idle) {
        return failure(Status::PreflightFailed,
                       L"Return the current split session to Windows before starting this plan.");
    }

    const auto applied = m_hostClient.applyProfile(profile, 2500u, &error);
    if (!applied || !applied->succeeded()) {
        const Status status = applied ? runtimeStatus(applied->code) : Status::TimedOut;
        const std::string detail = applied && !applied->diagnostic.empty()
            ? applied->diagnostic : error;
        return failure(status, detail.empty()
            ? L"The background runtime rejected the current Seat profile."
            : wideFromUtf8(detail));
    }

    const auto planned = m_hostClient.command(hostipc::MessageType::PlanSession, 2500u, &error);
    if (!planned || !planned->succeeded()) {
        const Status status = planned ? runtimeStatus(planned->code) : Status::TimedOut;
        const std::string detail = planned && !planned->diagnostic.empty()
            ? planned->diagnostic : error;
        return failure(status, detail.empty()
            ? L"Whole-machine runtime preflight rejected the launch."
            : wideFromUtf8(detail));
    }

    const auto started = m_hostClient.command(hostipc::MessageType::StartSession, 5000u, &error);
    if (!started || !started->succeeded()) {
        const Status status = started ? runtimeStatus(started->code) : Status::TimedOut;
        const std::string detail = started && !started->diagnostic.empty()
            ? started->diagnostic : error;
        (void)m_hostClient.command(hostipc::MessageType::StopAndReturnToWindows, 5000u, nullptr);
        refreshControlSurface();
        return failure(status, detail.empty()
            ? L"The split runtime could not enter Active state."
            : wideFromUtf8(detail));
    }

    std::vector<SeatId> assignedSeats;
    bool rollbackVerified = true;
    const auto rollback = [&]() {
        for (auto it = assignedSeats.rbegin(); it != assignedSeats.rend(); ++it) {
            hostipc::SeatGameCommandPayload payload;
            payload.seatId = *it;
            const auto stopped = m_hostClient.seatGameCommand(
                hostipc::MessageType::StopSeatGame, payload, 5000u, nullptr);
            rollbackVerified = rollbackVerified && stopped && stopped->succeeded();
        }
        const auto returned = m_hostClient.command(
            hostipc::MessageType::StopAndReturnToWindows, 5000u, nullptr);
        rollbackVerified = rollbackVerified && returned && returned->succeeded();
        refreshControlSurface();
    };

    for (const auto& seatPlan : plan.seats) {
        hostipc::SeatGameCommandPayload payload;
        payload.seatId = seatPlan.seatId;
        payload.binding = runtime::SeatGameBinding{seatPlan.playerId, seatPlan.gameId};
        const auto assigned = m_hostClient.seatGameCommand(
            hostipc::MessageType::AssignSeatGame, payload, 2500u, &error);
        if (!assigned || !assigned->succeeded()) {
            const Status status = assigned ? seatStatus(assigned->code) : Status::TimedOut;
            const std::string detail = assigned && !assigned->diagnostic.empty()
                ? assigned->diagnostic : error;
            rollback();
            return failure(rollbackVerified ? status : Status::RecoveryRequired,
                           !rollbackVerified
                               ? L"Seat assignment failed and rollback could not be fully verified."
                               : (detail.empty() ? L"The runtime rejected a Seat game assignment."
                                                 : wideFromUtf8(detail)));
        }
        assignedSeats.push_back(seatPlan.seatId);
    }

    // The UI never launches provider targets itself. StartSeatGame succeeds only
    // when the runtime owns a previously installed typed provider plan for that
    // exact Seat. Until Agent 3 supplies that production installer/protocol, this
    // call fails closed and the rollback below returns the machine to Windows.
    for (const auto& seatPlan : plan.seats) {
        hostipc::SeatGameCommandPayload payload;
        payload.seatId = seatPlan.seatId;
        const auto seatStarted = m_hostClient.seatGameCommand(
            hostipc::MessageType::StartSeatGame, payload, 10000u, &error);
        if (!seatStarted || !seatStarted->succeeded()) {
            const Status status = seatStarted ? seatStatus(seatStarted->code) : Status::TimedOut;
            const std::string detail = seatStarted && !seatStarted->diagnostic.empty()
                ? seatStarted->diagnostic : error;
            rollback();
            return failure(rollbackVerified ? status : Status::RecoveryRequired,
                           !rollbackVerified
                               ? L"Game start failed and rollback could not be fully verified."
                               : (detail.empty() ? L"The runtime could not start the validated Seat game plan."
                                                 : wideFromUtf8(detail)));
        }
    }

    refreshControlSurface();
    return failure(Status::Success, L"Starting the selected game on the assigned Seat(s)...");
}

launcher_ui::LauncherExitAction Win32App::openGameLibrary() {
    WorkspaceManager candidate;
    if (!captureWorkspaceFromTiles(candidate)) {
        // Do not silently fall back to a stale persisted assignment after an
        // invalid edit. Present two empty Seats so launcher preflight fails
        // closed and the user can return to Setup / Diagnostics to repair it.
        candidate = WorkspaceManager{};
        (void)candidate.createSeat(L"Seat 1");
        (void)candidate.createSeat(L"Seat 2");
    }

    hostipc::ProfilePayload hostProfile;
    hostProfile.managementSeatId = candidate.managementSeatId();
    hostProfile.seats = candidate.getAllSeats();

    profile::SeatConfigDocument document;
    document.managementSeatId = hostProfile.managementSeatId;
    for (const auto& source : hostProfile.seats) {
        if (source.seatId != 1u && source.seatId != 2u) continue;
        profile::PersistedSeatConfig seat;
        seat.seatId = source.seatId;
        seat.name = source.name;
        seat.displayIds = source.displayIds;
        seat.primaryDisplayId = source.primaryDisplayId;
        seat.keyboardIds = source.keyboardIds;
        seat.mouseIds = source.mouseIds;
        seat.controllerIds = source.controllerIds;
        seat.audioOutputEndpointId = source.audioOutputEndpointId;
        seat.audioInputEndpointId = source.audioInputEndpointId;
        seat.active = source.active;
        document.seats.push_back(std::move(seat));
    }

    // The launcher receives only the bounded requirement projection. It never
    // receives trusted provider/evidence provenance and it never manufactures a
    // requirement when the fixed local authority is missing, corrupt, or stale.
    std::vector<plan::GameRuntimeRequirement> requirementProjection;
    const auto trustedRequirements =
        requirement::makeDefaultProductionTrustedRequirementSource();
    if (trustedRequirements) {
        (void)requirement::resolveCurrentRequirementProjection(
            *trustedRequirements, requirementProjection);
    }

    auto activate = [this, hostProfile = std::move(hostProfile)](
                        const plan::ProviderAwareLaunchPlan& launchPlan) {
        return activateLauncherPlan(launchPlan, hostProfile);
    };
    // Games is the product's primary user-facing window. Hardware Setup is hidden
    // before this call, so making Games an owned window of that hidden HWND leaves
    // Windows (and accessibility/automation clients) with no real primary window.
    // Keep lifecycle coordination explicit instead of relying on hidden-window ownership.
    return launcher_ui::showLauncherWindow(
        nullptr, std::move(document), std::move(requirementProjection),
        std::move(activate), &m_launcherNavigationState);
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
        if (HWND launcher = FindWindowW(L"HydraSeatGameLibraryWindow", nullptr);
            launcher != nullptr) {
            ShowWindow(launcher, SW_RESTORE);
            SetForegroundWindow(launcher);
            return 0;
        }
        g_appInstance->applyManagementSeatPlacement();
        (void)g_appInstance->openGameLibrary();
        return 0;
    }
    if (uMsg == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        UINT dpi = GetDpiForWindow(hwnd);
        if (dpi == 0u) dpi = 96u;
        limits->ptMinTrackSize.x = MulDiv(860, static_cast<int>(dpi), 96);
        limits->ptMinTrackSize.y = MulDiv(620, static_cast<int>(dpi), 96);
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
        if (wParam == VK_ESCAPE &&
            g_appInstance->m_identificationCapture.snapshot().state ==
                InputIdentificationState::Waiting) {
            g_appInstance->m_identificationCapture.cancel();
            g_appInstance->updateDeviceAssignmentUi();
            return 0;
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    } else if (uMsg == WM_TIMER && wParam == TIMER_IDENTIFICATION && g_appInstance) {
        const auto steadyNow = std::chrono::steady_clock::now().time_since_epoch();
        const auto identificationStateBefore =
            g_appInstance->m_identificationCapture.snapshot().state;
        g_appInstance->m_identificationCapture.advanceTime(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(steadyNow).count()));
        if (g_appInstance->m_identificationCapture.snapshot().state !=
            identificationStateBefore) {
            g_appInstance->updateDeviceAssignmentUi();
        }
        return 0;
    } else if (uMsg == WM_TIMER && wParam == TIMER_HOST_REFRESH && g_appInstance) {
        g_appInstance->refreshControlSurface();
        return 0;
    } else if (uMsg == WM_SIZE && g_appInstance) {
        g_appInstance->layoutHardwareSetup();
        return 0;
    } else if (uMsg == WM_COMMAND) {
        const int wmId = LOWORD(wParam);
        if (g_appInstance && HIWORD(wParam) == BN_CLICKED && lParam != 0) {
            HWND source = reinterpret_cast<HWND>(lParam);
            const auto selected = std::find_if(
                g_appInstance->m_deviceTiles.begin(), g_appInstance->m_deviceTiles.end(),
                [source](const auto& tile) { return tile && tile->hwndControl == source; });
            if (selected != g_appInstance->m_deviceTiles.end()) {
                g_appInstance->selectDeviceTile(selected->get());
                return 0;
            }
        }
        if (wmId == ID_BTN_REFRESH && g_appInstance) {
            g_appInstance->refreshHardware();
        } else if (wmId == ID_BTN_SAVE_PROF && g_appInstance) {
            g_appInstance->saveWorkspaceProfile();
        } else if (wmId == ID_BTN_LOAD_PROF && g_appInstance) {
            g_appInstance->loadWorkspaceProfile();
        } else if (wmId == ID_BTN_STOP_SESSION && g_appInstance) {
            g_appInstance->stopSessionAndReturnToWindows();
        } else if (wmId == ID_BTN_RECONFIGURE && g_appInstance) {
            g_appInstance->beginReconfigure();
        } else if (wmId == ID_BTN_GAME_LIBRARY && g_appInstance) {
            // This control means what it says: leave the hardware screen. The
            // previous implementation opened another modal launcher while leaving
            // this window visible underneath, so "Back to Games" appeared broken.
            // Identification/selection are view state, not durable Seat authority.
            g_appInstance->m_identificationCapture.reset();
            g_appInstance->selectDeviceTile(nullptr);
            ShowWindow(g_appInstance->m_hwnd, SW_HIDE);
            const auto action = g_appInstance->openGameLibrary();
            if (action == launcher_ui::LauncherExitAction::OpenHardwareSetup) {
                g_appInstance->refreshHardware();
                ShowWindow(g_appInstance->m_hwnd, SW_SHOW);
                UpdateWindow(g_appInstance->m_hwnd);
                SetForegroundWindow(g_appInstance->m_hwnd);
            } else {
                DestroyWindow(g_appInstance->m_hwnd);
            }
        } else if (wmId == ID_BTN_ASSIGN_SEAT1 && g_appInstance) {
            g_appInstance->assignSelectedDevice(PartitionOwner::Player1);
        } else if (wmId == ID_BTN_ASSIGN_SEAT2 && g_appInstance) {
            g_appInstance->assignSelectedDevice(PartitionOwner::Player2);
        } else if (wmId == ID_BTN_UNASSIGN_DEVICE && g_appInstance) {
            g_appInstance->assignSelectedDevice(PartitionOwner::Pool);
        } else if (wmId == ID_BTN_MAKE_PRIMARY && g_appInstance) {
            g_appInstance->makeSelectedDisplayPrimary();
        } else if (wmId == ID_BTN_IDENTIFY_KEYBOARD && g_appInstance) {
            if (g_appInstance->m_identificationCapture.snapshot().state ==
                InputIdentificationState::Waiting) {
                g_appInstance->m_identificationCapture.cancel();
                g_appInstance->updateDeviceAssignmentUi();
            } else {
                g_appInstance->beginInputIdentification(InputIdentificationKind::Keyboard);
            }
        } else if (wmId == ID_BTN_IDENTIFY_MOUSE && g_appInstance) {
            g_appInstance->beginInputIdentification(InputIdentificationKind::Mouse);
        }
    } else if (uMsg == WM_DESTROY) {
        KillTimer(hwnd, TIMER_IDENTIFICATION);
        KillTimer(hwnd, TIMER_HOST_REFRESH);
        if (g_appInstance) g_appInstance->m_hostClient.close();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int Win32App::run() {
    if (m_duplicateLaunch) return 0;
    const auto launcherAction = openGameLibrary();
    if (launcherAction == launcher_ui::LauncherExitAction::OpenHardwareSetup) {
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
        SetForegroundWindow(m_hwnd);
    } else {
        DestroyWindow(m_hwnd);
    }
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (activateFocusedButtonWithEnter(m_hwnd, msg)) continue;
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && m_hwnd != nullptr &&
            (msg.hwnd == m_hwnd || IsChild(m_hwnd, msg.hwnd))) {
            HWND current = GetFocus();
            if (current == m_hwnd || !IsChild(m_hwnd, current)) current = nullptr;
            HWND next = GetNextDlgTabItem(
                m_hwnd, current, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            if (next != nullptr) {
                SetFocus(next);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

} // namespace gui
} // namespace hydra
#endif
