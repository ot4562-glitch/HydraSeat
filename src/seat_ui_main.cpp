#include "hydra/display_topology.hpp"
#include "hydra/seat_display_layout.hpp"
#include "hydra/seat_host_client.hpp"
#include "hydra/seat_hotkey_model.hpp"
#include "hydra/seat_launcher_model.hpp"
#include "hydra/seat_notification_model.hpp"
#include "hydra/ui_localization.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

using hydra::SeatConfig;
using hydra::SeatId;
using hydra::runtime::HostLifecyclePhase;
using hydra::runtime::HostRuntimeSnapshot;
using hydra::runtime::RuntimeSessionId;
using hydra::runtime::SeatGameBinding;
using hydra::runtime::SeatGamePhase;
using hydra::runtime::SeatGameState;
using hydra::runtime::SeatRuntimeState;
using hydra::runtime::SeatSessionPhase;
using hydra::seatui::SeatLauncherModel;
using hydra::seatui::SeatLauncherPhase;

struct Options {
    SeatId seatId{0};
    bool controlledSelfTest{false};
    bool hostSelfTest{false};
    SeatGamePhase fixturePhase{SeatGamePhase::Idle};
    std::string expectedPhase;
    std::filesystem::path reportPath;
    std::uint32_t holdMs{0};
};

bool parseU32(std::wstring_view value, std::uint32_t& result) {
    if (value.empty()) return false;
    std::uint64_t parsed = 0;
    for (wchar_t ch : value) {
        if (ch < L'0' || ch > L'9') return false;
        parsed = parsed * 10u + static_cast<std::uint32_t>(ch - L'0');
        if (parsed > 0xffffffffull) return false;
    }
    result = static_cast<std::uint32_t>(parsed);
    return true;
}

std::string ascii(std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch > 0x7f) return {};
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

std::optional<SeatGamePhase> parseFixturePhase(std::wstring_view value) {
    if (value == L"idle") return SeatGamePhase::Idle;
    if (value == L"planning") return SeatGamePhase::Planning;
    if (value == L"starting") return SeatGamePhase::Starting;
    if (value == L"playing") return SeatGamePhase::Playing;
    if (value == L"stopping") return SeatGamePhase::Stopping;
    if (value == L"warning") return SeatGamePhase::Degraded;
    if (value == L"recovery") return SeatGamePhase::RecoveryRequired;
    return std::nullopt;
}

bool parseOptions(int argc, wchar_t** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--controlled-self-test") {
            options.controlledSelfTest = true;
        } else if (argument == L"--host-self-test") {
            options.hostSelfTest = true;
        } else if (argument == L"--seat" && index + 1 < argc) {
            if (!parseU32(argv[++index], options.seatId)) return false;
        } else if (argument == L"--phase" && index + 1 < argc) {
            const auto phase = parseFixturePhase(argv[++index]);
            if (!phase) return false;
            options.fixturePhase = *phase;
        } else if (argument == L"--expect" && index + 1 < argc) {
            options.expectedPhase = ascii(argv[++index]);
            if (options.expectedPhase.empty()) return false;
        } else if (argument == L"--report" && index + 1 < argc) {
            options.reportPath = argv[++index];
        } else if (argument == L"--hold-ms" && index + 1 < argc) {
            if (!parseU32(argv[++index], options.holdMs) || options.holdMs > 10000u) {
                return false;
            }
        } else {
            return false;
        }
    }
    return options.seatId != 0 && !(options.controlledSelfTest && options.hostSelfTest);
}

HostRuntimeSnapshot fixtureSnapshot(SeatId selectedSeat, SeatGamePhase phase) {
    HostRuntimeSnapshot snapshot;
    snapshot.schemaVersion = 3;
    snapshot.hostPhase = HostLifecyclePhase::Running;
    snapshot.sessionId.bytes[0] = 0x51u;
    snapshot.generation = 7;
    snapshot.transitionSequence = 13;
    snapshot.profileLoaded = true;
    snapshot.managementSeatId = 1;
    for (SeatId seatId : {SeatId{1}, SeatId{2}}) {
        SeatConfig config;
        config.seatId = seatId;
        config.name = L"Controlled Seat " + std::to_wstring(seatId);
        config.displayIds = {L"controlled-display-" + std::to_wstring(seatId)};
        config.primaryDisplayId = config.displayIds.front();
        snapshot.configuredSeats.push_back(config);
        snapshot.seats.push_back(
            SeatRuntimeState{seatId, SeatSessionPhase::Idle, {}});
        SeatGameState game;
        game.seatId = seatId;
        game.phase = seatId == selectedSeat ? phase : SeatGamePhase::Idle;
        game.generation = seatId == selectedSeat ? 4u : 2u;
        if (game.phase != SeatGamePhase::Idle) {
            game.binding = SeatGameBinding{
                "controlled-player-" + std::to_string(seatId),
                "controlled-game-" + std::to_string(seatId)};
        }
        if (game.phase == SeatGamePhase::Degraded) {
            game.diagnostic = "Controlled warning";
        } else if (game.phase == SeatGamePhase::RecoveryRequired) {
            game.diagnostic = "Controlled recovery";
        }
        snapshot.seatGames.push_back(std::move(game));
    }
    return snapshot;
}

bool writeReport(const Options& options, const SeatLauncherModel& model,
                 std::string_view source) {
    if (options.reportPath.empty()) return true;
    std::ofstream stream(options.reportPath, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    const auto& state = model.state();
    stream << "{\"schema_version\":1,\"source\":\"" << source
           << "\",\"seat_id\":" << state.seatId
           << ",\"phase\":\"" << hydra::seatui::seatLauncherPhaseName(state.phase)
           << "\",\"connected\":" << (state.connected ? "true" : "false")
           << ",\"can_end_playing\":" << (state.canEndPlaying ? "true" : "false")
           << ",\"authority_generation\":" << state.authorityGeneration
           << ",\"transition_sequence\":" << state.transitionSequence << "}";
    return stream.good();
}

int runControlledSelfTest(const Options& options) {
    SeatLauncherModel model(options.seatId);
    std::string error;
    if (!model.applySnapshot(fixtureSnapshot(options.seatId, options.fixturePhase), &error)) {
        return 20;
    }
    const auto actual = hydra::seatui::seatLauncherPhaseName(model.state().phase);
    if (!options.expectedPhase.empty() && actual != options.expectedPhase) return 21;
    if (model.state().canEndPlaying) {
        const auto command = model.endPlayingCommand(&error);
        if (!command || command->seatId != options.seatId || command->binding) return 22;
    }
    if (!writeReport(options, model, "controlled")) return 23;
    if (options.holdMs != 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(options.holdMs));
    }
    return 0;
}

int runHostSelfTest(const Options& options) {
    hydra::seatui::SeatHostClient client(options.seatId);
    std::string error;
    bool connected = false;
    for (int attempt = 0; attempt < 50 && !connected; ++attempt) {
        connected = client.connect(200u, &error);
        if (!connected) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!connected) return 30;
    const auto snapshot = client.resnapshot(2000u, &error);
    if (!snapshot) return 31;
    SeatLauncherModel model(options.seatId);
    if (!model.applySnapshot(*snapshot, &error)) return 32;
    const auto actual = hydra::seatui::seatLauncherPhaseName(model.state().phase);
    if (!options.expectedPhase.empty() && actual != options.expectedPhase) return 33;
    if (!writeReport(options, model, "host")) return 34;
    if (options.holdMs != 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(options.holdMs));
    }
    return 0;
}

#ifdef _WIN32

std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return L"Unavailable";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                              static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0,
                                          nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    (void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                              static_cast<int>(value.size()), result.data(), count,
                              nullptr, nullptr);
    return result;
}

std::wstring joined(const std::vector<std::string>& values, hydra::ui::Locale locale) {
    if (values.empty()) return std::wstring(hydra::ui::text(hydra::ui::TextId::None, locale));
    std::wstring result;
    for (const auto& value : values) {
        if (!result.empty()) result += L", ";
        result += wide(value);
    }
    return result;
}

class SeatWindow {
public:
    SeatWindow(HINSTANCE instance, Options options)
        : instance_(instance), options_(std::move(options)),
          model_(options_.seatId), client_(options_.seatId) {}

    int run(int showCommand) {
        const wchar_t* className = L"HydraSeat.SeatLauncher.v1";
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &SeatWindow::windowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = className;
        if (RegisterClassExW(&windowClass) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 40;

        const auto title = hydra::ui::formatOne(
            hydra::ui::TextId::SeatLauncherTitle, locale_, std::to_wstring(options_.seatId));
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, className, title.c_str(),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                CW_USEDEFAULT, CW_USEDEFAULT, 520, 430,
                                nullptr, nullptr, instance_, this);
        if (hwnd_ == nullptr) return 41;
        showCommand_ = showCommand;
        createControls();
        SetTimer(hwnd_, 1u, 1000u, nullptr);
        refreshFromHost();

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    enum : int { kEndPlaying = 1001, kReconnect = 1002 };

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
        SeatWindow* self = reinterpret_cast<SeatWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<SeatWindow*>(create->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
        }
        if (self == nullptr) return DefWindowProcW(hwnd, message, wParam, lParam);
        if (message == WM_TIMER) {
            self->refreshFromHost();
            return 0;
        }
        if (message == WM_COMMAND) {
            if (LOWORD(wParam) == kEndPlaying) self->endPlaying();
            if (LOWORD(wParam) == kReconnect) self->reconnect();
            return 0;
        }
        if (message == WM_KEYDOWN) {
            self->handleKey(static_cast<UINT>(wParam));
            return 0;
        }
        if (message == WM_CLOSE) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (message == WM_DESTROY) {
            KillTimer(hwnd, 1u);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    HWND label(int x, int y, int width, int height) {
        return CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                               x, y, width, height, hwnd_, nullptr, instance_, nullptr);
    }

    const wchar_t* t(hydra::ui::TextId id) const noexcept {
        return hydra::ui::text(id, locale_).data();
    }

    std::wstring_view phaseText() const noexcept {
        switch (model_.state().phase) {
            case SeatLauncherPhase::Disconnected:
                return hydra::ui::text(hydra::ui::TextId::StatusDisconnected, locale_);
            case SeatLauncherPhase::Idle:
                return hydra::ui::text(hydra::ui::TextId::StatusReady, locale_);
            case SeatLauncherPhase::Planning:
                return hydra::ui::text(hydra::ui::TextId::StatusReadyToStart, locale_);
            case SeatLauncherPhase::Starting:
                return hydra::ui::text(hydra::ui::TextId::StatusStartingGame, locale_);
            case SeatLauncherPhase::Playing:
                return hydra::ui::text(hydra::ui::TextId::StatusPlaying, locale_);
            case SeatLauncherPhase::Stopping:
                return hydra::ui::text(hydra::ui::TextId::StatusEndingPlay, locale_);
            case SeatLauncherPhase::Warning:
                return hydra::ui::text(hydra::ui::TextId::StatusNeedsAttention, locale_);
            case SeatLauncherPhase::Recovery:
                return hydra::ui::text(hydra::ui::TextId::StatusRecoveryRequired, locale_);
        }
        return {};
    }

    void createControls() {
        seatLabel_ = label(24, 20, 460, 28);
        statusLabel_ = label(24, 56, 460, 34);
        playerLabel_ = label(24, 108, 460, 24);
        gameLabel_ = label(24, 140, 460, 24);
        recentLabel_ = label(24, 188, 460, 42);
        availableLabel_ = label(24, 238, 460, 42);
        warningLabel_ = label(24, 294, 460, 42);
        endButton_ = CreateWindowExW(0, L"BUTTON", t(hydra::ui::TextId::EndPlaying),
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     24, 350, 150, 34, hwnd_,
                                     reinterpret_cast<HMENU>(kEndPlaying), instance_, nullptr);
        reconnectButton_ = CreateWindowExW(0, L"BUTTON", t(hydra::ui::TextId::Reconnect),
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           190, 350, 130, 34, hwnd_,
                                           reinterpret_cast<HMENU>(kReconnect), instance_, nullptr);
        const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        for (HWND control : {seatLabel_, statusLabel_, playerLabel_, gameLabel_,
                             recentLabel_, availableLabel_, warningLabel_,
                             endButton_, reconnectButton_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }

    void handleKey(UINT virtualKey) {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        std::optional<hydra::seatui::SeatHotkeyChord> chord;
        if (virtualKey == VK_F5) {
            chord = hydra::seatui::SeatHotkeyChord::RefreshStatus;
        } else if (virtualKey == VK_F1) {
            chord = hydra::seatui::SeatHotkeyChord::RecoveryHelp;
        } else if (control && shift && virtualKey == 'E') {
            chord = hydra::seatui::SeatHotkeyChord::EndPlaying;
        } else if (control && shift && virtualKey == 'R') {
            chord = hydra::seatui::SeatHotkeyChord::EmergencyResetHelp;
        }
        if (!chord) return;

        const auto& state = model_.state();
        hydra::seatui::SeatHotkeyContext context;
        context.seatId = state.seatId;
        context.phase = state.phase;
        context.authorityGeneration = state.authorityGeneration;
        context.transitionSequence = state.transitionSequence;
        context.canEndPlaying = state.canEndPlaying;
        context.canReconnect = state.canReconnect;
        hydra::seatui::SeatHotkeyDecision decision;
        std::string error;
        if (!hotkeys_.evaluate(context, *chord, decision, &error)) return;
        switch (decision.action) {
            case hydra::seatui::SeatHotkeyAction::None:
                return;
            case hydra::seatui::SeatHotkeyAction::Resnapshot:
                reconnect();
                return;
            case hydra::seatui::SeatHotkeyAction::ConfirmEndPlaying:
                if (MessageBoxW(hwnd_, t(hydra::ui::TextId::ConfirmEndPlayingPrompt),
                                t(hydra::ui::TextId::EndPlaying),
                                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                    endPlaying();
                }
                return;
            case hydra::seatui::SeatHotkeyAction::ShowRecoveryHelp:
                MessageBoxW(hwnd_, t(hydra::ui::TextId::RecoveryHelpText),
                            t(hydra::ui::TextId::RecoveryAction), MB_OK | MB_ICONINFORMATION);
                return;
            case hydra::seatui::SeatHotkeyAction::ShowEmergencyResetHelp:
                MessageBoxW(hwnd_, t(hydra::ui::TextId::EmergencyResetHelpText),
                            t(hydra::ui::TextId::RecoveryAction), MB_OK | MB_ICONWARNING);
                return;
        }
    }

    void reconnect() {
        client_.close();
        model_.markDisconnected("Reconnect requested");
        refreshFromHost();
    }

    void endPlaying() {
        std::string error;
        if (!model_.endPlayingCommand(&error)) return;
        const auto result = client_.endPlaying(2000u, &error);
        if (!result) {
            model_.markDisconnected(error);
        }
        refreshFromHost();
    }

    void refreshFromHost() {
        std::string error;
        if (!client_.connected() && !client_.connect(250u, &error)) {
            model_.markDisconnected(error);
            render(false);
            return;
        }
        const auto snapshot = client_.resnapshot(500u, &error);
        if (!snapshot || !model_.applySnapshot(*snapshot, &error)) {
            client_.close();
            model_.markDisconnected(error);
            render(false);
            return;
        }
        render(placeOnAssignedDisplay());
    }

    bool placeOnAssignedDisplay() {
        const auto& state = model_.state();
        if (state.assignedDisplayIds.empty()) return false;
        hydra::display::DisplayTopologyInventory inventory;
        const auto topology = inventory.refresh();
        if (!topology.querySucceeded) return false;
        hydra::display::SeatDisplayRequest request;
        request.seatId = state.seatId;
        request.missingOutputPolicy = hydra::display::MissingOutputPolicy::Block;
        for (const auto& id : state.assignedDisplayIds) {
            const auto converted = utf8(id);
            if (converted.empty()) return false;
            request.outputs.push_back({converted, true, false});
        }
        if (state.primaryDisplayId) request.primaryOutputId = utf8(*state.primaryDisplayId);
        const auto layouts = hydra::display::buildSeatDisplayLayouts(topology, {request});
        if (!layouts.valid || layouts.groups.size() != 1u ||
            layouts.groups.front().seatId != state.seatId) return false;
        const auto& bounds = layouts.groups.front().globalBounds;
        const bool compact = state.nonIntrusiveWhilePlaying;
        const int desiredWidth = compact ? 380 : 520;
        const int desiredHeight = compact ? 112 : 430;
        const int width = std::max(300, std::min(desiredWidth, bounds.width()));
        const int height = std::max(90, std::min(desiredHeight, bounds.height()));
        const int x = bounds.left + std::max(0, (bounds.width() - width) / 2);
        const int y = compact ? bounds.top + 12 :
            bounds.top + std::max(0, (bounds.height() - height) / 2);
        SetWindowPos(hwnd_, compact ? HWND_TOPMOST : HWND_NOTOPMOST,
                     x, y, width, height, SWP_NOACTIVATE);
        compact_ = compact;
        return true;
    }

    void render(bool placementValid) {
        const auto& state = model_.state();
        std::string notificationError;
        (void)notifications_.apply(state, nullptr, &notificationError);
        const auto& notificationState = notifications_.state();
        const auto binding = state.currentBinding;
        SetWindowTextW(seatLabel_,
            (L"Seat " + std::to_wstring(state.seatId) + L"  " + state.seatName).c_str());
        SetWindowTextW(statusLabel_, phaseText().data());
        const auto player = binding ? wide(binding->playerId) :
            wide(state.choices.selectedPlayerId);
        const auto game = binding ? wide(binding->gameId) :
            wide(state.choices.selectedGameId);
        const auto none = std::wstring(hydra::ui::text(hydra::ui::TextId::None, locale_));
        const auto playerLine = hydra::ui::formatOne(
            hydra::ui::TextId::CurrentSelectedPlayer, locale_, player.empty() ? none : player);
        const auto gameLine = hydra::ui::formatOne(
            hydra::ui::TextId::CurrentSelectedGame, locale_, game.empty() ? none : game);
        const auto recentLine = hydra::ui::formatOne(
            hydra::ui::TextId::RecentGames, locale_, joined(state.choices.recentGameIds, locale_));
        const auto availableLine = hydra::ui::formatOne(
            hydra::ui::TextId::AvailableGames, locale_, joined(state.choices.availableGameIds, locale_));
        SetWindowTextW(playerLabel_, playerLine.c_str());
        SetWindowTextW(gameLabel_, gameLine.c_str());
        SetWindowTextW(recentLabel_, recentLine.c_str());
        SetWindowTextW(availableLabel_, availableLine.c_str());
        const auto notificationText = notificationState.notifications.empty()
            ? std::wstring{}
            : std::wstring(hydra::ui::notificationText(
                  notificationState.notifications.front().messageId, locale_));
        SetWindowTextW(warningLabel_, notificationText.c_str());
        EnableWindow(endButton_, state.canEndPlaying ? TRUE : FALSE);
        EnableWindow(reconnectButton_, state.canReconnect ? TRUE : FALSE);

        const int showDetail = compact_ ? SW_HIDE : SW_SHOW;
        for (HWND detail : {playerLabel_, gameLabel_, recentLabel_, availableLabel_,
                            warningLabel_, reconnectButton_}) {
            ShowWindow(detail, showDetail);
        }
        if (compact_) {
            SetWindowPos(endButton_, nullptr, 214, 44, 140, 32, SWP_NOZORDER);
        } else {
            SetWindowPos(endButton_, nullptr, 24, 350, 150, 34, SWP_NOZORDER);
        }
        if (placementValid) {
            ShowWindow(hwnd_, showCommand_ == SW_HIDE ? SW_SHOWNOACTIVATE : showCommand_);
        } else {
            ShowWindow(hwnd_, SW_HIDE);
        }
    }

    HINSTANCE instance_{nullptr};
    Options options_;
    hydra::ui::Locale locale_{hydra::ui::systemLocale()};
    SeatLauncherModel model_;
    hydra::seatui::SeatHotkeyModel hotkeys_{options_.seatId};
    hydra::seatui::SeatHostClient client_;
    hydra::seatui::SeatNotificationModel notifications_{options_.seatId};
    HWND hwnd_{nullptr};
    HWND seatLabel_{nullptr};
    HWND statusLabel_{nullptr};
    HWND playerLabel_{nullptr};
    HWND gameLabel_{nullptr};
    HWND recentLabel_{nullptr};
    HWND availableLabel_{nullptr};
    HWND warningLabel_{nullptr};
    HWND endButton_{nullptr};
    HWND reconnectButton_{nullptr};
    int showCommand_{SW_SHOWNORMAL};
    bool compact_{false};
};

#endif

} // namespace

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return 2;
    Options options;
    const bool valid = parseOptions(argc, argv, options);
    LocalFree(argv);
    if (!valid) return 2;
    if (options.controlledSelfTest) return runControlledSelfTest(options);
    if (options.hostSelfTest) return runHostSelfTest(options);
    SeatWindow window(instance, std::move(options));
    return window.run(showCommand);
}
#if defined(__GNUC__)
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR, int showCommand) {
    return wWinMain(instance, previous, GetCommandLineW(), showCommand);
}
#endif
#else
int main() { return 2; }
#endif
