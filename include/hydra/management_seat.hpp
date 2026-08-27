#pragma once

#include "hydra/seat_display_layout.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hydra::control {

enum class ControlSurfaceFallback : std::uint8_t {
    None = 0,
    WindowsPrimary = 1,
    FirstActiveOutput = 2,
};

struct ManagementSeatConfig {
    SeatId managementSeatId{1};
    std::int32_t preferredWidth{980};
    std::int32_t preferredHeight{750};
};

struct ControlSurfacePlacement {
    bool valid{false};
    SeatId managementSeatId{1};
    std::string targetOutputId;
    display::DisplayRect workArea;
    display::DisplayRect windowRect;
    ControlSurfaceFallback fallback{ControlSurfaceFallback::None};
    bool degraded{false};
    bool restoredSavedRect{false};
    std::vector<std::string> diagnostics;
};

struct GlobalControlPermission {
    SeatId managementSeatId{1};
    SeatId callerSeatId{0};
    bool sameWindowsUserSession{false};
    bool authenticatedControlRole{false};

    bool permitsGlobalMutation() const noexcept {
        return managementSeatId != 0 && callerSeatId == managementSeatId &&
               sameWindowsUserSession && authenticatedControlRole;
    }
};

ControlSurfacePlacement resolveControlSurfacePlacement(
    const ManagementSeatConfig& config,
    const std::vector<display::SeatDisplayGroup>& seatGroups,
    const display::DisplayTopologySnapshot& topology,
    std::optional<display::CoordinateRect> savedSeatLocalRect = std::nullopt);

} // namespace hydra::control
