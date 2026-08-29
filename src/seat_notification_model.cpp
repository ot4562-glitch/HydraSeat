#include "hydra/seat_notification_model.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace hydra::seatui {
namespace {

SeatNotification notificationForCode(std::string_view code) {
    if (code == "plan.MissingDisplay" || code == "plan.MissingKeyboard" ||
        code == "plan.MissingMouse" || code == "plan.MissingController" ||
        code == "plan.MissingAudioOutput" ||
        code == "plan.DuplicateExclusiveHardware") {
        return {"seat.requirements.devices", SeatNotificationSeverity::Blocking,
                SeatNotificationAction::OpenSeatSettings,
                "This Seat needs a device setting before the game can start."};
    }
    if (code == "plan.MissingRequirement" || code == "plan.DuplicateRequirement" ||
        code == "plan.StaleCompatibility" || code == "plan.MissingCapability") {
        return {"seat.requirements.review", SeatNotificationSeverity::Blocking,
                SeatNotificationAction::ReviewSetup,
                "Game requirements need review before starting."};
    }
    if (code == "plan.MissingTwoPlayerSetup" ||
        code == "plan.InvalidTwoPlayerSetup" ||
        code.rfind("mutation.", 0u) == 0u) {
        return {"seat.setup.review", SeatNotificationSeverity::Blocking,
                SeatNotificationAction::ReviewSetup,
                "The two-player setup needs review."};
    }
    if (code == "plan.HighRiskApprovalRequired" || code == "risk.protected") {
        return {"seat.protection.review", SeatNotificationSeverity::Warning,
                SeatNotificationAction::ReviewProtection,
                "This Protected / Experimental setup needs acknowledgement."};
    }
    if (code == "plan.ProviderUnavailable" || code == "plan.MissingProvider" ||
        code == "plan.ProviderLaunchRejected") {
        return {"seat.provider.unavailable", SeatNotificationSeverity::Blocking,
                SeatNotificationAction::Resnapshot,
                "The game provider is unavailable. Refresh and try again."};
    }
    if (code == "plan.AmbiguousAccountReference") {
        return {"seat.player.account", SeatNotificationSeverity::Blocking,
                SeatNotificationAction::ReviewSetup,
                "Choose the Player account to use for this game."};
    }
    if (code.rfind("requires.", 0u) == 0u || code == "setup.two-player") {
        return {"seat.preflight.information", SeatNotificationSeverity::Information,
                SeatNotificationAction::None,
                "The selected game has setup details to review."};
    }
    return {"seat.preflight.review", SeatNotificationSeverity::Blocking,
            SeatNotificationAction::ReviewSetup,
            "The selected game cannot start until its setup is reviewed."};
}

void appendUnique(std::vector<SeatNotification>& values,
                  std::set<std::string>& ids,
                  SeatNotification value) {
    if (values.size() >= kMaximumSeatNotifications ||
        !ids.insert(value.messageId).second) return;
    values.push_back(std::move(value));
}

} // namespace

SeatNotificationModel::SeatNotificationModel(SeatId seatId) : seatId_(seatId) {
    state_.seatId = seatId;
}

bool SeatNotificationModel::apply(const SeatLauncherState& launcher,
                                  const preflight::Summary* preflight,
                                  std::string* error) {
    if (seatId_ == 0 || launcher.seatId != seatId_) {
        if (error != nullptr) *error = "notification input does not match the assigned Seat";
        return false;
    }
    if (launcher.phase != SeatLauncherPhase::Disconnected &&
        (launcher.authorityGeneration < state_.authorityGeneration ||
         launcher.transitionSequence < state_.transitionSequence)) {
        if (error != nullptr) *error = "stale notification source was rejected";
        return false;
    }

    SeatNotificationState next;
    next.seatId = seatId_;
    next.authorityGeneration = launcher.phase == SeatLauncherPhase::Disconnected
        ? 0u : launcher.authorityGeneration;
    next.transitionSequence = launcher.phase == SeatLauncherPhase::Disconnected
        ? 0u : launcher.transitionSequence;
    std::set<std::string> ids;

    switch (launcher.phase) {
        case SeatLauncherPhase::Disconnected:
            appendUnique(next.notifications, ids,
                {"seat.host.disconnected", SeatNotificationSeverity::Blocking,
                 SeatNotificationAction::Resnapshot,
                 "HydraSeat is disconnected. Reconnect to refresh this Seat."});
            break;
        case SeatLauncherPhase::Starting:
            appendUnique(next.notifications, ids,
                {"seat.game.starting", SeatNotificationSeverity::Information,
                 SeatNotificationAction::None, "The game is starting."});
            break;
        case SeatLauncherPhase::Warning:
            appendUnique(next.notifications, ids,
                {"seat.game.warning", SeatNotificationSeverity::Warning,
                 SeatNotificationAction::EndPlaying,
                 "The game needs attention. End Playing if it does not recover."});
            break;
        case SeatLauncherPhase::Recovery:
            appendUnique(next.notifications, ids,
                {"seat.game.recovery", SeatNotificationSeverity::Blocking,
                 SeatNotificationAction::Resnapshot,
                 "This Seat needs recovery. Refresh status before taking another action."});
            break;
        case SeatLauncherPhase::Idle:
        case SeatLauncherPhase::Planning:
        case SeatLauncherPhase::Playing:
        case SeatLauncherPhase::Stopping:
            break;
    }

    if (preflight != nullptr && !preflight->canActivate) {
        for (const auto& message : preflight->messages) {
            if (message.seatId != 0u && message.seatId != seatId_) continue;
            appendUnique(next.notifications, ids, notificationForCode(message.code));
        }
    } else if (preflight != nullptr) {
        for (const auto& message : preflight->messages) {
            if (message.seatId != 0u && message.seatId != seatId_) continue;
            if (message.severity == preflight::Severity::Blocking) continue;
            appendUnique(next.notifications, ids, notificationForCode(message.code));
        }
    }

    std::stable_sort(next.notifications.begin(), next.notifications.end(),
                     [](const auto& left, const auto& right) {
        if (left.severity != right.severity) {
            return static_cast<std::uint8_t>(left.severity) >
                   static_cast<std::uint8_t>(right.severity);
        }
        return left.messageId < right.messageId;
    });
    state_ = std::move(next);
    if (error != nullptr) error->clear();
    return true;
}

std::string_view seatNotificationSeverityName(
    SeatNotificationSeverity severity) noexcept {
    switch (severity) {
        case SeatNotificationSeverity::Information: return "Information";
        case SeatNotificationSeverity::Warning: return "Warning";
        case SeatNotificationSeverity::Blocking: return "Blocking";
    }
    return "Unknown";
}

std::string_view seatNotificationActionName(
    SeatNotificationAction action) noexcept {
    switch (action) {
        case SeatNotificationAction::None: return "None";
        case SeatNotificationAction::Resnapshot: return "Resnapshot";
        case SeatNotificationAction::OpenSeatSettings: return "OpenSeatSettings";
        case SeatNotificationAction::ReviewSetup: return "ReviewSetup";
        case SeatNotificationAction::ReviewProtection: return "ReviewProtection";
        case SeatNotificationAction::EndPlaying: return "EndPlaying";
    }
    return "Unknown";
}

} // namespace hydra::seatui
