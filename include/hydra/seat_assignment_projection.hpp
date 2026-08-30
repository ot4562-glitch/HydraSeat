#pragma once

#include "hydra/workspace_manager.hpp"

#include <span>
#include <string>
#include <vector>

namespace hydra {

// A bounded, UI-independent description of one currently visible hardware
// assignment. seatId == 0 means the device is intentionally left in the Pool.
struct VisibleSeatDeviceAssignment {
    SeatDeviceType type{SeatDeviceType::Display};
    std::wstring stableId;
    SeatId seatId{0};
    bool primaryDisplay{false};

    bool operator==(const VisibleSeatDeviceAssignment&) const = default;
};

// Projects the currently visible device assignments over an existing persisted
// WorkspaceManager without discarding state that the legacy tile UI cannot edit
// (for example audio endpoints, disconnected devices, active flags, or explicit
// shareable-resource policy). The operation is transactional: output is changed
// only when the entire projection validates and succeeds.
bool projectVisibleSeatAssignments(
    const WorkspaceManager& base,
    std::span<const VisibleSeatDeviceAssignment> assignments,
    WorkspaceManager& output,
    std::string* error = nullptr);

} // namespace hydra
