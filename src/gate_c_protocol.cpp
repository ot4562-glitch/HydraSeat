#include "hydra/gate_c_protocol.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace hydra::gatec {
namespace {

class PayloadReader {
public:
    explicit PayloadReader(std::span<const std::byte> bytes) : m_bytes(bytes) {}

    bool readU8(std::uint8_t& value) {
        if (!require(1)) return false;
        value = std::to_integer<std::uint8_t>(m_bytes[m_offset++]);
        return true;
    }

    bool readBool(bool& value) {
        std::uint8_t raw = 0;
        if (!readU8(raw)) return false;
        if (raw > 1) {
            m_error = "boolean field is not 0 or 1";
            return false;
        }
        value = raw != 0;
        return true;
    }

    bool readU16(std::uint16_t& value) {
        if (!require(2)) return false;
        value = static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>(m_bytes[m_offset])) |
                static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(m_bytes[m_offset + 1])) << 8;
        m_offset += 2;
        return true;
    }

    bool readI16(std::int16_t& value) {
        std::uint16_t raw = 0;
        if (!readU16(raw)) return false;
        value = std::bit_cast<std::int16_t>(raw);
        return true;
    }

    bool readU32(std::uint32_t& value) {
        if (!require(4)) return false;
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(
                         std::to_integer<std::uint8_t>(
                             m_bytes[m_offset + shift / 8]))
                     << shift;
        }
        m_offset += 4;
        return true;
    }

    bool readI32(std::int32_t& value) {
        std::uint32_t raw = 0;
        if (!readU32(raw)) return false;
        value = std::bit_cast<std::int32_t>(raw);
        return true;
    }

    bool readU64(std::uint64_t& value) {
        if (!require(8)) return false;
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(
                         std::to_integer<std::uint8_t>(
                             m_bytes[m_offset + shift / 8]))
                     << shift;
        }
        m_offset += 8;
        return true;
    }

    bool readI64(std::int64_t& value) {
        std::uint64_t raw = 0;
        if (!readU64(raw)) return false;
        value = std::bit_cast<std::int64_t>(raw);
        return true;
    }

    template <std::size_t Size>
    bool readArray(std::array<std::uint8_t, Size>& value) {
        if (!require(Size)) return false;
        for (std::size_t index = 0; index < Size; ++index) {
            value[index] =
                std::to_integer<std::uint8_t>(m_bytes[m_offset + index]);
        }
        m_offset += Size;
        return true;
    }

    bool readPadding(std::size_t bytes) {
        if (!require(bytes)) return false;
        for (std::size_t index = 0; index < bytes; ++index) {
            if (m_bytes[m_offset + index] != std::byte{0}) {
                m_error = "reserved field is not zero";
                return false;
            }
        }
        m_offset += bytes;
        return true;
    }

    bool finished() const noexcept { return m_offset == m_bytes.size(); }
    const std::string& error() const noexcept { return m_error; }

private:
    bool require(std::size_t bytes) {
        if (bytes > m_bytes.size() - m_offset) {
            m_error = "payload is truncated";
            return false;
        }
        return true;
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_offset{0};
    std::string m_error;
};

void appendU8(std::vector<std::byte>& output, std::uint8_t value) {
    output.push_back(static_cast<std::byte>(value));
}

void appendBool(std::vector<std::byte>& output, bool value) {
    appendU8(output, value ? 1u : 0u);
}

void appendU16(std::vector<std::byte>& output, std::uint16_t value) {
    for (unsigned shift = 0; shift < 16; shift += 8) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void appendI16(std::vector<std::byte>& output, std::int16_t value) {
    appendU16(output, static_cast<std::uint16_t>(value));
}

void appendU32(std::vector<std::byte>& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void appendI32(std::vector<std::byte>& output, std::int32_t value) {
    appendU32(output, static_cast<std::uint32_t>(value));
}

void appendU64(std::vector<std::byte>& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void appendI64(std::vector<std::byte>& output, std::int64_t value) {
    appendU64(output, static_cast<std::uint64_t>(value));
}

template <std::size_t Size>
void appendArray(std::vector<std::byte>& output,
                 const std::array<std::uint8_t, Size>& value) {
    for (const auto byte : value) {
        appendU8(output, byte);
    }
}

void appendPadding(std::vector<std::byte>& output, std::size_t bytes) {
    output.insert(output.end(), bytes, std::byte{0});
}

bool knownMessageType(std::uint16_t raw) noexcept {
    return raw >= static_cast<std::uint16_t>(MessageType::Hello) &&
           raw <= static_cast<std::uint16_t>(MessageType::ProbeSnapshot);
}

bool finishDecode(const DecodedFrame& frame, MessageType expected,
                  const PayloadReader& reader, std::string* error) {
    if (frame.type != expected) {
        if (error != nullptr) {
            *error = "unexpected message type";
        }
        return false;
    }
    if (!reader.error().empty()) {
        if (error != nullptr) {
            *error = reader.error();
        }
        return false;
    }
    if (!reader.finished()) {
        if (error != nullptr) {
            *error = "payload contains trailing bytes";
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::vector<std::byte> wrap(MessageType type, std::uint64_t sequence,
                            std::vector<std::byte> payload) {
    return encodeFrame(type, sequence, payload);
}

} // namespace

std::vector<std::byte> encodeFrame(MessageType type,
                                   std::uint64_t sequence,
                                   std::span<const std::byte> payload) {
    if (payload.size() > kMaximumPayloadBytes ||
        payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }

    std::vector<std::byte> output;
    output.reserve(kFrameHeaderBytes + payload.size());
    appendU32(output, kProtocolMagic);
    appendU16(output, kProtocolVersion);
    appendU16(output, static_cast<std::uint16_t>(type));
    appendU32(output, static_cast<std::uint32_t>(payload.size()));
    appendU64(output, sequence);
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

FrameDecodeResult decodeFrame(std::span<const std::byte> bytes) {
    FrameDecodeResult result;
    if (bytes.size() < kFrameHeaderBytes) {
        result.error = "frame header is truncated";
        return result;
    }
    if (bytes.size() > kMaximumFrameBytes) {
        result.error = "frame exceeds maximum size";
        return result;
    }

    PayloadReader reader(bytes.first(kFrameHeaderBytes));
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t rawType = 0;
    std::uint32_t payloadSize = 0;
    std::uint64_t sequence = 0;
    if (!reader.readU32(magic) || !reader.readU16(version) ||
        !reader.readU16(rawType) || !reader.readU32(payloadSize) ||
        !reader.readU64(sequence)) {
        result.error = reader.error();
        return result;
    }
    if (magic != kProtocolMagic) {
        result.error = "protocol magic mismatch";
        return result;
    }
    if (version != kProtocolVersion) {
        result.error = "unsupported protocol version";
        return result;
    }
    if (!knownMessageType(rawType)) {
        result.error = "unknown message type";
        return result;
    }
    if (payloadSize > kMaximumPayloadBytes) {
        result.error = "payload exceeds maximum size";
        return result;
    }
    if (bytes.size() != kFrameHeaderBytes + payloadSize) {
        result.error = "frame size does not match payload size";
        return result;
    }

    DecodedFrame frame;
    frame.type = static_cast<MessageType>(rawType);
    frame.sequence = sequence;
    frame.payload.assign(bytes.begin() +
                             static_cast<std::ptrdiff_t>(kFrameHeaderBytes),
                         bytes.end());
    result.frame = std::move(frame);
    return result;
}

std::vector<std::byte> encodeHello(std::uint64_t sequence,
                                   const HelloMessage& message) {
    std::vector<std::byte> payload;
    payload.reserve(36);
    appendArray(payload, message.token);
    appendU32(payload, message.seatId);
    appendU32(payload, message.processId);
    appendU16(payload, message.architectureBits);
    appendPadding(payload, 2);
    appendU64(payload, message.targetWindow);
    return wrap(MessageType::Hello, sequence, std::move(payload));
}

std::vector<std::byte> encodeHelloAck(std::uint64_t sequence,
                                      const HelloAckMessage& message) {
    std::vector<std::byte> payload;
    payload.reserve(20);
    appendBool(payload, message.accepted);
    appendPadding(payload, 3);
    appendU32(payload, message.serverProcessId);
    appendU64(payload, message.grantedCapabilities);
    appendU32(payload, message.errorCode);
    return wrap(MessageType::HelloAck, sequence, std::move(payload));
}

std::vector<std::byte> encodeInputEvent(std::uint64_t sequence,
                                        const InputEventMessage& message) {
    std::vector<std::byte> payload;
    payload.reserve(32);
    appendU8(payload, static_cast<std::uint8_t>(message.kind));
    appendU8(payload, static_cast<std::uint8_t>(message.keyTransition));
    appendBool(payload, message.isTouchpad);
    appendPadding(payload, 1);
    appendU64(payload, message.timestampMicros);
    appendU32(payload, message.vkey);
    appendU16(payload, message.scanCode);
    appendU16(payload, message.keyboardFlags);
    appendI32(payload, message.deltaX);
    appendI32(payload, message.deltaY);
    appendU16(payload, message.mouseButtonFlags);
    appendI16(payload, message.wheelDelta);
    return wrap(MessageType::InputEvent, sequence, std::move(payload));
}

std::vector<std::byte> encodeControlState(
    std::uint64_t sequence, const ControlStateMessage& message) {
    std::vector<std::byte> payload;
    payload.reserve(28);
    appendI32(payload, message.cursorX);
    appendI32(payload, message.cursorY);
    appendBool(payload, message.clipEnabled);
    appendBool(payload, message.virtualForeground);
    appendBool(payload, message.virtualCapture);
    appendPadding(payload, 1);
    appendI32(payload, message.clipLeft);
    appendI32(payload, message.clipTop);
    appendI32(payload, message.clipRight);
    appendI32(payload, message.clipBottom);
    return wrap(MessageType::ControlState, sequence, std::move(payload));
}

std::vector<std::byte> encodeQuerySnapshot(
    std::uint64_t sequence, const QuerySnapshotMessage& message) {
    std::vector<std::byte> payload;
    payload.reserve(4);
    appendU16(payload, message.probeVkey);
    appendPadding(payload, 2);
    return wrap(MessageType::QuerySnapshot, sequence, std::move(payload));
}

std::vector<std::byte> encodeStateSnapshot(
    std::uint64_t sequence, const StateSnapshotMessage& message) {
    std::vector<std::byte> payload;
    payload.reserve(120);
    appendU64(payload, message.lastAppliedSequence);
    appendArray(payload, message.keyDownBits);
    appendArray(payload, message.keyPressedEdgeBits);
    appendU32(payload, message.mouseButtonsDown);
    appendI64(payload, message.wheelAccumulator);
    appendU16(payload, message.probeVkey);
    appendU16(payload, message.asyncKeyStateValue);
    appendU8(payload, message.keyboardStateByte);
    appendPadding(payload, 3);
    appendI32(payload, message.cursorX);
    appendI32(payload, message.cursorY);
    appendBool(payload, message.clipEnabled);
    appendBool(payload, message.virtualForeground);
    appendBool(payload, message.virtualCapture);
    appendPadding(payload, 1);
    appendI32(payload, message.clipLeft);
    appendI32(payload, message.clipTop);
    appendI32(payload, message.clipRight);
    appendI32(payload, message.clipBottom);
    return wrap(MessageType::StateSnapshot, sequence, std::move(payload));
}

std::vector<std::byte> encodeShutdown(std::uint64_t sequence) {
    return encodeFrame(MessageType::Shutdown, sequence, {});
}

std::vector<std::byte> encodeError(std::uint64_t sequence,
                                   const ErrorMessage& message) {
    std::vector<std::byte> payload;
    payload.reserve(4);
    appendU32(payload, message.errorCode);
    return wrap(MessageType::Error, sequence, std::move(payload));
}

bool decodeHello(const DecodedFrame& frame, HelloMessage& message,
                 std::string* error) {
    PayloadReader reader(frame.payload);
    std::uint16_t architecture = 0;
    if (!reader.readArray(message.token) || !reader.readU32(message.seatId) ||
        !reader.readU32(message.processId) ||
        !reader.readU16(architecture) || !reader.readPadding(2) ||
        !reader.readU64(message.targetWindow)) {
        if (error != nullptr) *error = reader.error();
        return false;
    }
    if (architecture != 32 && architecture != 64) {
        if (error != nullptr) *error = "architecture must be 32 or 64 bits";
        return false;
    }
    message.architectureBits = architecture;
    return finishDecode(frame, MessageType::Hello, reader, error);
}

bool decodeHelloAck(const DecodedFrame& frame, HelloAckMessage& message,
                    std::string* error) {
    PayloadReader reader(frame.payload);
    if (!reader.readBool(message.accepted) || !reader.readPadding(3) ||
        !reader.readU32(message.serverProcessId) ||
        !reader.readU64(message.grantedCapabilities) ||
        !reader.readU32(message.errorCode)) {
        if (error != nullptr) *error = reader.error();
        return false;
    }
    return finishDecode(frame, MessageType::HelloAck, reader, error);
}

bool decodeInputEvent(const DecodedFrame& frame, InputEventMessage& message,
                      std::string* error) {
    PayloadReader reader(frame.payload);
    std::uint8_t rawKind = 0;
    std::uint8_t rawTransition = 0;
    if (!reader.readU8(rawKind) || !reader.readU8(rawTransition) ||
        !reader.readBool(message.isTouchpad) || !reader.readPadding(1) ||
        !reader.readU64(message.timestampMicros) ||
        !reader.readU32(message.vkey) || !reader.readU16(message.scanCode) ||
        !reader.readU16(message.keyboardFlags) ||
        !reader.readI32(message.deltaX) || !reader.readI32(message.deltaY) ||
        !reader.readU16(message.mouseButtonFlags) ||
        !reader.readI16(message.wheelDelta)) {
        if (error != nullptr) *error = reader.error();
        return false;
    }
    if (rawKind < static_cast<std::uint8_t>(InputKind::Keyboard) ||
        rawKind > static_cast<std::uint8_t>(InputKind::Mouse)) {
        if (error != nullptr) *error = "unknown input kind";
        return false;
    }
    if (rawTransition > static_cast<std::uint8_t>(KeyTransition::Up)) {
        if (error != nullptr) *error = "unknown key transition";
        return false;
    }
    message.kind = static_cast<InputKind>(rawKind);
    message.keyTransition = static_cast<KeyTransition>(rawTransition);
    if (message.kind == InputKind::Keyboard) {
        if (message.vkey >= 256) {
            if (error != nullptr) *error = "keyboard virtual key is out of range";
            return false;
        }
        if (message.keyTransition != KeyTransition::Down &&
            message.keyTransition != KeyTransition::Up) {
            if (error != nullptr) *error = "keyboard event requires a down/up transition";
            return false;
        }
    } else if (message.keyTransition != KeyTransition::None) {
        if (error != nullptr) *error = "mouse event must not contain a key transition";
        return false;
    }
    return finishDecode(frame, MessageType::InputEvent, reader, error);
}

bool decodeControlState(const DecodedFrame& frame,
                        ControlStateMessage& message,
                        std::string* error) {
    PayloadReader reader(frame.payload);
    if (!reader.readI32(message.cursorX) || !reader.readI32(message.cursorY) ||
        !reader.readBool(message.clipEnabled) ||
        !reader.readBool(message.virtualForeground) ||
        !reader.readBool(message.virtualCapture) || !reader.readPadding(1) ||
        !reader.readI32(message.clipLeft) ||
        !reader.readI32(message.clipTop) ||
        !reader.readI32(message.clipRight) ||
        !reader.readI32(message.clipBottom)) {
        if (error != nullptr) *error = reader.error();
        return false;
    }
    if (message.clipEnabled &&
        (message.clipRight <= message.clipLeft ||
         message.clipBottom <= message.clipTop)) {
        if (error != nullptr) *error = "clip rectangle is invalid";
        return false;
    }
    return finishDecode(frame, MessageType::ControlState, reader, error);
}

bool decodeQuerySnapshot(const DecodedFrame& frame,
                         QuerySnapshotMessage& message,
                         std::string* error) {
    PayloadReader reader(frame.payload);
    if (!reader.readU16(message.probeVkey) || !reader.readPadding(2)) {
        if (error != nullptr) *error = reader.error();
        return false;
    }
    if (message.probeVkey != 0xffffu && message.probeVkey >= 256u) {
        if (error != nullptr) *error = "snapshot probe virtual key is out of range";
        return false;
    }
    return finishDecode(frame, MessageType::QuerySnapshot, reader, error);
}

bool decodeStateSnapshot(const DecodedFrame& frame,
                         StateSnapshotMessage& message,
                         std::string* error) {
    PayloadReader reader(frame.payload);
    if (!reader.readU64(message.lastAppliedSequence) ||
        !reader.readArray(message.keyDownBits) ||
        !reader.readArray(message.keyPressedEdgeBits) ||
        !reader.readU32(message.mouseButtonsDown) ||
        !reader.readI64(message.wheelAccumulator) ||
        !reader.readU16(message.probeVkey) ||
        !reader.readU16(message.asyncKeyStateValue) ||
        !reader.readU8(message.keyboardStateByte) ||
        !reader.readPadding(3) ||
        !reader.readI32(message.cursorX) || !reader.readI32(message.cursorY) ||
        !reader.readBool(message.clipEnabled) ||
        !reader.readBool(message.virtualForeground) ||
        !reader.readBool(message.virtualCapture) || !reader.readPadding(1) ||
        !reader.readI32(message.clipLeft) ||
        !reader.readI32(message.clipTop) ||
        !reader.readI32(message.clipRight) ||
        !reader.readI32(message.clipBottom)) {
        if (error != nullptr) *error = reader.error();
        return false;
    }
    if (message.probeVkey != 0xffffu && message.probeVkey >= 256u) {
        if (error != nullptr) *error = "snapshot probe virtual key is out of range";
        return false;
    }
    if (message.clipEnabled &&
        (message.clipRight <= message.clipLeft ||
         message.clipBottom <= message.clipTop)) {
        if (error != nullptr) *error = "snapshot clip rectangle is invalid";
        return false;
    }
    return finishDecode(frame, MessageType::StateSnapshot, reader, error);
}

bool decodeShutdown(const DecodedFrame& frame, std::string* error) {
    PayloadReader reader(frame.payload);
    return finishDecode(frame, MessageType::Shutdown, reader, error);
}

bool decodeError(const DecodedFrame& frame, ErrorMessage& message,
                 std::string* error) {
    PayloadReader reader(frame.payload);
    if (!reader.readU32(message.errorCode)) {
        if (error != nullptr) *error = reader.error();
        return false;
    }
    return finishDecode(frame, MessageType::Error, reader, error);
}

std::string tokenToHex(const SessionToken& token) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(token.size() * 2);
    for (const auto byte : token) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

std::optional<SessionToken> tokenFromHex(std::string_view text) {
    if (text.size() != SessionToken{}.size() * 2) {
        return std::nullopt;
    }
    const auto nibble = [](char value) -> std::optional<std::uint8_t> {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint8_t>(value - 'a' + 10);
        }
        if (value >= 'A' && value <= 'F') {
            return static_cast<std::uint8_t>(value - 'A' + 10);
        }
        return std::nullopt;
    };

    SessionToken token{};
    for (std::size_t index = 0; index < token.size(); ++index) {
        const auto high = nibble(text[index * 2]);
        const auto low = nibble(text[index * 2 + 1]);
        if (!high || !low) {
            return std::nullopt;
        }
        token[index] = static_cast<std::uint8_t>((*high << 4) | *low);
    }
    return token;
}

std::string_view messageTypeName(MessageType type) noexcept {
    switch (type) {
    case MessageType::Hello: return "Hello";
    case MessageType::HelloAck: return "HelloAck";
    case MessageType::InputEvent: return "InputEvent";
    case MessageType::ControlState: return "ControlState";
    case MessageType::QuerySnapshot: return "QuerySnapshot";
    case MessageType::StateSnapshot: return "StateSnapshot";
    case MessageType::Shutdown: return "Shutdown";
    case MessageType::Error: return "Error";
    case MessageType::ProbeSnapshot: return "ProbeSnapshot";
    }
    return "Unknown";
}

} // namespace hydra::gatec
