#include "hydra/gate_c_adapter.h"
#include "hydra/gate_c_architecture.hpp"
#include "hydra/gate_c_protocol.hpp"
#include "hydra/gate_c_probe_snapshot.hpp"
#include "hydra/gate_c_transport.hpp"
#include "hydra/input_observation.hpp"
#include "hydra/input_router.hpp"
#include "hydra/virtual_input_state.hpp"
#include "hydra/workspace_manager.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
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
    bool selfTest{false};
    bool architectureSelfTest{false};
    bool baselineSelfTest{false};
    bool pollingShimSelfTest{false};
    bool cursorFocusShimSelfTest{false};
    bool rawInputShimSelfTest{false};
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
        << "  hydra_gate_c_host --cursor-focus-shim-self-test --target <hydra_gate_c_api_probe.exe>\n"
        << "                     --shim <hydra_gate_c_shim.dll>\n"
        << "  hydra_gate_c_host --raw-input-shim-self-test --target <hydra_gate_c_api_probe.exe>\n"
        << "                     --shim <hydra_gate_c_shim.dll>\n"
        << "  hydra_gate_c_host --protocol-error-self-test [--target <hydra_gate_c_target.exe>]\n"
        << "  hydra_gate_c_host [--profile <workspace_config.json>] [--trace <file.jsonl>]\n"
        << "                     [--target <hydra_gate_c_target.exe>]\n\n"
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
        static_cast<int>(options.protocolErrorSelfTest);
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
          m_processId(std::exchange(other.m_processId, 0)) {}
    ChildProcess& operator=(ChildProcess&& other) noexcept {
        if (this == &other) return *this;
        close();
        m_process = std::exchange(other.m_process, nullptr);
        m_processId = std::exchange(other.m_processId, 0);
        return *this;
    }

    bool valid() const noexcept { return m_process != nullptr; }
    HANDLE handle() const noexcept { return m_process; }
    std::uint32_t processId() const noexcept { return m_processId; }

    void assign(HANDLE process, std::uint32_t processId) noexcept {
        close();
        m_process = process;
        m_processId = processId;
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
        if (m_process != nullptr) {
            CloseHandle(m_process);
            m_process = nullptr;
            m_processId = 0;
        }
    }

private:
    HANDLE m_process{nullptr};
    std::uint32_t m_processId{0};
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

class TargetInputWriter {
public:
    explicit TargetInputWriter(TargetSession& session) : m_session(session) {}
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

    bool enqueue(const InputEventMessage& input) {
        if (m_failed.load() || m_stopping.load()) {
            return false;
        }

        {
            std::scoped_lock lock(m_mutex);
            if (m_failed.load() || m_stopping.load()) {
                return false;
            }
            if (m_queue.size() >= kMaximumQueuedFrames) {
                ++m_droppedFrames;
                return false;
            }
            const auto sequence = m_session.nextSequence++;
            auto frame = hydra::gatec::encodeInputEvent(sequence, input);
            if (frame.empty()) {
                return false;
            }
            m_queue.push_back(std::move(frame));
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
            m_droppedFrames.fetch_add(
                static_cast<std::uint64_t>(m_queue.size()));
            m_queue.clear();
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
    void writerLoop(std::stop_token token) {
        while (true) {
            std::vector<std::byte> frame;
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
                frame = std::move(m_queue.front());
                m_queue.pop_front();
            }

            std::string error;
            std::uint32_t systemError = 0;
            if (!m_session.channel.writeFrame(
                    frame, kWriteTimeoutMs, &error, &systemError)) {
                {
                    std::scoped_lock lock(m_errorMutex);
                    m_lastError = error + " (" +
                                  std::to_string(systemError) + ")";
                }
                m_failed.store(true);
                m_stopping.store(true);
                std::scoped_lock lock(m_mutex);
                m_queue.clear();
                break;
            }
        }
    }

    static constexpr std::size_t kMaximumQueuedFrames = 2048;
    static constexpr std::uint32_t kWriteTimeoutMs = 1000;

    TargetSession& m_session;
    mutable std::mutex m_errorMutex;
    std::string m_lastError;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<std::vector<std::byte>> m_queue;
    std::jthread m_thread;
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_failed{false};
    std::atomic<std::uint64_t> m_droppedFrames{0};
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
        if (m_options.targetPath.empty() && m_options.artifactRoot.empty()) {
            m_options.targetPath = siblingTargetPath(
                m_options.baselineSelfTest ||
                m_options.pollingShimSelfTest ||
                m_options.cursorFocusShimSelfTest ||
                m_options.rawInputShimSelfTest);
        }
        if ((m_options.pollingShimSelfTest ||
             m_options.cursorFocusShimSelfTest ||
             m_options.rawInputShimSelfTest) &&
            m_options.shimPath.empty() && m_options.artifactRoot.empty()) {
            m_options.shimPath = siblingShimPath();
        }
    }

    int run() {
        if (!m_options.artifactRoot.empty()) {
            const auto kind = (m_options.baselineSelfTest ||
                               m_options.pollingShimSelfTest ||
                               m_options.cursorFocusShimSelfTest ||
                               m_options.rawInputShimSelfTest)
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
            m_options.rawInputShimSelfTest) {
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
        if (m_options.protocolErrorSelfTest) {
            return runProtocolErrorSelfTest();
        }
        return runInteractive();
    }

private:
    bool launchTarget(TargetSession& session, bool headless,
                      std::wstring_view extraArguments = {},
                      std::uint32_t handshakeTimeoutMs = kHandshakeTimeoutMs,
                      bool requireOwnedWindow = false) {
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
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(
                m_options.targetPath.c_str(), commandLine.data(), nullptr,
                nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                &startup, &process)) {
            std::cerr << "CreateProcessW failed for Seat " << session.seatId
                      << ": " << GetLastError() << '\n';
            return false;
        }
        CloseHandle(process.hThread);
        session.process.assign(process.hProcess, process.dwProcessId);

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
        ack.grantedCapabilities = hydra::gatec::testCapabilityBits(
            requireOwnedWindow
                ? (m_options.rawInputShimSelfTest
                       ? hydra::gatec::kControlledRawInputProbeCapabilities
                       : (m_options.cursorFocusShimSelfTest
                       ? hydra::gatec::kControlledCursorFocusProbeCapabilities
                       : (m_options.pollingShimSelfTest
                              ? hydra::gatec::kControlledPollingProbeCapabilities
                              : hydra::gatec::kControlledApiProbeCapabilities)))
                : hydra::gatec::kControlledTargetCapabilities);
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
                cleanupSessions(sessions, true);
                return false;
            }

            const auto firstA = queryProbeSnapshot(sessions[0], 0x41);
            const auto secondA = queryProbeSnapshot(sessions[1], 0x41);
            const auto secondB = queryProbeSnapshot(sessions[1], 0x42);
            const auto firstASecond =
                queryProbeSnapshot(sessions[0], 0x41);
            if (!firstA || !secondA || !secondB || !firstASecond) {
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

        std::vector<TargetSession> sessions(2);
        for (std::size_t index = 0; index < sessions.size(); ++index) {
            sessions[index].seatId = activeSeats[index];
            if (!launchTarget(sessions[index], false)) {
                cleanupSessions(sessions, true);
                return 42;
            }
            seats.assignTargetWindow(sessions[index].seatId,
                                     sessions[index].targetWindow);
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
                cleanupSessions(sessions, true);
                return 43;
            }
        }

        std::vector<std::unique_ptr<TargetInputWriter>> writers;
        std::unordered_map<SeatId, TargetInputWriter*> writerBySeat;
        writers.reserve(sessions.size());
        for (auto& session : sessions) {
            auto writer = std::make_unique<TargetInputWriter>(session);
            if (!writer->start()) {
                for (auto& started : writers) started->stop();
                cleanupSessions(sessions, true);
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
                return found->second->enqueue(toProtocolEvent(event));
            });
        const auto bindings = observation.rebuildBindings();
        std::cout << "Gate C host bound " << bindings.boundDevices
                  << " exclusive keyboard/mouse identities.\n";
        if (!bindings.ambiguousSharedDevices.empty()) {
            std::cout << "Ambiguous shared input devices will fail closed: "
                      << bindings.ambiguousSharedDevices.size() << '\n';
        }

        hydra::InputTraceWriter trace(m_options.tracePath);
        hydra::InputRouter router;
        router.setGlobalCallback([&](const RawInputEvent& event) {
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
            cleanupSessions(sessions, true);
            return 45;
        }

        gStopRequested.store(false);
        SetConsoleCtrlHandler(consoleControlHandler, TRUE);
        std::cout
            << "Gate C controlled-process host is running.\n"
            << "Each target uses a separate process-local adapter DLL and virtual "
               "keyboard/mouse/cursor/focus state. No Windows API hook or physical "
               "suppression is active.\n"
            << "Press Ctrl+C or close either controlled target to stop.\n";

        bool writerFailure = false;
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

        const bool cleanExit = cleanupSessions(sessions, writerFailure);
        SetConsoleCtrlHandler(consoleControlHandler, FALSE);
        return writerFailure || !cleanExit ? 46 : EXIT_SUCCESS;
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
