#pragma once

#include "hydra/ui_localization.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace hydra::ui {

enum class Surface : std::uint8_t {
    ManagementGames = 0,
    SeatLauncherExpanded = 1,
    SeatLauncherCompact = 2,
};

enum class FocusAction : std::uint8_t {
    GameList = 0,
    AddExecutable,
    PlayerName,
    AddPlayer,
    PlayerRoster,
    Seat1Player,
    Seat1Game,
    Seat2Player,
    Seat2Game,
    TwoPlayerSetup,
    Play,
    EndPlaying,
    Reconnect,
    ProtectionConfirmation,
    Recovery,
};

enum class AccessibilityIssue : std::uint8_t {
    InvalidDpi = 0,
    SurfaceTooSmall,
    NoInputModality,
    CriticalActionHidden,
    LocalizedActionTooLong,
    ViewportOverflow,
    HitTargetTooSmall,
};

struct LayoutRequest {
    Surface surface{Surface::ManagementGames};
    std::uint32_t widthPx{0};
    std::uint32_t heightPx{0};
    std::uint32_t dpi{96};
    Locale locale{Locale::EnglishUnitedStates};
    bool pointerInput{true};
    bool keyboardInput{true};
    bool controllerInput{false};
    bool protectionConfirmationRequired{false};
    bool recoveryActionRequired{false};
};

struct LayoutAssessment {
    bool usable{false};
    std::uint32_t minimumWidthPx{0};
    std::uint32_t minimumHeightPx{0};
    std::vector<FocusAction> focusOrder;
    std::vector<AccessibilityIssue> issues;
};

// Pure readiness check used by both Win32 surfaces. It does not claim that a
// physical DPI/input/screen-reader matrix was exercised; it only guarantees that
// the declared layout contract cannot hide required safety/recovery actions.
LayoutAssessment assessLayout(const LayoutRequest& request);

std::string_view focusActionName(FocusAction action) noexcept;
std::string_view accessibilityIssueName(AccessibilityIssue issue) noexcept;

} // namespace hydra::ui
