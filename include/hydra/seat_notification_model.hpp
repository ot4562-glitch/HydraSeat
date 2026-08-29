#pragma once

#include "hydra/plan_preflight.hpp"
#include "hydra/seat_launcher_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::seatui {

inline constexpr std::size_t kMaximumSeatNotifications = 32u;

enum class SeatNotificationSeverity : std::uint8_t {
    Information = 0,
    Warning = 1,
    Blocking = 2,
};

enum class SeatNotificationAction : std::uint8_t {
    None = 0,
    Resnapshot = 1,
    OpenSeatSettings = 2,
    ReviewSetup = 3,
    ReviewProtection = 4,
    EndPlaying = 5,
};

struct SeatNotification {
    std::string messageId;
    SeatNotificationSeverity severity{SeatNotificationSeverity::Information};
    SeatNotificationAction action{SeatNotificationAction::None};
    std::string displayText;

    bool operator==(const SeatNotification&) const = default;
};

struct SeatNotificationState {
    SeatId seatId{0};
    std::uint64_t authorityGeneration{0};
    std::uint64_t transitionSequence{0};
    std::vector<SeatNotification> notifications;
};

// Rebuilds a privacy-safe notification list from authoritative phase and stable
// preflight codes. Raw host diagnostics, expert detail, typed text, paths, and
// provider/account values are never copied into the output.
class SeatNotificationModel {
public:
    explicit SeatNotificationModel(SeatId seatId);

    bool apply(const SeatLauncherState& launcher,
               const preflight::Summary* preflight = nullptr,
               std::string* error = nullptr);
    const SeatNotificationState& state() const noexcept { return state_; }

private:
    SeatId seatId_{0};
    SeatNotificationState state_;
};

std::string_view seatNotificationSeverityName(
    SeatNotificationSeverity severity) noexcept;
std::string_view seatNotificationActionName(
    SeatNotificationAction action) noexcept;

} // namespace hydra::seatui
