#pragma once

#include "hydra/workspace_manager.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::audio {

inline constexpr std::size_t kMaxEndpointIdChars = 4096u;
inline constexpr std::size_t kMaxFriendlyNameChars = 1024u;
inline constexpr std::size_t kMaxAudioEndpoints = 256u;
inline constexpr std::uint32_t kEndpointStateActive = 0x00000001u;
inline constexpr std::uint32_t kEndpointStateDisabled = 0x00000002u;
inline constexpr std::uint32_t kEndpointStateNotPresent = 0x00000004u;
inline constexpr std::uint32_t kEndpointStateUnplugged = 0x00000008u;
inline constexpr std::uint32_t kKnownEndpointStateMask =
    kEndpointStateActive | kEndpointStateDisabled |
    kEndpointStateNotPresent | kEndpointStateUnplugged;

inline constexpr std::uint8_t kDefaultRoleConsole = 0x01u;
inline constexpr std::uint8_t kDefaultRoleMultimedia = 0x02u;
inline constexpr std::uint8_t kDefaultRoleCommunications = 0x04u;
inline constexpr std::uint8_t kKnownDefaultRoleMask =
    kDefaultRoleConsole | kDefaultRoleMultimedia | kDefaultRoleCommunications;

enum class DataFlow : std::uint8_t {
    Render = 0,
    Capture = 1,
};

struct EndpointRecord {
    std::wstring endpointId;
    std::wstring friendlyName;
    DataFlow flow{DataFlow::Render};
    std::uint32_t stateMask{kEndpointStateNotPresent};
    std::uint8_t defaultRoleMask{0};

    bool operator==(const EndpointRecord&) const = default;
};

struct EndpointSnapshot {
    std::uint64_t sourceGeneration{0};
    bool notificationsAvailable{false};
    std::vector<EndpointRecord> endpoints;

    bool operator==(const EndpointSnapshot&) const = default;
};

class EndpointSource {
public:
    virtual ~EndpointSource() = default;

    // Read-only. Implementations must not change endpoint routing, volume,
    // default-device policy, or any other persistent Windows audio state.
    virtual bool enumerate(std::vector<EndpointRecord>& endpoints,
                           std::string& error) noexcept = 0;
    virtual std::uint64_t changeGeneration() const noexcept = 0;
    virtual bool notificationsAvailable() const noexcept = 0;
};

std::shared_ptr<EndpointSource> makeNativeEndpointSource();

class EndpointInventory {
public:
    explicit EndpointInventory(std::shared_ptr<EndpointSource> source);

    bool refresh(std::string* error = nullptr);
    bool needsRefresh() const noexcept;
    const std::optional<EndpointSnapshot>& current() const noexcept {
        return current_;
    }

private:
    std::shared_ptr<EndpointSource> source_;
    std::optional<EndpointSnapshot> current_;
};

enum class AssignmentIssueCode : std::uint8_t {
    InvalidSeatId = 0,
    DuplicateSeatId = 1,
    V1SeatLimitExceeded = 2,
    OutputEndpointMissing = 3,
    InputEndpointMissing = 4,
    OutputFlowMismatch = 5,
    InputFlowMismatch = 6,
    OutputEndpointUnavailable = 7,
    InputEndpointUnavailable = 8,
};

struct AssignmentIssue {
    AssignmentIssueCode code{AssignmentIssueCode::InvalidSeatId};
    SeatId seatId{0};
    std::wstring endpointId;
    bool configurationError{true};

    bool operator==(const AssignmentIssue&) const = default;
};

struct SeatAssignmentStatus {
    SeatId seatId{0};
    bool outputConfigured{false};
    bool outputAvailable{false};
    bool inputConfigured{false};
    bool inputAvailable{false};

    bool operator==(const SeatAssignmentStatus&) const = default;
};

struct AssignmentValidation {
    bool configurationValid{true};
    bool configuredEndpointsReady{true};
    std::vector<SeatAssignmentStatus> seats;
    std::vector<AssignmentIssue> issues;
};

// Audio remains optional at Seat persistence time. This validator checks only
// configured endpoint references and the v1 two-active-Seat bound. A later
// launch preflight decides whether a selected game requires distinct audio.
AssignmentValidation validateSeatAssignments(
    std::span<const SeatConfig> seats,
    const EndpointSnapshot& snapshot);

bool isEndpointCurrentlyAvailable(const EndpointRecord& endpoint) noexcept;
std::string_view dataFlowName(DataFlow flow) noexcept;
std::string_view assignmentIssueCodeName(AssignmentIssueCode code) noexcept;

} // namespace hydra::audio
