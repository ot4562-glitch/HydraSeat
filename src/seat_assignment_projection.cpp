#include "hydra/seat_assignment_projection.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <unordered_set>
#include <utility>

namespace hydra {
namespace {

constexpr std::size_t kMaximumVisibleAssignments = 256u;

void setError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::wstring normalizedId(std::wstring_view value) {
    std::wstring normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t ch) {
        return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch - L'A' + L'a') : ch;
    });
    return normalized;
}

std::wstring assignmentKey(SeatDeviceType type, std::wstring_view stableId) {
    return std::to_wstring(static_cast<unsigned>(type)) + L":" + normalizedId(stableId);
}

bool visibleType(SeatDeviceType type) noexcept {
    switch (type) {
    case SeatDeviceType::Display:
    case SeatDeviceType::Keyboard:
    case SeatDeviceType::Mouse:
    case SeatDeviceType::Controller:
        return true;
    case SeatDeviceType::AudioOutput:
    case SeatDeviceType::AudioInput:
        return false;
    }
    return false;
}

bool unassign(WorkspaceManager& manager, SeatId seatId, SeatDeviceType type,
              const std::wstring& stableId) {
    switch (type) {
    case SeatDeviceType::Display:
        return manager.unassignDisplay(seatId, stableId);
    case SeatDeviceType::Keyboard:
        return manager.unassignKeyboard(seatId, stableId);
    case SeatDeviceType::Mouse:
        return manager.unassignMouse(seatId, stableId);
    case SeatDeviceType::Controller:
        return manager.unassignController(seatId, stableId);
    case SeatDeviceType::AudioOutput:
    case SeatDeviceType::AudioInput:
        return false;
    }
    return false;
}

bool assign(WorkspaceManager& manager, const VisibleSeatDeviceAssignment& assignment) {
    switch (assignment.type) {
    case SeatDeviceType::Display:
        return manager.assignDisplay(
            assignment.seatId, assignment.stableId, assignment.primaryDisplay);
    case SeatDeviceType::Keyboard:
        return manager.assignKeyboard(assignment.seatId, assignment.stableId);
    case SeatDeviceType::Mouse:
        return manager.assignMouse(assignment.seatId, assignment.stableId);
    case SeatDeviceType::Controller:
        return manager.assignController(assignment.seatId, assignment.stableId);
    case SeatDeviceType::AudioOutput:
    case SeatDeviceType::AudioInput:
        return false;
    }
    return false;
}

bool ensureTwoSeats(const WorkspaceManager& base, WorkspaceManager& candidate,
                    std::string* error) {
    candidate = base;
    const auto existing = candidate.getAllSeats();
    if (existing.size() == 2u && candidate.getSeat(1u) != nullptr &&
        candidate.getSeat(2u) != nullptr) {
        return true;
    }

    const SeatId preferredManagementSeat = base.managementSeatId();
    candidate = WorkspaceManager{};
    if (candidate.createSeat(L"Seat 1") != 1u || candidate.createSeat(L"Seat 2") != 2u) {
        setError(error, "could not create the required two Seat records");
        return false;
    }
    if (preferredManagementSeat == 2u && !candidate.setManagementSeatId(2u)) {
        setError(error, "could not preserve Management Seat 2");
        return false;
    }
    return true;
}

bool sanitizeLegacySeatNames(WorkspaceManager& candidate, std::string* error) {
    for (const SeatId seatId : {1u, 2u}) {
        const auto* seat = candidate.getSeat(seatId);
        if (seat == nullptr) {
            setError(error, "projection requires Seat 1 and Seat 2");
            return false;
        }
        const std::wstring legacy = L"Player " + std::to_wstring(seatId);
        if (seat->name == legacy &&
            !candidate.renameSeat(seatId, L"Seat " + std::to_wstring(seatId))) {
            setError(error, "could not migrate a legacy Player-named Seat");
            return false;
        }
    }
    return true;
}

} // namespace

bool projectVisibleSeatAssignments(
    const WorkspaceManager& base,
    std::span<const VisibleSeatDeviceAssignment> assignments,
    WorkspaceManager& output,
    std::string* error) {
    if (error != nullptr) error->clear();
    if (assignments.size() > kMaximumVisibleAssignments) {
        setError(error, "visible device assignment count exceeds the bounded limit");
        return false;
    }

    WorkspaceManager candidate;
    if (!ensureTwoSeats(base, candidate, error) ||
        !sanitizeLegacySeatNames(candidate, error)) {
        return false;
    }

    std::unordered_set<std::wstring> visibleKeys;
    visibleKeys.reserve(assignments.size());
    for (const auto& assignment : assignments) {
        if (!visibleType(assignment.type)) {
            setError(error, "visible assignment contains a non-tile device type");
            return false;
        }
        if (assignment.stableId.empty()) {
            setError(error, "visible assignment has an empty stable device id");
            return false;
        }
        if (assignment.seatId > 2u) {
            setError(error, "visible assignment references an unsupported Seat id");
            return false;
        }
        if (assignment.primaryDisplay && assignment.type != SeatDeviceType::Display) {
            setError(error, "only display assignments may be marked primary");
            return false;
        }
        if (!visibleKeys.insert(assignmentKey(assignment.type, assignment.stableId)).second) {
            setError(error, "visible assignment contains a duplicate stable device id");
            return false;
        }
    }

    for (const auto& assignment : assignments) {
        if (candidate.isDeviceShareable(assignment.type, assignment.stableId)) {
            // The legacy tile UI can display only one owner. Preserve an existing
            // multi-owner policy rather than silently collapsing it on Save.
            continue;
        }

        const auto previousOwners = candidate.findDeviceOwners(
            assignment.type, assignment.stableId);
        for (const SeatId owner : previousOwners) {
            if (!unassign(candidate, owner, assignment.type, assignment.stableId)) {
                setError(error, "could not remove a previous visible device assignment");
                return false;
            }
        }

        if (assignment.seatId != 0u && !assign(candidate, assignment)) {
            setError(error, "could not apply a visible device assignment");
            return false;
        }
    }

    for (const SeatId seatId : {1u, 2u}) {
        const auto* seat = candidate.getSeat(seatId);
        if (seat == nullptr) {
            setError(error, "projection lost a required Seat");
            return false;
        }
        if (!seat->displayIds.empty() && !seat->primaryDisplayId &&
            !candidate.setPrimaryDisplay(seatId, seat->displayIds.front())) {
            setError(error, "could not choose a deterministic primary display");
            return false;
        }
    }

    output = std::move(candidate);
    return true;
}

} // namespace hydra
