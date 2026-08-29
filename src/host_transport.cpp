#include "hydra/host_transport.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <limits>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#endif

namespace hydra::hostipc {
namespace {

constexpr std::uint32_t kHandshakeTimeoutMs = 5000u;
constexpr std::size_t kMaxConnectedClients = 16u;
constexpr std::size_t kSeenCorrelationCapacity = 128u;

std::uint64_t makeCorrelationSeed() noexcept {
    std::uint64_t value = 0;
#ifdef _WIN32
    const NTSTATUS status = BCryptGenRandom(
        nullptr, reinterpret_cast<PUCHAR>(&value), static_cast<ULONG>(sizeof(value)),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status >= 0 && value != 0) return value;
#else
    try {
        std::random_device random;
        value = (static_cast<std::uint64_t>(random()) << 32u) ^
                static_cast<std::uint64_t>(random());
        if (value != 0) return value;
    } catch (...) {
    }
#endif
    static std::atomic<std::uint64_t> fallback{1u};
    const auto clock = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    value = clock ^ (fallback.fetch_add(1u, std::memory_order_relaxed) *
                     0x9e3779b97f4a7c15ull);
    return value == 0 ? 1u : value;
}

#ifdef _WIN32

struct UniqueHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : value(handle) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value(other.value) {
        other.value = INVALID_HANDLE_VALUE;
    }
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            value = other.value;
            other.value = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    bool valid() const noexcept {
        return value != nullptr && value != INVALID_HANDLE_VALUE;
    }
    HANDLE release() noexcept {
        const auto handle = value;
        value = INVALID_HANDLE_VALUE;
        return handle;
    }
    void reset(HANDLE replacement = INVALID_HANDLE_VALUE) noexcept {
        if (valid()) CloseHandle(value);
        value = replacement;
    }
};

std::string win32ErrorMessage(const char* prefix, DWORD code) {
    return std::string(prefix) + " (Win32=" + std::to_string(code) + ")";
}

bool overlappedTransfer(HANDLE handle, bool write, void* buffer, DWORD bytes,
                        std::uint32_t timeoutMs, DWORD& transferred,
                        std::string* error) {
    UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.valid()) {
        if (error) *error = win32ErrorMessage("CreateEvent failed", GetLastError());
        return false;
    }
    OVERLAPPED overlapped{};
    overlapped.hEvent = event.value;
    BOOL started = write
        ? WriteFile(handle, buffer, bytes, &transferred, &overlapped)
        : ReadFile(handle, buffer, bytes, &transferred, &overlapped);
    if (started == FALSE) {
        const DWORD code = GetLastError();
        if (code != ERROR_IO_PENDING) {
            if (error) *error = win32ErrorMessage(write ? "WriteFile failed" : "ReadFile failed", code);
            return false;
        }
        const DWORD wait = WaitForSingleObject(event.value, timeoutMs);
        if (wait != WAIT_OBJECT_0) {
            (void)CancelIoEx(handle, &overlapped);
            if (error) {
                *error = wait == WAIT_TIMEOUT ? "host pipe I/O timeout"
                                              : win32ErrorMessage("host pipe wait failed", GetLastError());
            }
            return false;
        }
        if (GetOverlappedResult(handle, &overlapped, &transferred, FALSE) == FALSE) {
            if (error) *error = win32ErrorMessage("GetOverlappedResult failed", GetLastError());
            return false;
        }
    }
    return true;
}

bool writeAll(HANDLE handle, std::span<const std::byte> bytes,
              std::uint32_t timeoutMs, std::string* error) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD transferred = 0;
        auto* data = const_cast<std::byte*>(bytes.data() + offset);
        if (!overlappedTransfer(handle, true, data, chunk, timeoutMs, transferred, error) ||
            transferred == 0) {
            return false;
        }
        offset += transferred;
    }
    return true;
}

bool readAll(HANDLE handle, std::span<std::byte> bytes,
             std::uint32_t timeoutMs, std::string* error) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD transferred = 0;
        if (!overlappedTransfer(handle, false, bytes.data() + offset, chunk,
                                timeoutMs, transferred, error) || transferred == 0) {
            return false;
        }
        offset += transferred;
    }
    return true;
}

std::uint32_t readLe32(const std::byte* data) {
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[0])) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[1])) << 8u) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[2])) << 16u) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[3])) << 24u);
}

std::uint64_t readLe64(const std::byte* data) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        value |= static_cast<std::uint64_t>(
                     std::to_integer<std::uint8_t>(data[shift / 8u]))
                 << shift;
    }
    return value;
}

bool sendFrame(HANDLE handle, const Frame& frame, std::uint32_t timeoutMs,
               std::string* error) {
    const auto bytes = encodeFrame(frame);
    if (bytes.empty()) {
        if (error) *error = "host frame encoding failed";
        return false;
    }
    return writeAll(handle, bytes, timeoutMs, error);
}

std::optional<Frame> receiveFrame(HANDLE handle, std::uint32_t timeoutMs,
                                  std::string* error,
                                  DecodeResult* decodeResult = nullptr,
                                  std::uint64_t* headerCorrelation = nullptr) {
    std::array<std::byte, kHostProtocolHeaderBytes> header{};
    if (!readAll(handle, header, timeoutMs, error)) return std::nullopt;
    if (headerCorrelation) *headerCorrelation = readLe64(header.data() + 16);
    const std::uint32_t payloadSize = readLe32(header.data() + 12);
    if (payloadSize > kHostProtocolMaxPayloadBytes) {
        if (error) *error = "host frame payload exceeds protocol bound";
        if (decodeResult) {
            decodeResult->ok = false;
            decodeResult->error = ErrorCode::Malformed;
            decodeResult->diagnostic = "host frame payload exceeds protocol bound";
        }
        return std::nullopt;
    }
    std::vector<std::byte> bytes(header.begin(), header.end());
    const auto headerSize = bytes.size();
    bytes.resize(headerSize + payloadSize);
    if (payloadSize != 0 &&
        !readAll(handle,
                 std::span<std::byte>(bytes).subspan(headerSize, payloadSize),
                 timeoutMs, error)) {
        return std::nullopt;
    }
    DecodeResult decoded;
    auto frame = decodeFrame(bytes, &decoded);
    if (decodeResult) *decodeResult = decoded;
    if (!frame && error) *error = decoded.diagnostic;
    return frame;
}

struct TokenIdentity {
    std::vector<std::byte> sid;
    DWORD sessionId{0};
};

std::optional<TokenIdentity> tokenIdentity(HANDLE token) {
    DWORD bytes = 0;
    (void)GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    if (bytes == 0) return std::nullopt;
    std::vector<std::byte> storage(bytes);
    if (GetTokenInformation(token, TokenUser, storage.data(), bytes, &bytes) == FALSE) {
        return std::nullopt;
    }
    auto* user = reinterpret_cast<TOKEN_USER*>(storage.data());
    const DWORD sidBytes = GetLengthSid(user->User.Sid);
    TokenIdentity identity;
    identity.sid.resize(sidBytes);
    if (CopySid(sidBytes, identity.sid.data(), user->User.Sid) == FALSE) {
        return std::nullopt;
    }
    DWORD sessionBytes = sizeof(identity.sessionId);
    if (GetTokenInformation(token, TokenSessionId, &identity.sessionId,
                            sessionBytes, &sessionBytes) == FALSE) {
        return std::nullopt;
    }
    return identity;
}

std::optional<TokenIdentity> currentIdentity() {
    HANDLE rawToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken) == FALSE) {
        return std::nullopt;
    }
    UniqueHandle token(rawToken);
    return tokenIdentity(token.value);
}

bool sameIdentity(const TokenIdentity& a, const TokenIdentity& b) {
    return a.sessionId == b.sessionId && !a.sid.empty() && !b.sid.empty() &&
           EqualSid(const_cast<std::byte*>(a.sid.data()),
                    const_cast<std::byte*>(b.sid.data())) != FALSE;
}

bool validatePipeClientIdentity(HANDLE pipe, std::string* error) {
    ULONG clientPid = 0;
    if (GetNamedPipeClientProcessId(pipe, &clientPid) == FALSE || clientPid == 0) {
        if (error) *error = "unable to identify named-pipe client process";
        return false;
    }
    UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, clientPid));
    if (!process.valid()) {
        if (error) *error = "unable to open named-pipe client process for identity check";
        return false;
    }
    HANDLE rawToken = nullptr;
    if (OpenProcessToken(process.value, TOKEN_QUERY, &rawToken) == FALSE) {
        if (error) *error = "unable to query named-pipe client token";
        return false;
    }
    UniqueHandle token(rawToken);
    const auto client = tokenIdentity(token.value);
    const auto server = currentIdentity();
    if (!client || !server || !sameIdentity(*client, *server)) {
        if (error) *error = "host IPC client is not the same Windows user/session";
        return false;
    }
    return true;
}

std::optional<std::wstring> currentUserSidString() {
    const auto identity = currentIdentity();
    if (!identity || identity->sid.empty()) return std::nullopt;
    LPWSTR raw = nullptr;
    if (ConvertSidToStringSidW(
            reinterpret_cast<PSID>(const_cast<std::byte*>(identity->sid.data())),
            &raw) == FALSE || raw == nullptr) {
        return std::nullopt;
    }
    std::wstring value(raw);
    LocalFree(raw);
    return value;
}

UniqueHandle createServerPipe(std::string* error) {
    const auto sid = currentUserSidString();
    if (!sid) {
        if (error) *error = "unable to resolve current Windows user SID";
        return {};
    }
    const std::wstring sddl = L"D:P(A;;GA;;;" + *sid + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) == FALSE) {
        if (error) *error = win32ErrorMessage("security descriptor creation failed", GetLastError());
        return {};
    }
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    const auto name = currentHostPipeName();
    HANDLE pipe = CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        static_cast<DWORD>(kMaxConnectedClients),
        static_cast<DWORD>(kHostProtocolMaxPayloadBytes + kHostProtocolHeaderBytes),
        static_cast<DWORD>(kHostProtocolMaxPayloadBytes + kHostProtocolHeaderBytes),
        0, &attributes);
    const DWORD code = GetLastError();
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) {
        if (error) *error = win32ErrorMessage("CreateNamedPipe failed", code);
        return {};
    }
    return UniqueHandle(pipe);
}

bool connectServerPipe(HANDLE pipe, std::atomic<bool>& stopRequested,
                       std::string* error) {
    UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.valid()) {
        if (error) *error = "unable to create host accept event";
        return false;
    }
    OVERLAPPED overlapped{};
    overlapped.hEvent = event.value;
    BOOL connected = ConnectNamedPipe(pipe, &overlapped);
    if (connected != FALSE) return true;
    DWORD code = GetLastError();
    if (code == ERROR_PIPE_CONNECTED) return true;
    if (code != ERROR_IO_PENDING) {
        if (error) *error = win32ErrorMessage("ConnectNamedPipe failed", code);
        return false;
    }
    while (!stopRequested.load(std::memory_order_acquire)) {
        const DWORD wait = WaitForSingleObject(event.value, 100);
        if (wait == WAIT_OBJECT_0) {
            DWORD transferred = 0;
            if (GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) != FALSE) {
                return true;
            }
            code = GetLastError();
            if (code == ERROR_PIPE_CONNECTED) return true;
            if (error) *error = win32ErrorMessage("ConnectNamedPipe completion failed", code);
            return false;
        }
        if (wait != WAIT_TIMEOUT) {
            if (error) *error = "host accept wait failed";
            return false;
        }
    }
    (void)CancelIoEx(pipe, &overlapped);
    return false;
}

UniqueHandle connectClientPipe(std::uint32_t timeoutMs, std::string* error) {
    const auto name = currentHostPipeName();
    if (WaitNamedPipeW(name.c_str(), timeoutMs) == FALSE) {
        if (error) *error = win32ErrorMessage("host pipe is unavailable", GetLastError());
        return {};
    }
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        if (error) *error = win32ErrorMessage("host pipe connection failed", GetLastError());
        return {};
    }
    return UniqueHandle(pipe);
}

#endif // _WIN32

} // namespace

std::wstring currentHostPipeName() {
#ifdef _WIN32
    DWORD sessionId = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) == FALSE) sessionId = 0;
    return L"\\\\.\\pipe\\HydraSeat.Host.v3." + std::to_wstring(sessionId);
#else
    return L"HydraSeat.Host.v3.unsupported";
#endif
}

class HostControlClient::Impl {
public:
    Impl() : nextCorrelation(makeCorrelationSeed()) {}
#ifdef _WIN32
    UniqueHandle pipe;
#endif
    ClientRole role{ClientRole::ReadOnly};
    std::atomic<std::uint64_t> nextCorrelation;
    std::optional<std::uint64_t> subscriptionCorrelation;

    std::uint64_t allocateCorrelation() {
        auto value = nextCorrelation.fetch_add(1, std::memory_order_relaxed);
        if (value == 0) value = nextCorrelation.fetch_add(1, std::memory_order_relaxed);
        return value;
    }
};

HostControlClient::HostControlClient() : impl_(std::make_unique<Impl>()) {}
HostControlClient::~HostControlClient() = default;
HostControlClient::HostControlClient(HostControlClient&&) noexcept = default;
HostControlClient& HostControlClient::operator=(HostControlClient&&) noexcept = default;

bool HostControlClient::connect(ClientRole requestedRole, std::uint32_t timeoutMs,
                                std::string* error) {
    const SeatId defaultSeatId = requestedRole == ClientRole::ReadOnly ? 0u : 1u;
    return connectForSeat(requestedRole, defaultSeatId, timeoutMs, error);
}

bool HostControlClient::connectForSeat(ClientRole requestedRole, SeatId requestedSeatId,
                                       std::uint32_t timeoutMs,
                                std::string* error) {
    close();
#ifdef _WIN32
    impl_->pipe = connectClientPipe(timeoutMs, error);
    if (!impl_->pipe.valid()) return false;
    impl_->role = requestedRole;
    const auto correlation = impl_->allocateCorrelation();
    Frame helloFrame{MessageType::Hello, correlation, encodeHello(Hello{requestedRole, requestedSeatId})};
    if (!sendFrame(impl_->pipe.value, helloFrame, timeoutMs, error)) {
        close();
        return false;
    }
    const auto response = receiveFrame(impl_->pipe.value, timeoutMs, error);
    if (response && response->correlationId == correlation && response->type == MessageType::Error) {
        const auto protocolError = decodeError(response->payload);
        if (error) {
            *error = protocolError ? protocolError->diagnostic : "host rejected control handshake";
        }
        close();
        return false;
    }
    if (!response || response->correlationId != correlation ||
        response->type != MessageType::HelloAck) {
        if (error && (!response || error->empty())) *error = "host hello acknowledgement mismatch";
        close();
        return false;
    }
    const auto ack = decodeHelloAck(response->payload);
    DWORD expectedSession = 0;
    (void)ProcessIdToSessionId(GetCurrentProcessId(), &expectedSession);
    if (!ack || ack->role != requestedRole || ack->seatId != requestedSeatId ||
        ack->windowsSessionId != expectedSession ||
        (requestedRole == ClientRole::Control && ack->managementSeatId != requestedSeatId)) {
        if (error) *error = "host hello acknowledgement identity mismatch";
        close();
        return false;
    }
    return true;
#else
    (void)requestedRole; (void)requestedSeatId; (void)timeoutMs;
    if (error) *error = "host IPC transport is Windows-only";
    return false;
#endif
}

void HostControlClient::close() noexcept {
#ifdef _WIN32
    if (impl_) {
        impl_->pipe.reset();
        impl_->subscriptionCorrelation.reset();
    }
#endif
}

bool HostControlClient::connected() const noexcept {
#ifdef _WIN32
    return impl_ && impl_->pipe.valid();
#else
    return false;
#endif
}

ClientRole HostControlClient::role() const noexcept {
    return impl_->role;
}

std::optional<Frame> HostControlClient::transact(
    MessageType requestType, std::span<const std::byte> payload,
    std::uint64_t correlationId, std::uint32_t timeoutMs, std::string* error) {
#ifdef _WIN32
    if (!connected() || correlationId == 0) {
        if (error) *error = "host client is disconnected or correlation is zero";
        return std::nullopt;
    }
    Frame request{requestType, correlationId,
                  std::vector<std::byte>(payload.begin(), payload.end())};
    if (!sendFrame(impl_->pipe.value, request, timeoutMs, error)) return std::nullopt;
    auto response = receiveFrame(impl_->pipe.value, timeoutMs, error);
    if (!response || response->correlationId != correlationId) {
        if (error && (!response || error->empty())) *error = "host response correlation mismatch";
        return std::nullopt;
    }
    return response;
#else
    (void)requestType; (void)payload; (void)correlationId; (void)timeoutMs;
    if (error) *error = "host IPC transport is Windows-only";
    return std::nullopt;
#endif
}

std::optional<runtime::HostRuntimeSnapshot> HostControlClient::getSnapshot(
    std::uint32_t timeoutMs, std::string* error) {
    const auto correlation = impl_->allocateCorrelation();
    const auto response = transact(MessageType::GetSnapshot, {}, correlation, timeoutMs, error);
    if (!response || response->type != MessageType::Snapshot) {
        if (response && response->type == MessageType::Error && error) {
            const auto decoded = decodeError(response->payload);
            if (decoded) *error = decoded->diagnostic;
        }
        return std::nullopt;
    }
    auto snapshot = decodeSnapshot(response->payload);
    if (!snapshot && error) *error = "host snapshot payload is malformed";
    return snapshot;
}

std::optional<runtime::RuntimeCommandResult> HostControlClient::command(
    MessageType requestType, std::uint32_t timeoutMs, std::string* error,
    std::optional<ErrorPayload>* protocolError) {
    const auto correlation = impl_->allocateCorrelation();
    const auto response = transact(requestType, {}, correlation, timeoutMs, error);
    if (!response) return std::nullopt;
    if (response->type == MessageType::Error) {
        auto decoded = decodeError(response->payload);
        if (protocolError) *protocolError = decoded;
        if (error && decoded) *error = decoded->diagnostic;
        return std::nullopt;
    }
    if (response->type != responseTypeFor(requestType)) {
        if (error) *error = "unexpected host command response type";
        return std::nullopt;
    }
    auto result = decodeCommandResult(response->payload);
    if (!result && error) *error = "host command result payload is malformed";
    return result;
}

std::optional<runtime::RuntimeCommandResult> HostControlClient::applyProfile(
    const ProfilePayload& profile, std::uint32_t timeoutMs, std::string* error,
    std::optional<ErrorPayload>* protocolError) {
    const auto payload = encodeProfilePayload(profile);
    if (payload.empty()) {
        if (error) *error = "profile payload is invalid or exceeds protocol bounds";
        return std::nullopt;
    }
    const auto correlation = impl_->allocateCorrelation();
    const auto response = transact(MessageType::ApplyProfile, payload, correlation, timeoutMs, error);
    if (!response) return std::nullopt;
    if (response->type == MessageType::Error) {
        auto decoded = decodeError(response->payload);
        if (protocolError) *protocolError = decoded;
        if (error && decoded) *error = decoded->diagnostic;
        return std::nullopt;
    }
    if (response->type != MessageType::ApplyProfileResult) {
        if (error) *error = "unexpected host ApplyProfile response type";
        return std::nullopt;
    }
    auto result = decodeCommandResult(response->payload);
    if (!result && error) *error = "host ApplyProfile result payload is malformed";
    return result;
}

std::optional<runtime::SeatGameCommandResult> HostControlClient::seatGameCommand(
    MessageType requestType, const SeatGameCommandPayload& commandPayload,
    std::uint32_t timeoutMs, std::string* error,
    std::optional<ErrorPayload>* protocolError) {
    if (requestType != MessageType::AssignSeatGame &&
        requestType != MessageType::StartSeatGame &&
        requestType != MessageType::StopSeatGame) {
        if (error) *error = "unsupported Seat game command type";
        return std::nullopt;
    }
    const auto payload = encodeSeatGameCommandPayload(commandPayload);
    if (payload.empty()) {
        if (error) *error = "Seat game command payload is malformed";
        return std::nullopt;
    }
    const auto correlation = impl_->allocateCorrelation();
    const auto response = transact(requestType, payload, correlation, timeoutMs, error);
    if (!response) return std::nullopt;
    if (response->type == MessageType::Error) {
        auto decoded = decodeError(response->payload);
        if (protocolError) *protocolError = decoded;
        if (error && decoded) *error = decoded->diagnostic;
        return std::nullopt;
    }
    if (response->type != responseTypeFor(requestType)) {
        if (error) *error = "unexpected Seat game command response type";
        return std::nullopt;
    }
    auto result = decodeSeatGameCommandResult(response->payload);
    if (!result && error) *error = "Seat game command result payload is malformed";
    return result;
}

std::optional<runtime::SeatGameCommandResult> HostControlClient::reconcileSeatGames(
    std::uint32_t timeoutMs, std::string* error,
    std::optional<ErrorPayload>* protocolError) {
    const auto correlation = impl_->allocateCorrelation();
    const auto response = transact(MessageType::ReconcileSeatGames, {}, correlation,
                                   timeoutMs, error);
    if (!response) return std::nullopt;
    if (response->type == MessageType::Error) {
        auto decoded = decodeError(response->payload);
        if (protocolError) *protocolError = decoded;
        if (error && decoded) *error = decoded->diagnostic;
        return std::nullopt;
    }
    if (response->type != MessageType::ReconcileSeatGamesResult) {
        if (error) *error = "unexpected Seat reconcile response type";
        return std::nullopt;
    }
    auto result = decodeSeatGameCommandResult(response->payload);
    if (!result && error) *error = "Seat reconcile result payload is malformed";
    return result;
}

bool HostControlClient::ping(std::uint64_t nonce, std::uint32_t timeoutMs,
                             std::string* error) {
    const auto correlation = impl_->allocateCorrelation();
    const auto payload = encodePing(nonce);
    const auto response = transact(MessageType::Ping, payload, correlation, timeoutMs, error);
    if (!response || response->type != MessageType::Pong) return false;
    const auto pong = decodePing(response->payload);
    return pong && *pong == nonce;
}

std::optional<SubscriptionStart> HostControlClient::beginSubscription(
    std::uint64_t afterSequence, std::uint32_t maxEvents,
    std::uint32_t timeoutMs, std::string* error) {
    const auto correlation = impl_->allocateCorrelation();
    const auto payload = encodeSubscribeRequest(SubscribeRequest{afterSequence, maxEvents});
    const auto response = transact(MessageType::SubscribeEvents, payload, correlation,
                                   timeoutMs, error);
    if (!response || response->type != MessageType::SubscribeAck) {
        if (response && response->type == MessageType::Error && error) {
            const auto decoded = decodeError(response->payload);
            if (decoded) *error = decoded->diagnostic;
        }
        return std::nullopt;
    }
    auto snapshot = decodeSnapshot(response->payload);
    if (!snapshot) {
        if (error) *error = "subscription acknowledgement snapshot is malformed";
        return std::nullopt;
    }
    impl_->subscriptionCorrelation = correlation;
    return SubscriptionStart{std::move(*snapshot), correlation};
}

ReceivedEvent HostControlClient::readSubscriptionEvent(std::uint32_t timeoutMs,
                                                        std::string* error) {
    ReceivedEvent result;
#ifdef _WIN32
    if (!connected()) {
        if (error) *error = "host client is disconnected";
        return result;
    }
    if (!impl_->subscriptionCorrelation) {
        if (error) *error = "host client has no active event subscription";
        return result;
    }
    const auto frame = receiveFrame(impl_->pipe.value, timeoutMs, error);
    if (!frame) return result;
    if (frame->correlationId != *impl_->subscriptionCorrelation) {
        if (error) *error = "host subscription correlation mismatch";
        close();
        return result;
    }
    if (frame->type == MessageType::RuntimeEvent) {
        result.event = decodeRuntimeEvent(frame->payload);
        if (!result.event && error) *error = "runtime event payload is malformed";
    } else if (frame->type == MessageType::Error) {
        result.error = decodeError(frame->payload);
        if (!result.error && error) *error = "subscription error payload is malformed";
    } else if (error) {
        *error = "unexpected subscription frame type";
    }
#else
    (void)timeoutMs;
    if (error) *error = "host IPC transport is Windows-only";
#endif
    return result;
}

class HostControlServer::Impl {
public:
    explicit Impl(runtime::RuntimeHost& runtimeHost) : host(runtimeHost) {}

    struct ClientThread {
        std::thread worker;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    runtime::RuntimeHost& host;
    std::atomic<bool> stopRequested{false};
    std::atomic<std::size_t> activeClients{0};
    std::mutex threadsMutex;
    std::vector<ClientThread> threads;

    void reapFinishedThreads() {
        std::lock_guard lock(threadsMutex);
        auto it = threads.begin();
        while (it != threads.end()) {
            if (it->finished && it->finished->load(std::memory_order_acquire)) {
                if (it->worker.joinable()) it->worker.join();
                it = threads.erase(it);
            } else {
                ++it;
            }
        }
    }

#ifdef _WIN32
    bool sendError(HANDLE pipe, std::uint64_t correlation, ErrorCode code,
                   std::string diagnostic) {
        return sendFrame(pipe,
                         Frame{MessageType::Error, correlation,
                               encodeError(ErrorPayload{code, std::move(diagnostic)})},
                         kDefaultHostIpcTimeoutMs, nullptr);
    }

    bool rememberCorrelation(std::uint64_t correlation,
                             std::deque<std::uint64_t>& order,
                             std::unordered_set<std::uint64_t>& seen) {
        if (seen.contains(correlation)) return false;
        seen.insert(correlation);
        order.push_back(correlation);
        if (order.size() > kSeenCorrelationCapacity) {
            seen.erase(order.front());
            order.pop_front();
        }
        return true;
    }

    void subscriptionLoop(HANDLE pipe, const SubscribeRequest& request,
                          std::uint64_t correlation) {
        auto snapshot = host.snapshot();
        if (request.afterSequence > snapshot.transitionSequence) {
            (void)sendError(pipe, correlation, ErrorCode::ResnapshotRequired,
                            "subscription sequence is ahead of authoritative snapshot");
            return;
        }
        if (!sendFrame(pipe, Frame{MessageType::SubscribeAck, correlation,
                                   encodeSnapshot(snapshot)},
                       kDefaultHostIpcTimeoutMs, nullptr)) {
            return;
        }
        std::uint64_t sequence = snapshot.transitionSequence;
        while (!stopRequested.load(std::memory_order_acquire)) {
            bool overflow = false;
            const auto events = host.transitionEventsAfter(sequence, request.maxEvents, overflow);
            if (overflow) {
                (void)sendError(pipe, correlation, ErrorCode::ResnapshotRequired,
                                "runtime event subscription overflowed; reconnect and resnapshot");
                return;
            }
            for (const auto& event : events) {
                if (!sendFrame(pipe,
                               Frame{MessageType::RuntimeEvent, correlation,
                                     encodeRuntimeEvent(event)},
                               kDefaultHostIpcTimeoutMs, nullptr)) {
                    return;
                }
                sequence = event.sequence;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }

    void clientLoop(HANDLE rawPipe) {
        UniqueHandle pipe(rawPipe);
        const auto decrement = [this] {
            activeClients.fetch_sub(1, std::memory_order_acq_rel);
        };
        struct ActiveGuard {
            decltype(decrement)& fn;
            ~ActiveGuard() { fn(); }
        } activeGuard{decrement};

        std::string identityError;
        if (!validatePipeClientIdentity(pipe.value, &identityError)) return;

        const auto helloFrame = receiveFrame(pipe.value, kHandshakeTimeoutMs, nullptr);
        if (!helloFrame || helloFrame->type != MessageType::Hello ||
            helloFrame->correlationId == 0) {
            return;
        }
        const auto hello = decodeHello(helloFrame->payload);
        if (!hello) {
            (void)sendError(pipe.value, helloFrame->correlationId, ErrorCode::Malformed,
                            "malformed host hello payload");
            return;
        }
        const auto authoritySnapshot = host.snapshot();
        if (hello->role == ClientRole::Control &&
            hello->seatId != authoritySnapshot.managementSeatId) {
            (void)sendError(pipe.value, helloFrame->correlationId,
                            ErrorCode::PermissionDenied,
                            "global control is restricted to the configured Management Seat");
            return;
        }
        if (hello->role == ClientRole::SeatControl) {
            const auto configured = std::find_if(
                authoritySnapshot.configuredSeats.begin(),
                authoritySnapshot.configuredSeats.end(),
                [&](const SeatConfig& seat) {
                    return seat.active && seat.seatId == hello->seatId;
                });
            if (configured == authoritySnapshot.configuredSeats.end()) {
                (void)sendError(pipe.value, helloFrame->correlationId,
                                ErrorCode::PermissionDenied,
                                "Seat control requires an active configured Seat identity");
                return;
            }
        }
        DWORD sessionId = 0;
        (void)ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
        if (!sendFrame(pipe.value,
                       Frame{MessageType::HelloAck, helloFrame->correlationId,
                             encodeHelloAck(HelloAck{hello->role, hello->seatId, authoritySnapshot.managementSeatId,
                                          GetCurrentProcessId(), sessionId})},
                       kHandshakeTimeoutMs, nullptr)) {
            return;
        }

        host.controlClientConnected();
        struct HostClientGuard {
            runtime::RuntimeHost& host;
            ~HostClientGuard() { host.controlClientDisconnected(); }
        } hostClientGuard{host};

        std::deque<std::uint64_t> order;
        std::unordered_set<std::uint64_t> seen;
        (void)rememberCorrelation(helloFrame->correlationId, order, seen);

        while (!stopRequested.load(std::memory_order_acquire)) {
            std::string readError;
            const auto request = receiveFrame(pipe.value, 500u, &readError);
            if (!request) {
                if (readError == "host pipe I/O timeout") continue;
                return;
            }
            if (!rememberCorrelation(request->correlationId, order, seen)) {
                (void)sendError(pipe.value, request->correlationId,
                                ErrorCode::DuplicateCorrelation,
                                "duplicate command correlation ID");
                continue;
            }
            if (hello->role == ClientRole::ReadOnly && isMutatingRequest(request->type)) {
                (void)sendError(pipe.value, request->correlationId,
                                ErrorCode::PermissionDenied,
                                "read-only host client cannot mutate runtime state");
                continue;
            }
            if (hello->role == ClientRole::Control && isMutatingRequest(request->type) &&
                hello->seatId != host.snapshot().managementSeatId) {
                (void)sendError(pipe.value, request->correlationId,
                                ErrorCode::PermissionDenied,
                                "global control authority moved to another Management Seat");
                continue;
            }
            if (hello->role == ClientRole::SeatControl &&
                isMutatingRequest(request->type) &&
                request->type != MessageType::AssignSeatGame &&
                request->type != MessageType::StartSeatGame &&
                request->type != MessageType::StopSeatGame) {
                (void)sendError(pipe.value, request->correlationId,
                                ErrorCode::PermissionDenied,
                                "Seat control cannot mutate whole-machine runtime state");
                continue;
            }

            if (request->type == MessageType::GetSnapshot) {
                if (!request->payload.empty()) {
                    (void)sendError(pipe.value, request->correlationId,
                                    ErrorCode::Malformed, "GetSnapshot payload must be empty");
                    continue;
                }
                (void)sendFrame(pipe.value,
                                Frame{MessageType::Snapshot, request->correlationId,
                                      encodeSnapshot(host.snapshot())},
                                kDefaultHostIpcTimeoutMs, nullptr);
                continue;
            }
            if (request->type == MessageType::Ping) {
                const auto nonce = decodePing(request->payload);
                if (!nonce) {
                    (void)sendError(pipe.value, request->correlationId,
                                    ErrorCode::Malformed, "malformed ping payload");
                    continue;
                }
                (void)sendFrame(pipe.value,
                                Frame{MessageType::Pong, request->correlationId,
                                      encodePing(*nonce)},
                                kDefaultHostIpcTimeoutMs, nullptr);
                continue;
            }
            if (request->type == MessageType::SubscribeEvents) {
                const auto subscribe = decodeSubscribeRequest(request->payload);
                if (!subscribe) {
                    (void)sendError(pipe.value, request->correlationId,
                                    ErrorCode::Malformed, "malformed subscription request");
                    continue;
                }
                subscriptionLoop(pipe.value, *subscribe, request->correlationId);
                return;
            }
            if (request->type == MessageType::ApplyProfile) {
                const auto profile = decodeProfilePayload(request->payload);
                if (!profile) {
                    (void)sendError(pipe.value, request->correlationId,
                                    ErrorCode::Malformed, "malformed bounded profile payload");
                    continue;
                }
                const auto applied = host.loadProfile(
                    profile->seats, profile->managementSeatId, request->correlationId);
                if (!sendFrame(pipe.value,
                               Frame{MessageType::ApplyProfileResult, request->correlationId,
                                     encodeCommandResult(applied)},
                               kDefaultHostIpcTimeoutMs, nullptr)) {
                    return;
                }
                continue;
            }
            if (request->type == MessageType::AssignSeatGame ||
                request->type == MessageType::StartSeatGame ||
                request->type == MessageType::StopSeatGame) {
                const auto seatCommand = decodeSeatGameCommandPayload(request->payload);
                const bool bindingExpected = request->type == MessageType::AssignSeatGame;
                if (!seatCommand || seatCommand->binding.has_value() != bindingExpected) {
                    (void)sendError(pipe.value, request->correlationId,
                                    ErrorCode::Malformed,
                                    "malformed Seat game command payload");
                    continue;
                }
                if (hello->role == ClientRole::SeatControl &&
                    seatCommand->seatId != hello->seatId) {
                    (void)sendError(pipe.value, request->correlationId,
                                    ErrorCode::PermissionDenied,
                                    "Seat control cannot mutate another Seat game lifecycle");
                    continue;
                }
                runtime::SeatGameCommandResult seatResult;
                if (request->type == MessageType::AssignSeatGame) {
                    seatResult = host.assignSeatGame(
                        seatCommand->seatId, *seatCommand->binding,
                        request->correlationId);
                } else if (request->type == MessageType::StartSeatGame) {
                    seatResult = host.startSeatGame(seatCommand->seatId,
                                                    request->correlationId);
                } else {
                    seatResult = host.stopSeatGame(seatCommand->seatId,
                                                   request->correlationId);
                }
                if (!sendFrame(pipe.value,
                               Frame{responseTypeFor(request->type),
                                     request->correlationId,
                                     encodeSeatGameCommandResult(seatResult)},
                               kDefaultHostIpcTimeoutMs, nullptr)) return;
                continue;
            }
            if (request->type == MessageType::ReconcileSeatGames) {
                if (!request->payload.empty()) {
                    (void)sendError(pipe.value, request->correlationId,
                                    ErrorCode::Malformed,
                                    "Seat reconcile payload must be empty");
                    continue;
                }
                const auto seatResult = host.reconcileSeatGames(request->correlationId);
                if (!sendFrame(pipe.value,
                               Frame{MessageType::ReconcileSeatGamesResult,
                                     request->correlationId,
                                     encodeSeatGameCommandResult(seatResult)},
                               kDefaultHostIpcTimeoutMs, nullptr)) return;
                continue;
            }
            if (!isMutatingRequest(request->type) || !request->payload.empty()) {
                (void)sendError(pipe.value, request->correlationId,
                                ErrorCode::Unsupported,
                                "unsupported host request or unexpected payload");
                continue;
            }

            runtime::RuntimeCommandResult commandResult;
            switch (request->type) {
                case MessageType::PlanSession:
                    commandResult = host.plan(request->correlationId);
                    break;
                case MessageType::StartSession: {
                    const auto before = host.snapshot();
                    if (before.sessionPhase == runtime::SeatSessionPhase::Planning) {
                        commandResult = host.prepare(request->correlationId);
                        if (!commandResult.succeeded()) break;
                    }
                    commandResult = host.start(request->correlationId);
                    break;
                }
                case MessageType::StopAndReturnToWindows:
                    commandResult = host.stopAndReturnToWindows(request->correlationId);
                    break;
                case MessageType::BeginReconfigure:
                    commandResult = host.beginReconfigure(request->correlationId);
                    break;
                case MessageType::ExitHostWhenIdle:
                    commandResult = host.exitHostWhenIdle(request->correlationId);
                    break;
                case MessageType::EmergencyReset:
                    commandResult = host.reset(request->correlationId);
                    break;
                default:
                    (void)sendError(pipe.value, request->correlationId,
                                    ErrorCode::Unsupported, "unsupported host mutation request");
                    continue;
            }
            const auto responseType = responseTypeFor(request->type);
            if (!sendFrame(pipe.value,
                           Frame{responseType, request->correlationId,
                                 encodeCommandResult(commandResult)},
                           kDefaultHostIpcTimeoutMs, nullptr)) {
                return;
            }
            if (request->type == MessageType::ExitHostWhenIdle && commandResult.succeeded()) {
                stopRequested.store(true, std::memory_order_release);
                return;
            }
        }
    }
#endif
};

HostControlServer::HostControlServer(runtime::RuntimeHost& host)
    : impl_(std::make_unique<Impl>(host)) {}
HostControlServer::~HostControlServer() {
    requestStop();
    if (impl_) {
        std::lock_guard lock(impl_->threadsMutex);
        for (auto& thread : impl_->threads) {
            if (thread.worker.joinable()) thread.worker.join();
        }
    }
}

void HostControlServer::requestStop() noexcept {
    if (impl_) impl_->stopRequested.store(true, std::memory_order_release);
}

bool HostControlServer::serve(std::string* error) {
#ifdef _WIN32
    while (!impl_->stopRequested.load(std::memory_order_acquire)) {
        impl_->reapFinishedThreads();
        const auto snapshot = impl_->host.snapshot();
        if (snapshot.hostPhase == runtime::HostLifecyclePhase::ExitRequested) break;
        auto pipe = createServerPipe(error);
        if (!pipe.valid()) return false;
        std::string acceptError;
        if (!connectServerPipe(pipe.value, impl_->stopRequested, &acceptError)) {
            if (impl_->stopRequested.load(std::memory_order_acquire)) break;
            if (error) *error = std::move(acceptError);
            return false;
        }
        if (impl_->activeClients.load(std::memory_order_acquire) >= kMaxConnectedClients) {
            (void)impl_->sendError(pipe.value, 1, ErrorCode::Busy,
                                   "host IPC client limit reached");
            continue;
        }
        const HANDLE rawPipe = pipe.release();
        impl_->activeClients.fetch_add(1, std::memory_order_acq_rel);
        auto finished = std::make_shared<std::atomic<bool>>(false);
        std::lock_guard lock(impl_->threadsMutex);
        Impl::ClientThread clientThread;
        clientThread.finished = finished;
        clientThread.worker = std::thread([impl = impl_.get(), rawPipe, finished] {
            impl->clientLoop(rawPipe);
            finished->store(true, std::memory_order_release);
        });
        impl_->threads.push_back(std::move(clientThread));
    }

    requestStop();
    std::lock_guard lock(impl_->threadsMutex);
    for (auto& thread : impl_->threads) {
        if (thread.worker.joinable()) thread.worker.join();
    }
    impl_->threads.clear();
    return true;
#else
    if (error) *error = "host IPC server is Windows-only";
    return false;
#endif
}

} // namespace hydra::hostipc
