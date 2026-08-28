#include "hydra/audio_endpoint_inventory.hpp"

#include <algorithm>
#include <cwctype>
#include <set>
#include <tuple>
#include <utility>

namespace hydra::audio {
namespace {

constexpr unsigned kMaximumRefreshAttempts = 3u;

std::wstring canonicalId(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
    return result;
}

bool validFlow(DataFlow flow) noexcept {
    return flow == DataFlow::Render || flow == DataFlow::Capture;
}

bool validEndpoint(const EndpointRecord& endpoint, std::string& error) {
    if (endpoint.endpointId.empty() || endpoint.endpointId.size() > kMaxEndpointIdChars ||
        endpoint.endpointId.find(L'\0') != std::wstring::npos) {
        error = "audio endpoint ID is empty, embedded-null, or exceeds the bound";
        return false;
    }
    if (endpoint.friendlyName.size() > kMaxFriendlyNameChars ||
        endpoint.friendlyName.find(L'\0') != std::wstring::npos) {
        error = "audio endpoint friendly name is malformed or exceeds the bound";
        return false;
    }
    if (!validFlow(endpoint.flow)) {
        error = "audio endpoint has an unknown data flow";
        return false;
    }
    if (endpoint.stateMask == 0 ||
        (endpoint.stateMask & ~kKnownEndpointStateMask) != 0) {
        error = "audio endpoint has an unknown or empty state mask";
        return false;
    }
    if ((endpoint.defaultRoleMask & ~kKnownDefaultRoleMask) != 0) {
        error = "audio endpoint has an unknown default-role bit";
        return false;
    }
    return true;
}

bool normalizeRecords(std::vector<EndpointRecord>& endpoints, std::string& error) {
    if (endpoints.size() > kMaxAudioEndpoints) {
        error = "audio endpoint count exceeds the bounded inventory";
        return false;
    }

    std::set<std::pair<DataFlow, std::wstring>> identities;
    for (const auto& endpoint : endpoints) {
        if (!validEndpoint(endpoint, error)) return false;
        const auto key = std::make_pair(endpoint.flow, canonicalId(endpoint.endpointId));
        if (!identities.insert(key).second) {
            error = "audio endpoint inventory contains a duplicate stable identity";
            return false;
        }
    }

    std::sort(endpoints.begin(), endpoints.end(),
              [](const EndpointRecord& left, const EndpointRecord& right) {
                  return std::tuple{left.flow, canonicalId(left.endpointId)} <
                         std::tuple{right.flow, canonicalId(right.endpointId)};
              });
    return true;
}

const EndpointRecord* findEndpoint(const EndpointSnapshot& snapshot,
                                   std::wstring_view endpointId,
                                   DataFlow flow) {
    const auto wanted = canonicalId(endpointId);
    const auto found = std::find_if(
        snapshot.endpoints.begin(), snapshot.endpoints.end(),
        [&](const EndpointRecord& endpoint) {
            return endpoint.flow == flow && canonicalId(endpoint.endpointId) == wanted;
        });
    return found == snapshot.endpoints.end() ? nullptr : &*found;
}

const EndpointRecord* findEndpointAnyFlow(const EndpointSnapshot& snapshot,
                                          std::wstring_view endpointId) {
    const auto wanted = canonicalId(endpointId);
    const auto found = std::find_if(
        snapshot.endpoints.begin(), snapshot.endpoints.end(),
        [&](const EndpointRecord& endpoint) {
            return canonicalId(endpoint.endpointId) == wanted;
        });
    return found == snapshot.endpoints.end() ? nullptr : &*found;
}

void appendIssue(AssignmentValidation& validation,
                 AssignmentIssueCode code,
                 SeatId seatId,
                 std::wstring endpointId,
                 bool configurationError) {
    validation.issues.push_back(
        {code, seatId, std::move(endpointId), configurationError});
    if (configurationError) validation.configurationValid = false;
}

void validateReference(AssignmentValidation& validation,
                       SeatAssignmentStatus& status,
                       SeatId seatId,
                       bool seatActive,
                       const std::optional<std::wstring>& configuredId,
                       DataFlow expectedFlow,
                       const EndpointSnapshot& snapshot) {
    const bool output = expectedFlow == DataFlow::Render;
    if (!configuredId) return;

    if (output) {
        status.outputConfigured = true;
    } else {
        status.inputConfigured = true;
    }

    const auto* endpoint = findEndpoint(snapshot, *configuredId, expectedFlow);
    if (endpoint == nullptr) {
        const auto* wrongFlow = findEndpointAnyFlow(snapshot, *configuredId);
        appendIssue(validation,
                    wrongFlow != nullptr
                        ? (output ? AssignmentIssueCode::OutputFlowMismatch
                                  : AssignmentIssueCode::InputFlowMismatch)
                        : (output ? AssignmentIssueCode::OutputEndpointMissing
                                  : AssignmentIssueCode::InputEndpointMissing),
                    seatId, *configuredId, true);
        if (seatActive) validation.configuredEndpointsReady = false;
        return;
    }

    const bool available = isEndpointCurrentlyAvailable(*endpoint);
    if (output) {
        status.outputAvailable = available;
    } else {
        status.inputAvailable = available;
    }
    if (!available) {
        appendIssue(validation,
                    output ? AssignmentIssueCode::OutputEndpointUnavailable
                           : AssignmentIssueCode::InputEndpointUnavailable,
                    seatId, endpoint->endpointId, false);
        if (seatActive) validation.configuredEndpointsReady = false;
    }
}

} // namespace

EndpointInventory::EndpointInventory(std::shared_ptr<EndpointSource> source)
    : source_(std::move(source)) {}

bool EndpointInventory::refresh(std::string* error) {
    if (error != nullptr) error->clear();
    if (!source_) {
        if (error != nullptr) *error = "audio endpoint source is unavailable";
        return false;
    }

    const bool notifications = source_->notificationsAvailable();
    for (unsigned attempt = 0; attempt < kMaximumRefreshAttempts; ++attempt) {
        const auto beforeGeneration = source_->changeGeneration();
        std::vector<EndpointRecord> endpoints;
        std::string sourceError;
        if (!source_->enumerate(endpoints, sourceError)) {
            if (error != nullptr) {
                *error = sourceError.empty()
                    ? "audio endpoint enumeration failed"
                    : std::move(sourceError);
            }
            return false;
        }
        const auto afterGeneration = source_->changeGeneration();
        if (notifications && beforeGeneration != afterGeneration) {
            continue;
        }
        if (!normalizeRecords(endpoints, sourceError)) {
            if (error != nullptr) *error = std::move(sourceError);
            return false;
        }

        current_ = EndpointSnapshot{
            afterGeneration, notifications, std::move(endpoints)};
        return true;
    }

    if (error != nullptr) {
        *error = "audio endpoint topology changed during every bounded refresh attempt";
    }
    return false;
}

bool EndpointInventory::needsRefresh() const noexcept {
    if (!source_ || !current_) return true;
    if (!current_->notificationsAvailable) return false;
    return source_->changeGeneration() != current_->sourceGeneration;
}

AssignmentValidation validateSeatAssignments(
    std::span<const SeatConfig> seats,
    const EndpointSnapshot& snapshot) {
    AssignmentValidation validation;
    std::set<SeatId> seen;
    std::size_t activeSeats = 0;

    for (const auto& seat : seats) {
        SeatAssignmentStatus status;
        status.seatId = seat.seatId;

        if (seat.seatId == 0) {
            appendIssue(validation, AssignmentIssueCode::InvalidSeatId,
                        seat.seatId, {}, true);
        } else if (!seen.insert(seat.seatId).second) {
            appendIssue(validation, AssignmentIssueCode::DuplicateSeatId,
                        seat.seatId, {}, true);
        }
        if (seat.active) ++activeSeats;

        validateReference(validation, status, seat.seatId, seat.active,
                          seat.audioOutputEndpointId, DataFlow::Render, snapshot);
        validateReference(validation, status, seat.seatId, seat.active,
                          seat.audioInputEndpointId, DataFlow::Capture, snapshot);
        validation.seats.push_back(std::move(status));
    }

    if (activeSeats > 2u) {
        appendIssue(validation, AssignmentIssueCode::V1SeatLimitExceeded,
                    0, {}, true);
        validation.configuredEndpointsReady = false;
    }

    std::sort(validation.seats.begin(), validation.seats.end(),
              [](const SeatAssignmentStatus& left,
                 const SeatAssignmentStatus& right) {
                  return left.seatId < right.seatId;
              });
    std::sort(validation.issues.begin(), validation.issues.end(),
              [](const AssignmentIssue& left, const AssignmentIssue& right) {
                  return std::tie(left.seatId, left.code, left.endpointId) <
                         std::tie(right.seatId, right.code, right.endpointId);
              });
    return validation;
}

bool isEndpointCurrentlyAvailable(const EndpointRecord& endpoint) noexcept {
    return (endpoint.stateMask & kEndpointStateActive) != 0;
}

std::string_view dataFlowName(DataFlow flow) noexcept {
    switch (flow) {
        case DataFlow::Render: return "render";
        case DataFlow::Capture: return "capture";
    }
    return "unknown";
}

std::string_view assignmentIssueCodeName(AssignmentIssueCode code) noexcept {
    switch (code) {
        case AssignmentIssueCode::InvalidSeatId: return "invalid-seat-id";
        case AssignmentIssueCode::DuplicateSeatId: return "duplicate-seat-id";
        case AssignmentIssueCode::V1SeatLimitExceeded: return "v1-seat-limit-exceeded";
        case AssignmentIssueCode::OutputEndpointMissing: return "output-endpoint-missing";
        case AssignmentIssueCode::InputEndpointMissing: return "input-endpoint-missing";
        case AssignmentIssueCode::OutputFlowMismatch: return "output-flow-mismatch";
        case AssignmentIssueCode::InputFlowMismatch: return "input-flow-mismatch";
        case AssignmentIssueCode::OutputEndpointUnavailable: return "output-endpoint-unavailable";
        case AssignmentIssueCode::InputEndpointUnavailable: return "input-endpoint-unavailable";
    }
    return "unknown";
}

} // namespace hydra::audio
