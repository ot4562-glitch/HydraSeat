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
    std::int64_t logicalWidth = 0;
    for (const wchar_t ch : text) {
        logicalWidth += logicalGlyphWidth(ch);
        if (logicalWidth >= std::numeric_limits<int>::max()) {
            logicalWidth = std::numeric_limits<int>::max();
            break;
        }
    }
    const auto pixels = logicalWidth * dpi + 48;
    return static_cast<int>(std::min<std::int64_t>(
        pixels / 96, std::numeric_limits<int>::max()));
}

LauncherTextRequirements launcherTextRequirements(
    const LauncherTextMeasurements& measurements, std::uint32_t dpi) noexcept {
    LauncherTextRequirements out;
    if (dpi < 72u || dpi > 384u) return out;
    const auto metrics = launcherThemeMetrics(dpi);
    const int horizontalButtonPadding = metrics.space4 * 2;
    out.heroEyebrowMinimumWidth = saturatedAdd(
        measurements.heroEyebrowWidth, metrics.space2);
    out.heroTitleMinimumHeight = std::max(
        scaled(38, dpi), nonNegative(measurements.heroTitleHeight));
    out.heroStatusMinimumHeight = std::max(
        scaled(24, dpi), nonNegative(measurements.heroStatusHeight));
    out.useSeatOneMinimumWidth = std::max(
        metrics.minimumTarget,
        saturatedAdd(measurements.useSeatOneWidth, horizontalButtonPadding));
    out.useSeatTwoMinimumWidth = std::max(
        metrics.minimumTarget,
        saturatedAdd(measurements.useSeatTwoWidth, horizontalButtonPadding));
    out.useBothSeatsMinimumWidth = std::max(
        metrics.minimumTarget,
        saturatedAdd(measurements.useBothSeatsWidth, horizontalButtonPadding));
    out.configureMinimumWidth = std::max(
        metrics.minimumTarget,
        saturatedAdd(measurements.configureWidth, horizontalButtonPadding));
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
    if (dpi < 72u || dpi > 384u || clientWidthPx <= 0 || clientHeightPx <= 0) {
        return out;
    }
    const auto m = launcherThemeMetrics(dpi);
    out.minimumClientWidth = scaled(kLauncherMinimumClientWidthLogical, dpi);
    out.minimumClientHeight = scaled(kLauncherMinimumClientHeightLogical, dpi);
    out.gameRowHeight = std::max(
        m.gameRowHeight, nonNegative(textRequirements.gameRowMinimumHeight));
    if (clientWidthPx < out.minimumClientWidth ||
        clientHeightPx < out.minimumClientHeight) {
        return out;
    }
    out.narrow = clientWidthPx < scaled(kLauncherNarrowThresholdLogical, dpi);

    const int contentWidth = clientWidthPx - m.margin * 2;
    int y = 0;
    out.header = {0, 0, clientWidthPx, m.headerHeight};
    out.headerTitle = {m.margin, scaled(8, dpi), scaled(250, dpi), scaled(30, dpi)};
    out.headerSubtitle = {m.margin, scaled(38, dpi), scaled(320, dpi), scaled(18, dpi)};
    out.setupNavigation = {clientWidthPx - m.margin - scaled(190, dpi),
                           scaled(10, dpi), scaled(190, dpi), m.minimumTarget};

    y = out.header.bottom() + m.space4;
    const int heroEyebrowHeight = scaled(22, dpi);
    const int heroTitleHeight = std::max(
        scaled(38, dpi), nonNegative(textRequirements.heroTitleMinimumHeight));
    const int heroStatusMinimumHeight = std::max(
        scaled(24, dpi), nonNegative(textRequirements.heroStatusMinimumHeight));
    const int heroTextHeight = saturatedAdd(
        saturatedAdd(heroEyebrowHeight, m.space1),
        saturatedAdd(heroTitleHeight,
                     saturatedAdd(m.space1, heroStatusMinimumHeight)));
    const int requiredHeroHeight = saturatedAdd(m.space6 * 2, heroTextHeight);
    out.hero = {m.margin, y, contentWidth, std::max(m.heroHeight, requiredHeroHeight)};
    const auto heroInner = inset(out.hero, m.space6);
    const int heroActionGap = m.space2;
    const int seatOneMinimum = std::max(
        m.minimumTarget, nonNegative(textRequirements.useSeatOneMinimumWidth));
    const int seatTwoMinimum = std::max(
        m.minimumTarget, nonNegative(textRequirements.useSeatTwoMinimumWidth));
    const int bothMinimum = std::max(
        m.minimumTarget, nonNegative(textRequirements.useBothSeatsMinimumWidth));
    const int configureMinimum = std::max(
        m.minimumTarget, nonNegative(textRequirements.configureMinimumWidth));
    const int requiredSeatButtons = saturatedAdd(
        saturatedAdd(seatOneMinimum, seatTwoMinimum), bothMinimum);
    const int threeColumnRequired = saturatedAdd(
        requiredSeatButtons, heroActionGap * 2);
    const int leftColumnRequired = std::max(seatOneMinimum, bothMinimum);
    const int rightColumnRequired = std::max(seatTwoMinimum, configureMinimum);
    const int twoColumnRequired = saturatedAdd(
        saturatedAdd(leftColumnRequired, rightColumnRequired), heroActionGap);
    const int heroTextMinimum = std::max(
        scaled(out.narrow ? 180 : 220, dpi),
        nonNegative(textRequirements.heroEyebrowMinimumWidth));
    const int heroActionAvailable = std::max(
        0, heroInner.width - heroTextMinimum - m.space4);
    out.seatActionsTwoColumn = out.narrow || threeColumnRequired > heroActionAvailable;
    const int requiredActionWidth = out.seatActionsTwoColumn
        ? twoColumnRequired : std::max(threeColumnRequired, configureMinimum);
    const int preferredActionWidth = scaled(out.seatActionsTwoColumn ? 340 : 390, dpi);
    const int heroActionWidth = std::min(
        heroActionAvailable, std::max(requiredActionWidth, preferredActionWidth));
    if (heroActionWidth <= 0) return out;

    const int heroTextWidth = heroInner.width - heroActionWidth - m.space4;
    if (heroTextWidth <= 0) return out;
    out.heroEyebrow = {heroInner.x, heroInner.y, heroTextWidth, heroEyebrowHeight};
    out.heroTitle = {heroInner.x, out.heroEyebrow.bottom() + m.space1,
                     heroTextWidth, heroTitleHeight};
    const int heroStatusY = out.heroTitle.bottom() + m.space1;
    out.heroStatus = {heroInner.x, heroStatusY, heroTextWidth,
                      std::max(0, heroInner.bottom() - heroStatusY)};

    const int heroActionX = out.hero.right() - m.space6 - heroActionWidth;
    if (out.seatActionsTwoColumn) {
        const int availableColumns = std::max(0, heroActionWidth - heroActionGap);
        int leftWidth = leftColumnRequired;
        int rightWidth = rightColumnRequired;
        const int combinedColumnWidth = saturatedAdd(leftWidth, rightWidth);
        if (combinedColumnWidth <= availableColumns) {
            const int extra = availableColumns - combinedColumnWidth;
            leftWidth += extra / 2;
            rightWidth += extra - extra / 2;
        } else {
            leftWidth = availableColumns / 2;
            rightWidth = availableColumns - leftWidth;
        }
        const int secondColumnX = heroActionX + leftWidth + heroActionGap;
        const int secondRowY = heroInner.y + m.minimumTarget + m.space2;
        out.useSeatOne = {heroActionX, heroInner.y, leftWidth, m.minimumTarget};
        out.useSeatTwo = {secondColumnX, heroInner.y, rightWidth, m.minimumTarget};
        out.useBothSeats = {heroActionX, secondRowY, leftWidth, m.minimumTarget};
        out.configure = {secondColumnX, secondRowY, rightWidth, m.minimumTarget};
    } else {
        const int availableButtons = std::max(0, heroActionWidth - heroActionGap * 2);
        int seatOneWidth = seatOneMinimum;
        int seatTwoWidth = seatTwoMinimum;
        int bothWidth = bothMinimum;
        if (requiredSeatButtons <= availableButtons) {
            const int extra = availableButtons - requiredSeatButtons;
            seatOneWidth += extra / 3;
            seatTwoWidth += extra / 3;
            bothWidth += extra - (extra / 3) * 2;
        } else {
            seatOneWidth = availableButtons / 3;
            seatTwoWidth = availableButtons / 3;
            bothWidth = availableButtons - seatOneWidth - seatTwoWidth;
        }
        out.useSeatOne = {heroActionX, heroInner.y, seatOneWidth, m.minimumTarget};
        out.useSeatTwo = {out.useSeatOne.right() + heroActionGap, heroInner.y,
                          seatTwoWidth, m.minimumTarget};
        out.useBothSeats = {out.useSeatTwo.right() + heroActionGap, heroInner.y,
                            bothWidth, m.minimumTarget};
        out.configure = {heroActionX, out.useSeatOne.bottom() + m.space2,
                         heroActionWidth, m.minimumTarget};
    }

    y = out.hero.bottom() + m.space3;
    out.seatsLabel = {m.margin, y, contentWidth, scaled(22, dpi)};
    y = out.seatsLabel.bottom() + m.space1;
    out.seat1Row = {m.margin, y, contentWidth, m.seatRowHeight};
    y = out.seat1Row.bottom();
    out.seat2Row = {m.margin, y, contentWidth, m.seatRowHeight};

    const int rowPadding = m.space3;
    const int labelWidth = scaled(out.narrow ? 88 : 112, dpi);
    const int statusWidth = scaled(out.narrow ? 140 : 180, dpi);
    const int fieldGap = m.space2;
    const int fieldWidth = (contentWidth - rowPadding * 2 - labelWidth - statusWidth -
                            fieldGap * 3) / 2;
    const int fieldHeight = m.minimumTarget;
    const int rowOffset = (m.seatRowHeight - fieldHeight) / 2;
    const auto assignRow = [&](const PixelRect& row, PixelRect& label, PixelRect& player,
                               PixelRect& game, PixelRect& status) {
        int x = row.x + rowPadding;
        label = {x, row.y + rowOffset, labelWidth, fieldHeight};
        x += labelWidth + fieldGap;
        player = {x, row.y + rowOffset, fieldWidth, fieldHeight};
        x += fieldWidth + fieldGap;
        game = {x, row.y + rowOffset, fieldWidth, fieldHeight};
        x += fieldWidth + fieldGap;
        status = {x, row.y + rowOffset, statusWidth, fieldHeight};
    };
    assignRow(out.seat1Row, out.seat1Label, out.seat1Player, out.seat1Game,
              out.seat1Status);
    assignRow(out.seat2Row, out.seat2Label, out.seat2Player, out.seat2Game,
              out.seat2Status);

    const int launchReasonHeight = std::max(
        scaled(40, dpi), nonNegative(textRequirements.launchReasonMinimumHeight));
    const int launchBarHeight = std::max(
        m.launchBarHeight, saturatedAdd(launchReasonHeight, m.space2 * 2));
    out.launchBar = {0, clientHeightPx - launchBarHeight,
                     clientWidthPx, launchBarHeight};
    out.play = {clientWidthPx - m.margin - scaled(170, dpi),
                out.launchBar.y + (launchBarHeight - m.primaryTarget) / 2,
                scaled(170, dpi), m.primaryTarget};
    out.launchReason = {m.margin,
                        out.launchBar.y + (launchBarHeight - launchReasonHeight) / 2,
                        out.play.x - m.margin - m.space4, launchReasonHeight};

    y = out.seat2Row.bottom() + m.space3;
    out.libraryLabel = {m.margin, y, contentWidth, scaled(22, dpi)};
    y = out.libraryLabel.bottom() + m.space2;
    const int libraryBottom = out.launchBar.y - m.space3;
    const int actionWidth = scaled(out.narrow ? 132 : 150, dpi);
    out.gameList = {m.margin, y, contentWidth - actionWidth - m.space3,
                    std::max(0, libraryBottom - y)};
    out.refresh = {out.gameList.right() + m.space3, y, actionWidth, m.minimumTarget};
    out.addExecutable = {out.refresh.x, out.refresh.bottom() + m.space2,
                         actionWidth, m.minimumTarget};

    // The secondary Setup / Diagnostics page intentionally shares the same
    // bounded content area; the renderer toggles it instead of exposing these
    // controls on the ordinary launcher surface.
    const int top = out.header.bottom() + m.space4;
    out.advancedHeading = {m.margin, top, scaled(360, dpi), scaled(32, dpi)};
    out.backToGames = out.setupNavigation;
    const int columnGap = m.space4;
    const int columnWidth = (contentWidth - columnGap) / 2;
    const int left = m.margin;
    const int right = left + columnWidth + columnGap;
    int ay = out.advancedHeading.bottom() + m.space3;
    out.playerNameLabel = {left, ay, columnWidth, scaled(20, dpi)};
    ay = out.playerNameLabel.bottom() + m.space1;
    out.playerName = {left, ay, columnWidth - scaled(150, dpi) - m.space2,
                      m.minimumTarget};
    out.addPlayer = {out.playerName.right() + m.space2, ay, scaled(150, dpi),
                     m.minimumTarget};
    ay = out.playerName.bottom() + m.space2;
    out.playerRoster = {left, ay, columnWidth - scaled(190, dpi) - m.space2,
                        m.minimumTarget};
    out.renamePlayer = {out.playerRoster.right() + m.space2, ay, scaled(90, dpi),
                        m.minimumTarget};
    out.removePlayer = {out.renamePlayer.right() + m.space2, ay, scaled(90, dpi),
                        m.minimumTarget};
    ay = out.playerRoster.bottom() + m.space3;
    out.setupButton = {left, ay, scaled(240, dpi), m.minimumTarget};
    ay = out.setupButton.bottom() + m.space3;
    out.diagnostics = {left, ay, columnWidth,
                       std::max(scaled(120, dpi), out.launchBar.y - m.space3 - ay)};

    int ry = out.advancedHeading.bottom() + m.space3;
    out.privacyHeading = {right, ry, columnWidth, scaled(28, dpi)};
    ry = out.privacyHeading.bottom() + m.space2;
    out.privacySharing = {right, ry, columnWidth, m.minimumTarget};
    ry = out.privacySharing.bottom() + m.space2;
    out.privacyRetentionLabel = {right, ry, scaled(190, dpi), m.minimumTarget};
    out.privacyRetention = {out.privacyRetentionLabel.right() + m.space2, ry,
                            scaled(70, dpi), m.minimumTarget};
    ry = out.privacyRetentionLabel.bottom() + m.space2;
    out.privacySave = {right, ry, scaled(220, dpi), m.minimumTarget};
    ry = out.privacySave.bottom() + m.space3;
    out.localResultsHeading = {right, ry, columnWidth, scaled(28, dpi)};
    ry = out.localResultsHeading.bottom() + m.space2;
    const int resultButtonWidth = scaled(150, dpi);
    out.localResults = {right, ry, columnWidth - resultButtonWidth - m.space2,
                        std::max(scaled(120, dpi), out.launchBar.y - m.space3 - ry)};
    out.exportLocalResult = {out.localResults.right() + m.space2, ry,
                             resultButtonWidth, m.minimumTarget};
    out.deleteLocalResult = {out.exportLocalResult.x,
                             out.exportLocalResult.bottom() + m.space2,
                             resultButtonWidth, m.minimumTarget};
    out.clearLocalResults = {out.exportLocalResult.x,
                             out.deleteLocalResult.bottom() + m.space2,
                             resultButtonWidth, m.minimumTarget};

    const bool localizedCriticalTextFits =
        out.heroEyebrow.width >= nonNegative(textRequirements.heroEyebrowMinimumWidth) &&
        out.heroTitle.height >= nonNegative(textRequirements.heroTitleMinimumHeight) &&
        out.heroStatus.height >= nonNegative(textRequirements.heroStatusMinimumHeight) &&
        out.useSeatOne.width >= nonNegative(textRequirements.useSeatOneMinimumWidth) &&
        out.useSeatTwo.width >= nonNegative(textRequirements.useSeatTwoMinimumWidth) &&
        out.useBothSeats.width >= nonNegative(textRequirements.useBothSeatsMinimumWidth) &&
        out.configure.width >= nonNegative(textRequirements.configureMinimumWidth);
    out.valid = localizedCriticalTextFits &&
                rectFitsClient(out.setupNavigation, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.hero, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.heroEyebrow, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.heroTitle, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.heroStatus, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.useSeatOne, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.useSeatTwo, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.useBothSeats, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.configure, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat1Row, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat2Row, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat1Player, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat1Game, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat1Status, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat2Player, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat2Game, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.seat2Status, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.gameList, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.refresh, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.addExecutable, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.launchReason, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.play, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.backToGames, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.playerName, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.addPlayer, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.playerRoster, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.renamePlayer, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.removePlayer, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.setupButton, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.diagnostics, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.privacySharing, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.privacyRetention, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.privacySave, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.localResults, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.exportLocalResult, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.deleteLocalResult, clientWidthPx, clientHeightPx) &&
                rectFitsClient(out.clearLocalResults, clientWidthPx, clientHeightPx) &&
                out.play.height >= m.minimumTarget &&
                out.useSeatOne.height >= m.minimumTarget &&
                out.useSeatTwo.height >= m.minimumTarget &&
                out.useBothSeats.height >= m.minimumTarget &&
                out.refresh.height >= m.minimumTarget &&
                out.addExecutable.height >= m.minimumTarget &&
                out.gameRowHeight >= m.minimumTarget &&
                out.gameList.height >= m.minimumTarget;
    return out;
}

} // namespace hydra::ui
