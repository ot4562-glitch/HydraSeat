#include "hydra/gate_c_adapter.h"
#include "hydra/gate_c_architecture.hpp"
#include "hydra/gate_c_probe_snapshot.hpp"
#include "hydra/gate_c_protocol.hpp"
#include "hydra/gate_c_transport.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
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
using hydra::gatec::ProbeComparison;
using hydra::gatec::QuerySnapshotMessage;
using hydra::gatec::SessionToken;
using hydra::gatec::TransportStatus;

class AdapterOwner {
public:
    AdapterOwner() : m_handle(hydra_gate_c_adapter_create()) {}
    ~AdapterOwner() { hydra_gate_c_adapter_destroy(m_handle); }

    AdapterOwner(const AdapterOwner&) = delete;
    AdapterOwner& operator=(const AdapterOwner&) = delete;

    HydraGateCAdapterHandle get() const noexcept { return m_handle; }
    explicit operator bool() const noexcept { return m_handle != nullptr; }

private:
    HydraGateCAdapterHandle m_handle{nullptr};
};

[[maybe_unused]] HydraGateCAdapterInputEventV1 toAdapterEvent(
    const InputEventMessage& message) noexcept {
    HydraGateCAdapterInputEventV1 result{};
    result.struct_size = sizeof(result);
    result.kind = static_cast<std::uint8_t>(
        message.kind == hydra::gatec::InputKind::Keyboard
                      ? HYDRA_GATE_C_ADAPTER_INPUT_KEYBOARD
                      : HYDRA_GATE_C_ADAPTER_INPUT_MOUSE);
    result.key_transition = static_cast<std::uint8_t>(message.keyTransition);
    result.is_touchpad = message.isTouchpad ? 1u : 0u;
    result.timestamp_micros = message.timestampMicros;
    result.vkey = message.vkey;
    result.scan_code = message.scanCode;
    result.keyboard_flags = message.keyboardFlags;
    result.delta_x = message.deltaX;
    result.delta_y = message.deltaY;
    result.mouse_button_flags = message.mouseButtonFlags;
    result.wheel_delta = message.wheelDelta;
    return result;
}

[[maybe_unused]] HydraGateCAdapterControlStateV1 toAdapterControl(
    const ControlStateMessage& message) noexcept {
    HydraGateCAdapterControlStateV1 result{};
    result.struct_size = sizeof(result);
    result.cursor_x = message.cursorX;
    result.cursor_y = message.cursorY;
    result.clip_enabled = message.clipEnabled ? 1u : 0u;
    result.virtual_foreground = message.virtualForeground ? 1u : 0u;
    result.virtual_capture = message.virtualCapture ? 1u : 0u;
    result.clip_left = message.clipLeft;
    result.clip_top = message.clipTop;
    result.clip_right = message.clipRight;
    result.clip_bottom = message.clipBottom;
    return result;
}

[[maybe_unused]] std::uint64_t monotonicMicros() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct ProbeOptions {
    std::wstring pipeName;
    std::uint32_t seatId{0};
    SessionToken token{};
    bool tokenSet{false};
    bool headless{false};
    bool baselineSelfTest{false};
    bool testMissingWindow{false};
    bool testNoHandshake{false};
    bool testAbnormalExit{false};
    bool showHelp{false};
};

void printUsage(std::ostream& output) {
    output
        << "HydraSeat Gate C Win32 API baseline probe\n\n"
        << "Usage:\n"
        << "  hydra_gate_c_api_probe --pipe <name> --seat <id> --token <32-hex> [--headless]\n"
        << "  hydra_gate_c_api_probe --baseline-self-test\n\n"
        << "The probe reads ordinary Win32 APIs and the direct Gate C adapter side\n"
        << "by side. It does not patch, hook, inject, or mutate global input state.\n";
}

#ifdef _WIN32

constexpr wchar_t kWindowClass[] = L"HydraSeatGateCApiProbe";
constexpr UINT kCaptureRequestMessage = WM_APP + 0x51;
constexpr UINT kStateChangedMessage = WM_APP + 0x52;
constexpr std::uint32_t kIoTimeoutMs = 5000;
constexpr std::uint32_t kReadPollMs = 250;
constexpr std::uint32_t kUiCaptureTimeoutMs = 3000;

std::uint64_t runtimeWindowValue(HWND window) noexcept {
    return static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(window));
}

bool parseUnsigned(std::wstring_view text, std::uint32_t& value) {
    if (text.empty()) return false;
    std::uint64_t parsed = 0;
    for (const auto character : text) {
        if (character < L'0' || character > L'9') return false;
        parsed = parsed * 10 + static_cast<std::uint64_t>(character - L'0');
        if (parsed > (std::numeric_limits<std::uint32_t>::max)()) return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

std::string wideAsciiToString(std::wstring_view text) {
    std::string result;
    result.reserve(text.size());
    for (const auto character : text) {
        const auto code = static_cast<std::uint32_t>(character);
        if (code > 0x7fu) return {};
        result.push_back(static_cast<char>(code));
    }
    return result;
}

ProbeOptions parseOptions(int argc, wchar_t** argv, bool& valid) {
    ProbeOptions options;
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
        } else if (argument == L"--baseline-self-test") {
            options.baselineSelfTest = true;
        } else if (argument == L"--test-missing-window") {
            options.testMissingWindow = true;
        } else if (argument == L"--test-no-handshake") {
            options.testNoHandshake = true;
        } else if (argument == L"--test-abnormal-exit") {
            options.testAbnormalExit = true;
        } else if (argument == L"--help" || argument == L"-h") {
            options.showHelp = true;
        } else {
            valid = false;
        }
    }

    const bool connectedMode = !options.pipeName.empty() ||
        options.seatId != 0 || options.tokenSet;
    if (!options.showHelp && !options.baselineSelfTest && !connectedMode) {
        valid = false;
    }
    if (connectedMode &&
        (options.pipeName.empty() || options.seatId == 0 ||
         !options.tokenSet)) {
        valid = false;
    }
    if (options.testMissingWindow && options.testNoHandshake) valid = false;
    return options;
}

ProbeComparison captureComparison(HydraGateCAdapterHandle adapter,
                                  std::uint64_t sequence,
                                  std::uint32_t seatId,
                                  std::uint16_t probeVkey,
                                  HWND targetWindow) {
    ProbeComparison comparison;
    comparison.sequence = sequence;
    comparison.monotonicTimestampMicros = monotonicMicros();
    comparison.processId = GetCurrentProcessId();
    comparison.threadId = GetCurrentThreadId();
    comparison.seatId = seatId;
    comparison.probeVkey = probeVkey;
    comparison.targetWindowRuntimeValue = runtimeWindowValue(targetWindow);

    comparison.os.asyncKeyState = GetAsyncKeyState(probeVkey);
    comparison.os.keyState = GetKeyState(probeVkey);
    SetLastError(ERROR_SUCCESS);
    comparison.os.keyboardStateSucceeded =
        GetKeyboardState(comparison.os.keyboardState.data()) != FALSE;
    if (!comparison.os.keyboardStateSucceeded) {
        comparison.os.keyboardStateError = GetLastError();
    }

    POINT cursor{};
    SetLastError(ERROR_SUCCESS);
    comparison.os.cursorPositionSucceeded = GetCursorPos(&cursor) != FALSE;
    if (comparison.os.cursorPositionSucceeded) {
        comparison.os.cursorX = cursor.x;
        comparison.os.cursorY = cursor.y;
    } else {
        comparison.os.cursorPositionError = GetLastError();
    }

    RECT clip{};
    SetLastError(ERROR_SUCCESS);
    comparison.os.clipRectangleSucceeded = GetClipCursor(&clip) != FALSE;
    if (comparison.os.clipRectangleSucceeded) {
        comparison.os.clipRectangle = {
            clip.left, clip.top, clip.right, clip.bottom};
    } else {
        comparison.os.clipRectangleError = GetLastError();
    }
    comparison.os.foregroundWindowRuntimeValue =
        runtimeWindowValue(GetForegroundWindow());
    comparison.os.focusWindowRuntimeValue = runtimeWindowValue(GetFocus());
    comparison.os.captureWindowRuntimeValue = runtimeWindowValue(GetCapture());

    HydraGateCAdapterSnapshotV1 snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    comparison.adapter.snapshotResult = static_cast<std::uint32_t>(
        hydra_gate_c_adapter_get_snapshot(adapter, probeVkey, &snapshot));
    if (comparison.adapter.snapshotResult ==
        static_cast<std::uint32_t>(HYDRA_GATE_C_ADAPTER_OK)) {
        comparison.adapter.lastAppliedSequence =
            snapshot.last_applied_sequence;
        comparison.adapter.asyncKeyState = snapshot.async_key_state_value;
        std::copy_n(snapshot.key_down_bits,
                    comparison.adapter.keyDownBits.size(),
                    comparison.adapter.keyDownBits.begin());
        std::copy_n(snapshot.key_pressed_edge_bits,
                    comparison.adapter.keyPressedEdgeBits.size(),
                    comparison.adapter.keyPressedEdgeBits.begin());
        comparison.adapter.cursorX = snapshot.cursor_x;
        comparison.adapter.cursorY = snapshot.cursor_y;
        comparison.adapter.clipEnabled = snapshot.clip_enabled != 0;
        comparison.adapter.virtualForeground =
            snapshot.virtual_foreground != 0;
        comparison.adapter.virtualCapture = snapshot.virtual_capture != 0;
        comparison.adapter.clipRectangle = {
            snapshot.clip_left, snapshot.clip_top,
            snapshot.clip_right, snapshot.clip_bottom};
    }

    comparison.adapter.keyStateResult = static_cast<std::uint32_t>(
        hydra_gate_c_adapter_get_key_state(
            adapter, probeVkey, &comparison.adapter.keyState));
    comparison.adapter.keyboardStateResult = static_cast<std::uint32_t>(
        hydra_gate_c_adapter_get_keyboard_state(
            adapter, comparison.adapter.keyboardState.data(),
            comparison.adapter.keyboardState.size()));

    HydraGateCAdapterControlStateV1 control{};
    control.struct_size = sizeof(control);
    comparison.adapter.controlStateResult = static_cast<std::uint32_t>(
        hydra_gate_c_adapter_get_control_state(adapter, &control));
    if (comparison.adapter.controlStateResult ==
        static_cast<std::uint32_t>(HYDRA_GATE_C_ADAPTER_OK)) {
        comparison.adapter.cursorX = control.cursor_x;
        comparison.adapter.cursorY = control.cursor_y;
        comparison.adapter.clipEnabled = control.clip_enabled != 0;
        comparison.adapter.virtualForeground = control.virtual_foreground != 0;
        comparison.adapter.virtualCapture = control.virtual_capture != 0;
        comparison.adapter.clipRectangle = {
            control.clip_left, control.clip_top,
            control.clip_right, control.clip_bottom};
    }
    comparison.adapter.mouseStateResult = static_cast<std::uint32_t>(
        hydra_gate_c_adapter_get_mouse_state(
            adapter, &comparison.adapter.mouseButtonsDown,
            &comparison.adapter.wheelAccumulator));

    hydra::gatec::updateProbeComparison(comparison);
    return comparison;
}

bool registerProbeWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) != 0) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

int runLocalBaselineSelfTest(HINSTANCE instance) {
    AdapterOwner adapter;
    if (!adapter || hydra_gate_c_adapter_api_version() !=
                        HYDRA_GATE_C_ADAPTER_API_VERSION ||
        !registerProbeWindowClass(instance)) {
        return 9;
    }
    const HWND window = CreateWindowExW(
        0, kWindowClass, L"HydraSeat API Probe Self-Test",
        WS_OVERLAPPEDWINDOW, 0, 0, 320, 200,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr) return 10;

    ControlStateMessage control;
    control.cursorX = 17;
    control.cursorY = 29;
    control.clipEnabled = true;
    control.virtualForeground = true;
    control.virtualCapture = true;
    control.clipLeft = 0;
    control.clipTop = 0;
    control.clipRight = 100;
    control.clipBottom = 100;
    auto adapterControl = toAdapterControl(control);
    InputEventMessage key;
    key.kind = hydra::gatec::InputKind::Keyboard;
    key.keyTransition = hydra::gatec::KeyTransition::Down;
    key.vkey = 0x41;
    auto adapterKey = toAdapterEvent(key);
    if (hydra_gate_c_adapter_apply_control(adapter.get(), 1,
                                           &adapterControl) !=
            HYDRA_GATE_C_ADAPTER_OK ||
        hydra_gate_c_adapter_apply_input(adapter.get(), 2, &adapterKey) !=
            HYDRA_GATE_C_ADAPTER_OK) {
        DestroyWindow(window);
        return 11;
    }

    const auto comparison = captureComparison(
        adapter.get(), 3, 1, 0x41, window);
    const auto encoded = hydra::gatec::encodeProbeComparison(comparison);
    const auto decoded = hydra::gatec::decodeProbeComparison(encoded);
    const bool passed = decoded && decoded.comparison == comparison &&
        comparison.threadId == GetCurrentThreadId() &&
        comparison.os.keyboardStateSucceeded &&
        comparison.os.cursorPositionSucceeded &&
        comparison.os.clipRectangleSucceeded &&
        comparison.adapter.virtualForeground &&
        comparison.adapter.virtualCapture &&
        comparison.adapter.keyboardState[0x41] == 0x80u &&
        !comparison.osForegroundIsTarget &&
        !comparison.foregroundMatches;
    DestroyWindow(window);
    if (!passed) return 12;
    std::cout
        << "HydraSeat Gate C API probe baseline self-test passed: ordinary "
           "Win32 and direct adapter observations were captured side by side.\n";
    return EXIT_SUCCESS;
}

class ControlledProbe {
public:
    explicit ControlledProbe(ProbeOptions options)
        : m_options(std::move(options)) {}

    int run(HINSTANCE instance, int showCommand) {
        m_instance = instance;
        if (!m_adapter || hydra_gate_c_adapter_api_version() !=
                              HYDRA_GATE_C_ADAPTER_API_VERSION) {
            return 19;
        }
        if (m_options.testNoHandshake) {
            Sleep(2000);
            return 78;
        }
        if (!m_options.testMissingWindow && !createWindow(showCommand)) {
            return 20;
        }
        if (!connectAndHandshake()) {
            destroyWindow();
            return 21;
        }
        if (m_options.testAbnormalExit) {
            destroyWindow();
            return 77;
        }

        m_reader = std::jthread(
            [this](std::stop_token token) { readerLoop(token); });
        MSG message{};
        while (true) {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result == 0) break;
            if (result == -1) {
                m_exitCode.store(25);
                break;
            }
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
    struct CaptureRequest {
        std::mutex mutex;
        std::condition_variable condition;
        std::uint64_t sequence{0};
        std::uint16_t probeVkey{0xffffu};
        std::optional<ProbeComparison> comparison;
        bool cancelled{false};
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
        ControlledProbe* self = reinterpret_cast<ControlledProbe*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<ControlledProbe*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
            if (self != nullptr) self->m_hwnd.store(hwnd);
        }
        if (self != nullptr) {
            return self->windowMessage(hwnd, message, wParam, lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT windowMessage(HWND hwnd, UINT message, WPARAM wParam,
                          LPARAM lParam) {
        (void)wParam;
        switch (message) {
        case kCaptureRequestMessage:
            handleCaptureRequest(
                reinterpret_cast<CaptureRequest*>(lParam), hwnd);
            return 0;
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
            m_hwnd.store(nullptr);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    bool createWindow(int showCommand) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = ControlledProbe::WindowProc;
        windowClass.hInstance = m_instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kWindowClass;
        if (RegisterClassExW(&windowClass) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        const std::wstring title = L"HydraSeat Gate C API Probe - Seat " +
            std::to_wstring(m_options.seatId);
        const DWORD style = m_options.headless
            ? WS_OVERLAPPEDWINDOW
            : WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        const HWND window = CreateWindowExW(
            WS_EX_APPWINDOW, kWindowClass, title.c_str(), style,
            CW_USEDEFAULT, CW_USEDEFAULT, 760, 580,
            nullptr, nullptr, m_instance, this);
        if (window == nullptr) return false;
        m_hwnd.store(window);
        if (!m_options.headless) {
            ShowWindow(window, showCommand);
            UpdateWindow(window);
        }
        return true;
    }

    void destroyWindow() noexcept {
        if (const HWND window = m_hwnd.exchange(nullptr); window != nullptr) {
            DestroyWindow(window);
        }
    }

    bool connectAndHandshake() {
        std::string error;
        std::uint32_t systemError = 0;
        m_channel = hydra::gatec::connectGateCClient(
            m_options.pipeName, 10000, &error, &systemError);
        if (!m_channel.valid()) return false;

        HelloMessage hello;
        hello.token = m_options.token;
        hello.seatId = m_options.seatId;
        hello.processId = GetCurrentProcessId();
        const auto architecture = hydra::gatec::detectProcessArchitecture(
            GetCurrentProcess());
        if (!architecture) return false;
        hello.architectureBits =
            static_cast<std::uint16_t>(architecture.architecture);
        hello.targetWindow = runtimeWindowValue(m_hwnd.load());
        if (!m_channel.writeFrame(hydra::gatec::encodeHello(1, hello),
                                  kIoTimeoutMs, &error, &systemError)) {
            return false;
        }
        const auto response = m_channel.readFrame(kIoTimeoutMs);
        if (!response || !response.frame) return false;
        HelloAckMessage ack;
        if (!hydra::gatec::decodeHelloAck(*response.frame, ack, &error) ||
            response.frame->sequence != 1 || !ack.accepted) {
            return false;
        }
        if ((ack.grantedCapabilities &
             hydra::gatec::testCapabilityBits(
                 hydra::gatec::kControlledApiProbeCapabilities)) !=
            hydra::gatec::testCapabilityBits(
                hydra::gatec::kControlledApiProbeCapabilities)) {
            return false;
        }
        m_connected.store(true);
        return true;
    }

    void readerLoop(std::stop_token stopToken) {
        while (!stopToken.stop_requested() && !m_stopping.load()) {
            const auto result = m_channel.readFrame(kReadPollMs);
            if (result.status == TransportStatus::Timeout) continue;
            if (!result || !result.frame) {
                if (!m_shutdownReceived.load()) m_exitCode.store(24);
                break;
            }
            if (!processFrame(*result.frame)) {
                m_exitCode.store(23);
                break;
            }
        }
        m_stopping.store(true);
        if (const HWND window = m_hwnd.load(); window != nullptr) {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
    }

    bool processFrame(const DecodedFrame& frame) {
        std::string error;
        if (frame.type == MessageType::InputEvent) {
            InputEventMessage message;
            if (!hydra::gatec::decodeInputEvent(frame, message, &error)) {
                return sendError(frame.sequence, 1001);
            }
            const auto event = toAdapterEvent(message);
            const auto result = hydra_gate_c_adapter_apply_input(
                m_adapter.get(), frame.sequence, &event);
            if (result != HYDRA_GATE_C_ADAPTER_OK) {
                return sendError(frame.sequence,
                    1100u + static_cast<std::uint32_t>(result));
            }
            notifyStateChanged();
            return true;
        }
        if (frame.type == MessageType::ControlState) {
            ControlStateMessage message;
            if (!hydra::gatec::decodeControlState(frame, message, &error)) {
                return sendError(frame.sequence, 1003);
            }
            const auto control = toAdapterControl(message);
            const auto result = hydra_gate_c_adapter_apply_control(
                m_adapter.get(), frame.sequence, &control);
            if (result != HYDRA_GATE_C_ADAPTER_OK) {
                return sendError(frame.sequence,
                    1200u + static_cast<std::uint32_t>(result));
            }
            notifyStateChanged();
            return true;
        }
        if (frame.type == MessageType::QuerySnapshot) {
            QuerySnapshotMessage query;
            if (!hydra::gatec::decodeQuerySnapshot(frame, query, &error)) {
                return sendError(frame.sequence, 1005);
            }
            const auto comparison = requestUiCapture(
                frame.sequence, query.probeVkey);
            if (!comparison) return sendError(frame.sequence, 1401);
            const auto payload =
                hydra::gatec::encodeProbeComparison(*comparison);
            const auto response = hydra::gatec::encodeFrame(
                MessageType::ProbeSnapshot, frame.sequence, payload);
            if (payload.empty() || response.empty()) {
                return sendError(frame.sequence, 1402);
            }
            return m_channel.writeFrame(response, kIoTimeoutMs);
        }
        if (frame.type == MessageType::Shutdown) {
            if (!hydra::gatec::decodeShutdown(frame, &error)) {
                return sendError(frame.sequence, 1006);
            }
            m_shutdownReceived.store(true);
            m_stopping.store(true);
            return true;
        }
        return sendError(frame.sequence, 1099);
    }

    std::optional<ProbeComparison> requestUiCapture(
        std::uint64_t sequence, std::uint16_t probeVkey) {
        const HWND window = m_hwnd.load();
        if (window == nullptr) return std::nullopt;
        auto request = std::make_shared<CaptureRequest>();
        request->sequence = sequence;
        request->probeVkey = probeVkey;
        {
            std::scoped_lock lock(m_captureMapMutex);
            m_captureRequests.emplace(request.get(), request);
        }
        if (!PostMessageW(window, kCaptureRequestMessage, 0,
                          reinterpret_cast<LPARAM>(request.get()))) {
            std::scoped_lock lock(m_captureMapMutex);
            m_captureRequests.erase(request.get());
            return std::nullopt;
        }

        std::unique_lock lock(request->mutex);
        const bool ready = request->condition.wait_for(
            lock, std::chrono::milliseconds(kUiCaptureTimeoutMs), [&] {
                return request->comparison.has_value() || request->cancelled;
            });
        if (!ready || !request->comparison) {
            request->cancelled = true;
            lock.unlock();
            std::scoped_lock mapLock(m_captureMapMutex);
            m_captureRequests.erase(request.get());
            return std::nullopt;
        }
        return request->comparison;
    }

    void handleCaptureRequest(CaptureRequest* key, HWND window) {
        std::shared_ptr<CaptureRequest> request;
        {
            std::scoped_lock lock(m_captureMapMutex);
            const auto found = m_captureRequests.find(key);
            if (found == m_captureRequests.end()) return;
            request = found->second;
            m_captureRequests.erase(found);
        }
        const auto comparison = captureComparison(
            m_adapter.get(), request->sequence, m_options.seatId,
            request->probeVkey, window);
        {
            std::scoped_lock lock(request->mutex);
            if (!request->cancelled) request->comparison = comparison;
        }
        request->condition.notify_all();
        {
            std::scoped_lock lock(m_lastComparisonMutex);
            m_lastComparison = comparison;
        }
        InvalidateRect(window, nullptr, FALSE);
    }

    bool sendError(std::uint64_t sequence, std::uint32_t code) {
        std::string ignored;
        (void)m_channel.writeFrame(
            hydra::gatec::encodeError(sequence, ErrorMessage{code}),
            kIoTimeoutMs, &ignored, nullptr);
        return false;
    }

    void notifyStateChanged() {
        if (const HWND window = m_hwnd.load(); window != nullptr) {
            PostMessageW(window, kStateChangedMessage, 0, 0);
        }
    }

    void paint(HWND hwnd) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        HBRUSH background = CreateSolidBrush(RGB(15, 23, 42));
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(226, 232, 240));

        std::optional<ProbeComparison> last;
        {
            std::scoped_lock lock(m_lastComparisonMutex);
            last = m_lastComparison;
        }
        std::wostringstream text;
        text << L"HydraSeat Gate C API Baseline — Seat "
             << m_options.seatId
             << L"\n\nREAD-ONLY BASELINE — NO API INTERPOSITION INSTALLED"
             << L"\n\nProcess ID: " << GetCurrentProcessId();
        if (last) {
            text << L"\nProbe UI thread ID: " << last->threadId
                 << L"\nSequence: " << last->sequence
                 << L"\nProbe key: " << last->probeVkey
                 << L"\nOS GetAsyncKeyState: 0x" << std::hex
                 << static_cast<std::uint16_t>(last->os.asyncKeyState)
                 << L"\nOS GetKeyState: 0x"
                 << static_cast<std::uint16_t>(last->os.keyState)
                 << L"\nAdapter async state: 0x"
                 << last->adapter.asyncKeyState
                 << L"\nAdapter key state: 0x"
                 << last->adapter.keyState << std::dec
                 << L"\nOS cursor: (" << last->os.cursorX << L", "
                 << last->os.cursorY << L")"
                 << L"\nAdapter cursor: (" << last->adapter.cursorX
                 << L", " << last->adapter.cursorY << L")"
                 << L"\nOS foreground is target: "
                 << (last->osForegroundIsTarget ? L"true" : L"false")
                 << L"\nAdapter virtual foreground: "
                 << (last->adapter.virtualForeground ? L"true" : L"false")
                 << L"\nForeground views match: "
                 << (last->foregroundMatches ? L"true" : L"false")
                 << L"\nOS focus is target: "
                 << (last->osFocusIsTarget ? L"true" : L"false")
                 << L"\nOS capture is target: "
                 << (last->osCaptureIsTarget ? L"true" : L"false")
                 << L"\nAdapter virtual capture: "
                 << (last->adapter.virtualCapture ? L"true" : L"false");
        }
        auto content = text.str();
        client.left += 24;
        client.top += 20;
        client.right -= 24;
        DrawTextW(dc, content.c_str(), -1, &client,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        EndPaint(hwnd, &paint);
    }

    ProbeOptions m_options;
    HINSTANCE m_instance{nullptr};
    std::atomic<HWND> m_hwnd{nullptr};
    PipeChannel m_channel;
    AdapterOwner m_adapter;
    std::jthread m_reader;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_shutdownReceived{false};
    std::atomic<int> m_exitCode{0};
    std::mutex m_captureMapMutex;
    std::unordered_map<CaptureRequest*, std::shared_ptr<CaptureRequest>>
        m_captureRequests;
    std::mutex m_lastComparisonMutex;
    std::optional<ProbeComparison> m_lastComparison;
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
    if (options.baselineSelfTest && options.pipeName.empty()) {
        return runLocalBaselineSelfTest(GetModuleHandleW(nullptr));
    }
    ControlledProbe probe(options);
    return probe.run(GetModuleHandleW(nullptr), SW_SHOWNORMAL);
}
#else
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    printUsage(std::cerr);
    return 3;
}
#endif
