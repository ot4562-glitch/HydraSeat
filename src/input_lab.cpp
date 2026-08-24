#include "hydra/input_observation.hpp"
#include "hydra/input_router.hpp"
#include "hydra/workspace_manager.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using hydra::InputObservationSession;
using hydra::InputRouteDecision;
using hydra::InputRouteDisposition;
using hydra::InputRouteRecord;
using hydra::RawInputDeviceChange;
using hydra::RawInputEvent;
using hydra::SeatId;
using hydra::SeatRoutingPolicy;
using hydra::WorkspaceManager;

struct LabOptions {
    std::string profilePath{"workspace_config.json"};
    std::string tracePath{"hydra_input_lab.jsonl"};
    bool profileDisabled{false};
    bool selfTest{false};
    bool showHelp{false};
};

void printUsage(std::ostream& output) {
    output
        << "HydraSeat Phase 3 Gate A/B Input Lab\n\n"
        << "Usage:\n"
        << "  hydra_input_lab [--profile <workspace_config.json>] [--trace <trace.jsonl>]\n"
        << "  hydra_input_lab --no-profile\n"
        << "  hydra_input_lab --self-test\n\n"
        << "This lab observes physical Raw Input and routes owned-device diagnostics\n"
        << "to two HydraSeat-owned windows. It does NOT suppress normal Windows input\n"
        << "and is not a zero-bleed isolation backend.\n";
}

#ifndef _WIN32

LabOptions parseArguments(int argc, char** argv, bool& valid) {
    LabOptions options;
    valid = true;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--profile" && index + 1 < argc) {
            options.profilePath = argv[++index];
        } else if (argument == "--trace" && index + 1 < argc) {
            options.tracePath = argv[++index];
        } else if (argument == "--no-profile") {
            options.profileDisabled = true;
        } else if (argument == "--self-test") {
            options.selfTest = true;
        } else if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
        } else {
            valid = false;
        }
    }
    return options;
}

#else

std::string wideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), required,
            nullptr, nullptr) != required) {
        return {};
    }
    return result;
}

std::wstring utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return L"<invalid UTF-8>";
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), required) != required) {
        return L"<invalid UTF-8>";
    }
    return result;
}

LabOptions parseArguments(int argc, wchar_t** argv, bool& valid) {
    LabOptions options;
    valid = true;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--profile" && index + 1 < argc) {
            options.profilePath = wideToUtf8(argv[++index]);
        } else if (argument == L"--trace" && index + 1 < argc) {
            options.tracePath = wideToUtf8(argv[++index]);
        } else if (argument == L"--no-profile") {
            options.profileDisabled = true;
        } else if (argument == L"--self-test") {
            options.selfTest = true;
        } else if (argument == L"--help" || argument == L"-h") {
            options.showHelp = true;
        } else {
            valid = false;
        }
    }
    return options;
}

#endif

int runSelfTest() {
    WorkspaceManager seats;
    const auto seat1 = seats.createSeat(L"Seat 1");
    const auto seat2 = seats.createSeat(L"Seat 2");
    if (!seats.assignKeyboard(seat1, L"Keyboard:A") ||
        !seats.assignKeyboard(seat2, L"Keyboard:B") ||
        !seats.assignTargetWindow(seat1, 0x1111) ||
        !seats.assignTargetWindow(seat2, 0x2222)) {
        return 10;
    }

    SeatRoutingPolicy routing;
    std::vector<std::uint64_t> targets;
    InputObservationSession session(
        seats, routing,
        [&](const RawInputEvent&, const InputRouteDecision& decision) {
            targets.push_back(decision.targetHwnd);
            return true;
        });
    const auto bindings = session.rebuildBindings();
    if (bindings.boundDevices != 2 ||
        !bindings.ambiguousSharedDevices.empty()) {
        return 11;
    }

    RawInputEvent eventA;
    eventA.sequence = 1;
    eventA.deviceId = L"Keyboard:A";
    eventA.vkey = 'A';
    eventA.keyTransition = hydra::RawKeyTransition::Down;
    RawInputEvent eventB = eventA;
    eventB.sequence = 2;
    eventB.deviceId = L"Keyboard:B";

    const auto routeA = session.processInput(eventA);
    const auto routeB = session.processInput(eventB);
    if (routeA.disposition != InputRouteDisposition::Routed ||
        routeA.seatId != seat1 ||
        routeB.disposition != InputRouteDisposition::Routed ||
        routeB.seatId != seat2 ||
        targets != std::vector<std::uint64_t>{0x1111, 0x2222}) {
        return 12;
    }

    std::cout << "HydraSeat Input Lab self-test passed.\n";
    return EXIT_SUCCESS;
}

#ifdef _WIN32

constexpr wchar_t kSeatWindowClass[] = L"HydraSeatInputLabSeatWindow";
constexpr UINT kRouteNotification = WM_APP + 0x31;
constexpr UINT kRefreshNotification = WM_APP + 0x32;
constexpr UINT_PTR kRefreshTimer = 1;

class InputLabApp;

struct SeatWindowContext {
    InputLabApp* app{nullptr};
    SeatId seatId{0};
    HWND hwnd{nullptr};
    std::wstring title;
    std::uint64_t deliveredNotifications{0};
    std::uint64_t lastDeliveredSequence{0};
};

class InputLabApp {
public:
    explicit InputLabApp(LabOptions options)
        : m_options(std::move(options)),
          m_session(
              m_seats, m_routingPolicy,
              [this](const RawInputEvent& event,
                     const InputRouteDecision& decision) {
                  return dispatchToSeatWindow(event, decision);
              }) {}

    int run(HINSTANCE instance, int showCommand) {
        m_instance = instance;
        if (!loadOrCreateSeats()) {
            return 20;
        }
        if (!registerWindowClass()) {
            return 21;
        }
        if (!createSeatWindows(showCommand)) {
            return 22;
        }

        if (!m_trace.open(m_options.tracePath)) {
            MessageBoxW(nullptr,
                        L"The JSONL trace file could not be opened. The lab will continue without file logging.",
                        L"HydraSeat Input Lab", MB_OK | MB_ICONWARNING);
        }

        m_router.setGlobalCallback(
            [this](const RawInputEvent& event) { onInput(event); });
        m_router.setDeviceChangeCallback(
            [this](const RawInputDeviceChange& change) {
                onDeviceChange(change);
            });

        const auto bindingReport = m_session.rebuildBindings();
        m_bindingSummary = L"Bound devices: " +
                           std::to_wstring(bindingReport.boundDevices);
        if (!bindingReport.ambiguousSharedDevices.empty()) {
            m_bindingSummary += L" | Ambiguous shared devices: " +
                                std::to_wstring(
                                    bindingReport.ambiguousSharedDevices.size());
        }

        if (!m_router.initialize()) {
            std::wstring message = L"Raw Input initialization failed.";
            if (m_router.lastError()) {
                message += L"\nOperation: " + m_router.lastError()->operation +
                           L"\nWin32 error: " +
                           std::to_wstring(m_router.lastError()->systemError);
            }
            MessageBoxW(nullptr, message.c_str(), L"HydraSeat Input Lab",
                        MB_OK | MB_ICONERROR);
            return 23;
        }

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        m_trace.flush();
        m_router.stop();
        return static_cast<int>(message.wParam);
    }

    LRESULT windowMessage(SeatWindowContext& context, HWND hwnd,
                          UINT message, WPARAM wParam, LPARAM lParam) {
        (void)lParam;
        switch (message) {
        case WM_CREATE:
            SetTimer(hwnd, kRefreshTimer, 1000, nullptr);
            return 0;
        case WM_TIMER:
            if (wParam == kRefreshTimer) {
                InvalidateRect(hwnd, nullptr, FALSE);
                m_trace.flush();
            }
            return 0;
        case kRouteNotification:
            ++context.deliveredNotifications;
            context.lastDeliveredSequence = static_cast<std::uint64_t>(wParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case kRefreshNotification:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paintSeatWindow(context, hwnd);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kRefreshTimer);
            context.hwnd = nullptr;
            m_seats.assignTargetWindow(context.seatId, 0);
            m_session.rebuildBindings();
            if (std::none_of(
                    m_windows.begin(), m_windows.end(),
                    [](const auto& window) {
                        return window && window->hwnd != nullptr;
                    })) {
                PostQuitMessage(0);
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

private:
    static LRESULT CALLBACK SeatWindowProc(HWND hwnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
        auto* context = reinterpret_cast<SeatWindowContext*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            context = static_cast<SeatWindowContext*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(context));
            if (context != nullptr) {
                context->hwnd = hwnd;
            }
        }
        if (context != nullptr && context->app != nullptr) {
            return context->app->windowMessage(*context, hwnd, message,
                                               wParam, lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool loadOrCreateSeats() {
        bool loaded = false;
        if (!m_options.profileDisabled) {
            loaded = m_seats.loadFromFile(m_options.profilePath);
            if (!loaded) {
                m_profileMessage = L"Profile was not loaded: " +
                                   utf8ToWide(m_seats.lastError());
            } else {
                m_profileMessage = L"Profile: " +
                                   utf8ToWide(m_options.profilePath);
            }
        } else {
            m_profileMessage = L"Observer-only mode: no profile loaded.";
        }

        auto seats = m_seats.getAllSeats();
        std::vector<SeatId> activeIds;
        for (const auto& seat : seats) {
            if (seat.active) {
                activeIds.push_back(seat.seatId);
            }
        }
        while (activeIds.size() < 2) {
            const auto id = m_seats.createSeat(
                L"Unassigned Seat " +
                std::to_wstring(activeIds.size() + 1));
            activeIds.push_back(id);
        }
        std::sort(activeIds.begin(), activeIds.end());
        m_visibleSeatIds.assign(activeIds.begin(), activeIds.begin() + 2);

        if (!loaded && !m_options.profileDisabled) {
            m_profileMessage +=
                L"\nTwo empty Seats were created for observation. Use HydraSeat's assignment UI to generate stable device IDs for Gate B routing.";
        }
        if (activeIds.size() > 2) {
            m_profileMessage +=
                L"\nOnly the first two active Seats are shown by this Gate A/B lab.";
        }
        return true;
    }

    bool registerWindowClass() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = InputLabApp::SeatWindowProc;
        windowClass.hInstance = m_instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kSeatWindowClass;
        if (RegisterClassExW(&windowClass) != 0) {
            return true;
        }
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    bool createSeatWindows(int showCommand) {
        const int width = 620;
        const int height = 720;
        const int spacing = 24;
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int totalWidth = width * 2 + spacing;
        const int left = std::max(20, (screenWidth - totalWidth) / 2);

        for (std::size_t index = 0; index < m_visibleSeatIds.size(); ++index) {
            const SeatId seatId = m_visibleSeatIds[index];
            const auto* seat = m_seats.getSeat(seatId);
            auto context = std::make_unique<SeatWindowContext>();
            context->app = this;
            context->seatId = seatId;
            context->title = L"HydraSeat Input Lab - " +
                             (seat ? seat->name
                                   : L"Seat " + std::to_wstring(seatId));

            const int x = left + static_cast<int>(index) * (width + spacing);
            HWND hwnd = CreateWindowExW(
                WS_EX_APPWINDOW, kSeatWindowClass, context->title.c_str(),
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, 70, width, height,
                nullptr, nullptr, m_instance, context.get());
            if (hwnd == nullptr) {
                return false;
            }
            context->hwnd = hwnd;
            m_seats.assignTargetWindow(
                seatId, reinterpret_cast<std::uint64_t>(hwnd));
            ShowWindow(hwnd, showCommand);
            UpdateWindow(hwnd);
            m_windows.push_back(std::move(context));
        }
        return true;
    }

    bool dispatchToSeatWindow(const RawInputEvent& event,
                              const InputRouteDecision& decision) {
        const HWND target = reinterpret_cast<HWND>(decision.targetHwnd);
        if (target == nullptr || !IsWindow(target)) {
            return false;
        }
        return PostMessageW(
                   target, kRouteNotification,
                   static_cast<WPARAM>(event.sequence), 0) != FALSE;
    }

    void onInput(const RawInputEvent& event) {
        // Gate B requests no physical suppression. The route is diagnostic and
        // deliberately coexists with normal Windows input.
        const InputRouteRecord route = m_session.processInput(event, false);
        if (m_trace.isOpen()) {
            m_trace.writeInput(event, route);
        }

        if (route.disposition != InputRouteDisposition::Routed) {
            invalidateAll();
        }
    }

    void onDeviceChange(const RawInputDeviceChange& change) {
        m_session.processDeviceChange(change);
        if (m_trace.isOpen()) {
            m_trace.writeDeviceChange(change);
        }
        invalidateAll();
    }

    void invalidateAll() {
        for (const auto& window : m_windows) {
            if (window && window->hwnd != nullptr) {
                PostMessageW(window->hwnd, kRefreshNotification, 0, 0);
            }
        }
    }

    std::wstring assignedDeviceLines(const hydra::SeatConfig& seat) const {
        std::wostringstream output;
        const auto append = [&](std::wstring_view label,
                                const std::vector<std::wstring>& ids) {
            output << label << L" (" << ids.size() << L"):\n";
            if (ids.empty()) {
                output << L"  - none assigned\n";
                return;
            }
            for (const auto& id : ids) {
                const auto observed = m_session.ledger().device(id);
                output << L"  - " << id;
                if (observed) {
                    output << (observed->online ? L" [online]" : L" [offline]");
                } else {
                    output << L" [not observed yet]";
                }
                output << L"\n";
            }
        };
        append(L"Keyboards", seat.keyboardIds);
        append(L"Mice / touchpads", seat.mouseIds);
        append(L"Controllers", seat.controllerIds);
        return output.str();
    }

    std::wstring observedDeviceLines() const {
        const auto devices = m_session.ledger().devices();
        std::wostringstream output;
        output << L"Observed physical input identities (" << devices.size() << L"):\n";
        if (devices.empty()) {
            output << L"  - no Raw Input device event observed yet\n";
            return output.str();
        }
        constexpr std::size_t kMaxVisibleDevices = 6;
        const std::size_t visible = std::min(devices.size(), kMaxVisibleDevices);
        for (std::size_t index = 0; index < visible; ++index) {
            const auto& device = devices[index];
            output << L"  - " << device.deviceId
                   << (device.online ? L" [online]" : L" [offline]")
                   << L" events=" << device.eventCount << L"\n";
        }
        if (devices.size() > visible) {
            output << L"  ... " << (devices.size() - visible)
                   << L" more in the JSONL trace\n";
        }
        return output.str();
    }

    std::wstring globalDiagnostics() const {
        const auto& routerStats = m_router.statistics();
        std::wostringstream output;
        output << L"Decoded Raw Input: " << routerStats.decodedEvents
               << L" | Dropped: " << routerStats.droppedEvents << L"\n"
               << L"Device arrivals: " << routerStats.deviceArrivals
               << L" | removals: " << routerStats.deviceRemovals << L"\n"
               << L"Unassigned events: " << m_session.unassignedEvents()
               << L" | ambiguous: " << m_session.ambiguousEvents() << L"\n"
               << L"Inactive Seat: " << m_session.inactiveSeatEvents()
               << L" | missing target: " << m_session.missingTargetEvents();
        return output.str();
    }

    void paintSeatWindow(const SeatWindowContext& context, HWND hwnd) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);

        HBRUSH background = CreateSolidBrush(RGB(15, 23, 42));
        FillRect(dc, &client, background);
        DeleteObject(background);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(226, 232, 240));

        RECT margin = client;
        margin.left += 24;
        margin.top += 20;
        margin.right -= 24;
        margin.bottom -= 20;

        HFONT titleFont = CreateFontW(
            27, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT bodyFont = CreateFontW(
            17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        const auto oldFont = SelectObject(dc, titleFont);
        DrawTextW(dc, context.title.c_str(), -1, &margin,
                  DT_LEFT | DT_TOP | DT_SINGLELINE);
        margin.top += 42;

        SetTextColor(dc, RGB(248, 113, 113));
        SelectObject(dc, bodyFont);
        const wchar_t* warning =
            L"GATE A/B DIAGNOSTIC ONLY — NORMAL WINDOWS INPUT IS NOT SUPPRESSED";
        RECT warningRect = margin;
        warningRect.bottom = warningRect.top + 52;
        DrawTextW(dc, warning, -1, &warningRect,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
        margin.top += 60;

        SetTextColor(dc, RGB(148, 163, 184));
        RECT profileRect = margin;
        profileRect.bottom = profileRect.top + 70;
        DrawTextW(dc, m_profileMessage.c_str(), -1, &profileRect,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
        margin.top += 78;

        SetTextColor(dc, RGB(96, 165, 250));
        RECT bindingRect = margin;
        bindingRect.bottom = bindingRect.top + 28;
        DrawTextW(dc, m_bindingSummary.c_str(), -1, &bindingRect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE);
        margin.top += 34;

        const auto* seat = m_seats.getSeat(context.seatId);
        const auto metrics = m_session.seat(context.seatId);
        std::wostringstream routeText;
        if (metrics) {
            routeText << L"Routed events: " << metrics->routedEvents
                      << L" | keyboard: " << metrics->keyboardEvents
                      << L" | mouse: " << metrics->mouseEvents << L"\n"
                      << L"Dispatch failures: " << metrics->dispatchFailures
                      << L" | last sequence: " << metrics->lastSequence << L"\n"
                      << L"Last physical device: "
                      << (metrics->lastDeviceId.empty()
                              ? L"none"
                              : metrics->lastDeviceId) << L"\n"
                      << L"Target notifications processed: "
                      << context.deliveredNotifications
                      << L" | last delivered sequence: "
                      << context.lastDeliveredSequence;
        } else {
            routeText << L"No route metrics yet.";
        }
        SetTextColor(dc, RGB(74, 222, 128));
        RECT routeRect = margin;
        routeRect.bottom = routeRect.top + 100;
        const auto routeString = routeText.str();
        DrawTextW(dc, routeString.c_str(), -1, &routeRect,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
        margin.top += 108;

        SetTextColor(dc, RGB(226, 232, 240));
        if (seat != nullptr) {
            const auto devices = assignedDeviceLines(*seat);
            RECT devicesRect = margin;
            devicesRect.bottom = devicesRect.top + 155;
            DrawTextW(dc, devices.c_str(), -1, &devicesRect,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
            margin.top += 162;
        }

        SetTextColor(dc, RGB(203, 213, 225));
        const auto observedDevices = observedDeviceLines();
        RECT observedRect = margin;
        observedRect.bottom = observedRect.top + 120;
        DrawTextW(dc, observedDevices.c_str(), -1, &observedRect,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        margin.top += 126;

        SetTextColor(dc, RGB(148, 163, 184));
        const auto diagnostics = globalDiagnostics();
        RECT diagnosticsRect = margin;
        diagnosticsRect.bottom = client.bottom - 24;
        DrawTextW(dc, diagnostics.c_str(), -1, &diagnosticsRect,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);

        SelectObject(dc, oldFont);
        DeleteObject(titleFont);
        DeleteObject(bodyFont);
        EndPaint(hwnd, &paint);
    }

    LabOptions m_options;
    HINSTANCE m_instance{nullptr};
    WorkspaceManager m_seats;
    SeatRoutingPolicy m_routingPolicy;
    InputObservationSession m_session;
    hydra::InputRouter m_router;
    hydra::InputTraceWriter m_trace;

    std::vector<SeatId> m_visibleSeatIds;
    std::vector<std::unique_ptr<SeatWindowContext>> m_windows;
    std::wstring m_profileMessage;
    std::wstring m_bindingSummary;
};

#endif

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    bool valid = false;
    const auto options = parseArguments(argc, argv, valid);
    if (!valid || options.showHelp) {
        printUsage(valid ? std::cout : std::cerr);
        return valid ? EXIT_SUCCESS : 2;
    }
    if (options.selfTest) {
        return runSelfTest();
    }

    InputLabApp app(options);
    return app.run(GetModuleHandleW(nullptr), SW_SHOWNORMAL);
}
#else
int main(int argc, char** argv) {
    bool valid = false;
    const auto options = parseArguments(argc, argv, valid);
    if (!valid || options.showHelp) {
        printUsage(valid ? std::cout : std::cerr);
        return valid ? EXIT_SUCCESS : 2;
    }
    if (options.selfTest) {
        return runSelfTest();
    }
    std::cerr << "hydra_input_lab interactive windows are available only on Windows.\n";
    return 3;
}
#endif
