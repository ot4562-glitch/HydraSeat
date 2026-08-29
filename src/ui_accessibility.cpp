#include "hydra/ui_accessibility.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace hydra::ui {
namespace {

std::uint32_t scale(std::uint32_t logical, std::uint32_t dpi) noexcept {
    const std::uint64_t value = static_cast<std::uint64_t>(logical) * dpi + 95u;
    return static_cast<std::uint32_t>(value / 96u);
}

bool hasIssue(const LayoutAssessment& result, AccessibilityIssue issue) {
    return std::find(result.issues.begin(), result.issues.end(), issue) != result.issues.end();
}

void addIssue(LayoutAssessment& result, AccessibilityIssue issue) {
    if (!hasIssue(result, issue)) result.issues.push_back(issue);
}

bool actionLabelFits(TextId id, Locale locale) noexcept {
    constexpr std::size_t kMaximumCriticalActionCodeUnits = 128u;
    const auto value = text(id, locale);
    return !value.empty() && value.size() <= kMaximumCriticalActionCodeUnits;
}

void appendSafetyActions(const LayoutRequest& request, LayoutAssessment& result) {
    if (request.protectionConfirmationRequired) {
        result.focusOrder.push_back(FocusAction::ProtectionConfirmation);
        if (!actionLabelFits(TextId::ProtectedExperimentConfirmation, request.locale)) {
            addIssue(result, AccessibilityIssue::LocalizedActionTooLong);
        }
    }
    if (request.recoveryActionRequired) {
        result.focusOrder.push_back(FocusAction::Recovery);
        if (!actionLabelFits(TextId::RecoveryAction, request.locale)) {
            addIssue(result, AccessibilityIssue::LocalizedActionTooLong);
        }
    }
}

} // namespace

LayoutAssessment assessLayout(const LayoutRequest& request) {
    LayoutAssessment result;
    if (request.dpi < 72u || request.dpi > 384u) {
        addIssue(result, AccessibilityIssue::InvalidDpi);
        return result;
    }

    std::uint32_t logicalWidth = 0;
    std::uint32_t logicalHeight = 0;
    switch (request.surface) {
        case Surface::ManagementGames:
            logicalWidth = 900u;
            logicalHeight = 600u;
            result.focusOrder = {
                FocusAction::GameList,
                FocusAction::AddExecutable,
                FocusAction::PlayerName,
                FocusAction::AddPlayer,
                FocusAction::PlayerRoster,
                FocusAction::Seat1Player,
                FocusAction::Seat1Game,
                FocusAction::Seat2Player,
                FocusAction::Seat2Game,
                FocusAction::TwoPlayerSetup,
                FocusAction::Play,
            };
            break;
        case Surface::SeatLauncherExpanded:
            logicalWidth = 420u;
            logicalHeight = 360u;
            result.focusOrder = {FocusAction::EndPlaying, FocusAction::Reconnect};
            break;
        case Surface::SeatLauncherCompact:
            logicalWidth = 300u;
            logicalHeight = 90u;
            result.focusOrder = {FocusAction::EndPlaying};
            if (request.protectionConfirmationRequired || request.recoveryActionRequired) {
                addIssue(result, AccessibilityIssue::CriticalActionHidden);
            }
            break;
    }

    result.minimumWidthPx = scale(logicalWidth, request.dpi);
    result.minimumHeightPx = scale(logicalHeight, request.dpi);
    if (request.widthPx < result.minimumWidthPx || request.heightPx < result.minimumHeightPx) {
        addIssue(result, AccessibilityIssue::SurfaceTooSmall);
    }
    if (!request.pointerInput && !request.keyboardInput && !request.controllerInput) {
        addIssue(result, AccessibilityIssue::NoInputModality);
    }

    if (request.surface != Surface::SeatLauncherCompact) {
        appendSafetyActions(request, result);
    }

    const std::array<std::pair<FocusAction, TextId>, 6> criticalLabels{{
        {FocusAction::Play, TextId::Play},
        {FocusAction::EndPlaying, TextId::EndPlaying},
        {FocusAction::Reconnect, TextId::Reconnect},
        {FocusAction::TwoPlayerSetup, TextId::CreateTwoPlayerSetup},
        {FocusAction::ProtectionConfirmation, TextId::ProtectedExperimentConfirmation},
        {FocusAction::Recovery, TextId::RecoveryAction},
    }};
    for (const auto& [action, label] : criticalLabels) {
        if (std::find(result.focusOrder.begin(), result.focusOrder.end(), action) !=
                result.focusOrder.end() &&
            !actionLabelFits(label, request.locale)) {
            addIssue(result, AccessibilityIssue::LocalizedActionTooLong);
        }
    }

    result.usable = result.issues.empty();
    return result;
}

std::string_view focusActionName(FocusAction action) noexcept {
    switch (action) {
        case FocusAction::GameList: return "GameList";
        case FocusAction::AddExecutable: return "AddExecutable";
        case FocusAction::PlayerName: return "PlayerName";
        case FocusAction::AddPlayer: return "AddPlayer";
        case FocusAction::PlayerRoster: return "PlayerRoster";
        case FocusAction::Seat1Player: return "Seat1Player";
        case FocusAction::Seat1Game: return "Seat1Game";
        case FocusAction::Seat2Player: return "Seat2Player";
        case FocusAction::Seat2Game: return "Seat2Game";
        case FocusAction::TwoPlayerSetup: return "TwoPlayerSetup";
        case FocusAction::Play: return "Play";
        case FocusAction::EndPlaying: return "EndPlaying";
        case FocusAction::Reconnect: return "Reconnect";
        case FocusAction::ProtectionConfirmation: return "ProtectionConfirmation";
        case FocusAction::Recovery: return "Recovery";
    }
    return "Unknown";
}

std::string_view accessibilityIssueName(AccessibilityIssue issue) noexcept {
    switch (issue) {
        case AccessibilityIssue::InvalidDpi: return "InvalidDpi";
        case AccessibilityIssue::SurfaceTooSmall: return "SurfaceTooSmall";
        case AccessibilityIssue::NoInputModality: return "NoInputModality";
        case AccessibilityIssue::CriticalActionHidden: return "CriticalActionHidden";
        case AccessibilityIssue::LocalizedActionTooLong: return "LocalizedActionTooLong";
    }
    return "Unknown";
}

} // namespace hydra::ui
