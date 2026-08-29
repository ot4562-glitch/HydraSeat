#include "hydra/seat_navigation_model.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace hydra::seatui {
namespace {

bool validRegion(const SeatDisplayRegion& region) noexcept {
    return !region.displayId.empty() && region.displayId.size() <= 512u &&
           region.right > region.left && region.bottom > region.top;
}

bool contains(const SeatDisplayRegion& region, std::int32_t x, std::int32_t y) noexcept {
    return x >= region.left && x < region.right && y >= region.top && y < region.bottom;
}

} // namespace

SeatNavigationModel::SeatNavigationModel(SeatId seatId) : seatId_(seatId) {
    state_.seatId = seatId;
}

bool SeatNavigationModel::configureDisplays(std::vector<SeatDisplayRegion> regions,
                                            std::uint64_t authorityGeneration,
                                            std::string* error) {
    if (seatId_ == 0u || authorityGeneration == 0u || regions.empty() ||
        regions.size() > kMaximumSeatNavigationDisplays) {
        if (error != nullptr) *error = "Seat navigation display group is invalid";
        return false;
    }
    if (state_.authorityGeneration != 0u && authorityGeneration < state_.authorityGeneration) {
        if (error != nullptr) *error = "stale Seat navigation display generation was rejected";
        return false;
    }
    std::set<std::string> ids;
    for (const auto& region : regions) {
        if (!validRegion(region) || !ids.insert(region.displayId).second) {
            if (error != nullptr) *error = "Seat navigation display region is malformed or duplicate";
            return false;
        }
    }
    std::sort(regions.begin(), regions.end(), [](const auto& left, const auto& right) {
        return left.displayId < right.displayId;
    });
    displays_ = std::move(regions);
    state_.authorityGeneration = authorityGeneration;
    state_.mode = SeatNavigationMode::ControllerFocus;
    state_.displayId.clear();
    state_.pointerX = 0;
    state_.pointerY = 0;
    state_.focusIndex = 0u;
    if (error != nullptr) error->clear();
    return true;
}

bool SeatNavigationModel::applyPointer(const SeatPointerSample& sample,
                                       std::string* error) {
    if (sample.seatId != seatId_ || sample.authorityGeneration == 0u ||
        sample.authorityGeneration != state_.authorityGeneration) {
        if (error != nullptr) *error = "pointer sample does not match Seat navigation authority";
        return false;
    }
    const auto region = std::find_if(displays_.begin(), displays_.end(),
                                     [&](const auto& value) {
                                         return value.displayId == sample.displayId;
                                     });
    if (region == displays_.end() || !contains(*region, sample.x, sample.y)) {
        if (error != nullptr) *error = "pointer sample escapes the assigned Seat display group";
        return false;
    }
    state_.mode = SeatNavigationMode::SeatLocalPointer;
    state_.displayId = sample.displayId;
    state_.pointerX = sample.x;
    state_.pointerY = sample.y;
    if (error != nullptr) error->clear();
    return true;
}

bool SeatNavigationModel::controllerStep(int direction, std::size_t itemCount,
                                         std::string* error) {
    if (itemCount == 0u || itemCount > kMaximumSeatNavigationItems ||
        (direction != -1 && direction != 1)) {
        if (error != nullptr) *error = "controller focus step is invalid or out of bounds";
        return false;
    }
    state_.mode = SeatNavigationMode::ControllerFocus;
    if (state_.focusIndex >= itemCount) state_.focusIndex = 0u;
    if (direction > 0) {
        state_.focusIndex = (state_.focusIndex + 1u) % itemCount;
    } else {
        state_.focusIndex = state_.focusIndex == 0u ? itemCount - 1u : state_.focusIndex - 1u;
    }
    if (error != nullptr) error->clear();
    return true;
}

std::string_view seatNavigationModeName(SeatNavigationMode mode) noexcept {
    switch (mode) {
        case SeatNavigationMode::ControllerFocus: return "ControllerFocus";
        case SeatNavigationMode::SeatLocalPointer: return "SeatLocalPointer";
    }
    return "Unknown";
}

} // namespace hydra::seatui
