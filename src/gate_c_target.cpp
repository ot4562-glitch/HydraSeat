#include "hydra/gate_c_protocol.hpp"
#include "hydra/gate_c_transport.hpp"
#include "hydra/virtual_input_state.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using hydra::gatec::ControlStateMessage;
using hydra::gatec::DecodedFrame;
using hydra::gatec::ErrorMessage;
using hydra::gatec::HelloAckMessage;
using hydra::gatec::HelloMessage;
using hydra::gatec::InputEventMessage;
using hydra::gatec::MessageType;
using hydra::gatec::PipeChannel;
using hydra::gatec::QuerySnapshotMessage;
using hydra::gatec::SessionToken;
using hydra::gatec::StateSnapshotMessage;
using hydra::gatec::TransportStatus;
using hydra::gatec::VirtualInputState;

struct TargetOptions {
    std::wstring pipeName;
    std::uint32_t seatId{0};
    SessionToken token{};
    bool tokenSet{false};
    bool headless{false};
    bool selfTest{false};
    bool showHelp{false};
};

void printUsage(std::ostream& output) {
    output
        << "HydraSeat Gate C controlled target\n\n"
        << "Usage:\n"
        << "  hydra_gate_c_target --pipe <name> --seat <id> --token <32-hex> [--headless]\n"
        << "  hydra_gate_c_target --self-test\n\n"
        << "This executable is a HydraSeat-owned test process. It does not hook\n"
        << "Windows APIs or attach to a commercial game.\n";
}

int runLocalSelfTest() {
    VirtualInputState state;
    ControlStateMessage control;
    control.cursorX = 10;
    control.cursorY = 20;
    control.virtualForeground = true;
    control.clipEnabled = true;
    control.clipLeft = 0;
    control.clipTop = 0;
    control.clipRight = 100;
    control.clipBottom = 100;
    if (!state.applyControl(1, control)) {
        return 10;
    }

    InputEventMessage key;
    key.kind = hydra::gatec::InputKind::Keyboard;
    key.keyTransition = hydra::gatec::KeyTransition::Down;
    key.vkey = 0x41;
    if (!state.applyInput(2, key) || !state.keyDown(0x41) ||
        state.consumeAsyncKeyState(0x41) != 0x8001u) {
        return 11;
    }

    std::cout << "HydraSeat Gate C target self-test passed.\n";
    return EXIT_SUCCESS;
}

#ifdef _WIN32

constexpr wchar_t kWindowClass[] = L"HydraSeatGateCControlledTarget";
constexpr UINT kStateChangedMessage = WM_APP + 0x43;
constexpr std::uint32_t kIoTimeoutMs = 5000;
constexpr std::uint32_t kReadPollMs = 250;

std::wstring asciiToWide(std::string_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (const auto character : text) {
        result.push_back(static_cast<wchar_t>(
            static_cast<unsigned char>(character)));
    }
    return result;
}

bool parseUnsigned(std::wstring_view text, std::uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    std::uint64_t parsed = 0;
    for (const auto character : text) {
        if (character < L'0' || character > L'9') {
            return false;
        }
        parsed = parsed * 10 + static_cast<std::uint64_t>(character - L'0');
        if (parsed > ~std::uint32_t{0}) {
            return false;
        }
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

std::string wideAsciiToString(std::wstring_view text) {
    std::string result;
    result.reserve(text.size());
    for (const auto character : text) {
        const auto code = static_cast<std::uint32_t>(character);
        if (code > 0x7fu) {
            return {};
        }
        result.push_back(static_cast<char>(code));
    }
    return result;
}

TargetOptions parseOptions(int argc, wchar_t** argv, bool& valid) {
    TargetOptions options;
    valid = true;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--pipe" && index + 1 < argc) {
            options.pipeName = argv[++index];
        } else if (argument == L"--seat" && index + 1 < argc) {
            valid = parseUnsigned(argv[++index], options.seatId) && valid;
        } else if (argument == L"--token" && index + 1 < argc) {
            const auto token = hydra::gatec::tokenFromHex(
                wideAsciiToString(argv[++index]));
            if (!token) {
                valid = false;
            } else {
                options.token = *token;
                options.tokenSet = true;
            }
        } else if (argument == L"--headless") {
            options.headless = true;
        } else if (argument == L"--self-test") {
            options.selfTest = true;
        } else if (argument == L"--help" || argument == L"-h") {
            options.showHelp = true;
        } else {
            valid = false;
        }
    }

    if (!options.selfTest && !options.showHelp &&
        (options.pipeName.empty() || options.seatId == 0 ||
         !options.tokenSet)) {
        valid = false;
    }
    return options;
}

class ControlledTarget {
public:
    explicit ControlledTarget(TargetOptions options)
        : m_options(std::move(options)) {}

    int run(HINSTANCE instance, int showCommand) {
        m_instance = instance;
        if (!m_options.headless && !createWindow(showCommand)) {
            return 20;
        }
        if (!connectAndHandshake()) {
            if (m_hwnd != nullptr) {
                DestroyWindow(m_hwnd);
                m_hwnd = nullptr;
            }
            return 21;
        }

        if (m_options.headless) {
            readerLoop(std::stop_token{});
            return m_exitCode.load();
        }

        m_reader = std::jthread(
            [this](std::stop_token stopToken) { readerLoop(stopToken); });

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        m_stopping.store(true);
        m_reader.request_stop();
        m_reader.join();
        m_channel.close();
        return m_exitCode.load();
    }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
        ControlledTarget* self = reinterpret_cast<ControlledTarget*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<ControlledTarget*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
            if (self != nullptr) {
                self->m_hwnd = hwnd;
            }
        }
        if (self != nullptr) {
            return self->windowMessage(hwnd, message, wParam, lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT windowMessage(HWND hwnd, UINT message, WPARAM wParam,
                          LPARAM lParam) {
        (void)wParam;
        (void)lParam;
        switch (message) {
        case kStateChangedMessage:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_PAINT:
            paint(hwnd);
            return 0;
        case WM_CLOSE:
            m_stopping.store(true);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            m_hwnd = nullptr;
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    bool createWindow(int showCommand) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = ControlledTarget::WindowProc;
        windowClass.hInstance = m_instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kWindowClass;
        if (RegisterClassExW(&windowClass) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        const std::wstring title =
            L"HydraSeat Gate C Target - Seat " +
            std::to_wstring(m_options.seatId);
        m_hwnd = CreateWindowExW(
            WS_EX_APPWINDOW, kWindowClass, title.c_str(),
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            650, 520, nullptr, nullptr, m_instance, this);
        if (m_hwnd == nullptr) {
            return false;
        }
        ShowWindow(m_hwnd, showCommand);
        UpdateWindow(m_hwnd);
        return true;
    }

    bool connectAndHandshake() {
        std::string error;
        std::uint32_t systemError = 0;
        m_channel = hydra::gatec::connectGateCClient(
            m_options.pipeName, 10000, &error, &systemError);
        if (!m_channel.valid()) {
            setProtocolError("connect failed: " + error, systemError);
            return false;
        }

        HelloMessage hello;
        hello.token = m_options.token;
        hello.seatId = m_options.seatId;
        hello.processId = GetCurrentProcessId();
        hello.architectureBits =
            static_cast<std::uint16_t>(sizeof(void*) * 8);
        hello.targetWindow = reinterpret_cast<std::uint64_t>(m_hwnd);
        if (!m_channel.writeFrame(hydra::gatec::encodeHello(1, hello),
                                  kIoTimeoutMs, &error, &systemError)) {
            setProtocolError("Hello write failed: " + error, systemError);
            return false;
        }

        const auto response = m_channel.readFrame(kIoTimeoutMs);
        if (!response || !response.frame) {
            setProtocolError("HelloAck read failed: " + response.error,
                             response.systemError);
            return false;
        }
        HelloAckMessage ack;
        if (!hydra::gatec::decodeHelloAck(*response.frame, ack, &error) ||
            response.frame->sequence != 1 || !ack.accepted) {
            setProtocolError(
                error.empty() ? "HelloAck rejected the controlled target"
                              : "HelloAck decode failed: " + error,
                ack.errorCode);
            return false;
        }
        if ((ack.grantedCapabilities &
             hydra::gatec::testCapabilityBits(
                 hydra::gatec::kControlledTargetCapabilities)) !=
            hydra::gatec::testCapabilityBits(
                hydra::gatec::kControlledTargetCapabilities)) {
            setProtocolError("HelloAck omitted required controlled-test capabilities",
                             0);
            return false;
        }

        m_connected.store(true);
        return true;
    }

    void readerLoop(std::stop_token stopToken) {
        while (!stopToken.stop_requested() && !m_stopping.load()) {
            const auto result = m_channel.readFrame(kReadPollMs);
            if (result.status == TransportStatus::Timeout) {
                continue;
            }
            if (!result || !result.frame) {
                if (result.status != TransportStatus::Disconnected) {
                    setProtocolError(result.error, result.systemError);
                    m_exitCode.store(22);
                }
                break;
            }
            if (!processFrame(*result.frame)) {
                m_exitCode.store(23);
                break;
            }
        }

        m_stopping.store(true);
        if (m_hwnd != nullptr) {
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        }
    }

    bool processFrame(const DecodedFrame& frame) {
        std::string error;
        if (frame.type == MessageType::InputEvent) {
            InputEventMessage message;
            if (!hydra::gatec::decodeInputEvent(frame, message, &error)) {
                return sendError(frame.sequence, 1001, error);
            }
            {
                std::scoped_lock lock(m_stateMutex);
                if (!m_state.applyInput(frame.sequence, message)) {
                    return sendError(frame.sequence, 1002,
                                     "stale or invalid input sequence");
                }
            }
            notifyStateChanged();
            return true;
        }
        if (frame.type == MessageType::ControlState) {
            ControlStateMessage message;
            if (!hydra::gatec::decodeControlState(frame, message, &error)) {
                return sendError(frame.sequence, 1003, error);
            }
            {
                std::scoped_lock lock(m_stateMutex);
                if (!m_state.applyControl(frame.sequence, message)) {
                    return sendError(frame.sequence, 1004,
                                     "stale or invalid control sequence");
                }
            }
            notifyStateChanged();
            return true;
        }
        if (frame.type == MessageType::QuerySnapshot) {
            QuerySnapshotMessage query;
            if (!hydra::gatec::decodeQuerySnapshot(frame, query, &error)) {
                return sendError(frame.sequence, 1005, error);
            }

            StateSnapshotMessage snapshot;
            {
                std::scoped_lock lock(m_stateMutex);
                snapshot = m_state.snapshot();
                snapshot.probeVkey = query.probeVkey;
                if (query.probeVkey < 256u) {
                    const auto keyboard = m_state.keyboardState();
                    snapshot.keyboardStateByte =
                        keyboard[static_cast<std::size_t>(query.probeVkey)];
                    snapshot.asyncKeyStateValue =
                        m_state.consumeAsyncKeyState(query.probeVkey);
                }
            }
            return m_channel.writeFrame(
                hydra::gatec::encodeStateSnapshot(frame.sequence, snapshot),
                kIoTimeoutMs, &error, nullptr);
        }
        if (frame.type == MessageType::Shutdown) {
            if (!hydra::gatec::decodeShutdown(frame, &error)) {
                return sendError(frame.sequence, 1006, error);
            }
            m_stopping.store(true);
            return true;
        }

        return sendError(frame.sequence, 1099,
                         "unexpected message type: " +
                             std::string(hydra::gatec::messageTypeName(frame.type)));
    }

    bool sendError(std::uint64_t sequence, std::uint32_t code,
                   std::string_view message) {
        setProtocolError(std::string(message), code);
        std::string writeError;
        return m_channel.writeFrame(
            hydra::gatec::encodeError(sequence, ErrorMessage{code}),
            kIoTimeoutMs, &writeError, nullptr);
    }

    void notifyStateChanged() {
        if (m_hwnd != nullptr) {
            PostMessageW(m_hwnd, kStateChangedMessage, 0, 0);
        }
    }

    void setProtocolError(std::string message, std::uint32_t systemError) {
        std::scoped_lock lock(m_errorMutex);
        m_lastError = std::move(message);
        m_lastSystemError = systemError;
        notifyStateChanged();
    }

    void paint(HWND hwnd) {
        StateSnapshotMessage snapshot;
        {
            std::scoped_lock lock(m_stateMutex);
            snapshot = m_state.snapshot();
        }
        std::string lastError;
        std::uint32_t systemError = 0;
        {
            std::scoped_lock lock(m_errorMutex);
            lastError = m_lastError;
            systemError = m_lastSystemError;
        }

        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        HBRUSH background = CreateSolidBrush(RGB(15, 23, 42));
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);

        HFONT titleFont = CreateFontW(
            28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT bodyFont = CreateFontW(
            18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
        const auto oldFont = SelectObject(dc, titleFont);

        RECT textRect = client;
        textRect.left += 24;
        textRect.top += 20;
        textRect.right -= 24;
        SetTextColor(dc, RGB(226, 232, 240));
        const std::wstring title =
            L"Controlled Target Process — Seat " +
            std::to_wstring(m_options.seatId);
        DrawTextW(dc, title.c_str(), -1, &textRect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE);
        textRect.top += 46;

        SelectObject(dc, bodyFont);
        SetTextColor(dc, RGB(248, 113, 113));
        const wchar_t* warning =
            L"TEST ADAPTER STATE ONLY — NO WINDOWS API HOOK IS INSTALLED";
        DrawTextW(dc, warning, -1, &textRect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE);
        textRect.top += 40;

        const bool actualForeground = GetForegroundWindow() == hwnd;
        std::wostringstream status;
        status << L"Connected: " << (m_connected.load() ? L"yes" : L"no")
               << L"\nProcess ID: " << GetCurrentProcessId()
               << L"\nLast applied sequence: " << snapshot.lastAppliedSequence
               << L"\nVirtual foreground: "
               << (snapshot.virtualForeground ? L"true" : L"false")
               << L"\nActual OS foreground: "
               << (actualForeground ? L"true" : L"false")
               << L"\nVirtual capture: "
               << (snapshot.virtualCapture ? L"true" : L"false")
               << L"\nVirtual cursor: (" << snapshot.cursorX << L", "
               << snapshot.cursorY << L")"
               << L"\nVirtual clip: "
               << (snapshot.clipEnabled ? L"enabled" : L"disabled")
               << L" [" << snapshot.clipLeft << L", " << snapshot.clipTop
               << L", " << snapshot.clipRight << L", "
               << snapshot.clipBottom << L"]"
               << L"\nMouse buttons: 0x" << std::hex
               << snapshot.mouseButtonsDown << std::dec
               << L"\nWheel accumulator: " << snapshot.wheelAccumulator
               << L"\nKey A down: "
               << (hydra::gatec::snapshotKeyDown(snapshot, 0x41)
                       ? L"true" : L"false")
               << L"\nKey B down: "
               << (hydra::gatec::snapshotKeyDown(snapshot, 0x42)
                       ? L"true" : L"false");
        if (!lastError.empty()) {
            status << L"\n\nProtocol error: " << asciiToWide(lastError)
                   << L" (" << systemError << L")";
        }
        const auto statusText = status.str();
        SetTextColor(dc, RGB(148, 163, 184));
        DrawTextW(dc, statusText.c_str(), -1, &textRect,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

        SelectObject(dc, oldFont);
        DeleteObject(titleFont);
        DeleteObject(bodyFont);
        EndPaint(hwnd, &paint);
    }

    TargetOptions m_options;
    HINSTANCE m_instance{nullptr};
    HWND m_hwnd{nullptr};
    PipeChannel m_channel;
    VirtualInputState m_state;
    std::mutex m_stateMutex;
    std::mutex m_errorMutex;
    std::string m_lastError;
    std::uint32_t m_lastSystemError{0};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stopping{false};
    std::atomic<int> m_exitCode{0};
    std::jthread m_reader;
};

#endif

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    bool valid = false;
    const auto options = parseOptions(argc, argv, valid);
    if (!valid || options.showHelp) {
        printUsage(valid ? std::cout : std::cerr);
        return valid ? EXIT_SUCCESS : 2;
    }
    if (options.selfTest) {
        return runLocalSelfTest();
    }

    ControlledTarget target(options);
    return target.run(GetModuleHandleW(nullptr), SW_SHOWNORMAL);
}
#else
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
        return runLocalSelfTest();
    }
    printUsage(std::cerr);
    return 3;
}
#endif
