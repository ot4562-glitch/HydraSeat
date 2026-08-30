#pragma once

#include "hydra/production_launch_runtime.hpp"
#include "hydra/runtime_state.hpp"
#include "hydra/seat_game_lifecycle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::hostipc {

constexpr std::uint32_t kHostProtocolMagic = 0x31505348u; // "HSP1" little-endian
constexpr std::uint16_t kHostProtocolVersion = 4u;
constexpr std::size_t kHostProtocolHeaderBytes = 24u;
constexpr std::size_t kHostProtocolMaxPayloadBytes = 64u * 1024u;
constexpr std::size_t kHostProtocolMaxStringBytes = 2048u;
constexpr std::size_t kHostProtocolMaxSeats = 64u;
constexpr std::size_t kHostProtocolMaxPlanSeats = 2u;
constexpr std::size_t kHostProtocolMaxPlanArguments = 128u;
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
    AssignSeatGame = 25,
    AssignSeatGameResult = 26,
    StartSeatGame = 27,
    StartSeatGameResult = 28,
    StopSeatGame = 29,
    StopSeatGameResult = 30,
    ReconcileSeatGames = 31,
    ReconcileSeatGamesResult = 32,
    GetProviderPlanRegistry = 33,
    ProviderPlanRegistry = 34,
    InstallProviderPlan = 35,
    InstallProviderPlanResult = 36,
    RemoveProviderPlan = 37,
    RemoveProviderPlanResult = 38,
};

enum class ClientRole : std::uint8_t {
    ReadOnly = 0,
    Control = 1,
    // May mutate only the temporary game lifecycle of the authenticated Seat.
    // It has no profile, whole-machine, reconcile, or other Seat authority.
    SeatControl = 2,
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

struct SeatGameCommandPayload {
    SeatId seatId{0};
    std::optional<runtime::SeatGameBinding> binding;
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

// P4-SEAT-01 reconnect/state boundary. The payload is fixed-order, bounded to
// the v1 two-Seat limit, pointer-free, and rejects future enum values/reserved
// bytes. It transports temporary runtime bindings, never persisted credentials.
std::vector<std::byte> encodeSeatGameStates(
    std::span<const runtime::SeatGameState> states);
std::optional<std::vector<runtime::SeatGameState>> decodeSeatGameStates(
    std::span<const std::byte> payload);
std::vector<std::byte> encodeSeatGameCommandResult(
    const runtime::SeatGameCommandResult& result);
std::optional<runtime::SeatGameCommandResult> decodeSeatGameCommandResult(
    std::span<const std::byte> payload);
std::vector<std::byte> encodeSeatGameCommandPayload(
    const SeatGameCommandPayload& payload);
std::optional<SeatGameCommandPayload> decodeSeatGameCommandPayload(
    std::span<const std::byte> payload);

// Protocol-v4 host-owned immutable provider-plan installation. The nested plan
// is pointer-free, typed, and bounded independently from the frame header.
std::vector<std::byte> encodeProviderPlanRegistrySnapshot(
    const production::ProviderPlanRegistrySnapshot& snapshot);
std::optional<production::ProviderPlanRegistrySnapshot>
decodeProviderPlanRegistrySnapshot(std::span<const std::byte> payload);
std::vector<std::byte> encodeProviderPlanInstallRequest(
    const production::ProviderPlanInstallRequest& request);
std::optional<production::ProviderPlanInstallRequest>
decodeProviderPlanInstallRequest(std::span<const std::byte> payload);
std::vector<std::byte> encodeProviderPlanRemoveRequest(
    const production::ProviderPlanRemoveRequest& request);
std::optional<production::ProviderPlanRemoveRequest>
decodeProviderPlanRemoveRequest(std::span<const std::byte> payload);
std::vector<std::byte> encodeProviderPlanInstallResult(
    const production::ProviderPlanInstallResult& result);
std::optional<production::ProviderPlanInstallResult>
decodeProviderPlanInstallResult(std::span<const std::byte> payload);

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
