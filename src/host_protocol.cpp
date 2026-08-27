#include "hydra/host_protocol.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace hydra::hostipc {
namespace {

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value & 0xffu));
        u8(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    }
    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32u; shift += 8u) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }
    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64u; shift += 8u) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }
    void raw(std::span<const std::byte> value) {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    void string(std::string_view value) {
        const auto bounded = value.substr(0, kHostProtocolMaxStringBytes);
        u32(static_cast<std::uint32_t>(bounded.size()));
        for (const char ch : bounded) u8(static_cast<std::uint8_t>(ch));
    }
    std::vector<std::byte> take() { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    bool u8(std::uint8_t& value) {
        if (remaining() < 1u) return false;
        value = std::to_integer<std::uint8_t>(bytes_[offset_++]);
        return true;
    }
    bool u16(std::uint16_t& value) {
        std::uint8_t a = 0, b = 0;
        if (!u8(a) || !u8(b)) return false;
        value = static_cast<std::uint16_t>(a) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(b) << 8u);
        return true;
    }
    bool u32(std::uint32_t& value) {
        value = 0;
        for (unsigned shift = 0; shift < 32u; shift += 8u) {
            std::uint8_t part = 0;
            if (!u8(part)) return false;
            value |= static_cast<std::uint32_t>(part) << shift;
        }
        return true;
    }
    bool u64(std::uint64_t& value) {
        value = 0;
        for (unsigned shift = 0; shift < 64u; shift += 8u) {
            std::uint8_t part = 0;
            if (!u8(part)) return false;
            value |= static_cast<std::uint64_t>(part) << shift;
        }
        return true;
    }
    bool raw(std::span<std::byte> destination) {
        if (remaining() < destination.size()) return false;
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                    static_cast<std::ptrdiff_t>(destination.size()),
                    destination.begin());
        offset_ += destination.size();
        return true;
    }
    bool rawVector(std::size_t count, std::vector<std::byte>& destination) {
        if (count > remaining()) return false;
        destination.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                           bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + count));
        offset_ += count;
        return true;
    }
    bool string(std::string& value) {
        std::uint32_t length = 0;
        if (!u32(length) || length > kHostProtocolMaxStringBytes || length > remaining()) {
            return false;
        }
        value.clear();
        value.reserve(length);
        for (std::uint32_t index = 0; index < length; ++index) {
            std::uint8_t ch = 0;
            if (!u8(ch)) return false;
            value.push_back(static_cast<char>(ch));
        }
        return true;
    }
    std::size_t remaining() const noexcept { return bytes_.size() - offset_; }
    bool done() const noexcept { return offset_ == bytes_.size(); }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{0};
};

bool wideToUtf8(std::wstring_view text, std::string& out) {
    out.clear();
    auto append = [&](std::uint32_t cp) {
        if (cp <= 0x7fu) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ffu) {
            out.push_back(static_cast<char>(0xc0u | (cp >> 6u)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        } else if (cp <= 0xffffu) {
            out.push_back(static_cast<char>(0xe0u | (cp >> 12u)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        } else {
            out.push_back(static_cast<char>(0xf0u | (cp >> 18u)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        }
    };
    for (std::size_t index = 0; index < text.size(); ++index) {
        std::uint32_t cp = static_cast<std::uint32_t>(text[index]);
        if constexpr (sizeof(wchar_t) == 2u) {
            if (cp >= 0xd800u && cp <= 0xdbffu) {
                if (++index >= text.size()) return false;
                const auto low = static_cast<std::uint32_t>(text[index]);
                if (low < 0xdc00u || low > 0xdfffu) return false;
                cp = 0x10000u + ((cp - 0xd800u) << 10u) + (low - 0xdc00u);
            } else if (cp >= 0xdc00u && cp <= 0xdfffu) {
                return false;
            }
        }
        if (cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu)) return false;
        append(cp);
        if (out.size() > kHostProtocolMaxStringBytes) return false;
    }
    return true;
}

bool utf8ToWide(std::string_view text, std::wstring& out) {
    out.clear();
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index++]);
        std::uint32_t cp = 0;
        unsigned continuation = 0;
        if (first < 0x80u) cp = first;
        else if ((first & 0xe0u) == 0xc0u) { cp = first & 0x1fu; continuation = 1; }
        else if ((first & 0xf0u) == 0xe0u) { cp = first & 0x0fu; continuation = 2; }
        else if ((first & 0xf8u) == 0xf0u) { cp = first & 0x07u; continuation = 3; }
        else return false;
        if (index + continuation > text.size()) return false;
        for (unsigned count = 0; count < continuation; ++count) {
            const auto value = static_cast<unsigned char>(text[index++]);
            if ((value & 0xc0u) != 0x80u) return false;
            cp = (cp << 6u) | (value & 0x3fu);
        }
        if ((continuation == 1u && cp < 0x80u) ||
            (continuation == 2u && cp < 0x800u) ||
            (continuation == 3u && cp < 0x10000u) ||
            cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu)) {
            return false;
        }
        if constexpr (sizeof(wchar_t) == 2u) {
            if (cp <= 0xffffu) out.push_back(static_cast<wchar_t>(cp));
            else {
                cp -= 0x10000u;
                out.push_back(static_cast<wchar_t>(0xd800u + (cp >> 10u)));
                out.push_back(static_cast<wchar_t>(0xdc00u + (cp & 0x3ffu)));
            }
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return true;
}

bool writeWide(Writer& writer, std::wstring_view text) {
    std::string utf8;
    if (!wideToUtf8(text, utf8) || utf8.size() > kHostProtocolMaxStringBytes) return false;
    writer.string(utf8);
    return true;
}

bool readWide(Reader& reader, std::wstring& text) {
    std::string utf8;
    return reader.string(utf8) && utf8ToWide(utf8, text);
}

bool writeWideVector(Writer& writer, const std::vector<std::wstring>& values) {
    if (values.size() > 64u) return false;
    writer.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        if (!writeWide(writer, value)) return false;
    }
    return true;
}

bool readWideVector(Reader& reader, std::vector<std::wstring>& values) {
    std::uint32_t count = 0;
    if (!reader.u32(count) || count > 64u) return false;
    values.clear();
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::wstring value;
        if (!readWide(reader, value)) return false;
        values.push_back(std::move(value));
    }
    return true;
}

bool writeOptionalWide(Writer& writer, const std::optional<std::wstring>& value) {
    writer.u8(value ? 1u : 0u);
    return !value || writeWide(writer, *value);
}

bool readOptionalWide(Reader& reader, std::optional<std::wstring>& value) {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1u) return false;
    if (present == 0u) {
        value.reset();
        return true;
    }
    std::wstring decoded;
    if (!readWide(reader, decoded)) return false;
    value = std::move(decoded);
    return true;
}

bool validMessageType(std::uint16_t raw) {
    return raw >= static_cast<std::uint16_t>(MessageType::Hello) &&
           raw <= static_cast<std::uint16_t>(MessageType::ApplyProfileResult);
}

bool validHostPhase(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(runtime::HostLifecyclePhase::Stopped);
}

bool validSessionPhase(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(runtime::SeatSessionPhase::RecoveryRequired);
}

bool validRuntimeCommand(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(runtime::RuntimeCommand::BeginReconfigure);
}

bool validRuntimeResult(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(runtime::RuntimeResultCode::RecoveryRequired);
}

bool validRole(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(ClientRole::Control);
}

bool validError(std::uint16_t raw) {
    return raw <= static_cast<std::uint16_t>(ErrorCode::InternalError);
}

std::vector<std::byte> encodeTransitionBody(const runtime::RuntimeTransition& transition) {
    Writer writer;
    writer.u64(transition.sequence);
    writer.u64(transition.correlationId);
    writer.u8(static_cast<std::uint8_t>(transition.command));
    writer.u8(static_cast<std::uint8_t>(transition.from));
    writer.u8(static_cast<std::uint8_t>(transition.to));
    writer.u8(static_cast<std::uint8_t>(transition.result));
    writer.string(transition.diagnostic);
    return writer.take();
}

std::optional<runtime::RuntimeTransition> decodeTransitionBody(
    std::span<const std::byte> payload) {
    Reader reader(payload);
    runtime::RuntimeTransition value;
    std::uint8_t command = 0, from = 0, to = 0, result = 0;
    if (!reader.u64(value.sequence) || !reader.u64(value.correlationId) ||
        !reader.u8(command) || !reader.u8(from) || !reader.u8(to) ||
        !reader.u8(result) || !validRuntimeCommand(command) ||
        !validSessionPhase(from) || !validSessionPhase(to) ||
        !validRuntimeResult(result) || !reader.string(value.diagnostic) ||
        !reader.done()) {
        return std::nullopt;
    }
    value.command = static_cast<runtime::RuntimeCommand>(command);
    value.from = static_cast<runtime::SeatSessionPhase>(from);
    value.to = static_cast<runtime::SeatSessionPhase>(to);
    value.result = static_cast<runtime::RuntimeResultCode>(result);
    return value;
}

} // namespace

std::string_view messageTypeName(MessageType type) noexcept {
    switch (type) {
        case MessageType::Hello: return "hello";
        case MessageType::HelloAck: return "hello-ack";
        case MessageType::GetSnapshot: return "get-snapshot";
        case MessageType::Snapshot: return "snapshot";
        case MessageType::PlanSession: return "plan-session";
        case MessageType::PlanResult: return "plan-result";
        case MessageType::StartSession: return "start-session";
        case MessageType::StartResult: return "start-result";
        case MessageType::StopAndReturnToWindows: return "stop-and-return-to-windows";
        case MessageType::StopResult: return "stop-result";
        case MessageType::BeginReconfigure: return "begin-reconfigure";
        case MessageType::ReconfigureResult: return "reconfigure-result";
        case MessageType::ExitHostWhenIdle: return "exit-host-when-idle";
        case MessageType::ExitResult: return "exit-result";
        case MessageType::EmergencyReset: return "emergency-reset";
        case MessageType::ResetResult: return "reset-result";
        case MessageType::SubscribeEvents: return "subscribe-events";
        case MessageType::SubscribeAck: return "subscribe-ack";
        case MessageType::RuntimeEvent: return "runtime-event";
        case MessageType::Ping: return "ping";
        case MessageType::Pong: return "pong";
        case MessageType::Error: return "error";
        case MessageType::ApplyProfile: return "apply-profile";
        case MessageType::ApplyProfileResult: return "apply-profile-result";
    }
    return "unknown";
}

std::string_view errorCodeName(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::None: return "none";
        case ErrorCode::Malformed: return "malformed";
        case ErrorCode::VersionMismatch: return "version-mismatch";
        case ErrorCode::PermissionDenied: return "permission-denied";
        case ErrorCode::DuplicateCorrelation: return "duplicate-correlation";
        case ErrorCode::InvalidState: return "invalid-state";
        case ErrorCode::Busy: return "busy";
        case ErrorCode::RecoveryRequired: return "recovery-required";
        case ErrorCode::Unsupported: return "unsupported";
        case ErrorCode::ResnapshotRequired: return "resnapshot-required";
        case ErrorCode::InternalError: return "internal-error";
    }
    return "unknown";
}

std::vector<std::byte> encodeFrame(const Frame& frame) {
    if (frame.correlationId == 0 || frame.payload.size() > kHostProtocolMaxPayloadBytes) {
        return {};
    }
    Writer writer;
    writer.u32(kHostProtocolMagic);
    writer.u16(kHostProtocolVersion);
    writer.u16(static_cast<std::uint16_t>(frame.type));
    writer.u32(0u);
    writer.u32(static_cast<std::uint32_t>(frame.payload.size()));
    writer.u64(frame.correlationId);
    writer.raw(frame.payload);
    return writer.take();
}

std::optional<Frame> decodeFrame(std::span<const std::byte> bytes,
                                 DecodeResult* result) {
    auto fail = [&](ErrorCode code, std::string diagnostic) -> std::optional<Frame> {
        if (result) {
            result->ok = false;
            result->error = code;
            result->diagnostic = std::move(diagnostic);
        }
        return std::nullopt;
    };
    if (bytes.size() < kHostProtocolHeaderBytes) {
        return fail(ErrorCode::Malformed, "frame is shorter than the fixed header");
    }
    Reader reader(bytes);
    std::uint32_t magic = 0, reserved = 0, payloadSize = 0;
    std::uint16_t version = 0, rawType = 0;
    std::uint64_t correlationId = 0;
    if (!reader.u32(magic) || !reader.u16(version) || !reader.u16(rawType) ||
        !reader.u32(reserved) || !reader.u32(payloadSize) ||
        !reader.u64(correlationId)) {
        return fail(ErrorCode::Malformed, "fixed frame header is truncated");
    }
    if (magic != kHostProtocolMagic) {
        return fail(ErrorCode::Malformed, "frame magic mismatch");
    }
    if (version != kHostProtocolVersion) {
        return fail(ErrorCode::VersionMismatch, "host protocol version mismatch");
    }
    if (reserved != 0) {
        return fail(ErrorCode::Malformed, "reserved frame header bits are nonzero");
    }
    if (!validMessageType(rawType) || correlationId == 0 ||
        payloadSize > kHostProtocolMaxPayloadBytes ||
        reader.remaining() != payloadSize) {
        return fail(ErrorCode::Malformed, "frame type, correlation, or payload length is invalid");
    }
    Frame frame;
    frame.type = static_cast<MessageType>(rawType);
    frame.correlationId = correlationId;
    if (!reader.rawVector(payloadSize, frame.payload) || !reader.done()) {
        return fail(ErrorCode::Malformed, "frame payload is truncated");
    }
    if (result) {
        result->ok = true;
        result->error = ErrorCode::None;
        result->diagnostic.clear();
    }
    return frame;
}

std::vector<std::byte> encodeHello(const Hello& value) {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(value.role));
    writer.u8(0); writer.u8(0); writer.u8(0);
    writer.u32(value.seatId);
    return writer.take();
}

std::optional<Hello> decodeHello(std::span<const std::byte> payload) {
    Reader reader(payload);
    std::uint8_t role = 0, r1 = 0, r2 = 0, r3 = 0;
    Hello value;
    if (!reader.u8(role) || !reader.u8(r1) || !reader.u8(r2) || !reader.u8(r3) ||
        !reader.u32(value.seatId) || !reader.done() || !validRole(role) ||
        r1 != 0 || r2 != 0 || r3 != 0 ||
        (static_cast<ClientRole>(role) == ClientRole::Control && value.seatId == 0)) {
        return std::nullopt;
    }
    value.role = static_cast<ClientRole>(role);
    return value;
}

std::vector<std::byte> encodeHelloAck(const HelloAck& value) {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(value.role));
    writer.u8(0); writer.u8(0); writer.u8(0);
    writer.u32(value.seatId);
    writer.u32(value.managementSeatId);
    writer.u32(value.serverProcessId);
    writer.u32(value.windowsSessionId);
    return writer.take();
}

std::optional<HelloAck> decodeHelloAck(std::span<const std::byte> payload) {
    Reader reader(payload);
    std::uint8_t role = 0, r1 = 0, r2 = 0, r3 = 0;
    HelloAck value;
    if (!reader.u8(role) || !reader.u8(r1) || !reader.u8(r2) || !reader.u8(r3) ||
        !reader.u32(value.seatId) || !reader.u32(value.managementSeatId) ||
        !reader.u32(value.serverProcessId) || !reader.u32(value.windowsSessionId) ||
        !reader.done() || !validRole(role) || r1 != 0 || r2 != 0 || r3 != 0 ||
        value.managementSeatId == 0 ||
        (static_cast<ClientRole>(role) == ClientRole::Control && value.seatId == 0)) {
        return std::nullopt;
    }
    value.role = static_cast<ClientRole>(role);
    return value;
}

std::vector<std::byte> encodeProfilePayload(const ProfilePayload& profile) {
    if (profile.managementSeatId == 0 || profile.seats.empty() ||
        profile.seats.size() > kHostProtocolMaxSeats) {
        return {};
    }
    Writer writer;
    writer.u32(profile.managementSeatId);
    writer.u32(static_cast<std::uint32_t>(profile.seats.size()));
    for (const auto& seat : profile.seats) {
        if (seat.seatId == 0) return {};
        writer.u32(seat.seatId);
        writer.u8(seat.active ? 1u : 0u);
        writer.u8(0); writer.u8(0); writer.u8(0);
        writer.u64(seat.targetHwnd);
        if (!writeWide(writer, seat.name) ||
            !writeWideVector(writer, seat.displayIds) ||
            !writeOptionalWide(writer, seat.primaryDisplayId) ||
            !writeWideVector(writer, seat.keyboardIds) ||
            !writeWideVector(writer, seat.mouseIds) ||
            !writeWideVector(writer, seat.controllerIds) ||
            !writeOptionalWide(writer, seat.audioOutputEndpointId) ||
            !writeOptionalWide(writer, seat.audioInputEndpointId)) {
            return {};
        }
    }
    auto payload = writer.take();
    if (payload.size() > kHostProtocolMaxPayloadBytes) return {};
    return payload;
}

std::optional<ProfilePayload> decodeProfilePayload(std::span<const std::byte> payload) {
    if (payload.empty() || payload.size() > kHostProtocolMaxPayloadBytes) return std::nullopt;
    Reader reader(payload);
    ProfilePayload profile;
    std::uint32_t count = 0;
    if (!reader.u32(profile.managementSeatId) || profile.managementSeatId == 0 ||
        !reader.u32(count) || count == 0 || count > kHostProtocolMaxSeats) {
        return std::nullopt;
    }
    profile.seats.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        SeatConfig seat;
        std::uint8_t active = 0, r1 = 0, r2 = 0, r3 = 0;
        if (!reader.u32(seat.seatId) || seat.seatId == 0 ||
            !reader.u8(active) || !reader.u8(r1) || !reader.u8(r2) || !reader.u8(r3) ||
            active > 1u || r1 != 0 || r2 != 0 || r3 != 0 ||
            !reader.u64(seat.targetHwnd) || !readWide(reader, seat.name) ||
            !readWideVector(reader, seat.displayIds) ||
            !readOptionalWide(reader, seat.primaryDisplayId) ||
            !readWideVector(reader, seat.keyboardIds) ||
            !readWideVector(reader, seat.mouseIds) ||
            !readWideVector(reader, seat.controllerIds) ||
            !readOptionalWide(reader, seat.audioOutputEndpointId) ||
            !readOptionalWide(reader, seat.audioInputEndpointId)) {
            return std::nullopt;
        }
        seat.active = active != 0u;
        profile.seats.push_back(std::move(seat));
    }
    if (!reader.done()) return std::nullopt;
    return profile;
}

std::vector<std::byte> encodeSnapshot(const runtime::HostRuntimeSnapshot& snapshot) {
    if (snapshot.seats.size() > kHostProtocolMaxSeats ||
        snapshot.configuredSeats.size() > kHostProtocolMaxSeats) {
        return {};
    }
    std::vector<std::byte> configuredProfile;
    if (snapshot.profileLoaded) {
        configuredProfile = encodeProfilePayload(
            ProfilePayload{snapshot.managementSeatId, snapshot.configuredSeats});
        if (configuredProfile.empty()) return {};
    } else if (!snapshot.configuredSeats.empty()) {
        return {};
    }
    Writer writer;
    writer.u32(snapshot.schemaVersion);
    writer.u8(static_cast<std::uint8_t>(snapshot.hostPhase));
    writer.u8(static_cast<std::uint8_t>(snapshot.sessionPhase));
    writer.u16(0);
    writer.raw(std::as_bytes(std::span(snapshot.sessionId.bytes)));
    writer.u64(snapshot.generation);
    writer.u64(snapshot.transitionSequence);
    writer.u32(snapshot.connectedControlClients);
    writer.u32(snapshot.managementSeatId);
    writer.u8(snapshot.profileLoaded ? 1u : 0u);
    writer.u8(snapshot.mutationInProgress ? 1u : 0u);
    writer.u8(snapshot.lastTransition ? 1u : 0u);
    writer.u8(0);
    writer.u32(static_cast<std::uint32_t>(snapshot.seats.size()));
    writer.string(snapshot.diagnostic);
    for (const auto& seat : snapshot.seats) {
        writer.u32(seat.seatId);
        writer.u8(static_cast<std::uint8_t>(seat.phase));
        writer.u8(0); writer.u8(0); writer.u8(0);
        writer.string(seat.diagnostic);
    }
    writer.u32(static_cast<std::uint32_t>(configuredProfile.size()));
    writer.raw(configuredProfile);
    if (snapshot.lastTransition) {
        const auto event = encodeTransitionBody(*snapshot.lastTransition);
        writer.u32(static_cast<std::uint32_t>(event.size()));
        writer.raw(event);
    }
    auto bytes = writer.take();
    if (bytes.size() > kHostProtocolMaxPayloadBytes) return {};
    return bytes;
}

std::optional<runtime::HostRuntimeSnapshot> decodeSnapshot(
    std::span<const std::byte> payload) {
    Reader reader(payload);
    runtime::HostRuntimeSnapshot snapshot;
    std::uint8_t host = 0, session = 0, profile = 0, mutation = 0, hasLast = 0,
                 reservedByte = 0;
    std::uint16_t reserved16 = 0;
    std::uint32_t seatCount = 0;
    if (!reader.u32(snapshot.schemaVersion) || !reader.u8(host) ||
        !reader.u8(session) || !reader.u16(reserved16) || reserved16 != 0 ||
        !validHostPhase(host) || !validSessionPhase(session) ||
        !reader.raw(std::as_writable_bytes(std::span(snapshot.sessionId.bytes))) ||
        !reader.u64(snapshot.generation) || !reader.u64(snapshot.transitionSequence) ||
        !reader.u32(snapshot.connectedControlClients) || !reader.u32(snapshot.managementSeatId) ||
        snapshot.managementSeatId == 0 || snapshot.schemaVersion != 2u || !reader.u8(profile) ||
        !reader.u8(mutation) || !reader.u8(hasLast) || !reader.u8(reservedByte) ||
        profile > 1u || mutation > 1u || hasLast > 1u || reservedByte != 0 ||
        !reader.u32(seatCount) || seatCount > kHostProtocolMaxSeats ||
        !reader.string(snapshot.diagnostic)) {
        return std::nullopt;
    }
    snapshot.hostPhase = static_cast<runtime::HostLifecyclePhase>(host);
    snapshot.sessionPhase = static_cast<runtime::SeatSessionPhase>(session);
    snapshot.profileLoaded = profile != 0;
    snapshot.mutationInProgress = mutation != 0;
    snapshot.seats.reserve(seatCount);
    for (std::uint32_t index = 0; index < seatCount; ++index) {
        runtime::SeatRuntimeState seat;
        std::uint8_t phase = 0, r1 = 0, r2 = 0, r3 = 0;
        if (!reader.u32(seat.seatId) || seat.seatId == 0 || !reader.u8(phase) ||
            !reader.u8(r1) || !reader.u8(r2) || !reader.u8(r3) ||
            !validSessionPhase(phase) || r1 != 0 || r2 != 0 || r3 != 0 ||
            !reader.string(seat.diagnostic)) {
            return std::nullopt;
        }
        seat.phase = static_cast<runtime::SeatSessionPhase>(phase);
        snapshot.seats.push_back(std::move(seat));
    }
    std::uint32_t configuredLength = 0;
    std::vector<std::byte> configuredBytes;
    if (!reader.u32(configuredLength) || configuredLength > kHostProtocolMaxPayloadBytes ||
        !reader.rawVector(configuredLength, configuredBytes)) {
        return std::nullopt;
    }
    if (snapshot.profileLoaded) {
        const auto configured = decodeProfilePayload(configuredBytes);
        if (!configured || configured->managementSeatId != snapshot.managementSeatId) {
            return std::nullopt;
        }
        snapshot.configuredSeats = configured->seats;
    } else if (configuredLength != 0u) {
        return std::nullopt;
    }
    if (hasLast != 0) {
        std::uint32_t length = 0;
        std::vector<std::byte> event;
        if (!reader.u32(length) || length > kHostProtocolMaxPayloadBytes ||
            !reader.rawVector(length, event)) {
            return std::nullopt;
        }
        auto transition = decodeTransitionBody(event);
        if (!transition) return std::nullopt;
        snapshot.lastTransition = std::move(*transition);
    }
    if (!reader.done()) return std::nullopt;
    return snapshot;
}

std::vector<std::byte> encodeCommandResult(const runtime::RuntimeCommandResult& result) {
    const auto snapshot = encodeSnapshot(result.snapshot);
    if (snapshot.empty()) return {};
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(result.code));
    writer.u8(0); writer.u8(0); writer.u8(0);
    writer.u32(static_cast<std::uint32_t>(snapshot.size()));
    writer.raw(snapshot);
    writer.string(result.diagnostic);
    auto bytes = writer.take();
    if (bytes.size() > kHostProtocolMaxPayloadBytes) return {};
    return bytes;
}

std::optional<runtime::RuntimeCommandResult> decodeCommandResult(
    std::span<const std::byte> payload) {
    Reader reader(payload);
    runtime::RuntimeCommandResult result;
    std::uint8_t code = 0, r1 = 0, r2 = 0, r3 = 0;
    std::uint32_t snapshotLength = 0;
    std::vector<std::byte> snapshotBytes;
    if (!reader.u8(code) || !reader.u8(r1) || !reader.u8(r2) || !reader.u8(r3) ||
        !validRuntimeResult(code) || r1 != 0 || r2 != 0 || r3 != 0 ||
        !reader.u32(snapshotLength) || snapshotLength > kHostProtocolMaxPayloadBytes ||
        !reader.rawVector(snapshotLength, snapshotBytes) ||
        !reader.string(result.diagnostic) || !reader.done()) {
        return std::nullopt;
    }
    auto snapshot = decodeSnapshot(snapshotBytes);
    if (!snapshot) return std::nullopt;
    result.code = static_cast<runtime::RuntimeResultCode>(code);
    result.snapshot = std::move(*snapshot);
    return result;
}

std::vector<std::byte> encodeRuntimeEvent(const runtime::RuntimeTransition& transition) {
    return encodeTransitionBody(transition);
}

std::optional<runtime::RuntimeTransition> decodeRuntimeEvent(
    std::span<const std::byte> payload) {
    return decodeTransitionBody(payload);
}

std::vector<std::byte> encodeSubscribeRequest(const SubscribeRequest& request) {
    Writer writer;
    writer.u64(request.afterSequence);
    writer.u32(std::min<std::uint32_t>(request.maxEvents,
                                       static_cast<std::uint32_t>(kHostProtocolMaxEvents)));
    writer.u32(0);
    return writer.take();
}

std::optional<SubscribeRequest> decodeSubscribeRequest(
    std::span<const std::byte> payload) {
    Reader reader(payload);
    SubscribeRequest value;
    std::uint32_t reserved = 0;
    if (!reader.u64(value.afterSequence) || !reader.u32(value.maxEvents) ||
        !reader.u32(reserved) || !reader.done() || reserved != 0 ||
        value.maxEvents == 0 || value.maxEvents > kHostProtocolMaxEvents) {
        return std::nullopt;
    }
    return value;
}

std::vector<std::byte> encodePing(std::uint64_t nonce) {
    Writer writer;
    writer.u64(nonce);
    return writer.take();
}

std::optional<std::uint64_t> decodePing(std::span<const std::byte> payload) {
    Reader reader(payload);
    std::uint64_t nonce = 0;
    if (!reader.u64(nonce) || !reader.done()) return std::nullopt;
    return nonce;
}

std::vector<std::byte> encodeError(const ErrorPayload& error) {
    Writer writer;
    writer.u16(static_cast<std::uint16_t>(error.code));
    writer.u16(0);
    writer.string(error.diagnostic);
    return writer.take();
}

std::optional<ErrorPayload> decodeError(std::span<const std::byte> payload) {
    Reader reader(payload);
    std::uint16_t code = 0, reserved = 0;
    ErrorPayload value;
    if (!reader.u16(code) || !reader.u16(reserved) || !validError(code) ||
        reserved != 0 || !reader.string(value.diagnostic) || !reader.done()) {
        return std::nullopt;
    }
    value.code = static_cast<ErrorCode>(code);
    return value;
}

bool isMutatingRequest(MessageType type) noexcept {
    switch (type) {
        case MessageType::PlanSession:
        case MessageType::StartSession:
        case MessageType::StopAndReturnToWindows:
        case MessageType::BeginReconfigure:
        case MessageType::ExitHostWhenIdle:
        case MessageType::EmergencyReset:
        case MessageType::ApplyProfile:
            return true;
        default:
            return false;
    }
}

MessageType responseTypeFor(MessageType request) noexcept {
    switch (request) {
        case MessageType::Hello: return MessageType::HelloAck;
        case MessageType::GetSnapshot: return MessageType::Snapshot;
        case MessageType::PlanSession: return MessageType::PlanResult;
        case MessageType::StartSession: return MessageType::StartResult;
        case MessageType::StopAndReturnToWindows: return MessageType::StopResult;
        case MessageType::BeginReconfigure: return MessageType::ReconfigureResult;
        case MessageType::ExitHostWhenIdle: return MessageType::ExitResult;
        case MessageType::EmergencyReset: return MessageType::ResetResult;
        case MessageType::ApplyProfile: return MessageType::ApplyProfileResult;
        case MessageType::SubscribeEvents: return MessageType::SubscribeAck;
        case MessageType::Ping: return MessageType::Pong;
        default: return MessageType::Error;
    }
}

} // namespace hydra::hostipc
