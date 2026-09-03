#include "hydra/launcher_layout.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace hydra::ui {
namespace {

int scaled(int logical, std::uint32_t dpi) noexcept {
    const auto value = static_cast<std::int64_t>(logical) * dpi + 48;
    return static_cast<int>(value / 96);
}

PixelRect inset(PixelRect value, int amount) noexcept {
    value.x += amount;
    value.y += amount;
    value.width = std::max(0, value.width - amount * 2);
    value.height = std::max(0, value.height - amount * 2);
    return value;
}

int nonNegative(int value) noexcept {
    return std::max(0, value);
}

int saturatedAdd(int left, int right) noexcept {
    const auto sum = static_cast<std::int64_t>(nonNegative(left)) + nonNegative(right);
    return static_cast<int>(std::min<std::int64_t>(sum, std::numeric_limits<int>::max()));
}

int logicalGlyphWidth(wchar_t ch) noexcept {
    if (ch == L' ' || ch == L'\t') return 4;
    if (ch >= 0x2e80 || (ch >= 0x1100 && ch <= 0x11ff) ||
        (ch >= 0xac00 && ch <= 0xd7af)) {
        return 16;
    }
    if (ch >= 0x80) return 12;
    if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
        (ch >= L'0' && ch <= L'9')) {
        return 8;
    }
    return 6;
}

} // namespace

LauncherThemeMetrics launcherThemeMetrics(std::uint32_t dpi) noexcept {
    LauncherThemeMetrics value;
    value.dpi = dpi;
    value.margin = scaled(20, dpi);
    value.space1 = scaled(4, dpi);
    value.space2 = scaled(8, dpi);
    value.space3 = scaled(12, dpi);
    value.space4 = scaled(16, dpi);
    value.space6 = scaled(24, dpi);
    value.minimumTarget = scaled(44, dpi);
    value.primaryTarget = scaled(52, dpi);
    value.gameRowHeight = scaled(58, dpi);
    value.controlRadius = scaled(9, dpi);
    value.focusWidth = scaled(2, dpi);
    value.focusInset = scaled(3, dpi);
    value.statusMarker = scaled(10, dpi);
    value.headerHeight = scaled(64, dpi);
    value.heroHeight = scaled(148, dpi);
    value.seatRowHeight = scaled(64, dpi);
    value.launchBarHeight = scaled(68, dpi);
    return value;
}

LauncherButtonPresentation launcherButtonPresentation(
    LauncherButtonRole role, bool enabled, bool focused, bool pressed,
    bool highContrast) noexcept {
    LauncherButtonPresentation out;
    out.drawFocusFrame = focused;
    out.pressedOffset = enabled && pressed;
    out.useSystemColors = highContrast;
    if (highContrast) {
        out.surface = LauncherButtonSurface::System;
        return out;
    }
    if (!enabled) {
        out.surface = LauncherButtonSurface::Disabled;
        return out;
    }
    switch (role) {
    case LauncherButtonRole::Primary:
        out.surface = LauncherButtonSurface::Primary;
        break;
    case LauncherButtonRole::Secondary:
        out.surface = LauncherButtonSurface::Raised;
        break;
    case LauncherButtonRole::Quiet:
        out.surface = LauncherButtonSurface::Quiet;
        break;
    case LauncherButtonRole::Danger:
        out.surface = LauncherButtonSurface::Danger;
        break;
    }
    return out;
}

LauncherGameRowPresentation launcherGameRowPresentation(
    bool selected, bool focused, bool highContrast) noexcept {
    LauncherGameRowPresentation out;
    out.drawSelectionEdge = selected;
    out.drawFocusFrame = focused;
    out.useSystemColors = highContrast;
    out.surface = highContrast
        ? LauncherGameRowSurface::System
        : (selected ? LauncherGameRowSurface::Selected
                    : LauncherGameRowSurface::Open);
    return out;
}

LauncherSeatPresentation launcherSeatPresentation(
    bool configured, bool ready, bool highContrast) noexcept {
    LauncherSeatPresentation out;
    out.useSystemColors = highContrast;
    if (!configured) {
        out.state = LauncherSeatState::NotConfigured;
        out.marker = LauncherStatusMarker::Ring;
    } else if (ready) {
        out.state = LauncherSeatState::Ready;
        out.marker = LauncherStatusMarker::Dot;
    } else {
        out.state = LauncherSeatState::NeedsAttention;
        out.marker = LauncherStatusMarker::Triangle;
    }
    return out;
}

int launcherTextWidthFloor(std::wstring_view text, std::uint32_t dpi) noexcept {
    if (dpi < 72u || dpi > 384u || text.empty()) return 0;
    std::int64_t currentLineWidth = 0;
    std::int64_t maximumLineWidth = 0;
    for (const wchar_t ch : text) {
        if (ch == L'\r') continue;
        if (ch == L'\n') {
            maximumLineWidth = std::max(maximumLineWidth, currentLineWidth);
            currentLineWidth = 0;
            continue;
        }
        currentLineWidth += logicalGlyphWidth(ch);
        if (currentLineWidth >= std::numeric_limits<int>::max()) {
            currentLineWidth = std::numeric_limits<int>::max();
        }
    }
    maximumLineWidth = std::max(maximumLineWidth, currentLineWidth);
    const auto pixels = maximumLineWidth * dpi + 48;
    return static_cast<int>(std::min<std::int64_t>(
        pixels / 96, std::numeric_limits<int>::max()));
}

LauncherTextRequirements launcherTextRequirements(
    const LauncherTextMeasurements& measurements, std::uint32_t dpi) noexcept {
    LauncherTextRequirements out;
    if (dpi < 72u || dpi > 384u) return out;
    const auto metrics = launcherThemeMetrics(dpi);
    const int horizontalButtonPadding = metrics.space4 * 2;
    const int verticalButtonPadding = metrics.space2 * 2;
    const auto buttonWidth = [&](int measuredWidth) noexcept {
        return std::max(metrics.minimumTarget,
                        saturatedAdd(measuredWidth, horizontalButtonPadding));
    };
    const auto buttonHeight = [&](int measuredHeight) noexcept {
        return std::max(metrics.minimumTarget,
                        saturatedAdd(measuredHeight, verticalButtonPadding));
    };

    out.heroEyebrowMinimumWidth = saturatedAdd(
        measurements.heroEyebrowWidth, metrics.space2);
    out.heroTitleMinimumHeight = std::max(
        scaled(38, dpi), nonNegative(measurements.heroTitleHeight));
    out.heroStatusMinimumHeight = std::max(
        scaled(24, dpi), nonNegative(measurements.heroStatusHeight));
    out.sectionLabelMinimumHeight = std::max(
        scaled(24, dpi), nonNegative(measurements.sectionLabelHeight));
    out.playerLabelMinimumWidth = saturatedAdd(
        nonNegative(measurements.playerLabelWidth), metrics.space2);
    out.playerLabelMinimumHeight = std::max(
        scaled(20, dpi), nonNegative(measurements.playerLabelHeight));
    out.playerStatusMinimumWidth = saturatedAdd(
        nonNegative(measurements.playerStatusWidth),
        saturatedAdd(metrics.statusMarker, metrics.space2 + metrics.space1));
    out.playerStatusMinimumHeight = std::max(
        scaled(20, dpi), nonNegative(measurements.playerStatusHeight));

    out.configureMinimumWidth = buttonWidth(measurements.configureWidth);
    out.configureMinimumHeight = buttonHeight(measurements.configureHeight);

    out.refreshMinimumWidth = buttonWidth(measurements.refreshWidth);
    out.refreshMinimumHeight = buttonHeight(measurements.refreshHeight);
    out.addExecutableMinimumWidth = buttonWidth(measurements.addExecutableWidth);
    out.addExecutableMinimumHeight = buttonHeight(measurements.addExecutableHeight);
    out.playMinimumWidth = buttonWidth(measurements.playWidth);
    out.playMinimumHeight = buttonHeight(measurements.playHeight);

    out.addPlayerMinimumWidth = buttonWidth(measurements.addPlayerWidth);
    out.addPlayerMinimumHeight = buttonHeight(measurements.addPlayerHeight);

    out.launchReasonMinimumHeight = std::max(
        scaled(40, dpi), nonNegative(measurements.launchReasonHeight));
    out.gameRowMinimumHeight = std::max(
        metrics.gameRowHeight, nonNegative(measurements.gameRowHeight));
    return out;
}

std::wstring_view launcherStatusLabelText(std::wstring_view localizedText) noexcept {
    while (!localizedText.empty() &&
           (localizedText.front() == L' ' || localizedText.front() == L'\t')) {
        localizedText.remove_prefix(1);
    }
    if (!localizedText.empty()) {
        const wchar_t marker = localizedText.front();
        if (marker == L'○' || marker == L'●' || marker == L'△' || marker == L'▲') {
            localizedText.remove_prefix(1);
            while (!localizedText.empty() &&
                   (localizedText.front() == L' ' || localizedText.front() == L'\t')) {
                localizedText.remove_prefix(1);
            }
        }
    }
    return localizedText;
}

bool rectFitsClient(const PixelRect& rect, int width, int height) noexcept {
    return rect.visible() && rect.x >= 0 && rect.y >= 0 &&
           rect.right() <= width && rect.bottom() <= height;
}

LauncherLayout computeLauncherLayout(
    int clientWidthPx, int clientHeightPx, std::uint32_t dpi,
    const LauncherTextRequirements& textRequirements) noexcept {
    LauncherLayout out;
    if (dpi < 72u || dpi > 384u || clientWidthPx <= 0 || clientHeightPx <= 0) return out;

    const auto m = launcherThemeMetrics(dpi);
    out.minimumClientWidth = scaled(kLauncherMinimumClientWidthLogical, dpi);
    out.minimumClientHeight = scaled(kLauncherMinimumClientHeightLogical, dpi);
    out.gameRowHeight = std::max(m.gameRowHeight,
                                 nonNegative(textRequirements.gameRowMinimumHeight));
    if (clientWidthPx < out.minimumClientWidth ||
        clientHeightPx < out.minimumClientHeight) return out;

    out.narrow = clientWidthPx < scaled(kLauncherNarrowThresholdLogical, dpi);
    const int contentWidth = clientWidthPx - m.margin * 2;
    out.header = {0, 0, clientWidthPx, m.headerHeight};
    out.headerTitle = {m.margin, scaled(8, dpi), scaled(250, dpi), scaled(30, dpi)};
    out.headerSubtitle = {m.margin, scaled(38, dpi), scaled(320, dpi), scaled(18, dpi)};

    const int heroEyebrowHeight = scaled(22, dpi);
    const int heroTitleHeight = std::max(
        scaled(38, dpi), nonNegative(textRequirements.heroTitleMinimumHeight));
    const int heroStatusHeight = std::max(
        scaled(24, dpi), nonNegative(textRequirements.heroStatusMinimumHeight));
    const int heroTextHeight = saturatedAdd(
        saturatedAdd(heroEyebrowHeight, m.space1),
        saturatedAdd(heroTitleHeight, saturatedAdd(m.space1, heroStatusHeight)));
    const int configureHeight = std::max(
        m.minimumTarget, nonNegative(textRequirements.configureMinimumHeight));
    const int playerRowHeight = std::max(
        m.minimumTarget, nonNegative(textRequirements.addPlayerMinimumHeight));
    const int actionStackHeight = saturatedAdd(
        playerRowHeight, saturatedAdd(m.space2, configureHeight));
    const int heroHeight = std::max(
        m.heroHeight, saturatedAdd(m.space6 * 2, std::max(heroTextHeight, actionStackHeight)));
    int y = out.header.bottom() + m.space4;
    out.hero = {m.margin, y, contentWidth, heroHeight};
    const auto heroInner = inset(out.hero, m.space6);

    const int heroTextMinimum = std::max(
        scaled(out.narrow ? 180 : 220, dpi),
        nonNegative(textRequirements.heroEyebrowMinimumWidth));
    const int heroActionAvailable = std::max(
        0, heroInner.width - heroTextMinimum - m.space4);
    const int heroActionMinimum = std::max(
        nonNegative(textRequirements.configureMinimumWidth),
        saturatedAdd(scaled(76, dpi), saturatedAdd(m.minimumTarget * 2, m.space2 * 2)));
    const int heroActionWidth = std::min(
        heroActionAvailable,
        std::max(heroActionMinimum, scaled(out.narrow ? 320 : 390, dpi)));
    if (heroActionWidth <= 0) return out;
    const int heroTextWidth = heroInner.width - heroActionWidth - m.space4;
    if (heroTextWidth <= 0) return out;

    out.heroEyebrow = {heroInner.x, heroInner.y, heroTextWidth, heroEyebrowHeight};
    out.heroTitle = {heroInner.x, out.heroEyebrow.bottom() + m.space1,
                     heroTextWidth, heroTitleHeight};
    out.heroStatus = {heroInner.x, out.heroTitle.bottom() + m.space1,
                      heroTextWidth, heroStatusHeight};

    const int heroActionX = heroInner.right() - heroActionWidth;
    const int playerLabelWidth = scaled(76, dpi);
    const int requestedAddWidth = std::max(
        scaled(132, dpi), nonNegative(textRequirements.addPlayerMinimumWidth));
    const int maximumAddWidth = std::max(
        m.minimumTarget,
        heroActionWidth - playerLabelWidth - m.space2 * 2 - m.minimumTarget);
    const int addWidth = std::min(requestedAddWidth, maximumAddWidth);
    const int nameWidth = std::max(
        m.minimumTarget,
        heroActionWidth - playerLabelWidth - addWidth - m.space2 * 2);
    out.playerNameLabel = {heroActionX, heroInner.y, playerLabelWidth, playerRowHeight};
    out.playerName = {out.playerNameLabel.right() + m.space2, heroInner.y,
                      nameWidth, playerRowHeight};
    out.addPlayer = {out.playerName.right() + m.space2, heroInner.y,
                     heroInner.right() - (out.playerName.right() + m.space2),
                     playerRowHeight};
    out.configure = {heroActionX, heroInner.y + playerRowHeight + m.space2,
                     heroActionWidth, configureHeight};

    y = out.hero.bottom() + m.space3;
    const int sectionLabelHeight = std::max(
        scaled(24, dpi), nonNegative(textRequirements.sectionLabelMinimumHeight));
    out.seatsLabel = {m.margin, y, contentWidth, sectionLabelHeight};
    y = out.seatsLabel.bottom() + m.space1;
    const int playerLabelHeight = std::max(
        scaled(20, dpi), nonNegative(textRequirements.playerLabelMinimumHeight));
    const int playerStatusHeight = std::max(
        m.minimumTarget, nonNegative(textRequirements.playerStatusMinimumHeight));
    const int seatContentHeight = std::max({m.minimumTarget, playerLabelHeight,
                                            playerStatusHeight});
    const int seatRowHeight = std::max(
        m.seatRowHeight, saturatedAdd(seatContentHeight, m.space2));
    out.seat1Row = {m.margin, y, contentWidth, seatRowHeight};
    y = out.seat1Row.bottom();
    out.seat2Row = {m.margin, y, contentWidth, seatRowHeight};

    const int rowPadding = m.space3;
    const int labelWidth = std::max(
        scaled(out.narrow ? 88 : 112, dpi),
        nonNegative(textRequirements.playerLabelMinimumWidth));
    const int statusWidth = std::max(
        scaled(out.narrow ? 140 : 180, dpi),
        nonNegative(textRequirements.playerStatusMinimumWidth));
    const int fieldGap = m.space2;
    const int playerWidth = contentWidth - rowPadding * 2 - labelWidth - statusWidth -
                            fieldGap * 2;
    const int rowOffset = (seatRowHeight - m.minimumTarget) / 2;
    const auto assignRow = [&](const PixelRect& row, PixelRect& label,
                               PixelRect& player, PixelRect& status) {
        int x = row.x + rowPadding;
        label = {x, row.y + (row.height - playerLabelHeight) / 2,
                 labelWidth, playerLabelHeight};
        x += labelWidth + fieldGap;
        player = {x, row.y + rowOffset, playerWidth, m.minimumTarget};
        x += playerWidth + fieldGap;
        status = {x, row.y + rowOffset, statusWidth, m.minimumTarget};
    };
    assignRow(out.seat1Row, out.seat1Label, out.seat1Player, out.seat1Status);
    assignRow(out.seat2Row, out.seat2Label, out.seat2Player, out.seat2Status);

    const int launchReasonHeight = std::max(
        scaled(40, dpi), nonNegative(textRequirements.launchReasonMinimumHeight));
    const int playWidth = std::max(
        scaled(170, dpi), nonNegative(textRequirements.playMinimumWidth));
    const int playHeight = std::max(
        m.primaryTarget, nonNegative(textRequirements.playMinimumHeight));
    const int launchBarHeight = std::max(
        m.launchBarHeight,
        saturatedAdd(std::max(launchReasonHeight, playHeight), m.space2 * 2));
    out.launchBar = {0, clientHeightPx - launchBarHeight, clientWidthPx, launchBarHeight};
    out.play = {clientWidthPx - m.margin - playWidth,
                out.launchBar.y + (launchBarHeight - playHeight) / 2,
                playWidth, playHeight};
    out.launchReason = {m.margin,
                        out.launchBar.y + (launchBarHeight - launchReasonHeight) / 2,
                        out.play.x - m.margin - m.space4, launchReasonHeight};

    y = out.seat2Row.bottom() + m.space3;
    out.libraryLabel = {m.margin, y, contentWidth, sectionLabelHeight};
    y = out.libraryLabel.bottom() + m.space2;
    const int actionWidth = std::max({
        scaled(out.narrow ? 132 : 150, dpi),
        nonNegative(textRequirements.refreshMinimumWidth),
        nonNegative(textRequirements.addExecutableMinimumWidth)});
    const int refreshHeight = std::max(
        m.minimumTarget, nonNegative(textRequirements.refreshMinimumHeight));
    const int addExecutableHeight = std::max(
        m.minimumTarget, nonNegative(textRequirements.addExecutableMinimumHeight));
    out.gameList = {m.margin, y, contentWidth - actionWidth - m.space3,
                    std::max(0, out.launchBar.y - m.space3 - y)};
    out.refresh = {out.gameList.right() + m.space3, y, actionWidth, refreshHeight};
    out.addExecutable = {out.refresh.x, out.refresh.bottom() + m.space2,
                         actionWidth, addExecutableHeight};

    const bool measuredTextFits =
        out.heroEyebrow.width >= nonNegative(textRequirements.heroEyebrowMinimumWidth) &&
        out.heroTitle.height >= nonNegative(textRequirements.heroTitleMinimumHeight) &&
        out.heroStatus.height >= nonNegative(textRequirements.heroStatusMinimumHeight) &&
        out.configure.width >= nonNegative(textRequirements.configureMinimumWidth) &&
        out.configure.height >= nonNegative(textRequirements.configureMinimumHeight) &&
        out.addPlayer.width >= nonNegative(textRequirements.addPlayerMinimumWidth) &&
        out.addPlayer.height >= nonNegative(textRequirements.addPlayerMinimumHeight) &&
        out.refresh.width >= nonNegative(textRequirements.refreshMinimumWidth) &&
        out.refresh.height >= nonNegative(textRequirements.refreshMinimumHeight) &&
        out.addExecutable.width >= nonNegative(textRequirements.addExecutableMinimumWidth) &&
        out.addExecutable.height >= nonNegative(textRequirements.addExecutableMinimumHeight) &&
        out.play.width >= nonNegative(textRequirements.playMinimumWidth) &&
        out.play.height >= nonNegative(textRequirements.playMinimumHeight) &&
        out.launchReason.height >= nonNegative(textRequirements.launchReasonMinimumHeight) &&
        out.gameRowHeight >= nonNegative(textRequirements.gameRowMinimumHeight);
    out.valid = measuredTextFits &&
                rectFitsClient(out.hero, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.heroEyebrow, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.heroTitle, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.heroStatus, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.playerNameLabel, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.playerName, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.addPlayer, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.configure, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat1Player, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat1Status, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat2Player, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat2Status, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.gameList, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.refresh, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.addExecutable, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.launchReason, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.play, clientWidthPx, clientHeightPx) &&
                out.gameList.height >= m.minimumTarget;
    return out;
}

} // namespace hydra::ui
