#include "hydra/gate_c_protocol.hpp"
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
using hydra::gatec::QuerySnapshotMessage;
using hydra::gatec::SessionToken;
using hydra::gatec::StateSnapshotMessage;
using hydra::gatec::TransportStatus;

struct HostOptions {
    std::wstring targetPath;
    std::string profilePath{"workspace_config.json"};
    std::string tracePath{"hydra_gate_c_host.jsonl"};
    bool selfTest{false};
    bool headlessTargets{false};
    bool showHelp{false};
};

void printUsage(std::ostream& output) {
    output
        << "HydraSeat Gate C controlled-process host\n\n"
        << "Usage:\n"
        << "  hydra_gate_c_host --self-test [--target <hydra_gate_c_target.exe>]\n"
        << "  hydra_gate_c_host [--profile <workspace_config.json>] [--trace <file.jsonl>]\n"
        << "                     [--target <hydra_gate_c_target.exe>] [--headless-targets]\n\n"
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
        } else if (argument == L"--profile" && index + 1 < argc) {
            options.profilePath = wideToUtf8(argv[++index]);
        } else if (argument == L"--trace" && index + 1 < argc) {
            options.tracePath = wideToUtf8(argv[++index]);
        } else if (argument == L"--self-test") {
            options.selfTest = true;
            options.headlessTargets = true;
        } else if (argument == L"--help" || argument == L"-h") {
            options.showHelp = true;
        } else {
            return false;
        }
    }
    return true;
}

std::wstring siblingTargetPath() {
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
    path += L"hydra_gate_c_target.exe";
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
        const auto sequence = m_session.nextSequence++;
        auto frame = hydra::gatec::encodeInputEvent(sequence, input);
        if (frame.empty()) {
            return false;
        }

        {
            std::scoped_lock lock(m_mutex);
            if (m_queue.size() >= kMaximumQueuedFrames) {
                ++m_droppedFrames;
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
                if (m_queue.empty()) {
                    if (token.stop_requested() || m_stopping.load()) {
                        break;
                    }
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

class GateCHost {
public:
    explicit GateCHost(HostOptions options)
        : m_options(std::move(options)) {
        if (m_options.targetPath.empty()) {
            m_options.targetPath = siblingTargetPath();
        }
    }

    int run() {
        if (GetFileAttributesW(m_options.targetPath.c_str()) ==
            INVALID_FILE_ATTRIBUTES) {
            std::cerr << "Controlled target was not found: "
                      << wideToUtf8(m_options.targetPath) << '\n';
            return 20;
        }
        if (m_options.selfTest) {
            return runProcessSelfTest();
        }
        return runInteractive();
    }

private:
    bool launchTarget(TargetSession& session, bool headless) {
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

        if (!hydra::gatec::waitForGateCClient(
                session.channel, kHandshakeTimeoutMs, &error, &systemError)) {
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
            hello.processId != session.process.processId()) {
            std::cerr << "Hello identity validation failed for Seat "
                      << session.seatId << ": " << error << '\n';
            rejectHello(session, 2002);
            return false;
        }

        session.targetWindow = hello.targetWindow;
        HelloAckMessage ack;
        ack.accepted = true;
        ack.serverProcessId = GetCurrentProcessId();
        ack.grantedCapabilities = hydra::gatec::testCapabilityBits(
            hydra::gatec::kControlledTargetCapabilities);
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
               "cursor, clip, and virtual-focus state.\n";
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
