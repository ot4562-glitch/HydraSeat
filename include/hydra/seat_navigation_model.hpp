#pragma once

#include "hydra/seat_launcher_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::seatui {

inline constexpr std::size_t kMaximumSeatNavigationDisplays = 16u;
inline constexpr std::size_t kMaximumSeatNavigationItems = 256u;

enum class SeatNavigationMode : std::uint8_t {
    ControllerFocus = 0,
    SeatLocalPointer = 1,
};

struct SeatDisplayRegion {
    std::string displayId;
    std::int32_t left{0};
    std::int32_t top{0};
    std::int32_t right{0};
    std::int32_t bottom{0};

    bool operator==(const SeatDisplayRegion&) const = default;
};

struct SeatPointerSample {
    SeatId seatId{0};
    std::string displayId;
    std::int32_t x{0};
    std::int32_t y{0};
    std::uint64_t authorityGeneration{0};
};

struct SeatNavigationState {
    SeatId seatId{0};
    SeatNavigationMode mode{SeatNavigationMode::ControllerFocus};
    std::string displayId;
    std::int32_t pointerX{0};
    std::int32_t pointerY{0};
    std::size_t focusIndex{0};
    std::uint64_t authorityGeneration{0};
};

// Pure launcher navigation state. It consumes only already Seat-scoped pointer
// samples or controller focus steps. It never calls SetCursorPos, ClipCursor,
// ShowCursor, installs hooks, or mutates the Windows system cursor.
class SeatNavigationModel {
public:
    explicit SeatNavigationModel(SeatId seatId);

    bool configureDisplays(std::vector<SeatDisplayRegion> regions,
                           std::uint64_t authorityGeneration,
                           std::string* error = nullptr);
    bool applyPointer(const SeatPointerSample& sample, std::string* error = nullptr);
    bool controllerStep(int direction, std::size_t itemCount, std::string* error = nullptr);

    const SeatNavigationState& state() const noexcept { return state_; }
    const std::vector<SeatDisplayRegion>& displays() const noexcept { return displays_; }

private:
    SeatId seatId_{0};
    SeatNavigationState state_;
    std::vector<SeatDisplayRegion> displays_;
};

std::string_view seatNavigationModeName(SeatNavigationMode mode) noexcept;

} // namespace hydra::seatui
