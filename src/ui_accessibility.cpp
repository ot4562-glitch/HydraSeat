#include "hydra/ui_accessibility.hpp"

#include <algorithm>
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
            logicalWidth = 720u;
            logicalHeight = 500u;
            result.focusOrder = {
                FocusAction::PlayerName,
                FocusAction::AddPlayer,
                FocusAction::HardwareSetup,
                FocusAction::Seat1Player,
                FocusAction::Seat2Player,
                FocusAction::GameList,
                FocusAction::Refresh,
                FocusAction::AddExecutable,
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

    result.usable = result.issues.empty();
    return result;
}

std::string_view focusActionName(FocusAction action) noexcept {
    switch (action) {
        case FocusAction::PlayerName: return "PlayerName";
        case FocusAction::AddPlayer: return "AddPlayer";
        case FocusAction::HardwareSetup: return "HardwareSetup";
        case FocusAction::Seat1Player: return "Seat1Player";
        case FocusAction::Seat2Player: return "Seat2Player";
        case FocusAction::GameList: return "GameList";
        case FocusAction::Refresh: return "Refresh";
        case FocusAction::AddExecutable: return "AddExecutable";
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
