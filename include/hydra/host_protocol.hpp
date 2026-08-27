#pragma once

#include "hydra/runtime_state.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::hostipc {

constexpr std::uint32_t kHostProtocolMagic = 0x31505348u; // "HSP1" little-endian
constexpr std::uint16_t kHostProtocolVersion = 2u;
constexpr std::size_t kHostProtocolHeaderBytes = 24u;
constexpr std::size_t kHostProtocolMaxPayloadBytes = 64u * 1024u;
constexpr std::size_t kHostProtocolMaxStringBytes = 2048u;
constexpr std::size_t kHostProtocolMaxSeats = 64u;
constexpr std::size_t kHostProtocolMaxEvents = 128u;

// Every request has exactly one response with the same nonzero correlation ID.
// RuntimeEvent frames are the only server-originated frames that may use a
// subscription correlation ID more than once.
enum class MessageType : std::uint16_t {
    Hello = 1,
    HelloAck = 2,
    GetSnapshot = 3,
    Snapshot = 4,
    PlanSession = 5,
    PlanResult = 6,
    StartSession = 7,
    StartResult = 8,
    StopAndReturnToWindows = 9,
    StopResult = 10,
    BeginReconfigure = 11,
    ReconfigureResult = 12,
    ExitHostWhenIdle = 13,
    ExitResult = 14,
    EmergencyReset = 15,
    ResetResult = 16,
    SubscribeEvents = 17,
    SubscribeAck = 18,
    RuntimeEvent = 19,
    Ping = 20,
    Pong = 21,
    Error = 22,
    ApplyProfile = 23,
    ApplyProfileResult = 24,
};

enum class ClientRole : std::uint8_t {
    ReadOnly = 0,
    Control = 1,
};

enum class ErrorCode : std::uint16_t {
    None = 0,
    Malformed = 1,
    VersionMismatch = 2,
    PermissionDenied = 3,
    DuplicateCorrelation = 4,
    InvalidState = 5,
    Busy = 6,
    RecoveryRequired = 7,
    Unsupported = 8,
    ResnapshotRequired = 9,
    InternalError = 10,
};

struct Frame {
    MessageType type{MessageType::Error};
    std::uint64_t correlationId{0};
    std::vector<std::byte> payload;
};

struct Hello {
    ClientRole role{ClientRole::ReadOnly};
    SeatId seatId{0};
};

struct HelloAck {
    ClientRole role{ClientRole::ReadOnly};
    SeatId seatId{0};
    SeatId managementSeatId{1};
    std::uint32_t serverProcessId{0};
    std::uint32_t windowsSessionId{0};
};

struct ProfilePayload {
    SeatId managementSeatId{1};
    std::vector<SeatConfig> seats;
};

struct SubscribeRequest {
    std::uint64_t afterSequence{0};
    std::uint32_t maxEvents{kHostProtocolMaxEvents};
};

struct ErrorPayload {
    ErrorCode code{ErrorCode::InternalError};
    std::string diagnostic;
};

struct DecodeResult {
    bool ok{false};
    ErrorCode error{ErrorCode::Malformed};
    std::string diagnostic;
};

std::string_view messageTypeName(MessageType type) noexcept;
std::string_view errorCodeName(ErrorCode code) noexcept;

std::vector<std::byte> encodeFrame(const Frame& frame);
std::optional<Frame> decodeFrame(std::span<const std::byte> bytes,
                                 DecodeResult* result = nullptr);

std::vector<std::byte> encodeHello(const Hello& value);
std::optional<Hello> decodeHello(std::span<const std::byte> payload);
std::vector<std::byte> encodeHelloAck(const HelloAck& value);
std::optional<HelloAck> decodeHelloAck(std::span<const std::byte> payload);

std::vector<std::byte> encodeProfilePayload(const ProfilePayload& profile);
std::optional<ProfilePayload> decodeProfilePayload(std::span<const std::byte> payload);

std::vector<std::byte> encodeSnapshot(const runtime::HostRuntimeSnapshot& snapshot);
std::optional<runtime::HostRuntimeSnapshot> decodeSnapshot(
    std::span<const std::byte> payload);

std::vector<std::byte> encodeCommandResult(const runtime::RuntimeCommandResult& result);
std::optional<runtime::RuntimeCommandResult> decodeCommandResult(
    std::span<const std::byte> payload);

std::vector<std::byte> encodeRuntimeEvent(const runtime::RuntimeTransition& transition);
std::optional<runtime::RuntimeTransition> decodeRuntimeEvent(
    std::span<const std::byte> payload);

std::vector<std::byte> encodeSubscribeRequest(const SubscribeRequest& request);
std::optional<SubscribeRequest> decodeSubscribeRequest(
    std::span<const std::byte> payload);

std::vector<std::byte> encodePing(std::uint64_t nonce);
std::optional<std::uint64_t> decodePing(std::span<const std::byte> payload);

std::vector<std::byte> encodeError(const ErrorPayload& error);
std::optional<ErrorPayload> decodeError(std::span<const std::byte> payload);

bool isMutatingRequest(MessageType type) noexcept;
MessageType responseTypeFor(MessageType request) noexcept;

} // namespace hydra::hostipc
