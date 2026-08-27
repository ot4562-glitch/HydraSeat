#include "hydra/management_seat.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hydra::control {
namespace {

const display::SeatDisplayGroup* findGroup(
    const std::vector<display::SeatDisplayGroup>& groups, SeatId seatId) noexcept {
    const auto found = std::find_if(groups.begin(), groups.end(), [seatId](const auto& group) {
        return group.seatId == seatId;
    });
    return found == groups.end() ? nullptr : &*found;
}

const display::SeatDisplayOutput* findGroupOutput(
    const display::SeatDisplayGroup& group, const std::string& outputId) noexcept {
    const auto found = std::find_if(group.outputs.begin(), group.outputs.end(),
                                    [&](const auto& output) {
                                        return output.outputId == outputId;
                                    });
    return found == group.outputs.end() ? nullptr : &*found;
}

const display::DisplayOutput* findActiveTopologyOutput(
    const display::DisplayTopologySnapshot& topology, const std::string& outputId) noexcept {
    const auto found = std::find_if(topology.outputs.begin(), topology.outputs.end(),
                                    [&](const auto& output) {
                                        return output.active &&
                                               output.identity.stableKey() == outputId;
                                    });
    return found == topology.outputs.end() ? nullptr : &*found;
}

const display::DisplayOutput* fallbackOutput(
    const display::DisplayTopologySnapshot& topology, ControlSurfaceFallback& fallback) noexcept {
    const auto primary = std::find_if(topology.outputs.begin(), topology.outputs.end(),
                                      [](const auto& output) {
                                          return output.active && output.primary;
                                      });
    if (primary != topology.outputs.end()) {
        fallback = ControlSurfaceFallback::WindowsPrimary;
        return &*primary;
    }
    const auto firstActive = std::find_if(topology.outputs.begin(), topology.outputs.end(),
                                          [](const auto& output) { return output.active; });
    if (firstActive != topology.outputs.end()) {
        fallback = ControlSurfaceFallback::FirstActiveOutput;
        return &*firstActive;
    }
    return nullptr;
}

bool hasArea(const display::DisplayRect& rect) noexcept {
    return rect.right > rect.left && rect.bottom > rect.top;
}

bool fullyInside(const display::DisplayRect& outer,
                 const display::DisplayRect& inner) noexcept {
    return hasArea(inner) && inner.left >= outer.left && inner.top >= outer.top &&
           inner.right <= outer.right && inner.bottom <= outer.bottom;
}

display::DisplayRect workAreaFor(const display::DisplayRect& outputBounds) noexcept {
#ifdef _WIN32
    RECT rect{outputBounds.left, outputBounds.top, outputBounds.right, outputBounds.bottom};
    const HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL);
    if (monitor != nullptr) {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) != FALSE) {
            return {info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom};
        }
    }
#endif
    return outputBounds;
}

display::DisplayRect centeredRect(const display::DisplayRect& workArea,
                                  std::int32_t preferredWidth,
                                  std::int32_t preferredHeight) noexcept {
    const auto availableWidth = std::max<std::int32_t>(1, workArea.width());
    const auto availableHeight = std::max<std::int32_t>(1, workArea.height());
    const auto width = std::clamp(preferredWidth, 1, availableWidth);
    const auto height = std::clamp(preferredHeight, 1, availableHeight);
    const auto left = workArea.left + (availableWidth - width) / 2;
    const auto top = workArea.top + (availableHeight - height) / 2;
    return {left, top, left + width, top + height};
}

std::int32_t roundedCoordinate(double value) noexcept {
    const double lower = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    const double upper = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    return static_cast<std::int32_t>(std::llround(std::clamp(value, lower, upper)));
}

} // namespace

ControlSurfacePlacement resolveControlSurfacePlacement(
    const ManagementSeatConfig& config,
    const std::vector<display::SeatDisplayGroup>& seatGroups,
    const display::DisplayTopologySnapshot& topology,
    std::optional<display::CoordinateRect> savedSeatLocalRect) {
    ControlSurfacePlacement result;
    result.managementSeatId = config.managementSeatId;
    if (config.managementSeatId == 0 || config.preferredWidth <= 0 ||
        config.preferredHeight <= 0 || !topology.querySucceeded) {
        result.diagnostics.push_back(
            "Management Seat placement requires a valid Seat, window size, and topology");
        return result;
    }

    const auto* group = findGroup(seatGroups, config.managementSeatId);
    const display::SeatDisplayOutput* groupPrimary = nullptr;
    const display::DisplayOutput* target = nullptr;
    if (group != nullptr) {
        groupPrimary = findGroupOutput(*group, group->primaryOutputId);
        if (groupPrimary != nullptr) {
            target = findActiveTopologyOutput(topology, groupPrimary->outputId);
        }
    }

    if (target == nullptr) {
        result.degraded = true;
        target = fallbackOutput(topology, result.fallback);
        if (target == nullptr) {
            result.diagnostics.push_back(
                "Management Seat primary is unavailable and no active visible fallback exists");
            return result;
        }
        result.diagnostics.push_back(
            "Management Seat primary is unavailable; control console uses a visible Windows fallback");
    }

    result.targetOutputId = target->identity.stableKey();
    result.workArea = workAreaFor(target->desktopBounds);
    if (!hasArea(result.workArea)) {
        result.diagnostics.push_back("resolved control-console work area has no visible area");
        return result;
    }

    if (!result.degraded && group != nullptr && groupPrimary != nullptr && savedSeatLocalRect) {
        display::CoordinateTransform transform(*group);
        const auto topLeft = transform.seatToGlobal(
            {savedSeatLocalRect->left, savedSeatLocalRect->top});
        const auto bottomRight = transform.seatToGlobal(
            {savedSeatLocalRect->right, savedSeatLocalRect->bottom});
        const display::DisplayRect restored{
            roundedCoordinate(topLeft.x), roundedCoordinate(topLeft.y),
            roundedCoordinate(bottomRight.x), roundedCoordinate(bottomRight.y)};
        if (fullyInside(result.workArea, restored)) {
            result.windowRect = restored;
            result.restoredSavedRect = true;
        } else {
            result.diagnostics.push_back(
                "saved Management Seat rectangle is off-screen or stale and was rejected");
        }
    }

    if (!result.restoredSavedRect) {
        result.windowRect = centeredRect(result.workArea, config.preferredWidth,
                                         config.preferredHeight);
    }
    result.valid = fullyInside(result.workArea, result.windowRect);
    if (!result.valid) {
        result.diagnostics.push_back("control console could not be placed wholly inside a visible work area");
    }
    return result;
}

} // namespace hydra::control
