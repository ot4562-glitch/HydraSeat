#include "hydra/gate_c_adapter.h"
#include "hydra/gate_c_architecture.hpp"
#include "hydra/gate_c_recovery.hpp"
#include "hydra/rollback_registry.hpp"
#include "hydra/reset_actions.hpp"
#include "hydra/gate_c_protocol.hpp"
#include "hydra/gate_c_probe_snapshot.hpp"
#include "hydra/gate_c_transport.hpp"
#include "hydra/input_metrics.hpp"
#include "hydra/input_observation.hpp"
#include "hydra/input_router.hpp"
#include "hydra/virtual_input_state.hpp"
#include "hydra/workspace_manager.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using hydra::InputObservationSession;
using hydra::InputRouteDecision;
using hydra::InputRouteRecord;
using hydra::RawInputDeviceChange;
using hydra::RawInputEvent;
using hydra::SeatId;
using hydra::SeatRoutingPolicy;
using hydra::WorkspaceManager;
using hydra::gatec::ControlStateMessage;
using hydra::gatec::HelloAckMessage;
using hydra::gatec::HelloMessage;
using hydra::gatec::InputEventMessage;
using hydra::gatec::InputKind;
using hydra::gatec::KeyTransition;
using hydra::gatec::MessageType;
using hydra::gatec::PipeChannel;
using hydra::gatec::ProbeComparison;
using hydra::gatec::QuerySnapshotMessage;
using hydra::gatec::SessionToken;
using hydra::gatec::StateSnapshotMessage;
using hydra::gatec::TransportStatus;

struct HostOptions {
    std::wstring targetPath;
    std::wstring shimPath;
    std::wstring artifactRoot;
    hydra::gatec::ProcessArchitecture requestedArchitecture{
        hydra::gatec::ProcessArchitecture::Unknown};
    std::string profilePath{"workspace_config.json"};
    std::string tracePath{"hydra_gate_c_host.jsonl"};
    std::string metricsReportPath{"hydra_gate_c_metrics.json"};
    std::wstring recoveryDirectory;
    std::string recoveryScenario;
    bool recoverySelfTest{false};
    bool metricsDiagnostic{false};
    bool traceSensitiveKeyIds{false};
    bool selfTest{false};
    bool architectureSelfTest{false};
    bool baselineSelfTest{false};
    bool pollingShimSelfTest{false};
    bool cursorFocusShimSelfTest{false};
    bool rawInputShimSelfTest{false};
    bool xinputSelfTest{false};
    bool protocolErrorSelfTest{false};
    bool showHelp{false};
};

void printUsage(std::ostream& output) {
    output
        << "HydraSeat Gate C controlled-process host\n\n"
        << "Usage:\n"
        << "  hydra_gate_c_host --self-test [--target <hydra_gate_c_target.exe>]\n"
        << "  hydra_gate_c_host --architecture-self-test --artifact-root <gate-c>\n"
        << "                     --target-architecture <x86|x64>\n"
        << "  hydra_gate_c_host --baseline-self-test --target <hydra_gate_c_api_probe.exe>\n"
        << "  hydra_gate_c_host --polling-shim-self-test --target <hydra_gate_c_api_probe.exe>\n"
        << "                     --shim <hydra_gate_c_shim.dll>\n"
        << "  hydra_gate_c_host --xinput-self-test --target <hydra_gate_c_api_probe.exe>\n"
        << "  hydra_gate_c_host --cursor-focus-shim-self-test --target <hydra_gate_c_api_probe.exe>\n"
        << "                     --shim <hydra_gate_c_shim.dll>\n"
        << "  hydra_gate_c_host --raw-input-shim-self-test --target <hydra_gate_c_api_probe.exe>\n"
        << "                     --shim <hydra_gate_c_shim.dll>\n"
        << "  hydra_gate_c_host --protocol-error-self-test [--target <hydra_gate_c_target.exe>]\n"
        << "  hydra_gate_c_host --recovery-self-test <scenario> --recovery-dir <path>\n"
        << "  hydra_gate_c_host [--profile <workspace_config.json>] [--trace <file.jsonl>]\n"
        << "                     [--metrics-report <file.json>] [--metrics-diagnostic]\n"
        << "                     [--trace-sensitive-keys]\n"
        << "                     [--target <hydra_gate_c_target.exe>]\n\n"
        << "Metrics are redacted by default; --metrics-diagnostic retains key/button IDs.\n"
        << "JSONL key identifiers are also redacted by default; --trace-sensitive-keys is a separate explicit opt-in.\n"
        << "The host launches only HydraSeat's controlled target executable. It does\n"
        << "not inject, hook, hide devices, or attach to a commercial game.\n";
}

#ifndef _WIN32

int portableSelfTest() {
    const auto token = hydra::gatec::generateSessionToken();
    if (!token) {
        return 9;
    }
    const auto tokenText = hydra::gatec::tokenToHex(*token);
    if (!hydra::gatec::tokenFromHex(tokenText)) {
        return 10;
    }
    std::cout << "HydraSeat Gate C host portable self-test passed.\n";
    return EXIT_SUCCESS;
}

#else

constexpr std::uint32_t kHandshakeTimeoutMs = 10000;
constexpr std::uint32_t kIoTimeoutMs = 5000;
constexpr std::uint32_t kProcessExitTimeoutMs = 5000;

std::atomic<bool> gStopRequested{false};

BOOL WINAPI consoleControlHandler(DWORD controlType) {
    if (controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT ||
        controlType == CTRL_CLOSE_EVENT || controlType == CTRL_LOGOFF_EVENT ||
        controlType == CTRL_SHUTDOWN_EVENT) {
        gStopRequested.store(true);
        return TRUE;
    }
    return FALSE;
}

enum class SessionEndKind : std::uint32_t {
    None = 0,
    Logoff = 1,
    Shutdown = 2,
};

// Interactive Gate C loads user32 for Raw Input and controlled windows. Windows
// does not reliably deliver CTRL_LOGOFF_EVENT/CTRL_SHUTDOWN_EVENT to such a
// console process, so real desktop-session termination is observed through a
// dedicated hidden top-level window instead. The window runs on a dedicated
// message thread so WM_QUERYENDSESSION can wait for the ordinary control loop
// to finish durable rollback before Windows is told the session may end. The
// wait is bounded; timeout or unproven cleanup vetoes the session end rather
// than allowing an Active crash journal to survive.
class GateCSessionEndWindow {
public:
    GateCSessionEndWindow() = default;
    GateCSessionEndWindow(const GateCSessionEndWindow&) = delete;
    GateCSessionEndWindow& operator=(const GateCSessionEndWindow&) = delete;

    ~GateCSessionEndWindow() {
        close();
    }

    bool initialize(std::string* error = nullptr) {
        if (m_thread.joinable()) return m_initSucceeded.load();

        m_readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_cleanupEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (m_readyEvent == nullptr || m_cleanupEvent == nullptr) {
            const DWORD systemError = GetLastError();
            if (m_readyEvent != nullptr) {
                CloseHandle(m_readyEvent);
                m_readyEvent = nullptr;
            }
            if (m_cleanupEvent != nullptr) {
                CloseHandle(m_cleanupEvent);
                m_cleanupEvent = nullptr;
            }
            if (error != nullptr) {
                *error = "session-end event creation failed: " +
                         std::to_string(systemError);
            }
            return false;
        }

        m_initSucceeded.store(false);
        m_initStage.store(InitStage::None);
        m_initError.store(ERROR_SUCCESS);
        m_cleanupSucceeded.store(false);
        m_lastKind.store(SessionEndKind::None);
        m_lastMessage.store(0);
        m_queryTimedOut.store(false);
        m_thread = std::thread([this] { messageThread(); });

        const DWORD ready = WaitForSingleObject(m_readyEvent, kStartupTimeoutMs);
        if (ready != WAIT_OBJECT_0 || !m_initSucceeded.load()) {
            if (error != nullptr) {
                if (ready == WAIT_TIMEOUT) {
                    *error = "session-end window startup timed out";
                } else if (ready != WAIT_OBJECT_0) {
                    *error = "session-end window startup wait failed: " +
                             std::to_string(GetLastError());
                } else {
                    const char* stage = m_initStage.load() == InitStage::RegisterClass
                        ? "RegisterClassExW(session-end) failed: "
                        : "CreateWindowExW(session-end) failed: ";
                    *error = std::string(stage) +
                             std::to_string(m_initError.load());
                }
            }
            close();
            return false;
        }
        return m_window.load() != nullptr;
    }

    void close() noexcept {
        if (m_cleanupEvent != nullptr) {
            m_cleanupSucceeded.store(false);
            (void)SetEvent(m_cleanupEvent);
        }
        const HWND window = m_window.load();
        bool closePosted = false;
        if (window != nullptr && IsWindow(window)) {
            closePosted = PostMessageW(window, WM_CLOSE, 0, 0) != FALSE;
        }
        const DWORD threadId = m_threadId.load();
        if (!closePosted && threadId != 0) {
            (void)PostThreadMessageW(threadId, WM_QUIT, 0, 0);
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
        m_window.store(nullptr);
        m_threadId.store(0);
        if (m_readyEvent != nullptr) {
            CloseHandle(m_readyEvent);
            m_readyEvent = nullptr;
        }
        if (m_cleanupEvent != nullptr) {
            CloseHandle(m_cleanupEvent);
            m_cleanupEvent = nullptr;
        }
    }

    [[nodiscard]] HWND window() const noexcept {
        return m_window.load();
    }

    [[nodiscard]] SessionEndKind lastKind() const noexcept {
        return m_lastKind.load();
    }

    [[nodiscard]] UINT lastMessage() const noexcept {
        return m_lastMessage.load();
    }

    [[nodiscard]] bool queryTimedOut() const noexcept {
        return m_queryTimedOut.load();
    }

    void signalCleanupComplete(bool succeeded) noexcept {
        m_cleanupSucceeded.store(succeeded);
        if (m_cleanupEvent != nullptr) {
            (void)SetEvent(m_cleanupEvent);
        }
    }

private:
    enum class InitStage : std::uint32_t {
        None = 0,
        RegisterClass = 1,
        CreateWindow = 2,
    };

    static constexpr wchar_t kWindowClass[] =
        L"HydraSeatGateCSessionEndMonitor";
    static constexpr DWORD kStartupTimeoutMs = 5'000;
    static constexpr DWORD kCleanupWaitMs = 5'000;

    static SessionEndKind kindFromFlags(LPARAM flags) noexcept {
        return (static_cast<std::uintptr_t>(flags) & ENDSESSION_LOGOFF) != 0
                   ? SessionEndKind::Logoff
                   : SessionEndKind::Shutdown;
    }

    void requestStop(UINT message, LPARAM flags) noexcept {
        m_lastKind.store(kindFromFlags(flags));
        m_lastMessage.store(message);
        gStopRequested.store(true);
    }

    void messageThread() noexcept {
        m_threadId.store(GetCurrentThreadId());
        MSG queueMessage{};
        (void)PeekMessageW(&queueMessage, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = GateCSessionEndWindow::WndProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kWindowClass;

        if (RegisterClassExW(&windowClass) == 0) {
            const DWORD systemError = GetLastError();
            if (systemError != ERROR_CLASS_ALREADY_EXISTS) {
                m_initStage.store(InitStage::RegisterClass);
                m_initError.store(systemError);
                (void)SetEvent(m_readyEvent);
                return;
            }
        }

        const HWND window = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kWindowClass,
            L"HydraSeat Session End Monitor",
            WS_POPUP,
            0, 0, 0, 0,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            this);
        if (window == nullptr) {
            m_initStage.store(InitStage::CreateWindow);
            m_initError.store(GetLastError());
            (void)SetEvent(m_readyEvent);
            return;
        }

        m_window.store(window);
        m_initSucceeded.store(true);
        (void)SetEvent(m_readyEvent);

        MSG message{};
        while (true) {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result <= 0) break;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        m_window.store(nullptr);
    }

    static LRESULT CALLBACK WndProc(HWND window, UINT message,
                                    WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<GateCSessionEndWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<GateCSessionEndWindow*>(create->lpCreateParams);
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(
                window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
                return FALSE;
            }
        }

        if (self != nullptr) {
            if (message == WM_QUERYENDSESSION) {
                self->requestStop(message, lParam);
                const DWORD wait = self->m_cleanupEvent == nullptr
                    ? WAIT_FAILED
                    : WaitForSingleObject(self->m_cleanupEvent, kCleanupWaitMs);
                if (wait == WAIT_TIMEOUT) {
                    self->m_queryTimedOut.store(true);
                }
                return wait == WAIT_OBJECT_0 &&
                           self->m_cleanupSucceeded.load()
                    ? TRUE
                    : FALSE;
            }
            if (message == WM_ENDSESSION) {
                if (wParam != FALSE) {
                    self->requestStop(message, lParam);
                }
                return 0;
            }
            if (message == WM_CLOSE) {
                DestroyWindow(window);
                return 0;
            }
            if (message == WM_DESTROY) {
                PostQuitMessage(0);
                return 0;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    std::thread m_thread;
    std::atomic<DWORD> m_threadId{0};
    HANDLE m_readyEvent{nullptr};
    HANDLE m_cleanupEvent{nullptr};
    std::atomic<HWND> m_window{nullptr};
    std::atomic<bool> m_initSucceeded{false};
    std::atomic<InitStage> m_initStage{InitStage::None};
    std::atomic<DWORD> m_initError{ERROR_SUCCESS};
    std::atomic<bool> m_cleanupSucceeded{false};
    std::atomic<bool> m_queryTimedOut{false};
    std::atomic<SessionEndKind> m_lastKind{SessionEndKind::None};
    std::atomic<UINT> m_lastMessage{0};
};

std::wstring utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), required) != required) {
        return {};
    }
    return result;
}

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

std::wstring quoteArgument(std::wstring_view argument) {
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

bool parseOptions(int argc, wchar_t** argv, HostOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--target" && index + 1 < argc) {
            options.targetPath = argv[++index];
        } else if (argument == L"--shim" && index + 1 < argc) {
            options.shimPath = argv[++index];
        } else if (argument == L"--artifact-root" && index + 1 < argc) {
            options.artifactRoot = argv[++index];
        } else if (argument == L"--target-architecture" && index + 1 < argc) {
            const auto parsed = hydra::gatec::parseProcessArchitecture(
                wideToUtf8(argv[++index]));
            if (!parsed) return false;
            options.requestedArchitecture = *parsed;
        } else if (argument == L"--profile" && index + 1 < argc) {
            options.profilePath = wideToUtf8(argv[++index]);
        } else if (argument == L"--trace" && index + 1 < argc) {
            options.tracePath = wideToUtf8(argv[++index]);
        } else if (argument == L"--metrics-report" && index + 1 < argc) {
            options.metricsReportPath = wideToUtf8(argv[++index]);
        } else if (argument == L"--recovery-dir" && index + 1 < argc) {
            options.recoveryDirectory = argv[++index];
        } else if (argument == L"--recovery-self-test" && index + 1 < argc) {
            options.recoveryScenario = wideToUtf8(argv[++index]);
            options.recoverySelfTest = !options.recoveryScenario.empty();
        } else if (argument == L"--metrics-diagnostic") {
            options.metricsDiagnostic = true;
        } else if (argument == L"--trace-sensitive-keys") {
            options.traceSensitiveKeyIds = true;
        } else if (argument == L"--self-test") {
            options.selfTest = true;
        } else if (argument == L"--architecture-self-test") {
            options.architectureSelfTest = true;
        } else if (argument == L"--baseline-self-test") {
            options.baselineSelfTest = true;
        } else if (argument == L"--polling-shim-self-test") {
            options.pollingShimSelfTest = true;
        } else if (argument == L"--cursor-focus-shim-self-test") {
            options.cursorFocusShimSelfTest = true;
        } else if (argument == L"--raw-input-shim-self-test") {
            options.rawInputShimSelfTest = true;
        } else if (argument == L"--xinput-self-test") {
            options.xinputSelfTest = true;
        } else if (argument == L"--protocol-error-self-test") {
            options.protocolErrorSelfTest = true;
        } else if (argument == L"--help" || argument == L"-h") {
            options.showHelp = true;
        } else {
            return false;
        }
    }
    const int selfTestModes = static_cast<int>(options.selfTest) +
        static_cast<int>(options.architectureSelfTest) +
        static_cast<int>(options.baselineSelfTest) +
        static_cast<int>(options.pollingShimSelfTest) +
        static_cast<int>(options.cursorFocusShimSelfTest) +
        static_cast<int>(options.rawInputShimSelfTest) +
        static_cast<int>(options.xinputSelfTest) +
        static_cast<int>(options.protocolErrorSelfTest) +
        static_cast<int>(options.recoverySelfTest);
    if (selfTestModes > 1) {
        return false;
    }
    const bool hasArtifactRoot = !options.artifactRoot.empty();
    const bool hasArchitecture = options.requestedArchitecture !=
        hydra::gatec::ProcessArchitecture::Unknown;
    if (hasArtifactRoot != hasArchitecture ||
        (hasArtifactRoot &&
         (!options.targetPath.empty() || !options.shimPath.empty())) ||
        (options.architectureSelfTest && !hasArtifactRoot) ||
        (!options.pollingShimSelfTest &&
         !options.cursorFocusShimSelfTest &&
         !options.rawInputShimSelfTest && !options.shimPath.empty())) {
        return false;
    }
    return true;
}

std::wstring siblingShimPath() {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return L"hydra_gate_c_shim.dll";
    }
    std::wstring path(buffer.data(), length);
    const auto separator = path.find_last_of(L"\\/");
    path.resize(separator == std::wstring::npos ? 0 : separator + 1);
    path += L"hydra_gate_c_shim.dll";
    return path;
}

std::wstring siblingWatchdogPath() {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return L"hydra_watchdog.exe";
    }
    std::wstring path(buffer.data(), length);
    const auto separator = path.find_last_of(L"\\/");
    path.resize(separator == std::wstring::npos ? 0 : separator + 1);
    path += L"hydra_watchdog.exe";
    return path;
}

std::wstring siblingTargetPath(bool apiProbe) {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return L"hydra_gate_c_target.exe";
    }
    std::wstring path(buffer.data(), length);
    const auto separator = path.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        path.resize(separator + 1);
    } else {
        path.clear();
    }
    path += apiProbe ? L"hydra_gate_c_api_probe.exe"
                     : L"hydra_gate_c_target.exe";
    return path;
}

class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess() { close(); }

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept
        : m_process(std::exchange(other.m_process, nullptr)),
          m_primaryThread(std::exchange(other.m_primaryThread, nullptr)),
          m_processId(std::exchange(other.m_processId, 0)) {}
    ChildProcess& operator=(ChildProcess&& other) noexcept {
        if (this == &other) return *this;
        close();
        m_process = std::exchange(other.m_process, nullptr);
        m_primaryThread = std::exchange(other.m_primaryThread, nullptr);
        m_processId = std::exchange(other.m_processId, 0);
        return *this;
    }

    bool valid() const noexcept { return m_process != nullptr; }
    HANDLE handle() const noexcept { return m_process; }
    std::uint32_t processId() const noexcept { return m_processId; }
    bool suspended() const noexcept { return m_primaryThread != nullptr; }

    void assign(HANDLE process, HANDLE primaryThread,
                std::uint32_t processId) noexcept {
        close();
        m_process = process;
        m_primaryThread = primaryThread;
        m_processId = processId;
    }

    bool resume() noexcept {
        if (!valid()) return false;
        if (m_primaryThread == nullptr) return true;
        if (ResumeThread(m_primaryThread) == static_cast<DWORD>(-1)) {
            return false;
        }
        CloseHandle(m_primaryThread);
        m_primaryThread = nullptr;
        return true;
    }

    bool running() const noexcept {
        return valid() && WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT;
    }

    bool wait(std::uint32_t timeoutMilliseconds,
              std::uint32_t* exitCode = nullptr) const {
        if (!valid()) return false;
        if (WaitForSingleObject(m_process, timeoutMilliseconds) != WAIT_OBJECT_0) {
            return false;
        }
        DWORD code = 0;
        if (!GetExitCodeProcess(m_process, &code)) {
            return false;
        }
        if (exitCode != nullptr) *exitCode = code;
        return true;
    }

    void terminate(std::uint32_t exitCode) noexcept {
        if (running()) {
            TerminateProcess(m_process, exitCode);
            WaitForSingleObject(m_process, 2000);
        }
    }

    void close() noexcept {
        if (m_primaryThread != nullptr) {
            CloseHandle(m_primaryThread);
            m_primaryThread = nullptr;
        }
        if (m_process != nullptr) {
            CloseHandle(m_process);
            m_process = nullptr;
            m_processId = 0;
        }
    }

private:
    HANDLE m_process{nullptr};
    HANDLE m_primaryThread{nullptr};
    std::uint32_t m_processId{0};
};

class GateCWatchdogClient {
public:
    GateCWatchdogClient() = default;
    ~GateCWatchdogClient() { closeTransport(); }

    GateCWatchdogClient(const GateCWatchdogClient&) = delete;
    GateCWatchdogClient& operator=(const GateCWatchdogClient&) = delete;

    bool start(const hydra::watchdog::RollbackPlanManifest& manifest,
               std::string* error = nullptr) {
        closeTransport();
        m_watchdog.terminate(0x52454357u); // "RECW".
        m_watchdog.close();

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        HANDLE controlRead = nullptr;
        HANDLE controlWrite = nullptr;
        HANDLE statusRead = nullptr;
        HANDLE statusWrite = nullptr;
        if (CreatePipe(&controlRead, &controlWrite, &security, 0) == FALSE ||
            CreatePipe(&statusRead, &statusWrite, &security, 0) == FALSE) {
            const DWORD systemError = GetLastError();
            if (controlRead != nullptr) CloseHandle(controlRead);
            if (controlWrite != nullptr) CloseHandle(controlWrite);
            if (statusRead != nullptr) CloseHandle(statusRead);
            if (statusWrite != nullptr) CloseHandle(statusWrite);
            setError(error, "watchdog pipe creation failed", systemError);
            return false;
        }
        if (SetHandleInformation(controlWrite, HANDLE_FLAG_INHERIT, 0) == FALSE ||
            SetHandleInformation(statusRead, HANDLE_FLAG_INHERIT, 0) == FALSE) {
            const DWORD systemError = GetLastError();
            CloseHandle(controlRead);
            CloseHandle(controlWrite);
            CloseHandle(statusRead);
            CloseHandle(statusWrite);
            setError(error, "watchdog host handle inheritance setup failed",
                     systemError);
            return false;
        }

        hydra::watchdog::ProcessIdentity hostIdentity;
        std::uint32_t identityError = 0;
        if (!hydra::watchdog::queryProcessIdentity(
                GetCurrentProcessId(), hostIdentity, &identityError)) {
            CloseHandle(controlRead);
            CloseHandle(controlWrite);
            CloseHandle(statusRead);
            CloseHandle(statusWrite);
            setError(error, "host creation identity query failed", identityError);
            return false;
        }

        const auto watchdogPath = siblingWatchdogPath();
        std::wstring command = quoteArgument(watchdogPath) +
            L" --control-handle " + handleNumber(controlRead) +
            L" --status-handle " + handleNumber(statusWrite) +
            L" --host-pid " + std::to_wstring(hostIdentity.processId) +
            L" --host-created " +
            std::to_wstring(hostIdentity.creationTime100ns) +
            L" --session " + sessionHex(manifest.lease.sessionId);
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        SIZE_T attributeBytes = 0;
        if (InitializeProcThreadAttributeList(nullptr, 1, 0,
                                              &attributeBytes) != FALSE ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            attributeBytes == 0) {
            CloseHandle(controlRead);
            CloseHandle(controlWrite);
            CloseHandle(statusRead);
            CloseHandle(statusWrite);
            setError(error, "watchdog handle-list sizing failed", GetLastError());
            return false;
        }
        std::vector<std::byte> attributeStorage(attributeBytes);
        startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
            attributeStorage.data());
        if (InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
                                              &attributeBytes) == FALSE) {
            const DWORD systemError = GetLastError();
            CloseHandle(controlRead);
            CloseHandle(controlWrite);
            CloseHandle(statusRead);
            CloseHandle(statusWrite);
            setError(error, "watchdog handle-list initialization failed",
                     systemError);
            return false;
        }
        const std::array<HANDLE, 2> inherited{controlRead, statusWrite};
        if (UpdateProcThreadAttribute(
                startup.lpAttributeList, 0,
                PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                const_cast<HANDLE*>(inherited.data()),
                inherited.size() * sizeof(HANDLE), nullptr, nullptr) == FALSE) {
            const DWORD systemError = GetLastError();
            DeleteProcThreadAttributeList(startup.lpAttributeList);
            CloseHandle(controlRead);
            CloseHandle(controlWrite);
            CloseHandle(statusRead);
            CloseHandle(statusWrite);
            setError(error, "watchdog inherited-handle allowlist failed",
                     systemError);
            return false;
        }

        PROCESS_INFORMATION process{};
        const BOOL created = CreateProcessW(
            watchdogPath.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT |
                CREATE_UNICODE_ENVIRONMENT,
            nullptr, nullptr, &startup.StartupInfo, &process);
        const DWORD createError = created == FALSE ? GetLastError() : 0;
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        CloseHandle(controlRead);
        CloseHandle(statusWrite);
        if (created == FALSE) {
            CloseHandle(controlWrite);
            CloseHandle(statusRead);
            setError(error, "hydra_watchdog launch failed", createError);
            return false;
        }
        CloseHandle(process.hThread);
        m_watchdog.assign(process.hProcess, nullptr, process.dwProcessId);
        m_controlWrite = controlWrite;
        m_statusRead = statusRead;
        m_manifest = manifest;
        m_sequence = 1;

        if (!sendFrame(hydra::watchdog::encodeRegisterPlan(
                m_sequence, m_manifest), error)) {
            failStart();
            return false;
        }
        hydra::watchdog::WatchdogStatus status;
        if (!waitStatus(status, kHandshakeTimeoutMs, error) ||
            status.state != hydra::watchdog::WatchdogRunState::Armed ||
            status.sessionId != m_manifest.lease.sessionId ||
            status.generation != m_manifest.lease.generation) {
            if (error != nullptr && error->empty()) {
                *error = "watchdog did not confirm the registered Gate C plan";
            }
            failStart();
            return false;
        }
        return true;
    }

    bool renew(std::string* error = nullptr) {
        if (!running()) {
            if (error != nullptr) *error = "watchdog is not running";
            return false;
        }
        return sendFrame(hydra::watchdog::encodeLeaseRenewal(
            ++m_sequence, m_manifest.lease), error);
    }

    bool disarm(std::string* error = nullptr) {
        if (!running()) {
            if (error != nullptr) *error = "watchdog is not running";
            return false;
        }
        const auto sequence = ++m_sequence;
        if (!sendFrame(hydra::watchdog::encodeDisarm(
                sequence, m_manifest.lease), error)) {
            return false;
        }
        hydra::watchdog::WatchdogStatus status;
        if (!waitStatus(status, kHandshakeTimeoutMs, error)) return false;
        if (status.state != hydra::watchdog::WatchdogRunState::Disarmed ||
            status.reason != hydra::watchdog::WatchdogTriggerReason::CleanDisarm ||
            status.sessionId != m_manifest.lease.sessionId ||
            status.generation != m_manifest.lease.generation) {
            if (error != nullptr) {
                *error = "watchdog refused clean Gate C disarm";
            }
            return false;
        }
        std::uint32_t exitCode = 0;
        if (!m_watchdog.wait(kProcessExitTimeoutMs, &exitCode) ||
            exitCode != 0) {
            if (error != nullptr) {
                *error = "watchdog did not exit cleanly after disarm";
            }
            return false;
        }
        closeTransport();
        return true;
    }

    bool restartForVerification(std::string* error = nullptr) {
        const auto manifest = m_manifest;
        return start(manifest, error);
    }

    bool running() const noexcept { return m_watchdog.running(); }
    std::uint32_t processId() const noexcept { return m_watchdog.processId(); }

    bool terminateForTest() noexcept {
        if (!m_watchdog.running()) return true;
        m_watchdog.terminate(0x5744474bu); // "WDGK".
        closeTransport();
        return !m_watchdog.running();
    }

private:
    static void setError(std::string* error, std::string_view message,
                         std::uint32_t systemError) {
        if (error != nullptr) {
            *error = std::string(message) + " (" +
                std::to_string(systemError) + ")";
        }
    }

    static std::wstring handleNumber(HANDLE handle) {
        return std::to_wstring(static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(handle)));
    }

    static std::wstring sessionHex(
        const hydra::watchdog::SessionId& sessionId) {
        constexpr wchar_t digits[] = L"0123456789abcdef";
        std::wstring result;
        result.reserve(sessionId.size() * 2);
        for (const auto byte : sessionId) {
            result.push_back(digits[(byte >> 4u) & 0x0fu]);
            result.push_back(digits[byte & 0x0fu]);
        }
        return result;
    }

    bool writeAll(const void* bytes, std::size_t size) {
        const auto* cursor = static_cast<const std::byte*>(bytes);
        std::size_t remaining = size;
        while (remaining != 0) {
            const DWORD chunk = static_cast<DWORD>((std::min)(
                remaining,
                static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
            DWORD written = 0;
            if (WriteFile(m_controlWrite, cursor, chunk, &written,
                          nullptr) == FALSE || written == 0) {
                return false;
            }
            cursor += written;
            remaining -= written;
        }
        return true;
    }

    bool sendFrame(const std::vector<std::byte>& frame,
                   std::string* error) {
        if (m_controlWrite == nullptr || frame.empty() ||
            frame.size() > (std::numeric_limits<std::uint32_t>::max)()) {
            if (error != nullptr) *error = "watchdog frame is invalid";
            return false;
        }
        const auto frameBytes = static_cast<std::uint32_t>(frame.size());
        const std::array<std::byte, 4> prefix{
            static_cast<std::byte>(frameBytes & 0xffu),
            static_cast<std::byte>((frameBytes >> 8u) & 0xffu),
            static_cast<std::byte>((frameBytes >> 16u) & 0xffu),
            static_cast<std::byte>((frameBytes >> 24u) & 0xffu)};
        if (!writeAll(prefix.data(), prefix.size()) ||
            !writeAll(frame.data(), frame.size())) {
            const DWORD systemError = GetLastError();
            setError(error, "watchdog frame write failed", systemError);
            return false;
        }
        return true;
    }

    bool waitStatus(hydra::watchdog::WatchdogStatus& status,
                    std::uint32_t timeoutMilliseconds,
                    std::string* error) {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeoutMilliseconds);
        while (std::chrono::steady_clock::now() < deadline) {
            if (m_statusRead == nullptr) return false;
            DWORD available = 0;
            std::array<std::byte, 4> prefix{};
            DWORD peeked = 0;
            if (PeekNamedPipe(m_statusRead, prefix.data(),
                              static_cast<DWORD>(prefix.size()),
                              &peeked, &available, nullptr) == FALSE) {
                setError(error, "watchdog status pipe failed", GetLastError());
                return false;
            }
            if (available < prefix.size() || peeked < prefix.size()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            const auto frameBytes =
                static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(prefix[0])) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(prefix[1])) << 8u) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(prefix[2])) << 16u) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(prefix[3])) << 24u);
            if (frameBytes < hydra::watchdog::kWatchdogFrameHeaderBytes ||
                frameBytes > hydra::watchdog::kWatchdogMaxFrameBytes) {
                if (error != nullptr) *error = "watchdog status frame length is invalid";
                return false;
            }
            const std::uint64_t totalBytes =
                static_cast<std::uint64_t>(prefix.size()) + frameBytes;
            if (static_cast<std::uint64_t>(available) < totalBytes) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            std::vector<std::byte> combined(static_cast<std::size_t>(totalBytes));
            DWORD read = 0;
            if (ReadFile(m_statusRead, combined.data(),
                         static_cast<DWORD>(combined.size()), &read,
                         nullptr) == FALSE ||
                static_cast<std::size_t>(read) != combined.size()) {
                setError(error, "watchdog status read failed", GetLastError());
                return false;
            }
            const auto decoded = hydra::watchdog::decodeWatchdogFrame(
                std::span<const std::byte>(combined).subspan(prefix.size()));
            std::string decodeError;
            if (!decoded ||
                !hydra::watchdog::decodeWatchdogStatus(
                    *decoded.frame, status, &decodeError)) {
                if (error != nullptr) {
                    *error = decodeError.empty()
                        ? "watchdog status decode failed"
                        : decodeError;
                }
                return false;
            }
            return true;
        }
        if (error != nullptr) *error = "watchdog status timed out";
        return false;
    }

    void failStart() noexcept {
        closeTransport();
        m_watchdog.terminate(0x52454346u); // "RECF".
        m_watchdog.close();
    }

    void closeTransport() noexcept {
        if (m_controlWrite != nullptr) {
            CloseHandle(m_controlWrite);
            m_controlWrite = nullptr;
        }
        if (m_statusRead != nullptr) {
            CloseHandle(m_statusRead);
            m_statusRead = nullptr;
        }
    }

    HANDLE m_controlWrite{nullptr};
    HANDLE m_statusRead{nullptr};
    ChildProcess m_watchdog;
    hydra::watchdog::RollbackPlanManifest m_manifest;
    std::uint64_t m_sequence{0};
};

struct TargetSession {
    SeatId seatId{0};
    SessionToken token{};
    std::wstring pipeName;
    PipeChannel channel;
    ChildProcess process;
    hydra::gatec::ProcessArchitecture architecture{
        hydra::gatec::ProcessArchitecture::Unknown};
    std::uint64_t targetWindow{0};
    std::uint64_t nextSequence{2};
    bool connected{false};
};

struct GateCRecoveryContext {
    explicit GateCRecoveryContext(std::filesystem::path directory)
        : storage(directory),
          store(storage),
          resetRegistration(std::move(directory)) {}

    hydra::recovery::NativeCrashJournalStorage storage;
    hydra::recovery::CrashJournalStore store;
    hydra::reset::RuntimeResetRegistrationStore resetRegistration;
    GateCWatchdogClient watchdog;
    std::optional<hydra::gatec::GateCRecoveryJournal> journal;
    std::uint64_t runtimeGeneration{0};
};

class TargetInputWriter {
public:
    TargetInputWriter(TargetSession& session,
                      hydra::InputMetricsRecorder* metrics)
        : m_session(session),
          m_metrics(metrics) {}
    ~TargetInputWriter() { stop(); }

    TargetInputWriter(const TargetInputWriter&) = delete;
    TargetInputWriter& operator=(const TargetInputWriter&) = delete;

    bool start() {
        if (!m_session.connected || !m_session.channel.valid() ||
            m_thread.joinable()) {
            return false;
        }
        m_thread = std::jthread(
            [this](std::stop_token token) { writerLoop(token); });
        return true;
    }

    bool enqueue(const InputEventMessage& input,
                 std::uint64_t correlationId,
                 hydra::InputMetricEventClass eventClass,
                 std::uint32_t detailCode) {
        if (m_failed.load() || m_stopping.load()) {
            return false;
        }

        QueuedInputFrame queued;
        queued.correlationId = correlationId;
        queued.eventClass = eventClass;
        queued.detailCode = detailCode;

        {
            std::scoped_lock lock(m_mutex);
            if (m_failed.load() || m_stopping.load()) {
                return false;
            }
            if (m_queue.size() >= kMaximumQueuedFrames) {
                const auto dropped =
                    m_droppedFrames.fetch_add(1u, std::memory_order_relaxed) + 1u;
                m_queueDepth.store(
                    static_cast<std::uint32_t>(m_queue.size()),
                    std::memory_order_relaxed);
                recordMetric(hydra::InputMetricStage::RouteDropped, queued,
                             hydra::monotonicInputMetricTimestampMicros(), dropped);
                return false;
            }
            const auto sequence = m_session.nextSequence++;
            queued.frame = hydra::gatec::encodeInputEvent(sequence, input);
            if (queued.frame.empty()) {
                return false;
            }
            m_queue.push_back(std::move(queued));
            const auto depth = static_cast<std::uint32_t>(m_queue.size());
            m_queueDepth.store(depth, std::memory_order_relaxed);
            const auto highWater = m_queueHighWater.load(std::memory_order_relaxed);
            if (depth > highWater) {
                m_queueHighWater.store(depth, std::memory_order_relaxed);
            }
            recordMetric(hydra::InputMetricStage::RouteEnqueued, m_queue.back(),
                         hydra::monotonicInputMetricTimestampMicros());
        }
        m_condition.notify_one();
        return true;
    }

    void stop() noexcept {
        if (!m_thread.joinable()) {
            return;
        }
        m_stopping.store(true);
        {
            std::scoped_lock lock(m_mutex);
            const auto discarded = static_cast<std::uint64_t>(m_queue.size());
            if (discarded != 0u) {
                const auto dropped = m_droppedFrames.fetch_add(
                    discarded, std::memory_order_relaxed) + discarded;
                recordMetric(hydra::InputMetricStage::RouteDropped,
                             m_queue.back(),
                             hydra::monotonicInputMetricTimestampMicros(),
                             dropped);
            }
            m_queue.clear();
            m_queueDepth.store(0u, std::memory_order_relaxed);
        }
        m_thread.request_stop();
        m_condition.notify_all();
        m_thread.join();
    }

    bool failed() const noexcept { return m_failed.load(); }
    std::uint64_t droppedFrames() const noexcept {
        return m_droppedFrames.load();
    }

    std::string lastError() const {
        std::scoped_lock lock(m_errorMutex);
        return m_lastError;
    }

private:
    struct QueuedInputFrame {
        std::vector<std::byte> frame;
        std::uint64_t correlationId{0};
        hydra::InputMetricEventClass eventClass{hydra::InputMetricEventClass::None};
        std::uint32_t detailCode{0};
    };

    void recordMetric(
        hydra::InputMetricStage stage,
        const QueuedInputFrame& queued,
        std::uint64_t timestampMicros,
        std::optional<std::uint64_t> droppedOverride = std::nullopt) noexcept {
        if (m_metrics == nullptr) return;

        hydra::InputMetricSample sample;
        sample.correlationId = queued.correlationId;
        sample.timestampMicros = timestampMicros;
        sample.stage = stage;
        sample.eventClass = queued.eventClass;
        sample.expectedSeatId = m_session.seatId;
        // Host queue/write stages describe routing intent only. Actual receiver
        // identity stays unknown until a target-side apply/query sample confirms it.
        sample.receivingSeatId = 0u;
        sample.targetProcessId = m_session.process.processId();
        sample.receivingProcessId = 0u;
        sample.detailCode = queued.detailCode;
        sample.queueDepth = m_queueDepth.load(std::memory_order_relaxed);
        sample.queueHighWater = m_queueHighWater.load(std::memory_order_relaxed);
        sample.queueDroppedCount = droppedOverride.value_or(
            m_droppedFrames.load(std::memory_order_relaxed));
        (void)m_metrics->tryRecord(sample);
    }

    void writerLoop(std::stop_token token) {
        while (true) {
            QueuedInputFrame queued;
            {
                std::unique_lock lock(m_mutex);
                m_condition.wait(lock, [&] {
                    return token.stop_requested() || m_stopping.load() ||
                           !m_queue.empty();
                });
                if (token.stop_requested() || m_stopping.load()) {
                    break;
                }
                if (m_queue.empty()) {
                    continue;
                }
                queued = std::move(m_queue.front());
                m_queue.pop_front();
                m_queueDepth.store(
                    static_cast<std::uint32_t>(m_queue.size()),
                    std::memory_order_relaxed);
                recordMetric(hydra::InputMetricStage::RouteDequeued, queued,
                             hydra::monotonicInputMetricTimestampMicros());
            }

            std::string error;
            std::uint32_t systemError = 0;
            if (!m_session.channel.writeFrame(
                    queued.frame, kWriteTimeoutMs, &error, &systemError)) {
                {
                    std::scoped_lock lock(m_errorMutex);
                    m_lastError = error + " (" +
                                  std::to_string(systemError) + ")";
                }
                m_failed.store(true);
                m_stopping.store(true);
                std::scoped_lock lock(m_mutex);
                const auto discarded =
                    static_cast<std::uint64_t>(m_queue.size()) + 1u;
                const auto dropped = m_droppedFrames.fetch_add(
                    discarded, std::memory_order_relaxed) + discarded;
                m_queue.clear();
                m_queueDepth.store(0u, std::memory_order_relaxed);
                recordMetric(hydra::InputMetricStage::RouteDropped, queued,
                             hydra::monotonicInputMetricTimestampMicros(), dropped);
                break;
            }
            recordMetric(hydra::InputMetricStage::RouteWritten, queued,
                         hydra::monotonicInputMetricTimestampMicros());
        }
    }

    static constexpr std::size_t kMaximumQueuedFrames = 2048;
    static constexpr std::uint32_t kWriteTimeoutMs = 1000;

    TargetSession& m_session;
    hydra::InputMetricsRecorder* m_metrics{nullptr};
    mutable std::mutex m_errorMutex;
    std::string m_lastError;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<QueuedInputFrame> m_queue;
    std::jthread m_thread;
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_failed{false};
    std::atomic<std::uint64_t> m_droppedFrames{0};
    std::atomic<std::uint32_t> m_queueDepth{0};
    std::atomic<std::uint32_t> m_queueHighWater{0};
};

InputEventMessage toProtocolEvent(const RawInputEvent& event) {
    InputEventMessage message;
    message.timestampMicros = event.monotonicTimestampMicros;
    message.isTouchpad = event.isTouchpad;
    if (event.keyTransition != hydra::RawKeyTransition::None) {
        message.kind = InputKind::Keyboard;
        message.keyTransition =
            event.keyTransition == hydra::RawKeyTransition::Down
                ? KeyTransition::Down
                : KeyTransition::Up;
        message.vkey = event.vkey;
        message.scanCode = event.scanCode;
        message.keyboardFlags = event.keyboardFlags;
    } else {
        message.kind = InputKind::Mouse;
        message.deltaX = event.deltaX;
        message.deltaY = event.deltaY;
        message.mouseButtonFlags = event.mouseButtonFlags;
        message.wheelDelta = event.wheelDelta;
    }
    return message;
}

bool probeAdapterKeyDown(const ProbeComparison& comparison,
                         std::uint32_t vkey) noexcept {
    if (vkey >= 256u) return false;
    const auto byteIndex = static_cast<std::size_t>(vkey / 8u);
    const auto mask = static_cast<std::uint8_t>(1u << (vkey % 8u));
    return (comparison.adapter.keyDownBits[byteIndex] & mask) != 0;
}

class GateCHost {
public:
    explicit GateCHost(HostOptions options)
        : m_options(std::move(options)) {
        const bool recoveryShimProbe = m_options.recoverySelfTest &&
            m_options.recoveryScenario == "shim-abnormal-exit";
        if (m_options.targetPath.empty() && m_options.artifactRoot.empty()) {
            m_options.targetPath = siblingTargetPath(
                m_options.baselineSelfTest ||
                m_options.pollingShimSelfTest ||
                m_options.cursorFocusShimSelfTest ||
                m_options.rawInputShimSelfTest ||
                m_options.xinputSelfTest || recoveryShimProbe);
        }
        if ((m_options.pollingShimSelfTest ||
             m_options.cursorFocusShimSelfTest ||
             m_options.rawInputShimSelfTest || recoveryShimProbe) &&
            m_options.shimPath.empty() && m_options.artifactRoot.empty()) {
            m_options.shimPath = siblingShimPath();
        }
    }

    int run() {
        if (!m_options.artifactRoot.empty()) {
            const auto kind = (m_options.baselineSelfTest ||
                               m_options.pollingShimSelfTest ||
                               m_options.cursorFocusShimSelfTest ||
                               m_options.rawInputShimSelfTest ||
                               m_options.xinputSelfTest)
                ? hydra::gatec::GateCArtifactKind::ApiProbe
                : hydra::gatec::GateCArtifactKind::ControlledTarget;
            const auto selected = hydra::gatec::resolveGateCArtifacts(
                std::filesystem::path(m_options.artifactRoot),
                hydra::gatec::defaultGateCArtifactManifest(),
                m_options.requestedArchitecture, kind);
            if (!selected) {
                std::cerr << "Gate C artifact selection failed ["
                          << hydra::gatec::artifactSelectionStatusName(
                                 selected.status)
                          << "]: " << selected.error << '\n';
                return 20;
            }
            m_options.targetPath =
                selected.selection->executablePath.wstring();
            if (m_options.pollingShimSelfTest ||
                m_options.cursorFocusShimSelfTest ||
                m_options.rawInputShimSelfTest) {
                m_options.shimPath = selected.selection->shimPath.wstring();
            }
            m_expectedArchitecture = selected.selection->architecture;
        }
        if (GetFileAttributesW(m_options.targetPath.c_str()) ==
            INVALID_FILE_ATTRIBUTES) {
            std::cerr << "Controlled target was not found: "
                      << wideToUtf8(m_options.targetPath) << '\n';
            return 20;
        }
        const auto imageArchitecture =
            hydra::gatec::detectPortableExecutableArchitecture(
                std::filesystem::path(m_options.targetPath));
        if (!imageArchitecture) {
            std::cerr << "Controlled target PE architecture detection failed ["
                      << hydra::gatec::architectureDetectionStatusName(
                             imageArchitecture.status)
                      << "]: " << imageArchitecture.error << '\n';
            return 21;
        }
        if (m_expectedArchitecture ==
            hydra::gatec::ProcessArchitecture::Unknown) {
            m_expectedArchitecture = imageArchitecture.architecture;
        } else if (m_expectedArchitecture != imageArchitecture.architecture) {
            std::cerr << "Controlled target PE architecture does not match "
                         "the selected artifact architecture.\n";
            return 22;
        }
        if (m_options.pollingShimSelfTest ||
            m_options.cursorFocusShimSelfTest ||
            m_options.rawInputShimSelfTest ||
            (m_options.recoverySelfTest &&
             m_options.recoveryScenario == "shim-abnormal-exit")) {
            if (GetFileAttributesW(m_options.shimPath.c_str()) ==
                INVALID_FILE_ATTRIBUTES) {
                std::cerr << "Controlled polling shim was not found.\n";
                return 23;
            }
            const auto shimArchitecture =
                hydra::gatec::detectPortableExecutableArchitecture(
                    std::filesystem::path(m_options.shimPath));
            if (!shimArchitecture ||
                shimArchitecture.architecture != m_expectedArchitecture) {
                std::cerr << "Controlled polling shim architecture does not "
                             "match the selected probe.\n";
                return 24;
            }
        }
        if (m_options.selfTest || m_options.architectureSelfTest) {
            return runProcessSelfTest();
        }
        if (m_options.baselineSelfTest) {
            return runApiProbeBaselineSelfTest(false, false);
        }
        if (m_options.pollingShimSelfTest) {
            return runApiProbeBaselineSelfTest(true, false);
        }
        if (m_options.cursorFocusShimSelfTest) {
            return runApiProbeBaselineSelfTest(true, true, false);
        }
        if (m_options.rawInputShimSelfTest) {
            return runApiProbeBaselineSelfTest(true, false, true);
        }
        if (m_options.xinputSelfTest) {
            return runXInputSelfTest();
        }
        if (m_options.protocolErrorSelfTest) {
            return runProtocolErrorSelfTest();
        }
        if (m_options.recoverySelfTest) {
            return runRecoverySelfTest();
        }
        return runInteractive();
    }

private:
    std::optional<std::filesystem::path> recoveryDirectory(
        std::string* error = nullptr) const {
        if (!m_options.recoveryDirectory.empty()) {
            return std::filesystem::path(m_options.recoveryDirectory);
        }
        std::uint32_t systemError = 0;
        const auto directory = hydra::recovery::defaultCrashJournalDirectory(
            &systemError);
        if (!directory && error != nullptr) {
            *error = "default recovery directory resolution failed (" +
                std::to_string(systemError) + ")";
        }
        return directory;
    }

    bool preflightRecovery(GateCRecoveryContext& recovery,
                           std::string* error = nullptr) {
        const auto startupAssessment = recovery.store.assessStartupAndEnterSafeMode();
        if (startupAssessment.state !=
            hydra::recovery::StartupRecoveryState::Clean) {
            if (error != nullptr) {
                *error = startupAssessment.diagnostic.empty()
                    ? "Gate C recovery preflight is not clean"
                    : startupAssessment.diagnostic;
            }
            return false;
        }
        const auto resetRegistration = recovery.resetRegistration.load();
        if (resetRegistration.status !=
            hydra::reset::RuntimeRegistrationReadStatus::Missing) {
            if (error != nullptr) {
                *error = resetRegistration.status ==
                        hydra::reset::RuntimeRegistrationReadStatus::Success
                    ? "runtime reset registration remains; run hydra_reset before activation"
                    : resetRegistration.diagnostic;
            }
            return false;
        }
        if (startupAssessment.journal) {
            if (startupAssessment.journal->runtimeGeneration ==
                (std::numeric_limits<std::uint64_t>::max)()) {
                if (error != nullptr) *error = "runtime generation exhausted";
                return false;
            }
            recovery.runtimeGeneration =
                startupAssessment.journal->runtimeGeneration + 1;
        } else {
            recovery.runtimeGeneration = 1;
        }
        return true;
    }

    void terminateSessionsNoJournal(
        std::vector<TargetSession>& sessions) noexcept {
        for (auto it = sessions.rbegin(); it != sessions.rend(); ++it) {
            it->process.terminate(0x52454346u); // "RECF".
            it->channel.close();
            it->connected = false;
        }
    }

    bool rollbackGuardedSessions(GateCRecoveryContext& recovery,
                                 std::vector<TargetSession>& sessions,
                                 bool forceTerminate = false) {
        std::string error;
        bool journalHealthy = recovery.journal.has_value() &&
            recovery.journal->beginRollback(&error);
        bool processesClean = true;

        for (std::size_t reverse = sessions.size(); reverse != 0; --reverse) {
            const std::size_t index = reverse - 1;
            auto& session = sessions[index];
            if (session.process.running()) {
                bool graceful = false;
                if (!forceTerminate && session.connected) {
                    graceful = sendShutdown(session);
                }
                std::uint32_t exitCode = 0;
                if (!graceful ||
                    !session.process.wait(kProcessExitTimeoutMs, &exitCode)) {
                    session.process.terminate(0x52454354u); // "RECT".
                }
            }
            if (session.process.running()) processesClean = false;
            session.channel.close();
            session.connected = false;
            if (journalHealthy) {
                journalHealthy = recovery.journal->markActionRolledBack(
                    static_cast<std::uint32_t>(index + 1), &error);
            }
        }

        bool watchdogVerified = processesClean;
        if (watchdogVerified && !recovery.watchdog.running()) {
            watchdogVerified = recovery.watchdog.restartForVerification(&error);
        }
        if (watchdogVerified) {
            watchdogVerified = recovery.watchdog.disarm(&error);
        }

        bool resetRegistrationCleared = false;
        if (journalHealthy && processesClean && watchdogVerified) {
            journalHealthy = recovery.journal->verifyRollback(&error) &&
                recovery.journal->markCleanStop(&error);
            if (journalHealthy) {
                resetRegistrationCleared =
                    recovery.resetRegistration.remove(&error);
            }
        }
        if (!journalHealthy || !processesClean || !watchdogVerified ||
            !resetRegistrationCleared) {
            if (recovery.journal) {
                std::string ignored;
                (void)recovery.journal->markRecoveryRequired(&ignored);
            }
            if (!error.empty()) {
                std::cerr << "Gate C recovery rollback failed: " << error << '\n';
            }
            return false;
        }
        return true;
    }

    bool activateGuardedSessions(
        GateCRecoveryContext& recovery,
        std::vector<TargetSession>& sessions,
        bool headless,
        std::wstring_view extraArguments = {},
        bool requireOwnedWindow = false,
        std::optional<hydra::gatec::TestCapability> grantedOverride =
            std::nullopt,
        std::uint32_t leaseTimeoutMilliseconds = 20'000) {
        std::string error;
        if (!preflightRecovery(recovery, &error)) {
            std::cerr << "Gate C recovery preflight blocked activation: "
                      << error << '\n';
            return false;
        }

        for (auto& session : sessions) {
            if (!launchTarget(session, headless, extraArguments,
                              kHandshakeTimeoutMs, requireOwnedWindow, true)) {
                terminateSessionsNoJournal(sessions);
                return false;
            }
        }

        const auto recoveryToken = hydra::gatec::generateSessionToken();
        if (!recoveryToken) {
            terminateSessionsNoJournal(sessions);
            return false;
        }
        hydra::watchdog::SessionId sessionId = *recoveryToken;
        std::vector<hydra::gatec::GateCRecoveryTarget> targets;
        targets.reserve(sessions.size());
        for (std::size_t index = 0; index < sessions.size(); ++index) {
            hydra::watchdog::ProcessIdentity identity;
            std::uint32_t systemError = 0;
            if (!hydra::watchdog::queryProcessIdentity(
                    sessions[index].process.processId(), identity,
                    &systemError)) {
                terminateSessionsNoJournal(sessions);
                return false;
            }
            targets.push_back(hydra::gatec::GateCRecoveryTarget{
                static_cast<std::uint32_t>(index + 1),
                static_cast<std::uint32_t>(index + 1),
                recovery.runtimeGeneration, identity});
        }
        const auto manifest = hydra::gatec::makeGateCRecoveryPlan(
            sessionId, recovery.runtimeGeneration, leaseTimeoutMilliseconds,
            6'000, targets, &error);
        if (!manifest) {
            std::cerr << "Gate C recovery plan creation failed: " << error << '\n';
            terminateSessionsNoJournal(sessions);
            return false;
        }

        hydra::watchdog::ProcessIdentity ownerIdentity;
        std::uint32_t ownerIdentityError = 0;
        if (!hydra::watchdog::queryProcessIdentity(
                GetCurrentProcessId(), ownerIdentity, &ownerIdentityError)) {
            std::cerr << "Gate C reset owner identity query failed: "
                      << ownerIdentityError << '\n';
            terminateSessionsNoJournal(sessions);
            return false;
        }
        hydra::reset::RuntimeResetRegistration resetRegistration;
        resetRegistration.ownerProcess = ownerIdentity;
        resetRegistration.manifest = *manifest;
        if (!recovery.resetRegistration.write(resetRegistration, &error)) {
            std::cerr << "Gate C reset registration failed: " << error << '\n';
            terminateSessionsNoJournal(sessions);
            return false;
        }
        if (!recovery.watchdog.start(*manifest, &error)) {
            std::cerr << "Gate C watchdog arm failed: " << error << '\n';
            terminateSessionsNoJournal(sessions);
            std::string ignored;
            (void)recovery.resetRegistration.remove(&ignored);
            return false;
        }

        recovery.journal.emplace(recovery.store, *manifest,
                                 recovery.runtimeGeneration);
        if (!recovery.journal->begin({}, &error)) {
            std::cerr << "Gate C journal begin failed: " << error << '\n';
            terminateSessionsNoJournal(sessions);
            (void)recovery.watchdog.disarm();
            return false;
        }
        for (const auto& action : manifest->actions) {
            if (!recovery.journal->prepareAction(action.actionId, &error)) {
                std::cerr << "Gate C journal prepare failed: " << error << '\n';
                terminateSessionsNoJournal(sessions);
                (void)recovery.watchdog.disarm();
                return false;
            }
        }

        for (std::size_t index = 0; index < sessions.size(); ++index) {
            auto& session = sessions[index];
            if (!completeStagedTarget(session, kHandshakeTimeoutMs,
                                      requireOwnedWindow, grantedOverride) ||
                !recovery.journal->markActionApplied(
                    static_cast<std::uint32_t>(index + 1), &error) ||
                !recovery.journal->markActionVerified(
                    static_cast<std::uint32_t>(index + 1), &error)) {
                std::cerr << "Gate C guarded target activation failed for Seat "
                          << session.seatId << ": " << error << '\n';
                (void)rollbackGuardedSessions(recovery, sessions, true);
                return false;
            }
        }
        if (!recovery.journal->commitActivation(&error)) {
            std::cerr << "Gate C activation commit failed: " << error << '\n';
            (void)rollbackGuardedSessions(recovery, sessions, true);
            return false;
        }
        return true;
    }

    bool recoveryEndedClean(GateCRecoveryContext& recovery) {
        return recovery.store.assessStartupAndEnterSafeMode().state ==
            hydra::recovery::StartupRecoveryState::Clean;
    }

    int runRecoverySelfTest() {
        if (m_options.recoveryDirectory.empty()) {
            std::cerr << "Recovery self-test requires --recovery-dir.\n";
            return 120;
        }
        const auto directory = std::filesystem::path(
            m_options.recoveryDirectory);

        if (m_options.recoveryScenario == "stale-journal") {
            GateCRecoveryContext seed(directory);
            std::string error;
            if (!preflightRecovery(seed, &error)) return 121;
            hydra::watchdog::ProcessIdentity identity;
            std::uint32_t systemError = 0;
            if (!hydra::watchdog::queryProcessIdentity(
                    GetCurrentProcessId(), identity, &systemError)) {
                return 122;
            }
            const auto token = hydra::gatec::generateSessionToken();
            if (!token) return 123;
            const std::vector targets{hydra::gatec::GateCRecoveryTarget{
                1, 1, seed.runtimeGeneration, identity}};
            const auto manifest = hydra::gatec::makeGateCRecoveryPlan(
                *token, seed.runtimeGeneration, 2'000, 3'000,
                targets, &error);
            if (!manifest) return 124;
            seed.journal.emplace(seed.store, *manifest,
                                 seed.runtimeGeneration);
            if (!seed.journal->begin({}, &error) ||
                !seed.journal->prepareAction(1, &error)) {
                return 125;
            }
            GateCRecoveryContext restart(directory);
            if (preflightRecovery(restart, &error) ||
                !restart.store.loadSafeMode()) {
                return 126;
            }
            std::cout << "Gate C stale-journal startup block self-test passed.\n";
            return EXIT_SUCCESS;
        }

        GateCRecoveryContext recovery(directory);
        const bool shimScenario =
            m_options.recoveryScenario == "shim-abnormal-exit";
        const bool adapterScenario =
            m_options.recoveryScenario == "adapter-failure";
        std::vector<TargetSession> sessions(shimScenario || adapterScenario ? 1 : 2);
        for (std::size_t index = 0; index < sessions.size(); ++index) {
            sessions[index].seatId = static_cast<SeatId>(index + 1);
        }
        std::wstring extraArguments;
        bool requireOwnedWindow = false;
        std::optional<hydra::gatec::TestCapability> capabilities;
        if (adapterScenario) {
            extraArguments = L"--test-adapter-failure-after-handshake";
        } else if (shimScenario) {
            extraArguments = L"--polling-shim --shim " +
                quoteArgument(m_options.shimPath);
            requireOwnedWindow = true;
            capabilities = hydra::gatec::kControlledPollingProbeCapabilities;
        }

        const std::uint32_t leaseTimeoutMilliseconds =
            m_options.recoveryScenario == "lease-stall" ? 2'000u : 20'000u;
        if (!activateGuardedSessions(recovery, sessions, true,
                                     extraArguments, requireOwnedWindow,
                                     capabilities, leaseTimeoutMilliseconds)) {
            return 127;
        }

        if (m_options.recoveryScenario == "wait-host-kill") {
            std::cout << "recovery_host_ready watchdog_pid="
                      << recovery.watchdog.processId();
            for (const auto& session : sessions) {
                std::cout << " target_pid=" << session.process.processId();
            }
            std::cout << '\n' << std::flush;
            while (true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                std::string error;
                if (!recovery.watchdog.renew(&error)) {
                    (void)rollbackGuardedSessions(recovery, sessions, true);
                    return 128;
                }
            }
        }

        bool rollbackAlreadyComplete = false;

        if (m_options.recoveryScenario == "lease-stall") {
            std::this_thread::sleep_for(std::chrono::milliseconds(2'500));
        } else if (m_options.recoveryScenario == "target-killed") {
            sessions.front().process.terminate(0x544b494cu); // "TKIL".
        } else if (m_options.recoveryScenario == "watchdog-killed") {
            if (!recovery.watchdog.terminateForTest()) return 129;
        } else if (m_options.recoveryScenario == "pipe-disconnect") {
            sessions.front().connected = false;
            sessions.front().channel.close();
            std::uint32_t ignored = 0;
            if (!sessions.front().process.wait(kProcessExitTimeoutMs, &ignored)) {
                return 130;
            }
        } else if (m_options.recoveryScenario == "adapter-failure") {
            std::uint32_t exitCode = 0;
            if (!sessions.front().process.wait(kProcessExitTimeoutMs, &exitCode) ||
                exitCode != 82u) {
                return 131;
            }
        } else if (m_options.recoveryScenario == "shim-abnormal-exit") {
            sessions.front().process.terminate(0x53484b4cu); // "SHKL".
            std::uint32_t exitCode = 0;
            if (!sessions.front().process.wait(kProcessExitTimeoutMs, &exitCode) ||
                sessions.front().process.running()) {
                return 132;
            }
        } else if (m_options.recoveryScenario == "console-logoff" ||
                   m_options.recoveryScenario == "console-shutdown") {
            GateCSessionEndWindow sessionEndWindow;
            std::string sessionEndError;
            if (!sessionEndWindow.initialize(&sessionEndError)) {
                std::cerr << "Gate C session-end window self-test setup failed: "
                          << sessionEndError << '\n';
                return 133;
            }

            const bool logoffScenario =
                m_options.recoveryScenario == "console-logoff";
            const LPARAM sessionFlags = logoffScenario
                ? static_cast<LPARAM>(ENDSESSION_LOGOFF)
                : static_cast<LPARAM>(0);
            const SessionEndKind expectedKind = logoffScenario
                ? SessionEndKind::Logoff
                : SessionEndKind::Shutdown;

            gStopRequested.store(false);
            std::atomic<LRESULT> queryResult{FALSE};
            std::atomic<bool> queryCompleted{false};
            std::thread queryThread([&] {
                queryResult.store(SendMessageW(
                    sessionEndWindow.window(), WM_QUERYENDSESSION,
                    static_cast<WPARAM>(0), sessionFlags));
                queryCompleted.store(true);
            });

            const auto stopDeadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(2);
            while (!gStopRequested.load() &&
                   std::chrono::steady_clock::now() < stopDeadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!gStopRequested.load() || queryCompleted.load() ||
                sessionEndWindow.lastKind() != expectedKind ||
                sessionEndWindow.lastMessage() != WM_QUERYENDSESSION) {
                sessionEndWindow.signalCleanupComplete(false);
                queryThread.join();
                return 133;
            }

            const bool cleaned =
                rollbackGuardedSessions(recovery, sessions, true);
            sessionEndWindow.signalCleanupComplete(cleaned);
            queryThread.join();
            if (!cleaned || queryResult.load() == FALSE ||
                sessionEndWindow.queryTimedOut()) {
                return 133;
            }
            rollbackAlreadyComplete = true;

            gStopRequested.store(false);
            (void)SendMessageW(
                sessionEndWindow.window(), WM_ENDSESSION,
                static_cast<WPARAM>(TRUE), sessionFlags);
            if (!gStopRequested.load() ||
                sessionEndWindow.lastKind() != expectedKind ||
                sessionEndWindow.lastMessage() != WM_ENDSESSION) {
                return 133;
            }
            gStopRequested.store(false);
        } else if (m_options.recoveryScenario == "ui-killed") {
            for (int index = 0; index < 5; ++index) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                std::string error;
                if (!recovery.watchdog.renew(&error)) return 136;
            }
        } else if (m_options.recoveryScenario != "clean") {
            std::cerr << "Unknown recovery self-test scenario: "
                      << m_options.recoveryScenario << '\n';
            (void)rollbackGuardedSessions(recovery, sessions, true);
            return 134;
        }

        if ((!rollbackAlreadyComplete &&
             !rollbackGuardedSessions(recovery, sessions)) ||
            !recoveryEndedClean(recovery)) {
            return 135;
        }
        std::cout << "Gate C recovery self-test passed: "
                  << m_options.recoveryScenario << "\n";
        return EXIT_SUCCESS;
    }

    bool launchTarget(TargetSession& session, bool headless,
                      std::wstring_view extraArguments = {},
                      std::uint32_t handshakeTimeoutMs = kHandshakeTimeoutMs,
                      bool requireOwnedWindow = false,
                      bool stageOnly = false) {
        const auto token = hydra::gatec::generateSessionToken();
        if (!token) {
            std::cerr << "Cryptographic session-token generation failed for Seat "
                      << session.seatId << '\n';
            return false;
        }
        session.token = *token;
        session.pipeName = hydra::gatec::makeGateCPipeName(
            GetCurrentProcessId(), session.seatId, session.token);

        std::string error;
        std::uint32_t systemError = 0;
        session.channel = hydra::gatec::createGateCServerPipe(
            session.pipeName, &error, &systemError);
        if (!session.channel.valid()) {
            std::cerr << "Server pipe creation failed: " << error
                      << " (" << systemError << ")\n";
            return false;
        }

        std::wstring commandLine = quoteArgument(m_options.targetPath) +
            L" --pipe " + quoteArgument(session.pipeName) +
            L" --seat " + std::to_wstring(session.seatId) +
            L" --token " +
            quoteArgument(utf8ToWide(
                hydra::gatec::tokenToHex(session.token)));
        if (headless) {
            commandLine += L" --headless";
        }
        if (!extraArguments.empty()) {
            commandLine.push_back(L' ');
            commandLine.append(extraArguments);
        }
        if (m_options.rawInputShimSelfTest) {
            commandLine += L" --raw-input-shim --shim " +
                           quoteArgument(m_options.shimPath);
        } else if (m_options.cursorFocusShimSelfTest) {
            commandLine += L" --cursor-focus-shim --shim " +
                           quoteArgument(m_options.shimPath);
        } else if (m_options.pollingShimSelfTest) {
            commandLine += L" --polling-shim --shim " +
                           quoteArgument(m_options.shimPath);
        } else if (m_options.xinputSelfTest) {
            commandLine += L" --xinput-controlled";
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT |
            (stageOnly ? CREATE_SUSPENDED : 0u);
        if (!CreateProcessW(
                m_options.targetPath.c_str(), commandLine.data(), nullptr,
                nullptr, FALSE, creationFlags,
                nullptr, nullptr, &startup, &process)) {
            std::cerr << "CreateProcessW failed for Seat " << session.seatId
                      << ": " << GetLastError() << '\n';
            return false;
        }
        HANDLE primaryThread = nullptr;
        if (stageOnly) {
            primaryThread = process.hThread;
        } else {
            CloseHandle(process.hThread);
        }
        session.process.assign(process.hProcess, primaryThread,
                               process.dwProcessId);

        const auto processArchitecture =
            hydra::gatec::detectProcessArchitecture(session.process.handle());
        if (!processArchitecture ||
            processArchitecture.architecture != m_expectedArchitecture) {
            std::cerr << "Controlled child architecture validation failed for Seat "
                      << session.seatId << ": "
                      << (processArchitecture.error.empty()
                              ? "architecture mismatch"
                              : processArchitecture.error)
                      << '\n';
            session.process.terminate(90);
            return false;
        }
        session.architecture = processArchitecture.architecture;

        if (stageOnly) return true;
        return completeStagedTarget(session, handshakeTimeoutMs,
                                    requireOwnedWindow);
    }

    bool completeStagedTarget(
        TargetSession& session,
        std::uint32_t handshakeTimeoutMs = kHandshakeTimeoutMs,
        bool requireOwnedWindow = false,
        std::optional<hydra::gatec::TestCapability> grantedOverride =
            std::nullopt) {
        if (!session.process.valid()) {
            return false;
        }
        if (session.process.suspended() && !session.process.resume()) {
            std::cerr << "Controlled child resume failed for Seat "
                      << session.seatId << ": " << GetLastError() << '\n';
            session.process.terminate(90);
            return false;
        }

        std::string error;
        std::uint32_t systemError = 0;
        if (!hydra::gatec::waitForGateCClient(
                session.channel, handshakeTimeoutMs, &error, &systemError)) {
            std::cerr << "Pipe connection failed for Seat " << session.seatId
                      << ": " << error << " (" << systemError << ")\n";
            session.process.terminate(91);
            return false;
        }

        const auto helloResult = session.channel.readFrame(kIoTimeoutMs);
        if (!helloResult || !helloResult.frame ||
            helloResult.frame->type != MessageType::Hello ||
            helloResult.frame->sequence != 1) {
            std::cerr << "Invalid Hello frame for Seat " << session.seatId
                      << ": " << helloResult.error << '\n';
            rejectHello(session, 2001);
            return false;
        }

        HelloMessage hello;
        if (!hydra::gatec::decodeHello(*helloResult.frame, hello, &error) ||
            hello.token != session.token || hello.seatId != session.seatId ||
            hello.processId != session.process.processId() ||
            hello.architectureBits !=
                static_cast<std::uint16_t>(session.architecture)) {
            std::cerr << "Hello identity validation failed for Seat "
                      << session.seatId << ": " << error << '\n';
            rejectHello(session, 2002);
            return false;
        }

        if (requireOwnedWindow) {
            const HWND window = reinterpret_cast<HWND>(
                static_cast<std::uintptr_t>(hello.targetWindow));
            DWORD windowProcessId = 0;
            const DWORD windowThreadId = window == nullptr
                ? 0
                : GetWindowThreadProcessId(window, &windowProcessId);
            if (window == nullptr || windowThreadId == 0 ||
                windowProcessId != session.process.processId()) {
                std::cerr << "Probe target-window validation failed for Seat "
                          << session.seatId << '\n';
                rejectHello(session, 2003);
                return false;
            }
        }

        session.targetWindow = hello.targetWindow;
        HelloAckMessage ack;
        ack.accepted = true;
        ack.serverProcessId = GetCurrentProcessId();
        auto grantedCapabilities = grantedOverride.value_or(
            hydra::gatec::kControlledTargetCapabilities);
        if (!grantedOverride && requireOwnedWindow) {
            if (m_options.xinputSelfTest) {
                grantedCapabilities =
                    hydra::gatec::kControlledXInputProbeCapabilities;
            } else if (m_options.rawInputShimSelfTest) {
                grantedCapabilities =
                    hydra::gatec::kControlledRawInputProbeCapabilities;
            } else if (m_options.cursorFocusShimSelfTest) {
                grantedCapabilities =
                    hydra::gatec::kControlledCursorFocusProbeCapabilities;
            } else if (m_options.pollingShimSelfTest) {
                grantedCapabilities =
                    hydra::gatec::kControlledPollingProbeCapabilities;
            } else {
                grantedCapabilities =
                    hydra::gatec::kControlledApiProbeCapabilities;
            }
        }
        ack.grantedCapabilities =
            hydra::gatec::testCapabilityBits(grantedCapabilities);
        if (!session.channel.writeFrame(
                hydra::gatec::encodeHelloAck(1, ack), kIoTimeoutMs,
                &error, &systemError)) {
            std::cerr << "HelloAck write failed for Seat " << session.seatId
                      << ": " << error << " (" << systemError << ")\n";
            session.process.terminate(92);
            return false;
        }
        session.connected = true;
        return true;
    }

    void rejectHello(TargetSession& session, std::uint32_t errorCode) {
        HelloAckMessage ack;
        ack.accepted = false;
        ack.serverProcessId = GetCurrentProcessId();
        ack.errorCode = errorCode;
        session.channel.writeFrame(
            hydra::gatec::encodeHelloAck(1, ack), kIoTimeoutMs);
        session.process.terminate(errorCode);
    }

    bool sendControl(TargetSession& session,
                     const ControlStateMessage& control) {
        const auto sequence = session.nextSequence++;
        return session.channel.writeFrame(
            hydra::gatec::encodeControlState(sequence, control),
            kIoTimeoutMs);
    }

    bool sendInput(TargetSession& session,
                   const InputEventMessage& input) {
        const auto sequence = session.nextSequence++;
        return session.channel.writeFrame(
            hydra::gatec::encodeInputEvent(sequence, input),
            kIoTimeoutMs);
    }

    std::optional<StateSnapshotMessage> querySnapshot(
        TargetSession& session, std::uint16_t probeVkey) {
        QuerySnapshotMessage query;
        query.probeVkey = probeVkey;
        const auto sequence = session.nextSequence++;
        if (!session.channel.writeFrame(
                hydra::gatec::encodeQuerySnapshot(sequence, query),
                kIoTimeoutMs)) {
            return std::nullopt;
        }

        for (int attempts = 0; attempts < 4; ++attempts) {
            const auto result = session.channel.readFrame(kIoTimeoutMs);
            if (!result || !result.frame) {
                return std::nullopt;
            }
            if (result.frame->type == MessageType::Error) {
                hydra::gatec::ErrorMessage error;
                hydra::gatec::decodeError(*result.frame, error);
                std::cerr << "Target Seat " << session.seatId
                          << " reported protocol error " << error.errorCode
                          << '\n';
                continue;
            }
            if (result.frame->type != MessageType::StateSnapshot ||
                result.frame->sequence != sequence) {
                continue;
            }
            StateSnapshotMessage snapshot;
            if (!hydra::gatec::decodeStateSnapshot(*result.frame, snapshot)) {
                return std::nullopt;
            }
            return snapshot;
        }
        return std::nullopt;
    }

    std::optional<ProbeComparison> queryProbeSnapshot(
        TargetSession& session, std::uint16_t probeVkey) {
        QuerySnapshotMessage query;
        query.probeVkey = probeVkey;
        const auto sequence = session.nextSequence++;
        if (!session.channel.writeFrame(
                hydra::gatec::encodeQuerySnapshot(sequence, query),
                kIoTimeoutMs)) {
            return std::nullopt;
        }

        const auto result = session.channel.readFrame(kIoTimeoutMs);
        if (!result || !result.frame ||
            result.frame->type != MessageType::ProbeSnapshot ||
            result.frame->sequence != sequence) {
            return std::nullopt;
        }
        auto decoded = hydra::gatec::decodeProbeComparison(
            result.frame->payload);
        if (!decoded || !decoded.comparison ||
            decoded.comparison->sequence != sequence ||
            decoded.comparison->seatId != session.seatId ||
            decoded.comparison->processId != session.process.processId() ||
            decoded.comparison->targetWindowRuntimeValue !=
                session.targetWindow) {
            return std::nullopt;
        }
        return std::move(decoded.comparison);
    }

    bool sendControllerUpdate(
        TargetSession& session,
        const hydra::gatec::ControllerUpdateMessage& update) {
        const auto sequence = session.nextSequence++;
        return session.channel.writeFrame(
            hydra::gatec::encodeControllerUpdate(sequence, update),
            kIoTimeoutMs);
    }

    std::optional<hydra::gatec::ControllerSnapshotMessage>
    queryController(TargetSession& session,
                    const hydra::gatec::ControllerQueryMessage& query) {
        const auto sequence = session.nextSequence++;
        if (!session.channel.writeFrame(
                hydra::gatec::encodeControllerQuery(sequence, query),
                kIoTimeoutMs)) {
            return std::nullopt;
        }
        const auto response = session.channel.readFrame(kIoTimeoutMs);
        if (!response || !response.frame ||
            response.frame->type != MessageType::ControllerSnapshot ||
            response.frame->sequence != sequence) {
            return std::nullopt;
        }
        hydra::gatec::ControllerSnapshotMessage snapshot;
        if (!hydra::gatec::decodeControllerSnapshot(
                *response.frame, snapshot) ||
            snapshot.seatId != session.seatId ||
            snapshot.logicalSlot != query.logicalSlot) {
            return std::nullopt;
        }
        return snapshot;
    }

    bool sendShutdown(TargetSession& session) {
        if (!session.connected || !session.channel.valid()) {
            return true;
        }
        const auto sequence = session.nextSequence++;
        const bool sent = session.channel.writeFrame(
            hydra::gatec::encodeShutdown(sequence), kIoTimeoutMs);
        session.connected = false;
        return sent;
    }

    bool cleanupSessions(std::vector<TargetSession>& sessions,
                         bool force) {
        bool clean = true;
        for (auto& session : sessions) {
            if (force) {
                session.process.terminate(93);
            } else {
                sendShutdown(session);
            }
        }
        for (auto& session : sessions) {
            std::uint32_t exitCode = 0;
            if (!session.process.wait(kProcessExitTimeoutMs, &exitCode)) {
                session.process.terminate(93);
                clean = false;
            } else if (!force && exitCode != 0) {
                clean = false;
            }
            session.channel.close();
        }
        return clean;
    }

    int runProcessSelfTest() {
        std::vector<TargetSession> sessions(2);
        sessions[0].seatId = 1;
        sessions[1].seatId = 2;
        if (!launchTarget(sessions[0], true) ||
            !launchTarget(sessions[1], true)) {
            cleanupSessions(sessions, true);
            return 30;
        }

        ControlStateMessage control1;
        control1.cursorX = 10;
        control1.cursorY = 20;
        control1.clipEnabled = true;
        control1.virtualForeground = true;
        control1.virtualCapture = true;
        control1.clipLeft = 0;
        control1.clipTop = 0;
        control1.clipRight = 100;
        control1.clipBottom = 100;
        ControlStateMessage control2 = control1;
        control2.cursorX = 70;
        control2.cursorY = 80;

        if (!sendControl(sessions[0], control1) ||
            !sendControl(sessions[1], control2)) {
            cleanupSessions(sessions, true);
            return 31;
        }

        InputEventMessage keyA;
        keyA.kind = InputKind::Keyboard;
        keyA.keyTransition = KeyTransition::Down;
        keyA.vkey = 0x41;
        InputEventMessage keyB = keyA;
        keyB.vkey = 0x42;
        InputEventMessage mouse1;
        mouse1.kind = InputKind::Mouse;
        mouse1.deltaX = 5;
        mouse1.deltaY = 7;
        mouse1.mouseButtonFlags = hydra::gatec::kMouseLeftDown;
        mouse1.wheelDelta = 120;
        InputEventMessage mouse2 = mouse1;
        mouse2.deltaX = -8;
        mouse2.deltaY = -9;
        mouse2.mouseButtonFlags = hydra::gatec::kMouseRightDown;
        mouse2.wheelDelta = -120;

        if (!sendInput(sessions[0], keyA) ||
            !sendInput(sessions[0], mouse1) ||
            !sendInput(sessions[1], keyB) ||
            !sendInput(sessions[1], mouse2)) {
            cleanupSessions(sessions, true);
            return 32;
        }

        const auto seat1A = querySnapshot(sessions[0], 0x41);
        const auto seat2A = querySnapshot(sessions[1], 0x41);
        const auto seat2B = querySnapshot(sessions[1], 0x42);
        const auto seat1ASecond = querySnapshot(sessions[0], 0x41);
        if (!seat1A || !seat2A || !seat2B || !seat1ASecond) {
            cleanupSessions(sessions, true);
            return 33;
        }

        const bool seat1Isolated =
            hydra::gatec::snapshotKeyDown(*seat1A, 0x41) &&
            !hydra::gatec::snapshotKeyDown(*seat1A, 0x42) &&
            seat1A->keyboardStateByte == 0x80u &&
            seat1A->asyncKeyStateValue == 0x8001u &&
            seat1ASecond->asyncKeyStateValue == 0x8000u &&
            seat1A->cursorX == 15 && seat1A->cursorY == 27 &&
            (seat1A->mouseButtonsDown & 1u) != 0 &&
            seat1A->wheelAccumulator == 120;
        const bool seat2Isolated =
            !hydra::gatec::snapshotKeyDown(*seat2A, 0x41) &&
            hydra::gatec::snapshotKeyDown(*seat2B, 0x42) &&
            seat2A->keyboardStateByte == 0 &&
            seat2A->asyncKeyStateValue == 0 &&
            seat2B->keyboardStateByte == 0x80u &&
            seat2B->asyncKeyStateValue == 0x8001u &&
            seat2B->cursorX == 62 && seat2B->cursorY == 71 &&
            (seat2B->mouseButtonsDown & (1u << 1)) != 0 &&
            seat2B->wheelAccumulator == -120;
        const bool simultaneousVirtualFocus =
            seat1A->virtualForeground && seat2A->virtualForeground &&
            seat1A->virtualCapture && seat2A->virtualCapture;

        if (!seat1Isolated || !seat2Isolated ||
            !simultaneousVirtualFocus) {
            cleanupSessions(sessions, true);
            return 34;
        }

        if (!cleanupSessions(sessions, false)) {
            return 35;
        }
        std::cout
            << "HydraSeat Gate C process self-test passed: two separate target "
               "processes retained independent keyboard, async-edge, mouse, "
               "cursor, clip, and virtual-focus state; selected architecture "
            << hydra::gatec::processArchitectureName(m_expectedArchitecture)
            << ".\n";
        return EXIT_SUCCESS;
    }

    int runApiProbeBaselineSelfTest(bool pollingShim,
                                    bool cursorFocusShim,
                                    bool rawInputShim = false) {
        POINT globalCursorBefore{};
        RECT globalClipBefore{};
        const bool nativeBaselineAvailable = !cursorFocusShim ||
            (GetCursorPos(&globalCursorBefore) != FALSE &&
             GetClipCursor(&globalClipBefore) != FALSE);
        const HWND globalForegroundBefore = cursorFocusShim
            ? GetForegroundWindow()
            : nullptr;
        const HWND globalCaptureBefore = cursorFocusShim ? GetCapture()
                                                         : nullptr;
        if (!nativeBaselineAvailable) return 49;
        const auto runNormalCycle = [&]() -> bool {
            std::vector<TargetSession> sessions(2);
            sessions[0].seatId = 1;
            sessions[1].seatId = 2;
            if (!launchTarget(sessions[0], true, {},
                              kHandshakeTimeoutMs, true) ||
                !launchTarget(sessions[1], true, {},
                              kHandshakeTimeoutMs, true)) {
                std::cerr << "Gate C API self-test cycle failed at launch; architecture="
                          << hydra::gatec::processArchitectureName(m_expectedArchitecture)
                          << " raw=" << rawInputShim << '\n';
                cleanupSessions(sessions, true);
                return false;
            }

            ControlStateMessage firstControl;
            firstControl.cursorX = 10;
            firstControl.cursorY = 20;
            firstControl.clipEnabled = true;
            firstControl.virtualForeground = true;
            firstControl.virtualCapture = true;
            firstControl.clipLeft = 0;
            firstControl.clipTop = 0;
            firstControl.clipRight = 100;
            firstControl.clipBottom = 100;
            ControlStateMessage secondControl = firstControl;
            secondControl.cursorX = 70;
            secondControl.cursorY = 80;
            if (cursorFocusShim) {
                secondControl.cursorX = 500;
                secondControl.cursorY = 600;
                secondControl.clipLeft = 400;
                secondControl.clipTop = 400;
                secondControl.clipRight = 800;
                secondControl.clipBottom = 800;
            }
            if (!sendControl(sessions[0], firstControl) ||
                !sendControl(sessions[1], secondControl)) {
                std::cerr << "Gate C API self-test cycle failed at control write; architecture="
                          << hydra::gatec::processArchitectureName(m_expectedArchitecture)
                          << " raw=" << rawInputShim << '\n';
                cleanupSessions(sessions, true);
                return false;
            }

            InputEventMessage keyA;
            keyA.kind = InputKind::Keyboard;
            keyA.keyTransition = KeyTransition::Down;
            keyA.vkey = 0x41;
            InputEventMessage keyB = keyA;
            keyB.vkey = 0x42;
            InputEventMessage firstMouse;
            firstMouse.kind = InputKind::Mouse;
            firstMouse.deltaX = 5;
            firstMouse.deltaY = 7;
            firstMouse.mouseButtonFlags = hydra::gatec::kMouseLeftDown;
            firstMouse.wheelDelta = 120;
            InputEventMessage secondMouse = firstMouse;
            secondMouse.deltaX = -8;
            secondMouse.deltaY = -9;
            secondMouse.mouseButtonFlags = hydra::gatec::kMouseRightDown;
            secondMouse.wheelDelta = -120;
            if (!sendInput(sessions[0], keyA) ||
                !sendInput(sessions[0], firstMouse) ||
                !sendInput(sessions[1], keyB) ||
                !sendInput(sessions[1], secondMouse)) {
                std::cerr << "Gate C API self-test cycle failed at input write; architecture="
                          << hydra::gatec::processArchitectureName(m_expectedArchitecture)
                          << " raw=" << rawInputShim << '\n';
                cleanupSessions(sessions, true);
                return false;
            }

            const auto firstA = queryProbeSnapshot(sessions[0], 0x41);
            const auto secondA = queryProbeSnapshot(sessions[1], 0x41);
            const auto secondB = queryProbeSnapshot(sessions[1], 0x42);
            const auto firstASecond =
                queryProbeSnapshot(sessions[0], 0x41);
            if (!firstA || !secondA || !secondB || !firstASecond) {
                std::cerr << "Gate C API self-test cycle failed at snapshot query; architecture="
                          << hydra::gatec::processArchitectureName(m_expectedArchitecture)
                          << " raw=" << rawInputShim << '\n';
                cleanupSessions(sessions, true);
                return false;
            }

            const auto capturedOnOwnedUiThread = [&](const TargetSession& session,
                                                      const ProbeComparison& value) {
                DWORD windowProcessId = 0;
                const DWORD windowThreadId = GetWindowThreadProcessId(
                    reinterpret_cast<HWND>(static_cast<std::uintptr_t>(
                        session.targetWindow)), &windowProcessId);
                return windowThreadId != 0 &&
                    value.threadId == windowThreadId &&
                    value.processId == windowProcessId &&
                    value.os.keyboardStateSucceeded &&
                    value.os.cursorPositionSucceeded &&
                    value.os.clipRectangleSucceeded;
            };
            const auto adapterCallsSucceeded = [](const ProbeComparison& value) {
                return value.adapter.snapshotResult ==
                           static_cast<std::uint32_t>(
                               HYDRA_GATE_C_ADAPTER_OK) &&
                       value.adapter.keyStateResult ==
                           static_cast<std::uint32_t>(
                               HYDRA_GATE_C_ADAPTER_OK) &&
                       value.adapter.keyboardStateResult ==
                           static_cast<std::uint32_t>(
                               HYDRA_GATE_C_ADAPTER_OK) &&
                       value.adapter.controlStateResult ==
                           static_cast<std::uint32_t>(
                               HYDRA_GATE_C_ADAPTER_OK) &&
                       value.adapter.mouseStateResult ==
                           static_cast<std::uint32_t>(
                               HYDRA_GATE_C_ADAPTER_OK) &&
                       value.adapter.windowStateResult ==
                           static_cast<std::uint32_t>(
                               HYDRA_GATE_C_ADAPTER_OK);
            };

            const bool firstAdapterAsync = pollingShim
                ? firstA->adapter.asyncKeyState == 0x8000u &&
                      firstASecond->adapter.asyncKeyState == 0x8000u
                : firstA->adapter.asyncKeyState == 0x8001u &&
                      firstASecond->adapter.asyncKeyState == 0x8000u;
            const bool secondAdapterAsync = pollingShim
                ? secondA->adapter.asyncKeyState == 0 &&
                      secondB->adapter.asyncKeyState == 0x8000u
                : secondA->adapter.asyncKeyState == 0 &&
                      secondB->adapter.asyncKeyState == 0x8001u;
            const bool firstIsolated =
                probeAdapterKeyDown(*firstA, 0x41) &&
                !probeAdapterKeyDown(*firstA, 0x42) &&
                firstA->adapter.keyboardState[0x41] == 0x80u &&
                firstA->adapter.keyboardState[0x42] == 0 &&
                firstAdapterAsync &&
                firstA->adapter.cursorX == 15 &&
                firstA->adapter.cursorY == 27 &&
                (firstA->adapter.mouseButtonsDown & 1u) != 0 &&
                firstA->adapter.wheelAccumulator == 120;
            const bool secondIsolated =
                !probeAdapterKeyDown(*secondA, 0x41) &&
                probeAdapterKeyDown(*secondB, 0x42) &&
                secondA->adapter.keyboardState[0x41] == 0 &&
                secondB->adapter.keyboardState[0x42] == 0x80u &&
                secondAdapterAsync &&
                secondB->adapter.cursorX ==
                    (cursorFocusShim ? 492 : 62) &&
                secondB->adapter.cursorY ==
                    (cursorFocusShim ? 591 : 71) &&
                (secondB->adapter.mouseButtonsDown & (1u << 1)) != 0 &&
                secondB->adapter.wheelAccumulator == -120;
            const bool baselineDifferenceObserved = cursorFocusShim
                ? firstA->adapter.virtualForeground &&
                      secondA->adapter.virtualForeground &&
                      firstA->osForegroundIsTarget &&
                      secondA->osForegroundIsTarget &&
                      firstA->osActiveIsTarget &&
                      secondA->osActiveIsTarget &&
                      firstA->osFocusIsTarget && secondA->osFocusIsTarget &&
                      firstA->osCaptureIsTarget &&
                      secondA->osCaptureIsTarget &&
                      firstA->foregroundMatches &&
                      secondA->foregroundMatches && firstA->activeMatches &&
                      secondA->activeMatches && firstA->focusMatches &&
                      secondA->focusMatches && firstA->captureMatches &&
                      secondA->captureMatches
                : firstA->adapter.virtualForeground &&
                      secondA->adapter.virtualForeground &&
                      !firstA->osForegroundIsTarget &&
                      !secondA->osForegroundIsTarget &&
                      !firstA->foregroundMatches &&
                      !secondA->foregroundMatches;
            const bool completeObservations =
                capturedOnOwnedUiThread(sessions[0], *firstA) &&
                capturedOnOwnedUiThread(sessions[1], *secondA) &&
                adapterCallsSucceeded(*firstA) &&
                adapterCallsSucceeded(*secondA) &&
                firstA->monotonicTimestampMicros != 0 &&
                secondA->monotonicTimestampMicros != 0;

            const auto osValue = [](std::int16_t value) {
                return static_cast<std::uint16_t>(value);
            };
            const bool ordinaryPollingIsolated = !pollingShim ||
                (osValue(firstA->os.asyncKeyState) == 0x8001u &&
                 osValue(firstASecond->os.asyncKeyState) == 0x8000u &&
                 osValue(firstA->os.keyState) == 0x8000u &&
                 firstA->os.keyboardState[0x41] == 0x80u &&
                 firstA->os.keyboardState[0x42] == 0u &&
                 osValue(secondA->os.asyncKeyState) == 0u &&
                 osValue(secondB->os.asyncKeyState) == 0x8001u &&
                 osValue(secondA->os.keyState) == 0u &&
                 osValue(secondB->os.keyState) == 0x8000u &&
                 secondA->os.keyboardState[0x41] == 0u &&
                 secondB->os.keyboardState[0x42] == 0x80u &&
                 firstA->asyncDownMatches && firstA->keyStateDownMatches &&
                 firstA->keyboardStateDownMatches &&
                 secondA->asyncDownMatches && secondA->keyStateDownMatches &&
                 secondA->keyboardStateDownMatches &&
                 secondB->asyncDownMatches && secondB->keyStateDownMatches &&
                 secondB->keyboardStateDownMatches);

            const bool ordinaryCursorFocusIsolated = !cursorFocusShim ||
                (firstA->cursorMatches && secondA->cursorMatches &&
                 firstA->clipRectangleMatches &&
                 secondA->clipRectangleMatches &&
                 firstA->os.cursorX == 15 && firstA->os.cursorY == 27 &&
                 secondA->os.cursorX == 492 &&
                 secondA->os.cursorY == 591 &&
                 firstA->os.clipRectangle ==
                     hydra::gatec::ProbeRect{0, 0, 100, 100} &&
                 secondA->os.clipRectangle ==
                     hydra::gatec::ProbeRect{400, 400, 800, 800});

            const auto rawSnapshotPassed = [](const ProbeComparison& value) {
                return value.rawInput.enabled &&
                    value.rawInput.registrationLifecyclePassed &&
                    value.rawInput.registrationQueryPassed &&
                    value.rawInput.dataQueryPassed &&
                    value.rawInput.bufferReadPassed &&
                    value.rawInput.registeredCount == 2u &&
                    value.rawInput.keyboardExpected >= 1u &&
                    value.rawInput.keyboardCross == 0u &&
                    value.rawInput.mouseExpected >= 1u &&
                    value.rawInput.mouseCross == 0u &&
                    value.rawInput.dataReads >= 1u &&
                    value.rawInput.bufferPackets >= 1u &&
                    value.rawInput.apiFailures == 0u &&
                    value.rawInput.destroyedTargetFailures == 0u &&
                    value.rawInput.staleTokenFailures == 0u &&
                    value.rawInput.queueOverflowFailures == 0u &&
                    value.rawInput.lastSystemError == 0u;
            };
            const bool ordinaryRawInputIsolated = !rawInputShim ||
                (rawSnapshotPassed(*firstASecond) &&
                 rawSnapshotPassed(*secondB));

            bool releaseAndDestructionIsolated = true;
            if (cursorFocusShim) {
                auto releasedSecondControl = secondControl;
                // ControlState is a full-state message. Preserve the cursor
                // position reached by the earlier mouse delta while changing
                // only the virtual-capture intent for this acceptance step.
                releasedSecondControl.cursorX = 492;
                releasedSecondControl.cursorY = 591;
                releasedSecondControl.virtualCapture = false;
                if (!sendControl(sessions[1], releasedSecondControl)) {
                    cleanupSessions(sessions, true);
                    return false;
                }
                const auto firstAfterSecondRelease =
                    queryProbeSnapshot(sessions[0], 0x41);
                const auto secondAfterRelease =
                    queryProbeSnapshot(sessions[1], 0x42);
                releaseAndDestructionIsolated =
                    firstAfterSecondRelease && secondAfterRelease &&
                    firstAfterSecondRelease->osCaptureIsTarget &&
                    firstAfterSecondRelease->captureMatches &&
                    secondAfterRelease->os.captureWindowRuntimeValue == 0 &&
                    secondAfterRelease->adapter
                            .virtualCaptureWindowRuntimeValue == 0 &&
                    secondAfterRelease->captureMatches;

                std::uint32_t firstExitCode = 0;
                releaseAndDestructionIsolated =
                    releaseAndDestructionIsolated &&
                    sendShutdown(sessions[0]) &&
                    sessions[0].process.wait(kProcessExitTimeoutMs,
                                             &firstExitCode) &&
                    firstExitCode == 0 &&
                    !sessions[0].process.running();
                sessions[0].channel.close();
                const auto secondAfterFirstDestroyed =
                    queryProbeSnapshot(sessions[1], 0x42);
                releaseAndDestructionIsolated =
                    releaseAndDestructionIsolated &&
                    secondAfterFirstDestroyed &&
                    secondAfterFirstDestroyed->osForegroundIsTarget &&
                    secondAfterFirstDestroyed->osActiveIsTarget &&
                    secondAfterFirstDestroyed->osFocusIsTarget &&
                    secondAfterFirstDestroyed->foregroundMatches &&
                    secondAfterFirstDestroyed->activeMatches &&
                    secondAfterFirstDestroyed->focusMatches &&
                    secondAfterFirstDestroyed->captureMatches &&
                    secondAfterFirstDestroyed->adapter.cursorX == 492 &&
                    secondAfterFirstDestroyed->adapter.cursorY == 591;
            }

            const bool passed = firstIsolated && secondIsolated &&
                baselineDifferenceObserved && completeObservations &&
                ordinaryPollingIsolated && ordinaryCursorFocusIsolated &&
                ordinaryRawInputIsolated &&
                releaseAndDestructionIsolated;
            if (!passed && (cursorFocusShim || rawInputShim)) {
                std::cerr
                    << "Cursor/focus acceptance failed: firstIsolated="
                    << firstIsolated << " secondIsolated=" << secondIsolated
                    << " logicalWindowViews=" << baselineDifferenceObserved
                    << " completeObservations=" << completeObservations
                    << " polling=" << ordinaryPollingIsolated
                    << " cursorFocus=" << ordinaryCursorFocusIsolated
                    << " rawInput=" << ordinaryRawInputIsolated
                    << " releaseDestroy=" << releaseAndDestructionIsolated
                    << '\n';
            }
            if (rawInputShim) {
                const auto& firstRaw = firstASecond->rawInput;
                const auto& secondRaw = secondB->rawInput;
                std::cout
                    << "{\"event\":\"p3_raw_02_acceptance\""
                    << ",\"seat1_keyboard_expected\":"
                    << firstRaw.keyboardExpected
                    << ",\"seat1_keyboard_cross\":"
                    << firstRaw.keyboardCross
                    << ",\"seat1_mouse_expected\":"
                    << firstRaw.mouseExpected
                    << ",\"seat1_mouse_cross\":"
                    << firstRaw.mouseCross
                    << ",\"seat1_api_failures\":"
                    << firstRaw.apiFailures
                    << ",\"seat2_keyboard_expected\":"
                    << secondRaw.keyboardExpected
                    << ",\"seat2_keyboard_cross\":"
                    << secondRaw.keyboardCross
                    << ",\"seat2_mouse_expected\":"
                    << secondRaw.mouseExpected
                    << ",\"seat2_mouse_cross\":"
                    << secondRaw.mouseCross
                    << ",\"seat2_api_failures\":"
                    << secondRaw.apiFailures << "}\n";
            }
            const bool cleaned = cleanupSessions(sessions, !passed);
            if (!cleaned || sessions[0].process.running() ||
                sessions[1].process.running()) {
                std::cerr << "Gate C API self-test cycle failed at cleanup; architecture="
                          << hydra::gatec::processArchitectureName(m_expectedArchitecture)
                          << " raw=" << rawInputShim
                          << " passed=" << passed
                          << " cleaned=" << cleaned << '\n';
            }
            return passed && cleaned &&
                !sessions[0].process.running() &&
                !sessions[1].process.running();
        };

        // Two full cycles prove deterministic startup, capture, shutdown, and
        // release of both controlled child processes.
        if (!runNormalCycle() || !runNormalCycle()) return 50;
        if (rawInputShim) {
            std::cout
                << "HydraSeat Gate C Raw Input shim process self-test "
                   "passed: two startup-loaded controlled probes retained "
                   "independent registration, WM_INPUT, GetRawInputData, "
                   "GetRawInputBuffer, and polling state; raw_api_failures=0 "
                   "raw_cross_deliveries=0 raw_stale_tokens=0 "
                   "raw_queue_overflows=0.\n";
        } else if (cursorFocusShim) {
            POINT globalCursorAfter{};
            RECT globalClipAfter{};
            if (GetCursorPos(&globalCursorAfter) == FALSE ||
                GetClipCursor(&globalClipAfter) == FALSE ||
                globalCursorAfter.x != globalCursorBefore.x ||
                globalCursorAfter.y != globalCursorBefore.y ||
                EqualRect(&globalClipAfter, &globalClipBefore) == FALSE ||
                GetForegroundWindow() != globalForegroundBefore ||
                GetCapture() != globalCaptureBefore) {
                std::cerr << "Cursor/focus host-native global state changed during controlled virtualization.\n";
                return 57;
            }
        }

        TargetSession missingWindow;
        missingWindow.seatId = 11;
        if (launchTarget(missingWindow, true, L"--test-missing-window",
                         2000, true) ||
            missingWindow.process.running()) {
            missingWindow.process.terminate(101);
            missingWindow.channel.close();
            return 51;
        }
        missingWindow.channel.close();

        TargetSession handshakeTimeout;
        handshakeTimeout.seatId = 12;
        const auto timeoutStart = std::chrono::steady_clock::now();
        if (launchTarget(handshakeTimeout, true, L"--test-no-handshake",
                         250, true) ||
            handshakeTimeout.process.running() ||
            std::chrono::steady_clock::now() - timeoutStart >
                std::chrono::seconds(5)) {
            handshakeTimeout.process.terminate(102);
            handshakeTimeout.channel.close();
            return 52;
        }
        handshakeTimeout.channel.close();

        TargetSession abnormalExit;
        abnormalExit.seatId = 13;
        if (!launchTarget(abnormalExit, true, L"--test-abnormal-exit",
                          kHandshakeTimeoutMs, true)) {
            abnormalExit.process.terminate(103);
            abnormalExit.channel.close();
            return 53;
        }
        std::uint32_t abnormalCode = 0;
        if (!abnormalExit.process.wait(kProcessExitTimeoutMs, &abnormalCode) ||
            abnormalCode != 77u || abnormalExit.process.running()) {
            abnormalExit.process.terminate(104);
            abnormalExit.channel.close();
            return 54;
        }
        abnormalExit.channel.close();

        TargetSession hostDisconnect;
        hostDisconnect.seatId = 14;
        if (!launchTarget(hostDisconnect, true, {},
                          kHandshakeTimeoutMs, true)) {
            hostDisconnect.process.terminate(105);
            hostDisconnect.channel.close();
            return 55;
        }
        hostDisconnect.connected = false;
        hostDisconnect.channel.close();
        std::uint32_t disconnectCode = 0;
        if (!hostDisconnect.process.wait(
                kProcessExitTimeoutMs, &disconnectCode) ||
            disconnectCode != 24u || hostDisconnect.process.running()) {
            hostDisconnect.process.terminate(106);
            return 56;
        }

        if (cursorFocusShim) {
            std::cout
                << "HydraSeat Gate C cursor/focus shim process self-test "
                   "passed: two controlled probes used independent ordinary "
                   "cursor, clip, logical focus, capture, and polling APIs "
                   "without changing host-native global state.\n";
        } else if (pollingShim) {
            std::cout
                << "HydraSeat Gate C polling shim process self-test passed: "
                   "two startup-loaded controlled probes received independent "
                   "Seat A/B state through ordinary polling APIs; failure, "
                   "uninstall, and repeated teardown left no child process.\n";
        } else {
            std::cout
                << "HydraSeat Gate C API baseline self-test passed: two controlled "
                   "probe processes captured real Win32 polling/cursor/clip/focus/"
                   "capture observations beside independent Seat adapter state; "
                   "failure and repeated teardown paths left no child process.\n";
        }
        return EXIT_SUCCESS;
    }

    int runXInputSelfTest() {
        using namespace hydra::gatec;
        constexpr ControllerSourceIdentity sourceA{
            ControllerSourceKind::Synthetic, 0u,
            0x58494e5055544101ull};
        constexpr ControllerSourceIdentity sourceB{
            ControllerSourceKind::Synthetic, 1u,
            0x58494e5055544202ull};

        NormalizedXInputGamepad stateA;
        stateA.buttons = 0x1100u;
        stateA.leftTrigger = 20u;
        stateA.rightTrigger = 200u;
        stateA.thumbLX = -12000;
        stateA.thumbLY = 9000;
        stateA.thumbRX = -321;
        stateA.thumbRY = 654;
        NormalizedXInputGamepad stateB;
        stateB.buttons = 0x2200u;
        stateB.leftTrigger = 180u;
        stateB.rightTrigger = 5u;
        stateB.thumbLX = 123;
        stateB.thumbLY = -456;
        stateB.thumbRX = 16000;
        stateB.thumbRY = -7000;

        NormalizedXInputCapabilities capabilitiesA;
        capabilitiesA.subtype = 1u;
        capabilitiesA.flags = 1u;
        capabilitiesA.gamepad.buttons = 0xffffu;
        capabilitiesA.gamepad.leftTrigger = 255u;
        capabilitiesA.gamepad.rightTrigger = 255u;
        capabilitiesA.vibrationSupported = true;
        capabilitiesA.leftMotorMaximum = 65535u;
        capabilitiesA.rightMotorMaximum = 60000u;
        auto capabilitiesB = capabilitiesA;
        capabilitiesB.subtype = 2u;
        capabilitiesB.flags = 2u;
        capabilitiesB.rightMotorMaximum = 50000u;
        const NormalizedXInputBattery batteryA{
            true, XInputBatteryDeviceType::Gamepad,
            XInputBatteryType::Alkaline, XInputBatteryLevel::Full};
        const NormalizedXInputBattery batteryB{
            true, XInputBatteryDeviceType::Gamepad,
            XInputBatteryType::Wired, XInputBatteryLevel::Low};

        std::uint64_t seat1StateExpected = 0;
        std::uint64_t seat1StateCross = 0;
        std::uint64_t seat1CapabilityExpected = 0;
        std::uint64_t seat1CapabilityCross = 0;
        std::uint64_t seat1BatteryExpected = 0;
        std::uint64_t seat1BatteryCross = 0;
        std::uint64_t seat1VibrationExpected = 0;
        std::uint64_t seat1VibrationCross = 0;
        std::uint64_t seat2StateExpected = 0;
        std::uint64_t seat2StateCross = 0;
        std::uint64_t seat2CapabilityExpected = 0;
        std::uint64_t seat2CapabilityCross = 0;
        std::uint64_t seat2BatteryExpected = 0;
        std::uint64_t seat2BatteryCross = 0;
        std::uint64_t seat2VibrationExpected = 0;
        std::uint64_t seat2VibrationCross = 0;
        std::uint64_t apiFailures = 0;
        std::uint64_t staleAccepted = 0;

        const auto update = [](
            std::uint32_t seatId, ControllerUpdateKind kind,
            const ControllerSourceIdentity& source,
            std::uint64_t generation) {
            ControllerUpdateMessage value;
            value.seatId = seatId;
            value.kind = kind;
            value.logicalSlot = 0u;
            value.source = source;
            value.sourceGeneration = generation;
            return value;
        };

        const auto runCycle = [&]() {
            std::vector<TargetSession> sessions(2);
            sessions[0].seatId = 1u;
            sessions[1].seatId = 2u;
            if (!launchTarget(sessions[0], true, {},
                              kHandshakeTimeoutMs, true) ||
                !launchTarget(sessions[1], true, {},
                              kHandshakeTimeoutMs, true)) {
                ++apiFailures;
                cleanupSessions(sessions, true);
                return false;
            }

            auto mapA = update(1u, ControllerUpdateKind::Map,
                               sourceA, 10u);
            auto mapB = update(2u, ControllerUpdateKind::Map,
                               sourceB, 20u);
            auto inputA = update(1u, ControllerUpdateKind::State,
                                 sourceA, 10u);
            inputA.gamepad = stateA;
            auto inputB = update(2u, ControllerUpdateKind::State,
                                 sourceB, 20u);
            inputB.gamepad = stateB;
            auto capA = update(1u, ControllerUpdateKind::Capabilities,
                               sourceA, 10u);
            capA.capabilities = capabilitiesA;
            auto capB = update(2u, ControllerUpdateKind::Capabilities,
                               sourceB, 20u);
            capB.capabilities = capabilitiesB;
            auto batA = update(1u, ControllerUpdateKind::Battery,
                               sourceA, 10u);
            batA.battery = batteryA;
            auto batB = update(2u, ControllerUpdateKind::Battery,
                               sourceB, 20u);
            batB.battery = batteryB;
            const bool sent =
                sendControllerUpdate(sessions[0], mapA) &&
                sendControllerUpdate(sessions[1], mapB) &&
                sendControllerUpdate(sessions[0], inputA) &&
                sendControllerUpdate(sessions[1], inputB) &&
                sendControllerUpdate(sessions[0], capA) &&
                sendControllerUpdate(sessions[1], capB) &&
                sendControllerUpdate(sessions[0], batA) &&
                sendControllerUpdate(sessions[1], batB);
            ControllerQueryMessage queryA;
            queryA.seatId = 1u;
            ControllerQueryMessage queryB;
            queryB.seatId = 2u;
            const auto first = sent
                ? queryController(sessions[0], queryA)
                : std::nullopt;
            const auto second = sent
                ? queryController(sessions[1], queryB)
                : std::nullopt;
            if (!first || !second) {
                ++apiFailures;
                cleanupSessions(sessions, true);
                return false;
            }

            const bool firstStateExpected =
                first->stateResult == VirtualXInputResult::Success &&
                first->state.mapping.source == sourceA &&
                first->state.gamepad == stateA;
            const bool secondStateExpected =
                second->stateResult == VirtualXInputResult::Success &&
                second->state.mapping.source == sourceB &&
                second->state.gamepad == stateB;
            seat1StateExpected += firstStateExpected ? 1u : 0u;
            seat2StateExpected += secondStateExpected ? 1u : 0u;
            seat1StateCross +=
                (first->state.mapping.source == sourceB ||
                 first->state.gamepad == stateB) ? 1u : 0u;
            seat2StateCross +=
                (second->state.mapping.source == sourceA ||
                 second->state.gamepad == stateA) ? 1u : 0u;

            const bool firstCapabilityExpected =
                first->capabilitiesResult ==
                    VirtualXInputResult::Success &&
                first->capabilities.mapping.source == sourceA &&
                first->capabilities.capabilities == capabilitiesA;
            const bool secondCapabilityExpected =
                second->capabilitiesResult ==
                    VirtualXInputResult::Success &&
                second->capabilities.mapping.source == sourceB &&
                second->capabilities.capabilities == capabilitiesB;
            seat1CapabilityExpected +=
                firstCapabilityExpected ? 1u : 0u;
            seat2CapabilityExpected +=
                secondCapabilityExpected ? 1u : 0u;
            seat1CapabilityCross +=
                first->capabilities.mapping.source == sourceB ? 1u : 0u;
            seat2CapabilityCross +=
                second->capabilities.mapping.source == sourceA ? 1u : 0u;

            const bool firstBatteryExpected =
                first->batteryResult == VirtualXInputResult::Success &&
                first->battery.mapping.source == sourceA &&
                first->battery.battery == batteryA;
            const bool secondBatteryExpected =
                second->batteryResult == VirtualXInputResult::Success &&
                second->battery.mapping.source == sourceB &&
                second->battery.battery == batteryB;
            seat1BatteryExpected += firstBatteryExpected ? 1u : 0u;
            seat2BatteryExpected += secondBatteryExpected ? 1u : 0u;
            seat1BatteryCross +=
                first->battery.mapping.source == sourceB ? 1u : 0u;
            seat2BatteryCross +=
                second->battery.mapping.source == sourceA ? 1u : 0u;

            auto vibrationA = queryA;
            vibrationA.kind = ControllerQueryKind::Vibration;
            vibrationA.expectedMappingGeneration =
                first->state.mapping.mappingGeneration;
            vibrationA.expectedSourceGeneration = 10u;
            vibrationA.leftMotor = 100u;
            vibrationA.rightMotor = 200u;
            auto vibrationB = queryB;
            vibrationB.kind = ControllerQueryKind::Vibration;
            vibrationB.expectedMappingGeneration =
                second->state.mapping.mappingGeneration;
            vibrationB.expectedSourceGeneration = 20u;
            vibrationB.leftMotor = 500u;
            vibrationB.rightMotor = 600u;
            const auto routedA = queryController(sessions[0], vibrationA);
            const auto routedB = queryController(sessions[1], vibrationB);
            if (!routedA || !routedB) {
                ++apiFailures;
                cleanupSessions(sessions, true);
                return false;
            }
            seat1VibrationExpected +=
                routedA->vibrationResult == VirtualXInputResult::Success &&
                routedA->vibration.source == sourceA ? 1u : 0u;
            seat2VibrationExpected +=
                routedB->vibrationResult == VirtualXInputResult::Success &&
                routedB->vibration.source == sourceB ? 1u : 0u;
            seat1VibrationCross +=
                routedA->vibration.source == sourceB ? 1u : 0u;
            seat2VibrationCross +=
                routedB->vibration.source == sourceA ? 1u : 0u;

            auto disconnectA = update(
                1u, ControllerUpdateKind::Disconnect, sourceA, 10u);
            const bool disconnected =
                sendControllerUpdate(sessions[0], disconnectA);
            const auto afterDisconnect = disconnected
                ? queryController(sessions[0], queryA)
                : std::nullopt;
            const auto unaffectedSecond = disconnected
                ? queryController(sessions[1], queryB)
                : std::nullopt;
            const bool disconnectCleared = afterDisconnect &&
                afterDisconnect->stateResult ==
                    VirtualXInputResult::Disconnected &&
                !afterDisconnect->state.connected &&
                afterDisconnect->state.gamepad ==
                    NormalizedXInputGamepad{} &&
                afterDisconnect->capabilitiesResult ==
                    VirtualXInputResult::Disconnected &&
                afterDisconnect->batteryResult ==
                    VirtualXInputResult::Disconnected;
            const bool secondUnaffected = unaffectedSecond &&
                unaffectedSecond->stateResult ==
                    VirtualXInputResult::Success &&
                unaffectedSecond->state.mapping.source == sourceB &&
                unaffectedSecond->state.gamepad == stateB;

            auto reconnectA = inputA;
            reconnectA.sourceGeneration = 11u;
            auto reconnectCapA = capA;
            reconnectCapA.sourceGeneration = 11u;
            auto reconnectBatteryA = batA;
            reconnectBatteryA.sourceGeneration = 11u;
            const bool reconnected = disconnectCleared && secondUnaffected &&
                sendControllerUpdate(sessions[0], reconnectA) &&
                sendControllerUpdate(sessions[0], reconnectCapA) &&
                sendControllerUpdate(sessions[0], reconnectBatteryA);
            const auto afterReconnect = reconnected
                ? queryController(sessions[0], queryA)
                : std::nullopt;
            const bool reconnectGenerationPassed = afterReconnect &&
                afterReconnect->stateResult ==
                    VirtualXInputResult::Success &&
                afterReconnect->state.mapping.sourceGeneration == 11u &&
                afterReconnect->state.gamepad == stateA;

            auto staleVibration = vibrationA;
            const auto staleRoute = reconnectGenerationPassed
                ? queryController(sessions[0], staleVibration)
                : std::nullopt;
            if (staleRoute && staleRoute->vibrationResult ==
                                  VirtualXInputResult::Success) {
                ++staleAccepted;
            }
            auto freshVibration = vibrationA;
            freshVibration.expectedSourceGeneration = 11u;
            freshVibration.leftMotor = 0u;
            freshVibration.rightMotor = 0u;
            const auto freshRoute = staleRoute
                ? queryController(sessions[0], freshVibration)
                : std::nullopt;
            const bool generationRacePassed = staleRoute && freshRoute &&
                staleRoute->vibrationResult ==
                    VirtualXInputResult::StaleGeneration &&
                freshRoute->vibrationResult ==
                    VirtualXInputResult::Success &&
                freshRoute->vibration.source == sourceA &&
                freshRoute->vibration.leftMotor == 0u &&
                freshRoute->vibration.rightMotor == 0u;

            const bool passed = firstStateExpected && secondStateExpected &&
                firstCapabilityExpected && secondCapabilityExpected &&
                firstBatteryExpected && secondBatteryExpected &&
                routedA->vibrationResult == VirtualXInputResult::Success &&
                routedB->vibrationResult == VirtualXInputResult::Success &&
                disconnectCleared && secondUnaffected &&
                reconnectGenerationPassed && generationRacePassed;
            const bool cleaned = cleanupSessions(sessions, !passed);
            return passed && cleaned &&
                !sessions[0].process.running() &&
                !sessions[1].process.running();
        };

        const bool passed = runCycle() && runCycle() &&
            seat1StateExpected > 0 && seat2StateExpected > 0 &&
            seat1CapabilityExpected > 0 &&
            seat2CapabilityExpected > 0 &&
            seat1BatteryExpected > 0 && seat2BatteryExpected > 0 &&
            seat1VibrationExpected > 0 &&
            seat2VibrationExpected > 0 &&
            seat1StateCross == 0 && seat2StateCross == 0 &&
            seat1CapabilityCross == 0 &&
            seat2CapabilityCross == 0 &&
            seat1BatteryCross == 0 && seat2BatteryCross == 0 &&
            seat1VibrationCross == 0 &&
            seat2VibrationCross == 0 && apiFailures == 0 &&
            staleAccepted == 0;
        std::cout
            << "{\"event\":\"p3_ctrl_01_acceptance\""
            << ",\"seat1_state_expected\":" << seat1StateExpected
            << ",\"seat1_state_cross\":" << seat1StateCross
            << ",\"seat1_capability_expected\":"
            << seat1CapabilityExpected
            << ",\"seat1_capability_cross\":"
            << seat1CapabilityCross
            << ",\"seat1_battery_expected\":"
            << seat1BatteryExpected
            << ",\"seat1_battery_cross\":" << seat1BatteryCross
            << ",\"seat1_vibration_expected\":"
            << seat1VibrationExpected
            << ",\"seat1_vibration_cross\":"
            << seat1VibrationCross
            << ",\"seat2_state_expected\":" << seat2StateExpected
            << ",\"seat2_state_cross\":" << seat2StateCross
            << ",\"seat2_capability_expected\":"
            << seat2CapabilityExpected
            << ",\"seat2_capability_cross\":"
            << seat2CapabilityCross
            << ",\"seat2_battery_expected\":"
            << seat2BatteryExpected
            << ",\"seat2_battery_cross\":" << seat2BatteryCross
            << ",\"seat2_vibration_expected\":"
            << seat2VibrationExpected
            << ",\"seat2_vibration_cross\":"
            << seat2VibrationCross
            << ",\"api_failures\":" << apiFailures
            << ",\"stale_accepted\":" << staleAccepted << "}\n";
        if (!passed) return 70;
        std::cout
            << "HydraSeat Gate C controlled XInput process self-test passed: "
               "two process-local logical slot 0 mappings retained distinct "
               "state, capabilities, battery, vibration, and reconnect generations.\n";
        return EXIT_SUCCESS;
    }

    int runProtocolErrorSelfTest() {
        TargetSession session;
        session.seatId = 1;
        if (!launchTarget(session, true)) {
            session.process.terminate(94);
            session.channel.close();
            return 36;
        }

        ControlStateMessage control;
        control.cursorX = 10;
        control.cursorY = 10;
        control.virtualForeground = true;
        if (!sendControl(session, control)) {
            session.process.terminate(95);
            session.channel.close();
            return 37;
        }

        InputEventMessage staleInput;
        staleInput.kind = InputKind::Keyboard;
        staleInput.keyTransition = KeyTransition::Down;
        staleInput.vkey = 0x41;
        // sendControl consumed sequence 2. Reusing sequence 2 must be rejected
        // by the adapter and terminate the controlled target session.
        if (!session.channel.writeFrame(
                hydra::gatec::encodeInputEvent(2, staleInput),
                kIoTimeoutMs)) {
            session.process.terminate(96);
            session.channel.close();
            return 38;
        }

        const auto response = session.channel.readFrame(kIoTimeoutMs);
        if (!response || !response.frame ||
            response.frame->type != MessageType::Error ||
            response.frame->sequence != 2) {
            session.process.terminate(97);
            session.channel.close();
            return 39;
        }
        hydra::gatec::ErrorMessage errorMessage;
        if (!hydra::gatec::decodeError(*response.frame, errorMessage) ||
            errorMessage.errorCode != 1103u) {
            session.process.terminate(98);
            session.channel.close();
            return 40;
        }

        std::uint32_t exitCode = 0;
        if (!session.process.wait(kProcessExitTimeoutMs, &exitCode) ||
            exitCode != 23u) {
            session.process.terminate(99);
            session.channel.close();
            return 41;
        }
        session.channel.close();
        std::cout
            << "HydraSeat Gate C protocol-error self-test passed: stale "
               "state input produced an Error frame and fail-closed target exit.\n";
        return EXIT_SUCCESS;
    }

    int runInteractive() {
        WorkspaceManager seats;
        if (!seats.loadFromFile(m_options.profilePath)) {
            std::cerr << "Seat profile load failed: " << seats.lastError() << '\n';
            return 40;
        }
        std::vector<SeatId> activeSeats;
        for (const auto& seat : seats.getAllSeats()) {
            if (seat.active) activeSeats.push_back(seat.seatId);
        }
        if (activeSeats.size() < 2) {
            std::cerr << "Gate C requires at least two active Seats in the profile.\n";
            return 41;
        }
        activeSeats.resize(2);

        std::string recoveryError;
        const auto recoveryPath = recoveryDirectory(&recoveryError);
        if (!recoveryPath) {
            std::cerr << "Gate C recovery directory failed: "
                      << recoveryError << '\n';
            return 42;
        }
        GateCRecoveryContext recovery(*recoveryPath);
        GateCSessionEndWindow sessionEndWindow;
        std::string sessionEndError;
        if (!sessionEndWindow.initialize(&sessionEndError)) {
            std::cerr << "Gate C session-end monitor initialization failed: "
                      << sessionEndError << '\n';
            return 42;
        }

        std::vector<TargetSession> sessions(2);
        for (std::size_t index = 0; index < sessions.size(); ++index) {
            sessions[index].seatId = activeSeats[index];
        }
        if (!activateGuardedSessions(recovery, sessions, false)) {
            return 42;
        }
        auto rollbackAndSignal = [&](bool forceTerminate) {
            const bool clean = rollbackGuardedSessions(
                recovery, sessions, forceTerminate);
            sessionEndWindow.signalCleanupComplete(clean);
            return clean;
        };
        for (auto& session : sessions) {
            seats.assignTargetWindow(session.seatId, session.targetWindow);
        }

        ControlStateMessage control;
        control.cursorX = 400;
        control.cursorY = 300;
        control.clipEnabled = true;
        control.virtualForeground = true;
        control.virtualCapture = true;
        control.clipLeft = 0;
        control.clipTop = 0;
        control.clipRight = 800;
        control.clipBottom = 600;
        for (auto& session : sessions) {
            if (!sendControl(session, control)) {
                (void)rollbackAndSignal(true);
                return 43;
            }
        }

        hydra::InputMetricsRecorder metrics(
            hydra::kDefaultInputMetricsCapacity,
            m_options.metricsDiagnostic
                ? hydra::InputMetricsPrivacyMode::Diagnostic
                : hydra::InputMetricsPrivacyMode::Redacted);

        std::vector<std::unique_ptr<TargetInputWriter>> writers;
        std::unordered_map<SeatId, TargetInputWriter*> writerBySeat;
        writers.reserve(sessions.size());
        for (auto& session : sessions) {
            auto writer = std::make_unique<TargetInputWriter>(session, &metrics);
            if (!writer->start()) {
                for (auto& started : writers) started->stop();
                (void)rollbackAndSignal(true);
                return 44;
            }
            writerBySeat.emplace(session.seatId, writer.get());
            writers.push_back(std::move(writer));
        }

        SeatRoutingPolicy routingPolicy;
        InputObservationSession observation(
            seats, routingPolicy,
            [&](const RawInputEvent& event,
                const InputRouteDecision& decision) {
                if (!decision.seatId) return false;
                const auto found = writerBySeat.find(*decision.seatId);
                if (found == writerBySeat.end() || found->second == nullptr) {
                    return false;
                }
                return found->second->enqueue(
                    toProtocolEvent(event), event.sequence,
                    hydra::classifyInputMetricEvent(event),
                    hydra::inputMetricDetailCode(event));
            });
        const auto bindings = observation.rebuildBindings();
        std::cout << "Gate C host bound " << bindings.boundDevices
                  << " exclusive keyboard/mouse identities.\n";
        if (!bindings.ambiguousSharedDevices.empty()) {
            std::cout << "Ambiguous shared input devices will fail closed: "
                      << bindings.ambiguousSharedDevices.size() << '\n';
        }

        hydra::InputTraceWriter trace(
            m_options.tracePath,
            m_options.traceSensitiveKeyIds
                ? hydra::InputTracePrivacyMode::DiagnosticKeyIds
                : hydra::InputTracePrivacyMode::Redacted);
        if (m_options.traceSensitiveKeyIds) {
            std::cerr
                << "WARNING: --trace-sensitive-keys is enabled; JSONL trace records may reveal typed virtual-key identifiers.\n";
        }
        hydra::InputRouter router;
        router.setGlobalCallback([&](const RawInputEvent& event) {
            hydra::InputMetricSample physical;
            physical.correlationId = event.sequence;
            physical.timestampMicros = event.monotonicTimestampMicros;
            physical.stage = hydra::InputMetricStage::PhysicalObserved;
            physical.eventClass = hydra::classifyInputMetricEvent(event);
            physical.detailCode = hydra::inputMetricDetailCode(event);
            (void)metrics.tryRecord(physical);

            const InputRouteRecord route = observation.processInput(event, false);
            if (trace.isOpen()) trace.writeInput(event, route);
        });
        router.setDeviceChangeCallback([&](const RawInputDeviceChange& change) {
            observation.processDeviceChange(change);
            if (trace.isOpen()) trace.writeDeviceChange(change);
        });
        if (!router.initialize()) {
            std::cerr << "Raw Input host initialization failed.\n";
            for (auto& writer : writers) writer->stop();
            (void)rollbackAndSignal(true);
            return 45;
        }

        gStopRequested.store(false);
        if (!SetConsoleCtrlHandler(consoleControlHandler, TRUE)) {
            const DWORD systemError = GetLastError();
            std::cerr << "Gate C console control handler registration failed: "
                      << systemError << '\n';
            router.stop();
            trace.flush();
            for (auto& writer : writers) writer->stop();
            (void)rollbackAndSignal(true);
            return 47;
        }
        std::cout
            << "Gate C controlled-process host is running.\n"
            << "Each target uses a separate process-local adapter DLL and virtual "
               "keyboard/mouse/cursor/focus state. No Windows API hook or physical "
               "suppression is active.\n"
            << "Press Ctrl+C or close either controlled target to stop.\n";

        bool writerFailure = false;
        bool recoveryFailure = false;
        auto nextLeaseRenewal = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(500);
        while (!gStopRequested.load()) {
            router.processMessages();
            bool allRunning = true;
            for (const auto& session : sessions) {
                allRunning = allRunning && session.process.running();
            }
            for (const auto& writer : writers) {
                if (writer->failed()) {
                    std::cerr << "Gate C target writer failed: "
                              << writer->lastError() << '\n';
                    writerFailure = true;
                    break;
                }
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextLeaseRenewal) {
                std::string error;
                if (!recovery.watchdog.renew(&error)) {
                    std::cerr << "Gate C watchdog lease renewal failed: "
                              << error << '\n';
                    recoveryFailure = true;
                    break;
                }
                nextLeaseRenewal = now + std::chrono::milliseconds(500);
            }
            if (!allRunning || writerFailure) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        router.stop();
        trace.flush();
        for (auto& writer : writers) writer->stop();

        std::uint64_t droppedFrames = 0;
        for (const auto& writer : writers) {
            droppedFrames += writer->droppedFrames();
            if (writer->failed()) writerFailure = true;
        }
        if (droppedFrames != 0) {
            std::cerr << "Gate C bounded queues rejected " << droppedFrames
                      << " input frames; those route attempts were marked failed.\n";
        }

        constexpr std::uint64_t kRollbackMetricCorrelationId =
            (std::numeric_limits<std::uint64_t>::max)();
        hydra::InputMetricSample rollbackStart;
        rollbackStart.correlationId = kRollbackMetricCorrelationId;
        rollbackStart.timestampMicros = hydra::monotonicInputMetricTimestampMicros();
        rollbackStart.stage = hydra::InputMetricStage::RollbackStarted;
        (void)metrics.tryRecord(rollbackStart);

        const bool sessionEndRequested =
            sessionEndWindow.lastKind() != SessionEndKind::None;
        const bool cleanExit = rollbackAndSignal(
            writerFailure || recoveryFailure || sessionEndRequested);
        if (sessionEndWindow.queryTimedOut()) {
            std::cerr << "Gate C session-end cleanup exceeded the bounded query wait; "
                         "Windows session end was vetoed.\n";
        }

        auto rollbackComplete = rollbackStart;
        rollbackComplete.timestampMicros = hydra::monotonicInputMetricTimestampMicros();
        rollbackComplete.stage = hydra::InputMetricStage::RollbackCompleted;
        (void)metrics.tryRecord(rollbackComplete);

        bool metricsFailure = false;
        const auto metricsSnapshot = metrics.snapshot();
        hydra::InputMetricsReport metricsReport;
        const auto metricsResult =
            hydra::buildInputMetricsReport(metricsSnapshot, metricsReport);
        if (metricsResult != hydra::InputMetricsReportResult::Success) {
            std::cerr << "Gate C metrics report failed: "
                      << hydra::inputMetricsReportResultName(metricsResult) << '\n';
            metricsFailure = true;
        } else {
            std::ofstream metricsOutput(
                m_options.metricsReportPath,
                std::ios::out | std::ios::trunc | std::ios::binary);
            if (!metricsOutput.is_open()) {
                std::cerr << "Gate C metrics report could not open: "
                          << m_options.metricsReportPath << '\n';
                metricsFailure = true;
            } else {
                metricsOutput << hydra::encodeInputMetricsReportJson(metricsReport)
                              << '\n';
                metricsOutput.flush();
                if (!metricsOutput.good()) {
                    std::cerr << "Gate C metrics report write failed: "
                              << m_options.metricsReportPath << '\n';
                    metricsFailure = true;
                }
            }
        }
        SetConsoleCtrlHandler(consoleControlHandler, FALSE);
        return writerFailure || recoveryFailure || !cleanExit || metricsFailure
                   ? 46
                   : EXIT_SUCCESS;
    }

    HostOptions m_options;
    hydra::gatec::ProcessArchitecture m_expectedArchitecture{
        hydra::gatec::ProcessArchitecture::Unknown};
};

#endif

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    HostOptions options;
    if (!parseOptions(argc, argv, options) || options.showHelp) {
        printUsage(options.showHelp ? std::cout : std::cerr);
        return options.showHelp ? EXIT_SUCCESS : 2;
    }
    GateCHost host(std::move(options));
    return host.run();
}
#else
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
        return portableSelfTest();
    }
    printUsage(std::cerr);
    return 3;
}
#endif
