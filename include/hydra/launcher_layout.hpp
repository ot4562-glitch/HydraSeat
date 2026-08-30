#pragma once

#include <cstdint>
#include <string_view>

namespace hydra::ui {

inline constexpr int kLauncherMinimumClientWidthLogical = 640;
inline constexpr int kLauncherMinimumClientHeightLogical = 640;
inline constexpr int kLauncherNarrowThresholdLogical = 780;

struct PixelRect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    constexpr int right() const noexcept { return x + width; }
    constexpr int bottom() const noexcept { return y + height; }
    constexpr bool visible() const noexcept { return width > 0 && height > 0; }
};

struct LauncherThemeMetrics {
    std::uint32_t dpi{96};
    int margin{20};
    int space1{4};
    int space2{8};
    int space3{12};
    int space4{16};
    int space6{24};
    int minimumTarget{44};
    int primaryTarget{52};
    int gameRowHeight{58};
    int controlRadius{9};
    int focusWidth{2};
    int focusInset{3};
    int statusMarker{10};
    int headerHeight{64};
    int heroHeight{148};
    int seatRowHeight{64};
    int launchBarHeight{68};
};

// Win32 measures the current localized text with the actual selected font and DPI.
// Tests may feed deterministic measured extents through the same narrow contract.
// Widths/heights are physical pixels for the current DPI.
struct LauncherTextMeasurements {
    int heroEyebrowWidth{0};
    int heroTitleHeight{0};
    int heroStatusHeight{0};
    int useSeatOneWidth{0};
    int useSeatTwoWidth{0};
    int useBothSeatsWidth{0};
    int configureWidth{0};
    int launchReasonHeight{0};
    int gameRowHeight{0};
};

struct LauncherTextRequirements {
    int heroEyebrowMinimumWidth{0};
    int heroTitleMinimumHeight{0};
    int heroStatusMinimumHeight{0};
    int useSeatOneMinimumWidth{0};
    int useSeatTwoMinimumWidth{0};
    int useBothSeatsMinimumWidth{0};
    int configureMinimumWidth{0};
    int launchReasonMinimumHeight{0};
    int gameRowMinimumHeight{0};
};

struct LauncherPalette {
    // Native Win32 port of the light-only token system used by the user's
    // hufs-mc-web Admin Source UI. Keep the launcher structural: warm canvas,
    // light surfaces, navy hierarchy/focus, bronze only as a short accent, and
    // semantic colors reserved for actual status.
    std::uint32_t canvasIvory{0xF4EFE6u};
    std::uint32_t canvasDeep{0xECE5D8u};
    std::uint32_t surfaceIvory{0xFBF8F1u};
    std::uint32_t surfaceRaised{0xFFFDF9u};
    std::uint32_t surfacePlain{0xFFFFFFu};
    std::uint32_t brandNavy{0x002D56u};
    std::uint32_t navyStrong{0x001D38u};
    std::uint32_t inkNavy{0x101D2Bu};
    std::uint32_t mutedInk{0x4D5964u};
    std::uint32_t faintInk{0x5F6974u};
    std::uint32_t steel{0x2F5470u};
    std::uint32_t bronze{0x806341u};
    std::uint32_t lineSubtle{0xD7D7D4u};
    std::uint32_t lineDefault{0xC3C7C8u};
    std::uint32_t lineStrong{0x9CA8AEu};
    std::uint32_t success{0x2F7D52u};
    std::uint32_t warning{0x8D7150u};
    std::uint32_t danger{0x9A4040u};
    std::uint32_t neutral{0x69727Bu};
};

enum class LauncherButtonRole : std::uint8_t {
    Primary = 0,
    Secondary = 1,
    Quiet = 2,
    Danger = 3,
};

enum class LauncherButtonSurface : std::uint8_t {
    Primary = 0,
    Raised = 1,
    Quiet = 2,
    Danger = 3,
    Disabled = 4,
    System = 5,
};

struct LauncherButtonPresentation {
    LauncherButtonSurface surface{LauncherButtonSurface::Raised};
    bool drawFocusFrame{false};
    bool pressedOffset{false};
    bool useSystemColors{false};

    bool operator==(const LauncherButtonPresentation&) const = default;
};

enum class LauncherGameRowSurface : std::uint8_t {
    Open = 0,
    Selected = 1,
    System = 2,
};

struct LauncherGameRowPresentation {
    LauncherGameRowSurface surface{LauncherGameRowSurface::Open};
    bool drawSelectionEdge{false};
    bool drawFocusFrame{false};
    bool useSystemColors{false};

    bool operator==(const LauncherGameRowPresentation&) const = default;
};

enum class LauncherSeatState : std::uint8_t {
    NotConfigured = 0,
    Ready = 1,
    NeedsAttention = 2,
};

enum class LauncherStatusMarker : std::uint8_t {
    Ring = 0,
    Dot = 1,
    Triangle = 2,
};

struct LauncherSeatPresentation {
    LauncherSeatState state{LauncherSeatState::NotConfigured};
    LauncherStatusMarker marker{LauncherStatusMarker::Ring};
    bool useSystemColors{false};

    bool operator==(const LauncherSeatPresentation&) const = default;
};

struct LauncherLayout {
    bool valid{false};
    bool narrow{false};
    bool seatActionsTwoColumn{false};
    int minimumClientWidth{0};
    int minimumClientHeight{0};
    int gameRowHeight{0};

    PixelRect header;
    PixelRect headerTitle;
    PixelRect headerSubtitle;
    PixelRect setupNavigation;
    PixelRect hero;
    PixelRect heroEyebrow;
    PixelRect heroTitle;
    PixelRect heroStatus;
    PixelRect useSeatOne;
    PixelRect useSeatTwo;
    PixelRect useBothSeats;
    PixelRect configure;
    PixelRect seatsLabel;
    PixelRect seat1Row;
    PixelRect seat1Label;
    PixelRect seat1Player;
    PixelRect seat1Game;
    PixelRect seat1Status;
    PixelRect seat2Row;
    PixelRect seat2Label;
    PixelRect seat2Player;
    PixelRect seat2Game;
    PixelRect seat2Status;
    PixelRect libraryLabel;
    PixelRect gameList;
    PixelRect refresh;
    PixelRect addExecutable;
    PixelRect launchBar;
    PixelRect launchReason;
    PixelRect play;

    PixelRect advancedHeading;
    PixelRect backToGames;
    PixelRect playerNameLabel;
    PixelRect playerName;
    PixelRect addPlayer;
    PixelRect playerRoster;
    PixelRect renamePlayer;
    PixelRect removePlayer;
    PixelRect setupButton;
    PixelRect diagnostics;
    PixelRect privacyHeading;
    PixelRect privacySharing;
    PixelRect privacyRetentionLabel;
    PixelRect privacyRetention;
    PixelRect privacySave;
    PixelRect localResultsHeading;
    PixelRect localResults;
    PixelRect exportLocalResult;
    PixelRect deleteLocalResult;
    PixelRect clearLocalResults;
};

LauncherThemeMetrics launcherThemeMetrics(std::uint32_t dpi) noexcept;
constexpr LauncherPalette launcherPalette() noexcept { return {}; }
LauncherButtonPresentation launcherButtonPresentation(
    LauncherButtonRole role, bool enabled, bool focused, bool pressed,
    bool highContrast) noexcept;
LauncherGameRowPresentation launcherGameRowPresentation(
    bool selected, bool focused, bool highContrast) noexcept;
LauncherSeatPresentation launcherSeatPresentation(
    bool configured, bool ready, bool highContrast) noexcept;
// Deterministic lower bound only; the native renderer still measures the actual
// GDI glyph extent and uses the larger value. This keeps pure locale/DPI tests
// stable without pretending that an English average glyph width is authoritative.
int launcherTextWidthFloor(std::wstring_view text, std::uint32_t dpi) noexcept;
LauncherTextRequirements launcherTextRequirements(
    const LauncherTextMeasurements& measurements, std::uint32_t dpi) noexcept;
// Owner-drawn status controls already paint their shape cue. Remove only a known
// leading legacy marker glyph so the same state is not shown as two markers.
std::wstring_view launcherStatusLabelText(std::wstring_view localizedText) noexcept;
LauncherLayout computeLauncherLayout(
    int clientWidthPx, int clientHeightPx, std::uint32_t dpi,
    const LauncherTextRequirements& textRequirements = {}) noexcept;
bool rectFitsClient(const PixelRect& rect, int width, int height) noexcept;

} // namespace hydra::ui
