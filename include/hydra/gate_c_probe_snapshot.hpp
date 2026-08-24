#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hydra::gatec {

inline constexpr std::uint32_t kProbeSnapshotMagic = 0x31535048u; // "HPS1".
inline constexpr std::uint16_t kProbeSnapshotSchemaVersion = 2;
inline constexpr std::uint16_t kProbeSnapshotHeaderBytes = 16;
inline constexpr std::size_t kProbeSnapshotWireBytes = 848;
inline constexpr std::size_t kMaximumProbeSnapshotBytes = 1024;

struct ProbeRect {
    std::int32_t left{0};
    std::int32_t top{0};
    std::int32_t right{0};
    std::int32_t bottom{0};

    bool operator==(const ProbeRect&) const = default;
};

// Runtime HWND values are transient diagnostics scoped to one probe snapshot.
// They are never a persisted window identity or an authorization primitive.
struct OsInputSnapshot {
    std::int16_t asyncKeyState{0};
    std::int16_t keyState{0};
    bool keyboardStateSucceeded{false};
    std::uint32_t keyboardStateError{0};
    std::array<std::uint8_t, 256> keyboardState{};

    bool cursorPositionSucceeded{false};
    std::uint32_t cursorPositionError{0};
    std::int32_t cursorX{0};
    std::int32_t cursorY{0};

    bool clipRectangleSucceeded{false};
    std::uint32_t clipRectangleError{0};
    ProbeRect clipRectangle{};

    std::uint64_t foregroundWindowRuntimeValue{0};
    std::uint64_t activeWindowRuntimeValue{0};
    std::uint64_t focusWindowRuntimeValue{0};
    std::uint64_t captureWindowRuntimeValue{0};

    bool operator==(const OsInputSnapshot&) const = default;
};

struct AdapterInputSnapshot {
    std::uint32_t snapshotResult{0};
    std::uint32_t keyStateResult{0};
    std::uint32_t keyboardStateResult{0};
    std::uint32_t controlStateResult{0};
    std::uint32_t mouseStateResult{0};
    std::uint32_t windowStateResult{0};

    std::uint64_t lastAppliedSequence{0};
    std::uint16_t asyncKeyState{0};
    std::uint16_t keyState{0};
    std::array<std::uint8_t, 256> keyboardState{};
    std::array<std::uint8_t, 32> keyDownBits{};
    std::array<std::uint8_t, 32> keyPressedEdgeBits{};

    std::uint32_t mouseButtonsDown{0};
    std::int64_t wheelAccumulator{0};
    std::int32_t cursorX{0};
    std::int32_t cursorY{0};
    bool clipEnabled{false};
    bool virtualForeground{false};
    bool virtualCapture{false};
    ProbeRect clipRectangle{};
    std::uint64_t targetWindowRuntimeValue{0};
    std::uint64_t logicalForegroundWindowRuntimeValue{0};
    std::uint64_t logicalActiveWindowRuntimeValue{0};
    std::uint64_t logicalFocusWindowRuntimeValue{0};
    std::uint64_t virtualCaptureWindowRuntimeValue{0};

    bool operator==(const AdapterInputSnapshot&) const = default;
};

struct ProbeComparison {
    std::uint16_t schemaVersion{kProbeSnapshotSchemaVersion};
    std::uint64_t sequence{0};
    std::uint64_t monotonicTimestampMicros{0};
    std::uint32_t processId{0};
    std::uint32_t threadId{0};
    std::uint32_t seatId{0};
    std::uint16_t probeVkey{0xffffu};
    std::uint64_t targetWindowRuntimeValue{0};

    OsInputSnapshot os;
    AdapterInputSnapshot adapter;

    bool asyncDownMatches{false};
    bool keyStateDownMatches{false};
    bool keyboardStateDownMatches{false};
    bool cursorMatches{false};
    bool clipRectangleMatches{false};
    bool foregroundMatches{false};
    bool activeMatches{false};
    bool focusMatches{false};
    bool captureMatches{false};
    bool osForegroundIsTarget{false};
    bool osActiveIsTarget{false};
    bool osFocusIsTarget{false};
    bool osCaptureIsTarget{false};

    bool operator==(const ProbeComparison&) const = default;
};

struct ProbeSnapshotDecodeResult {
    std::optional<ProbeComparison> comparison;
    std::string error;

    explicit operator bool() const noexcept {
        return comparison.has_value();
    }
};

// Recomputes the deterministic comparison fields from the two observations.
void updateProbeComparison(ProbeComparison& comparison) noexcept;

// The wire format is fixed-width little-endian, versioned, and exactly
// kProbeSnapshotWireBytes bytes for schema version 2.
std::vector<std::byte> encodeProbeComparison(
    const ProbeComparison& comparison);
ProbeSnapshotDecodeResult decodeProbeComparison(
    std::span<const std::byte> bytes);

} // namespace hydra::gatec
