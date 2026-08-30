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
           raw <= static_cast<std::uint16_t>(MessageType::RemoveProviderPlanResult);
}

bool validHostPhase(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(runtime::HostLifecyclePhase::Stopped);
}

bool validSessionPhase(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(runtime::SeatSessionPhase::RecoveryRequired);
}

bool validSeatGamePhase(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(runtime::SeatGamePhase::RecoveryRequired);
}

bool validSeatGameResult(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(runtime::SeatGameResultCode::V1SeatLimitExceeded);
}

bool validSeatGameStateShape(runtime::SeatGamePhase phase, bool hasBinding) {
    switch (phase) {
        case runtime::SeatGamePhase::Idle:
        case runtime::SeatGamePhase::Degraded:
            return !hasBinding;
        case runtime::SeatGamePhase::Planning:
        case runtime::SeatGamePhase::Starting:
        case runtime::SeatGamePhase::Playing:
        case runtime::SeatGamePhase::Stopping:
            return hasBinding;
        case runtime::SeatGamePhase::RecoveryRequired:
            // Recovery may retain an active binding after stop failure or have
            // no binding after a failed partial-start cleanup.
            return true;
    }
    return false;
}

bool validWholeMachineReturnPolicy(
    std::span<const runtime::SeatGameState> states, bool requested) {
    if (!requested) return true;
    return !states.empty() &&
           std::all_of(states.begin(), states.end(), [](const auto& state) {
               return state.phase == runtime::SeatGamePhase::Idle;
           });
}

bool validRuntimeCommand(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(runtime::RuntimeCommand::ObserveSeatGameExit);
}

bool validRuntimeResult(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(runtime::RuntimeResultCode::RecoveryRequired);
}

bool validRole(std::uint8_t raw) {
    return raw <= static_cast<std::uint8_t>(ClientRole::SeatControl);
}

bool validError(std::uint16_t raw) {
    return raw <= static_cast<std::uint16_t>(ErrorCode::InternalError);
}

bool seatGameStatesMatchProfile(
    std::span<const runtime::SeatGameState> states,
    std::span<const SeatConfig> configuredSeats) {
    std::vector<SeatId> activeIds;
    for (const auto& seat : configuredSeats) {
        if (seat.active) activeIds.push_back(seat.seatId);
    }
    std::vector<SeatId> stateIds;
    stateIds.reserve(states.size());
    for (const auto& state : states) stateIds.push_back(state.seatId);
    std::sort(activeIds.begin(), activeIds.end());
    std::sort(stateIds.begin(), stateIds.end());
    return activeIds == stateIds;
}

std::vector<std::byte> encodeTransitionBody(const runtime::RuntimeTransition& transition) {
    Writer writer;
    writer.u64(transition.sequence);
    writer.u64(transition.correlationId);
    writer.u8(static_cast<std::uint8_t>(transition.command));
    writer.u8(static_cast<std::uint8_t>(transition.from));
    writer.u8(static_cast<std::uint8_t>(transition.to));
    writer.u8(static_cast<std::uint8_t>(transition.result));
    writer.u32(transition.seatId);
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
        !reader.u8(result) || !reader.u32(value.seatId) || !validRuntimeCommand(command) ||
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

bool writeBoundedString(Writer& writer, std::string_view value,
                        bool allowEmpty = false) {
    if ((!allowEmpty && value.empty()) || value.size() > kHostProtocolMaxStringBytes ||
        value.find('\0') != std::string_view::npos) {
        return false;
    }
    writer.string(value);
    return true;
}

bool writeOptionalString(Writer& writer,
                         const std::optional<std::string>& value) {
    writer.u8(value ? 1u : 0u);
    return !value || writeBoundedString(writer, *value);
}

bool readOptionalString(Reader& reader, std::optional<std::string>& value) {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1u) return false;
    if (present == 0u) {
        value.reset();
        return true;
    }
    std::string decoded;
    if (!reader.string(decoded) || decoded.empty() ||
        decoded.find('\0') != std::string::npos) {
        return false;
    }
    value = std::move(decoded);
    return true;
}

bool writePlanWideVector(Writer& writer,
                         const std::vector<std::wstring>& values) {
    if (values.size() > kHostProtocolMaxPlanArguments) return false;
    writer.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        if (!writeWide(writer, value)) return false;
    }
    return true;
}

bool readPlanWideVector(Reader& reader, std::vector<std::wstring>& values) {
    std::uint32_t count = 0;
    if (!reader.u32(count) || count > kHostProtocolMaxPlanArguments) return false;
    values.clear();
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::wstring value;
        if (!readWide(reader, value)) return false;
        values.push_back(std::move(value));
    }
    return true;
}

std::uint8_t requirementBits(const launch::Requirements& value) noexcept {
    return static_cast<std::uint8_t>((value.display ? 1u : 0u) |
        (value.keyboard ? 2u : 0u) | (value.mouse ? 4u : 0u) |
        (value.controller ? 8u : 0u) | (value.audioOutput ? 16u : 0u) |
        (value.windowOwnership ? 32u : 0u) | (value.recovery ? 64u : 0u) |
        (value.highRisk ? 128u : 0u));
}

launch::Requirements requirementsFromBits(std::uint8_t bits) noexcept {
    launch::Requirements value;
    value.display = (bits & 1u) != 0;
    value.keyboard = (bits & 2u) != 0;
    value.mouse = (bits & 4u) != 0;
    value.controller = (bits & 8u) != 0;
    value.audioOutput = (bits & 16u) != 0;
    value.windowOwnership = (bits & 32u) != 0;
    value.recovery = (bits & 64u) != 0;
    value.highRisk = (bits & 128u) != 0;
    return value;
}

std::uint8_t capabilityBits(const launch::Capabilities& value) noexcept {
    return static_cast<std::uint8_t>((value.process ? 1u : 0u) |
        (value.window ? 2u : 0u) | (value.display ? 4u : 0u) |
        (value.input ? 8u : 0u) | (value.controller ? 16u : 0u) |
        (value.audio ? 32u : 0u) | (value.recovery ? 64u : 0u));
}

launch::Capabilities capabilitiesFromBits(std::uint8_t bits) noexcept {
    launch::Capabilities value;
    value.process = (bits & 1u) != 0;
    value.window = (bits & 2u) != 0;
    value.display = (bits & 4u) != 0;
    value.input = (bits & 8u) != 0;
    value.controller = (bits & 16u) != 0;
    value.audio = (bits & 32u) != 0;
    value.recovery = (bits & 64u) != 0;
    return value;
}

bool writeCompatibility(
    Writer& writer,
    const std::optional<profile::CompatibilityReference>& compatibility) {
    writer.u8(compatibility ? 1u : 0u);
    if (!compatibility) return true;
    if (compatibility->evidenceRevision == 0 ||
        !writeBoundedString(writer, compatibility->recordId) ||
        !writeBoundedString(writer, compatibility->provenance)) {
        return false;
    }
    writer.u32(compatibility->evidenceRevision);
    return true;
}

bool readCompatibility(
    Reader& reader,
    std::optional<profile::CompatibilityReference>& compatibility) {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1u) return false;
    if (present == 0u) {
        compatibility.reset();
        return true;
    }
    profile::CompatibilityReference value;
    if (!reader.string(value.recordId) || value.recordId.empty() ||
        !reader.string(value.provenance) || value.provenance.empty() ||
        !reader.u32(value.evidenceRevision) || value.evidenceRevision == 0) {
        return false;
    }
    compatibility = std::move(value);
    return true;
}

bool writeInstanceRecipe(
    Writer& writer,
    const std::optional<profile::InstanceRecipe>& recipe) {
    writer.u8(recipe ? 1u : 0u);
    if (!recipe) return true;
    return writePlanWideVector(writer, recipe->arguments) &&
           writeOptionalWide(writer, recipe->workingDirectory) &&
           writeOptionalWide(writer, recipe->dataRoot);
}

bool readInstanceRecipe(
    Reader& reader,
    std::optional<profile::InstanceRecipe>& recipe) {
    std::uint8_t present = 0;
    if (!reader.u8(present) || present > 1u) return false;
    if (present == 0u) {
        recipe.reset();
        return true;
    }
    profile::InstanceRecipe value;
    if (!readPlanWideVector(reader, value.arguments) ||
        !readOptionalWide(reader, value.workingDirectory) ||
        !readOptionalWide(reader, value.dataRoot)) {
        return false;
    }
    recipe = std::move(value);
    return true;
}

bool writeProviderLaunchRequest(Writer& writer,
                                const provider::ProviderLaunchRequest& request) {
    if (!writeBoundedString(writer, request.providerId) ||
        !writeBoundedString(writer, request.gameId) ||
        !writeOptionalString(writer, request.providerAppId) ||
        !writeOptionalString(writer, request.accountRef) ||
        request.metadataRevision == 0 ||
        static_cast<std::uint8_t>(request.targetKind) >
            static_cast<std::uint8_t>(provider::LaunchTargetKind::ProviderUri)) {
        return false;
    }
    writer.u64(request.metadataRevision);
    writer.u8(static_cast<std::uint8_t>(request.targetKind));
    writer.u8(0u); writer.u16(0u);
    if (!writeWide(writer, request.target) ||
        !writePlanWideVector(writer, request.arguments) ||
        !writeOptionalWide(writer, request.workingDirectory) ||
        !writeBoundedString(writer, request.launchCorrelationId)) {
        return false;
    }
    return true;
}

bool readProviderLaunchRequest(Reader& reader,
                               provider::ProviderLaunchRequest& request) {
    std::uint8_t targetKind = 0, reserved8 = 0;
    std::uint16_t reserved16 = 0;
    if (!reader.string(request.providerId) || request.providerId.empty() ||
        !reader.string(request.gameId) || request.gameId.empty() ||
        !readOptionalString(reader, request.providerAppId) ||
        !readOptionalString(reader, request.accountRef) ||
        !reader.u64(request.metadataRevision) || request.metadataRevision == 0 ||
        !reader.u8(targetKind) ||
        targetKind > static_cast<std::uint8_t>(provider::LaunchTargetKind::ProviderUri) ||
        !reader.u8(reserved8) || !reader.u16(reserved16) ||
        reserved8 != 0 || reserved16 != 0 ||
        !readWide(reader, request.target) || request.target.empty() ||
        !readPlanWideVector(reader, request.arguments) ||
        !readOptionalWide(reader, request.workingDirectory) ||
        !reader.string(request.launchCorrelationId) ||
        request.launchCorrelationId.empty()) {
        return false;
    }
    request.targetKind = static_cast<provider::LaunchTargetKind>(targetKind);
    return true;
}

std::vector<std::byte> encodeProviderPlanBody(
    const plan::ProviderAwareLaunchPlan& value) {
    if (value.schemaVersion != plan::kProviderLaunchPlanSchemaVersion ||
        value.fingerprint == 0 || value.seats.empty() ||
        value.seats.size() > kHostProtocolMaxPlanSeats ||
        production::providerPlanFingerprint(value) != value.fingerprint) {
        return {};
    }
    std::vector<plan::SeatProviderLaunchPlan> seats = value.seats;
    std::sort(seats.begin(), seats.end(), [](const auto& left, const auto& right) {
        return left.seatId < right.seatId;
    });
    if (std::adjacent_find(seats.begin(), seats.end(), [](const auto& left, const auto& right) {
            return left.seatId == right.seatId;
        }) != seats.end()) {
        return {};
    }
    Writer writer;
    writer.u32(value.schemaVersion);
    writer.u64(value.fingerprint);
    writer.u16(static_cast<std::uint16_t>(seats.size()));
    writer.u16(0u);
    for (const auto& seat : seats) {
        if (seat.seatId == 0 || seat.requirementRevision == 0 ||
            seat.hardwareFingerprint == 0) return {};
        writer.u32(seat.seatId);
        if (!writeBoundedString(writer, seat.playerId) ||
            !writeBoundedString(writer, seat.gameId) ||
            !writeOptionalString(writer, seat.setupId)) return {};
        writer.u32(seat.instanceIndex);
        writer.u64(seat.requirementRevision);
        if (!writeCompatibility(writer, seat.compatibility) ||
            !writeInstanceRecipe(writer, seat.instanceRecipe)) return {};
        writer.u64(seat.hardwareFingerprint);
        writer.u8(requirementBits(seat.requirements));
        writer.u8(capabilityBits(seat.capabilities));
        writer.u16(0u);
        if (!writeProviderLaunchRequest(writer, seat.launchRequest)) return {};
    }
    auto bytes = writer.take();
    if (bytes.size() > kHostProtocolMaxPayloadBytes) return {};
    return bytes;
}

std::optional<plan::ProviderAwareLaunchPlan> decodeProviderPlanBody(
    std::span<const std::byte> payload) {
    Reader reader(payload);
    plan::ProviderAwareLaunchPlan value;
    std::uint16_t count = 0, reserved16 = 0;
    if (!reader.u32(value.schemaVersion) ||
        value.schemaVersion != plan::kProviderLaunchPlanSchemaVersion ||
        !reader.u64(value.fingerprint) || value.fingerprint == 0 ||
        !reader.u16(count) || count == 0 || count > kHostProtocolMaxPlanSeats ||
        !reader.u16(reserved16) || reserved16 != 0) {
        return std::nullopt;
    }
    value.seats.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        plan::SeatProviderLaunchPlan seat;
        std::uint8_t requirements = 0, capabilities = 0;
        std::uint16_t reserved = 0;
        if (!reader.u32(seat.seatId) || seat.seatId == 0 ||
            !reader.string(seat.playerId) || seat.playerId.empty() ||
            !reader.string(seat.gameId) || seat.gameId.empty() ||
            !readOptionalString(reader, seat.setupId) ||
            !reader.u32(seat.instanceIndex) ||
            !reader.u64(seat.requirementRevision) || seat.requirementRevision == 0 ||
            !readCompatibility(reader, seat.compatibility) ||
            !readInstanceRecipe(reader, seat.instanceRecipe) ||
            !reader.u64(seat.hardwareFingerprint) || seat.hardwareFingerprint == 0 ||
            !reader.u8(requirements) || !reader.u8(capabilities) ||
            (capabilities & 0x80u) != 0 || !reader.u16(reserved) || reserved != 0 ||
            !readProviderLaunchRequest(reader, seat.launchRequest)) {
            return std::nullopt;
        }
        seat.requirements = requirementsFromBits(requirements);
        seat.capabilities = capabilitiesFromBits(capabilities);
        value.seats.push_back(std::move(seat));
    }
    if (!reader.done()) return std::nullopt;
    std::sort(value.seats.begin(), value.seats.end(), [](const auto& left, const auto& right) {
        return left.seatId < right.seatId;
    });
    if (std::adjacent_find(value.seats.begin(), value.seats.end(),
                           [](const auto& left, const auto& right) {
                               return left.seatId == right.seatId;
                           }) != value.seats.end() ||
        production::providerPlanFingerprint(value) != value.fingerprint) {
        return std::nullopt;
    }
    return value;
}

bool validProviderPlanInstallCode(std::uint8_t raw) noexcept {
    return raw <= static_cast<std::uint8_t>(
        production::ProviderPlanInstallCode::BackendFailure);
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
        case MessageType::AssignSeatGame: return "assign-seat-game";
        case MessageType::AssignSeatGameResult: return "assign-seat-game-result";
        case MessageType::StartSeatGame: return "start-seat-game";
        case MessageType::StartSeatGameResult: return "start-seat-game-result";
        case MessageType::StopSeatGame: return "stop-seat-game";
        case MessageType::StopSeatGameResult: return "stop-seat-game-result";
        case MessageType::ReconcileSeatGames: return "reconcile-seat-games";
        case MessageType::ReconcileSeatGamesResult: return "reconcile-seat-games-result";
        case MessageType::GetProviderPlanRegistry: return "get-provider-plan-registry";
        case MessageType::ProviderPlanRegistry: return "provider-plan-registry";
        case MessageType::InstallProviderPlan: return "install-provider-plan";
        case MessageType::InstallProviderPlanResult: return "install-provider-plan-result";
        case MessageType::RemoveProviderPlan: return "remove-provider-plan";
        case MessageType::RemoveProviderPlanResult: return "remove-provider-plan-result";
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
        (static_cast<ClientRole>(role) != ClientRole::ReadOnly && value.seatId == 0)) {
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
        (static_cast<ClientRole>(role) != ClientRole::ReadOnly && value.seatId == 0)) {
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

std::vector<std::byte> encodeSeatGameStates(
    std::span<const runtime::SeatGameState> states) {
    if (states.size() > runtime::kV1MaximumActiveSeats) return {};
    std::vector<runtime::SeatGameState> ordered(states.begin(), states.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.seatId < right.seatId;
    });
    if (std::adjacent_find(ordered.begin(), ordered.end(),
                           [](const auto& left, const auto& right) {
                               return left.seatId == right.seatId;
                           }) != ordered.end()) return {};
    Writer writer;
    writer.u16(1u); // Seat lifecycle payload version.
    writer.u16(static_cast<std::uint16_t>(ordered.size()));
    for (const auto& state : ordered) {
        const auto rawPhase = static_cast<std::uint8_t>(state.phase);
        if (state.seatId == 0 || !validSeatGamePhase(rawPhase) ||
            !validSeatGameStateShape(state.phase, state.binding.has_value()) ||
            state.diagnostic.size() > kHostProtocolMaxStringBytes ||
            state.diagnostic.find('\0') != std::string::npos) return {};
        if (state.binding &&
            (state.binding->playerId.empty() || state.binding->gameId.empty() ||
             state.binding->playerId.size() > 256u || state.binding->gameId.size() > 256u ||
             state.binding->playerId.find('\0') != std::string::npos ||
             state.binding->gameId.find('\0') != std::string::npos)) return {};
        writer.u32(state.seatId);
        writer.u8(rawPhase);
        writer.u8(state.binding ? 1u : 0u);
        writer.u16(0u);
        writer.u64(state.generation);
        if (state.binding) {
            writer.string(state.binding->playerId);
            writer.string(state.binding->gameId);
        }
        writer.string(state.diagnostic);
    }
    auto bytes = writer.take();
    if (bytes.size() > kHostProtocolMaxPayloadBytes) return {};
    return bytes;
}

std::optional<std::vector<runtime::SeatGameState>> decodeSeatGameStates(
    std::span<const std::byte> payload) {
    Reader reader(payload);
    std::uint16_t version = 0;
    std::uint16_t count = 0;
    if (!reader.u16(version) || version != 1u || !reader.u16(count) ||
        count > runtime::kV1MaximumActiveSeats) {
        return std::nullopt;
    }
    std::vector<runtime::SeatGameState> states;
    states.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        runtime::SeatGameState state;
        std::uint8_t phase = 0;
        std::uint8_t hasBinding = 0;
        std::uint16_t reserved = 0;
        if (!reader.u32(state.seatId) || state.seatId == 0 ||
            !reader.u8(phase) || !validSeatGamePhase(phase) ||
            !reader.u8(hasBinding) || hasBinding > 1u ||
            !reader.u16(reserved) || reserved != 0 ||
            !reader.u64(state.generation)) {
            return std::nullopt;
        }
        state.phase = static_cast<runtime::SeatGamePhase>(phase);
        if (hasBinding != 0u) {
            runtime::SeatGameBinding binding;
            if (!reader.string(binding.playerId) || !reader.string(binding.gameId) ||
                binding.playerId.empty() || binding.gameId.empty() ||
                binding.playerId.size() > 256u || binding.gameId.size() > 256u ||
                binding.playerId.find('\0') != std::string::npos ||
                binding.gameId.find('\0') != std::string::npos) {
                return std::nullopt;
            }
            state.binding = std::move(binding);
        }
        if (!reader.string(state.diagnostic)) return std::nullopt;
        if (!validSeatGameStateShape(state.phase, state.binding.has_value())) {
            return std::nullopt;
        }
        states.push_back(std::move(state));
    }
    if (!reader.done()) return std::nullopt;
    std::sort(states.begin(), states.end(), [](const auto& left, const auto& right) {
        return left.seatId < right.seatId;
    });
    if (std::adjacent_find(states.begin(), states.end(), [](const auto& left, const auto& right) {
            return left.seatId == right.seatId;
        }) != states.end()) {
        return std::nullopt;
    }
    return states;
}

std::vector<std::byte> encodeSeatGameCommandResult(
    const runtime::SeatGameCommandResult& result) {
    if (!validSeatGameResult(static_cast<std::uint8_t>(result.code)) ||
        !validWholeMachineReturnPolicy(result.seats,
                                       result.wholeMachineReturnRequested) ||
        result.diagnostic.size() > kHostProtocolMaxStringBytes ||
        result.diagnostic.find('\0') != std::string::npos) return {};
    const auto states = encodeSeatGameStates(result.seats);
    if (states.empty() && !result.seats.empty()) return {};
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(result.code));
    writer.u8(result.wholeMachineReturnRequested ? 1u : 0u);
    writer.u16(0u);
    writer.u32(static_cast<std::uint32_t>(states.size()));
    writer.raw(states);
    writer.string(result.diagnostic);
    auto bytes = writer.take();
    if (bytes.size() > kHostProtocolMaxPayloadBytes) return {};
    return bytes;
}

std::optional<runtime::SeatGameCommandResult> decodeSeatGameCommandResult(
    std::span<const std::byte> payload) {
    Reader reader(payload);
    runtime::SeatGameCommandResult result;
    std::uint8_t code = 0;
    std::uint8_t returnRequested = 0;
    std::uint16_t reserved = 0;
    std::uint32_t length = 0;
    std::vector<std::byte> statesBytes;
    if (!reader.u8(code) || !validSeatGameResult(code) ||
        !reader.u8(returnRequested) || returnRequested > 1u ||
        !reader.u16(reserved) || reserved != 0 ||
        !reader.u32(length) || length > kHostProtocolMaxPayloadBytes ||
        !reader.rawVector(length, statesBytes) || !reader.string(result.diagnostic) ||
        !reader.done()) {
        return std::nullopt;
    }
    const auto states = decodeSeatGameStates(statesBytes);
    if (!states || !validWholeMachineReturnPolicy(*states,
                                                   returnRequested != 0u)) {
        return std::nullopt;
    }
    result.code = static_cast<runtime::SeatGameResultCode>(code);
    result.wholeMachineReturnRequested = returnRequested != 0u;
    result.seats = *states;
    return result;
}

std::vector<std::byte> encodeSeatGameCommandPayload(
    const SeatGameCommandPayload& payload) {
    if (payload.seatId == 0) return {};
    Writer writer;
    writer.u32(payload.seatId);
    writer.u8(payload.binding ? 1u : 0u);
    writer.u8(0u); writer.u16(0u);
    if (payload.binding) {
        if (payload.binding->playerId.empty() || payload.binding->gameId.empty() ||
            payload.binding->playerId.size() > 256u || payload.binding->gameId.size() > 256u ||
            payload.binding->playerId.find('\0') != std::string::npos ||
            payload.binding->gameId.find('\0') != std::string::npos) return {};
        writer.string(payload.binding->playerId);
        writer.string(payload.binding->gameId);
    }
    return writer.take();
}

std::optional<SeatGameCommandPayload> decodeSeatGameCommandPayload(
    std::span<const std::byte> payload) {
    Reader reader(payload);
    SeatGameCommandPayload result;
    std::uint8_t hasBinding = 0, reserved8 = 0;
    std::uint16_t reserved16 = 0;
    if (!reader.u32(result.seatId) || result.seatId == 0 ||
        !reader.u8(hasBinding) || hasBinding > 1u || !reader.u8(reserved8) ||
        !reader.u16(reserved16) || reserved8 != 0 || reserved16 != 0) return std::nullopt;
    if (hasBinding != 0u) {
        runtime::SeatGameBinding binding;
        if (!reader.string(binding.playerId) || !reader.string(binding.gameId) ||
            binding.playerId.empty() || binding.gameId.empty() ||
            binding.playerId.size() > 256u || binding.gameId.size() > 256u ||
            binding.playerId.find('\0') != std::string::npos ||
            binding.gameId.find('\0') != std::string::npos) return std::nullopt;
        result.binding = std::move(binding);
    }
    if (!reader.done()) return std::nullopt;
    return result;
}

std::vector<std::byte> encodeProviderPlanRegistrySnapshot(
    const production::ProviderPlanRegistrySnapshot& snapshot) {
    if (snapshot.schemaVersion != production::kProviderPlanInstallSchemaVersion ||
        snapshot.entries.size() > kHostProtocolMaxPlanSeats ||
        (snapshot.sessionGeneration == 0u && !snapshot.sessionId.empty()) ||
        (snapshot.sessionGeneration != 0u && snapshot.sessionId.empty())) {
        return {};
    }
    std::vector<production::ProviderPlanRegistryEntry> entries = snapshot.entries;
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.seatId < right.seatId;
    });
    if (std::adjacent_find(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            return left.seatId == right.seatId;
        }) != entries.end()) return {};

    Writer writer;
    writer.u16(1u);
    writer.u16(static_cast<std::uint16_t>(entries.size()));
    writer.u32(snapshot.schemaVersion);
    writer.u64(snapshot.registryRevision);
    writer.u64(snapshot.profileFingerprint);
    writer.raw(std::as_bytes(std::span(snapshot.sessionId.bytes)));
    writer.u64(snapshot.sessionGeneration);
    for (const auto& entry : entries) {
        if (entry.seatId == 0 || entry.installedRevision == 0 ||
            entry.planFingerprint == 0 || entry.planRevision == 0 ||
            entry.profileFingerprint != snapshot.profileFingerprint ||
            entry.sessionId != snapshot.sessionId ||
            entry.sessionGeneration != snapshot.sessionGeneration ||
            entry.seatGameGeneration == 0 || entry.playerId.empty() ||
            entry.playerId.size() > kHostProtocolMaxStringBytes ||
            entry.playerId.find('\0') != std::string::npos ||
            entry.gameId.empty() ||
            entry.gameId.size() > kHostProtocolMaxStringBytes ||
            entry.gameId.find('\0') != std::string::npos) {
            return {};
        }
        writer.u32(entry.seatId);
        writer.u64(entry.installedRevision);
        writer.u64(entry.planFingerprint);
        writer.u64(entry.planRevision);
        writer.u64(entry.profileFingerprint);
        writer.raw(std::as_bytes(std::span(entry.sessionId.bytes)));
        writer.u64(entry.sessionGeneration);
        writer.u64(entry.seatGameGeneration);
        // Strings are written after all fixed fields for deterministic decoding.
        // Re-emit them here because prevalidation above intentionally does not
        // mutate the wire layout.
        writer.string(entry.playerId);
        writer.string(entry.gameId);
    }
    auto bytes = writer.take();
    if (bytes.size() > kHostProtocolMaxPayloadBytes) return {};
    return bytes;
}

std::optional<production::ProviderPlanRegistrySnapshot>
decodeProviderPlanRegistrySnapshot(std::span<const std::byte> payload) {
    Reader reader(payload);
    production::ProviderPlanRegistrySnapshot snapshot;
    std::uint16_t version = 0, count = 0;
    if (!reader.u16(version) || version != 1u || !reader.u16(count) ||
        count > kHostProtocolMaxPlanSeats ||
        !reader.u32(snapshot.schemaVersion) ||
        snapshot.schemaVersion != production::kProviderPlanInstallSchemaVersion ||
        !reader.u64(snapshot.registryRevision) ||
        !reader.u64(snapshot.profileFingerprint) ||
        !reader.raw(std::as_writable_bytes(std::span(snapshot.sessionId.bytes))) ||
        !reader.u64(snapshot.sessionGeneration) ||
        (snapshot.sessionGeneration == 0u && !snapshot.sessionId.empty()) ||
        (snapshot.sessionGeneration != 0u && snapshot.sessionId.empty())) {
        return std::nullopt;
    }
    snapshot.entries.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        production::ProviderPlanRegistryEntry entry;
        if (!reader.u32(entry.seatId) || entry.seatId == 0 ||
            !reader.u64(entry.installedRevision) || entry.installedRevision == 0 ||
            !reader.u64(entry.planFingerprint) || entry.planFingerprint == 0 ||
            !reader.u64(entry.planRevision) || entry.planRevision == 0 ||
            !reader.u64(entry.profileFingerprint) ||
            !reader.raw(std::as_writable_bytes(std::span(entry.sessionId.bytes))) ||
            !reader.u64(entry.sessionGeneration) ||
            !reader.u64(entry.seatGameGeneration) || entry.seatGameGeneration == 0 ||
            !reader.string(entry.playerId) || entry.playerId.empty() ||
            !reader.string(entry.gameId) || entry.gameId.empty() ||
            entry.profileFingerprint != snapshot.profileFingerprint ||
            entry.sessionId != snapshot.sessionId ||
            entry.sessionGeneration != snapshot.sessionGeneration) {
            return std::nullopt;
        }
        snapshot.entries.push_back(std::move(entry));
    }
    if (!reader.done()) return std::nullopt;
    std::sort(snapshot.entries.begin(), snapshot.entries.end(),
              [](const auto& left, const auto& right) {
                  return left.seatId < right.seatId;
              });
    if (std::adjacent_find(snapshot.entries.begin(), snapshot.entries.end(),
                           [](const auto& left, const auto& right) {
                               return left.seatId == right.seatId;
                           }) != snapshot.entries.end()) return std::nullopt;
    return snapshot;
}

std::vector<std::byte> encodeProviderPlanInstallRequest(
    const production::ProviderPlanInstallRequest& request) {
    if (request.schemaVersion != production::kProviderPlanInstallSchemaVersion ||
        request.seatId == 0 || request.planFingerprint == 0 ||
        request.planRevision == 0 || request.profileFingerprint == 0 ||
        request.sessionId.empty() || request.sessionGeneration == 0 ||
        request.seatGameGeneration == 0 ||
        request.plan.fingerprint != request.planFingerprint) {
        return {};
    }
    const auto planBytes = encodeProviderPlanBody(request.plan);
    if (planBytes.empty()) return {};
    Writer writer;
    writer.u16(1u); writer.u16(0u);
    writer.u32(request.schemaVersion);
    writer.u32(request.seatId);
    writer.u64(request.expectedRegistryRevision);
    writer.u64(request.planFingerprint);
    writer.u64(request.planRevision);
    writer.u64(request.profileFingerprint);
    writer.raw(std::as_bytes(std::span(request.sessionId.bytes)));
    writer.u64(request.sessionGeneration);
    writer.u64(request.seatGameGeneration);
    writer.u32(static_cast<std::uint32_t>(planBytes.size()));
    writer.raw(planBytes);
    auto bytes = writer.take();
    if (bytes.size() > kHostProtocolMaxPayloadBytes) return {};
    return bytes;
}

std::optional<production::ProviderPlanInstallRequest>
decodeProviderPlanInstallRequest(std::span<const std::byte> payload) {
    Reader reader(payload);
    production::ProviderPlanInstallRequest request;
    std::uint16_t version = 0, reserved16 = 0;
    std::uint32_t planLength = 0;
    std::vector<std::byte> planBytes;
    if (!reader.u16(version) || version != 1u ||
        !reader.u16(reserved16) || reserved16 != 0 ||
        !reader.u32(request.schemaVersion) ||
        request.schemaVersion != production::kProviderPlanInstallSchemaVersion ||
        !reader.u32(request.seatId) || request.seatId == 0 ||
        !reader.u64(request.expectedRegistryRevision) ||
        !reader.u64(request.planFingerprint) || request.planFingerprint == 0 ||
        !reader.u64(request.planRevision) || request.planRevision == 0 ||
        !reader.u64(request.profileFingerprint) || request.profileFingerprint == 0 ||
        !reader.raw(std::as_writable_bytes(std::span(request.sessionId.bytes))) ||
        request.sessionId.empty() ||
        !reader.u64(request.sessionGeneration) || request.sessionGeneration == 0 ||
        !reader.u64(request.seatGameGeneration) || request.seatGameGeneration == 0 ||
        !reader.u32(planLength) || planLength == 0 ||
        planLength > kHostProtocolMaxPayloadBytes ||
        !reader.rawVector(planLength, planBytes) || !reader.done()) {
        return std::nullopt;
    }
    auto plan = decodeProviderPlanBody(planBytes);
    if (!plan || plan->fingerprint != request.planFingerprint) return std::nullopt;
    request.plan = std::move(*plan);
    const auto selected = std::find_if(
        request.plan.seats.begin(), request.plan.seats.end(),
        [&](const auto& seat) { return seat.seatId == request.seatId; });
    if (selected == request.plan.seats.end() ||
        production::providerPlanRevision(*selected) != request.planRevision) {
        return std::nullopt;
    }
    return request;
}

std::vector<std::byte> encodeProviderPlanRemoveRequest(
    const production::ProviderPlanRemoveRequest& request) {
    if (request.schemaVersion != production::kProviderPlanInstallSchemaVersion ||
        request.seatId == 0 || request.planFingerprint == 0 ||
        request.planRevision == 0 || request.profileFingerprint == 0 ||
        request.sessionId.empty() || request.sessionGeneration == 0 ||
        request.seatGameGeneration == 0) return {};
    Writer writer;
    writer.u16(1u); writer.u16(0u);
    writer.u32(request.schemaVersion);
    writer.u32(request.seatId);
    writer.u64(request.expectedRegistryRevision);
    writer.u64(request.planFingerprint);
    writer.u64(request.planRevision);
    writer.u64(request.profileFingerprint);
    writer.raw(std::as_bytes(std::span(request.sessionId.bytes)));
    writer.u64(request.sessionGeneration);
    writer.u64(request.seatGameGeneration);
    return writer.take();
}

std::optional<production::ProviderPlanRemoveRequest>
decodeProviderPlanRemoveRequest(std::span<const std::byte> payload) {
    Reader reader(payload);
    production::ProviderPlanRemoveRequest request;
    std::uint16_t version = 0, reserved16 = 0;
    if (!reader.u16(version) || version != 1u ||
        !reader.u16(reserved16) || reserved16 != 0 ||
        !reader.u32(request.schemaVersion) ||
        request.schemaVersion != production::kProviderPlanInstallSchemaVersion ||
        !reader.u32(request.seatId) || request.seatId == 0 ||
        !reader.u64(request.expectedRegistryRevision) ||
        !reader.u64(request.planFingerprint) || request.planFingerprint == 0 ||
        !reader.u64(request.planRevision) || request.planRevision == 0 ||
        !reader.u64(request.profileFingerprint) || request.profileFingerprint == 0 ||
        !reader.raw(std::as_writable_bytes(std::span(request.sessionId.bytes))) ||
        request.sessionId.empty() ||
        !reader.u64(request.sessionGeneration) || request.sessionGeneration == 0 ||
        !reader.u64(request.seatGameGeneration) || request.seatGameGeneration == 0 ||
        !reader.done()) return std::nullopt;
    return request;
}

std::vector<std::byte> encodeProviderPlanInstallResult(
    const production::ProviderPlanInstallResult& result) {
    if (!validProviderPlanInstallCode(static_cast<std::uint8_t>(result.code)) ||
        result.diagnostic.size() > kHostProtocolMaxStringBytes ||
        result.diagnostic.find('\0') != std::string::npos) return {};
    const auto registry = encodeProviderPlanRegistrySnapshot(result.registry);
    if (registry.empty()) return {};
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(result.code));
    writer.u8(0u); writer.u16(0u);
    writer.u32(static_cast<std::uint32_t>(registry.size()));
    writer.raw(registry);
    writer.string(result.diagnostic);
    auto bytes = writer.take();
    if (bytes.size() > kHostProtocolMaxPayloadBytes) return {};
    return bytes;
}

std::optional<production::ProviderPlanInstallResult>
decodeProviderPlanInstallResult(std::span<const std::byte> payload) {
    Reader reader(payload);
    production::ProviderPlanInstallResult result;
    std::uint8_t code = 0, reserved8 = 0;
    std::uint16_t reserved16 = 0;
    std::uint32_t registryLength = 0;
    std::vector<std::byte> registryBytes;
    if (!reader.u8(code) || !validProviderPlanInstallCode(code) ||
        !reader.u8(reserved8) || !reader.u16(reserved16) ||
        reserved8 != 0 || reserved16 != 0 ||
        !reader.u32(registryLength) || registryLength == 0 ||
        registryLength > kHostProtocolMaxPayloadBytes ||
        !reader.rawVector(registryLength, registryBytes) ||
        !reader.string(result.diagnostic) || !reader.done()) {
        return std::nullopt;
    }
    auto registry = decodeProviderPlanRegistrySnapshot(registryBytes);
    if (!registry) return std::nullopt;
    result.code = static_cast<production::ProviderPlanInstallCode>(code);
    result.registry = std::move(*registry);
    return result;
}

std::vector<std::byte> encodeSnapshot(const runtime::HostRuntimeSnapshot& snapshot) {
    if (snapshot.seats.size() > kHostProtocolMaxSeats ||
        snapshot.configuredSeats.size() > kHostProtocolMaxSeats ||
        !validWholeMachineReturnPolicy(snapshot.seatGames,
                                       snapshot.wholeMachineReturnRequested)) {
        return {};
    }
    std::vector<std::byte> configuredProfile;
    if (snapshot.profileLoaded) {
        if (!seatGameStatesMatchProfile(snapshot.seatGames,
                                        snapshot.configuredSeats)) return {};
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
    const auto seatGames = encodeSeatGameStates(snapshot.seatGames);
    if (seatGames.empty() && !snapshot.seatGames.empty()) return {};
    writer.u8(snapshot.wholeMachineReturnRequested ? 1u : 0u);
    writer.u8(0u); writer.u16(0u);
    writer.u32(static_cast<std::uint32_t>(seatGames.size()));
    writer.raw(seatGames);
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
        snapshot.managementSeatId == 0 || snapshot.schemaVersion != 3u || !reader.u8(profile) ||
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
    std::uint8_t returnRequested = 0, lifecycleReserved8 = 0;
    std::uint16_t lifecycleReserved16 = 0;
    std::uint32_t lifecycleLength = 0;
    std::vector<std::byte> lifecycleBytes;
    if (!reader.u8(returnRequested) || returnRequested > 1u ||
        !reader.u8(lifecycleReserved8) || !reader.u16(lifecycleReserved16) ||
        lifecycleReserved8 != 0 || lifecycleReserved16 != 0 ||
        !reader.u32(lifecycleLength) || lifecycleLength > kHostProtocolMaxPayloadBytes ||
        !reader.rawVector(lifecycleLength, lifecycleBytes)) return std::nullopt;
    const auto seatGames = decodeSeatGameStates(lifecycleBytes);
    if (!seatGames || !validWholeMachineReturnPolicy(*seatGames,
                                                      returnRequested != 0u)) {
        return std::nullopt;
    }
    snapshot.seatGames = *seatGames;
    snapshot.wholeMachineReturnRequested = returnRequested != 0u;
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
        if (!seatGameStatesMatchProfile(snapshot.seatGames,
                                        snapshot.configuredSeats)) {
            return std::nullopt;
        }
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
        case MessageType::AssignSeatGame:
        case MessageType::StartSeatGame:
        case MessageType::StopSeatGame:
        case MessageType::ReconcileSeatGames:
        case MessageType::InstallProviderPlan:
        case MessageType::RemoveProviderPlan:
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
        case MessageType::AssignSeatGame: return MessageType::AssignSeatGameResult;
        case MessageType::StartSeatGame: return MessageType::StartSeatGameResult;
        case MessageType::StopSeatGame: return MessageType::StopSeatGameResult;
        case MessageType::ReconcileSeatGames: return MessageType::ReconcileSeatGamesResult;
        case MessageType::GetProviderPlanRegistry: return MessageType::ProviderPlanRegistry;
        case MessageType::InstallProviderPlan: return MessageType::InstallProviderPlanResult;
        case MessageType::RemoveProviderPlan: return MessageType::RemoveProviderPlanResult;
        case MessageType::SubscribeEvents: return MessageType::SubscribeAck;
        case MessageType::Ping: return MessageType::Pong;
        default: return MessageType::Error;
    }
}

} // namespace hydra::hostipc
