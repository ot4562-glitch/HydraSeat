#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::gatec {

inline constexpr std::uint32_t kProtocolMagic = 0x31434748u; // "HGC1" on the wire.
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t kFrameHeaderBytes = 20;
inline constexpr std::size_t kMaximumPayloadBytes = 4096;
inline constexpr std::size_t kMaximumFrameBytes =
    kFrameHeaderBytes + kMaximumPayloadBytes;

using SessionToken = std::array<std::uint8_t, 16>;

enum class MessageType : std::uint16_t {
    Hello = 1,
    HelloAck = 2,
    InputEvent = 3,
    ControlState = 4,
    QuerySnapshot = 5,
    StateSnapshot = 6,
    Shutdown = 7,
    Error = 8,
    ProbeSnapshot = 9
};

enum class InputKind : std::uint8_t {
    Keyboard = 1,
    Mouse = 2
};

enum class KeyTransition : std::uint8_t {
    None = 0,
    Down = 1,
    Up = 2
};

enum class TestCapability : std::uint64_t {
    None = 0,
    VirtualKeyboardState = 1ull << 0,
    VirtualMouseState = 1ull << 1,
    VirtualCursorState = 1ull << 2,
    VirtualFocusState = 1ull << 3,
    SnapshotQuery = 1ull << 4,
    ApiProbeBaselineSnapshot = 1ull << 5,
    PollingApiShim = 1ull << 6
};

constexpr TestCapability operator|(TestCapability left,
                                   TestCapability right) noexcept {
    return static_cast<TestCapability>(
        static_cast<std::uint64_t>(left) |
        static_cast<std::uint64_t>(right));
}

constexpr std::uint64_t testCapabilityBits(TestCapability value) noexcept {
    return static_cast<std::uint64_t>(value);
}

inline constexpr TestCapability kControlledTargetCapabilities =
    TestCapability::VirtualKeyboardState |
    TestCapability::VirtualMouseState |
    TestCapability::VirtualCursorState |
    TestCapability::VirtualFocusState |
    TestCapability::SnapshotQuery;

inline constexpr TestCapability kControlledApiProbeCapabilities =
    kControlledTargetCapabilities |
    TestCapability::ApiProbeBaselineSnapshot;

inline constexpr TestCapability kControlledPollingProbeCapabilities =
    kControlledApiProbeCapabilities | TestCapability::PollingApiShim;

struct DecodedFrame {
    MessageType type{MessageType::Error};
    std::uint64_t sequence{0};
    std::vector<std::byte> payload;
};

struct FrameDecodeResult {
    std::optional<DecodedFrame> frame;
    std::string error;

    explicit operator bool() const noexcept { return frame.has_value(); }
};

struct HelloMessage {
    SessionToken token{};
    std::uint32_t seatId{0};
    std::uint32_t processId{0};
    std::uint16_t architectureBits{0};
    std::uint64_t targetWindow{0};

    bool operator==(const HelloMessage&) const = default;
};

struct HelloAckMessage {
    bool accepted{false};
    std::uint32_t serverProcessId{0};
    std::uint64_t grantedCapabilities{0};
    std::uint32_t errorCode{0};

    bool operator==(const HelloAckMessage&) const = default;
};

struct InputEventMessage {
    InputKind kind{InputKind::Keyboard};
    KeyTransition keyTransition{KeyTransition::None};
    bool isTouchpad{false};
    std::uint64_t timestampMicros{0};
    std::uint32_t vkey{0};
    std::uint16_t scanCode{0};
    std::uint16_t keyboardFlags{0};
    std::int32_t deltaX{0};
    std::int32_t deltaY{0};
    std::uint16_t mouseButtonFlags{0};
    std::int16_t wheelDelta{0};

    bool operator==(const InputEventMessage&) const = default;
};

struct ControlStateMessage {
    std::int32_t cursorX{0};
    std::int32_t cursorY{0};
    bool clipEnabled{false};
    bool virtualForeground{false};
    bool virtualCapture{false};
    std::int32_t clipLeft{0};
    std::int32_t clipTop{0};
    std::int32_t clipRight{0};
    std::int32_t clipBottom{0};

    bool operator==(const ControlStateMessage&) const = default;
};

struct QuerySnapshotMessage {
    std::uint16_t probeVkey{0xffffu};

    bool operator==(const QuerySnapshotMessage&) const = default;
};

struct StateSnapshotMessage {
    std::uint64_t lastAppliedSequence{0};
    std::array<std::uint8_t, 32> keyDownBits{};
    std::array<std::uint8_t, 32> keyPressedEdgeBits{};
    std::uint32_t mouseButtonsDown{0};
    std::int64_t wheelAccumulator{0};
    std::uint16_t probeVkey{0xffffu};
    std::uint16_t asyncKeyStateValue{0};
    std::uint8_t keyboardStateByte{0};
    std::int32_t cursorX{0};
    std::int32_t cursorY{0};
    bool clipEnabled{false};
    bool virtualForeground{false};
    bool virtualCapture{false};
    std::int32_t clipLeft{0};
    std::int32_t clipTop{0};
    std::int32_t clipRight{0};
    std::int32_t clipBottom{0};

    bool operator==(const StateSnapshotMessage&) const = default;
};

struct ErrorMessage {
    std::uint32_t errorCode{0};

    bool operator==(const ErrorMessage&) const = default;
};

std::vector<std::byte> encodeFrame(MessageType type,
                                   std::uint64_t sequence,
                                   std::span<const std::byte> payload);
FrameDecodeResult decodeFrame(std::span<const std::byte> bytes);

std::vector<std::byte> encodeHello(std::uint64_t sequence,
                                   const HelloMessage& message);
std::vector<std::byte> encodeHelloAck(std::uint64_t sequence,
                                      const HelloAckMessage& message);
std::vector<std::byte> encodeInputEvent(std::uint64_t sequence,
                                        const InputEventMessage& message);
std::vector<std::byte> encodeControlState(std::uint64_t sequence,
                                          const ControlStateMessage& message);
std::vector<std::byte> encodeQuerySnapshot(
    std::uint64_t sequence, const QuerySnapshotMessage& message = {});
std::vector<std::byte> encodeStateSnapshot(
    std::uint64_t sequence, const StateSnapshotMessage& message);
std::vector<std::byte> encodeShutdown(std::uint64_t sequence);
std::vector<std::byte> encodeError(std::uint64_t sequence,
                                   const ErrorMessage& message);

bool decodeHello(const DecodedFrame& frame, HelloMessage& message,
                 std::string* error = nullptr);
bool decodeHelloAck(const DecodedFrame& frame, HelloAckMessage& message,
                    std::string* error = nullptr);
bool decodeInputEvent(const DecodedFrame& frame, InputEventMessage& message,
                      std::string* error = nullptr);
bool decodeControlState(const DecodedFrame& frame,
                        ControlStateMessage& message,
                        std::string* error = nullptr);
bool decodeQuerySnapshot(const DecodedFrame& frame,
                         QuerySnapshotMessage& message,
                         std::string* error = nullptr);
bool decodeStateSnapshot(const DecodedFrame& frame,
                         StateSnapshotMessage& message,
                         std::string* error = nullptr);
bool decodeShutdown(const DecodedFrame& frame,
                    std::string* error = nullptr);
bool decodeError(const DecodedFrame& frame, ErrorMessage& message,
                 std::string* error = nullptr);

std::string tokenToHex(const SessionToken& token);
std::optional<SessionToken> tokenFromHex(std::string_view text);
std::string_view messageTypeName(MessageType type) noexcept;

} // namespace hydra::gatec
