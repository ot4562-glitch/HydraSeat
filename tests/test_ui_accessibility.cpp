#include "hydra/ui_accessibility.hpp"
#include "hydra/launcher_layout.hpp"
#include "hydra/ui_localization.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {

using namespace hydra::ui;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool has(const LayoutAssessment& result, AccessibilityIssue issue) {
    return std::find(result.issues.begin(), result.issues.end(), issue) != result.issues.end();
}

bool hasAction(const LayoutAssessment& result, FocusAction action) {
    return std::find(result.focusOrder.begin(), result.focusOrder.end(), action) !=
           result.focusOrder.end();
}

int scaledLogical(int logical, unsigned dpi) {
    return static_cast<int>((static_cast<long long>(logical) * dpi + 48) / 96);
}

bool overlaps(const PixelRect& left, const PixelRect& right) {
    return left.visible() && right.visible() &&
           left.x < right.right() && right.x < left.right() &&
           left.y < right.bottom() && right.y < left.bottom();
}

int deterministicWrappedHeight(std::wstring_view value, int width, unsigned dpi) {
    const int lineHeight = scaledLogical(20, dpi);
    if (value.empty() || width <= 0) return lineHeight;

    int lines = 0;
    std::size_t lineStart = 0;
    while (lineStart <= value.size()) {
        const auto lineEnd = value.find(L'\n', lineStart);
        const auto line = value.substr(
            lineStart, lineEnd == std::wstring_view::npos
                ? value.size() - lineStart : lineEnd - lineStart);
        const int textWidth = launcherTextWidthFloor(line, dpi);
        lines += std::max(1, (textWidth + width - 1) / width);
        if (lineEnd == std::wstring_view::npos) break;
        lineStart = lineEnd + 1;
    }
    return std::max(1, lines) * lineHeight;
}

LauncherTextRequirements localizedLauncherRequirements(
    Locale locale, unsigned dpi, const LauncherLayout& base) {
    LauncherTextMeasurements measured;
    const auto metrics = launcherThemeMetrics(dpi);
    const int buttonHorizontalPadding = metrics.space4 * 2;
    const auto measureButton = [&](TextId id, const PixelRect& rect,
                                   int& widthOut, int& heightOut) {
        const auto label = text(id, locale);
        widthOut = launcherTextWidthFloor(label, dpi);
        heightOut = deterministicWrappedHeight(
            label, std::max(1, rect.width - buttonHorizontalPadding), dpi);
    };

    measured.heroEyebrowWidth = launcherTextWidthFloor(
        text(TextId::SelectedGame, locale), dpi);
    measured.heroTitleHeight = deterministicWrappedHeight(
        text(TextId::NoGameSelected, locale),
        std::max(1, base.heroTitle.width), dpi);
    measured.heroStatusHeight = deterministicWrappedHeight(
        text(TextId::ChooseSeatForSelectedGame, locale),
        std::max(1, base.heroStatus.width), dpi);

    const std::wstring playerOne = std::wstring(text(TextId::Player, locale)) + L" 1";
    const std::wstring playerTwo = std::wstring(text(TextId::Player, locale)) + L" 2";
    measured.playerLabelWidth = std::max(
        launcherTextWidthFloor(playerOne, dpi), launcherTextWidthFloor(playerTwo, dpi));
    measured.playerLabelHeight = scaledLogical(20, dpi);
    for (const auto status : {text(TextId::ChoosePlayer, locale), text(TextId::None, locale),
                              text(TextId::StatusNeedsAttention, locale)}) {
        measured.playerStatusWidth = std::max(
            measured.playerStatusWidth, launcherTextWidthFloor(status, dpi));
        measured.playerStatusHeight = std::max(
            measured.playerStatusHeight,
            deterministicWrappedHeight(status, std::max(1, base.seat1Status.width), dpi));
    }

    measureButton(TextId::SeatHardwareSetup, base.configure,
                  measured.configureWidth, measured.configureHeight);
    measureButton(TextId::Refresh, base.refresh,
                  measured.refreshWidth, measured.refreshHeight);
    measureButton(TextId::AddExecutable, base.addExecutable,
                  measured.addExecutableWidth, measured.addExecutableHeight);
    measureButton(TextId::Play, base.play,
                  measured.playWidth, measured.playHeight);

    measureButton(TextId::AddPlayer, base.addPlayer,
                  measured.addPlayerWidth, measured.addPlayerHeight);

    measured.launchReasonHeight = deterministicWrappedHeight(
        text(TextId::RuntimeLaunchUnavailable, locale),
        std::max(1, base.launchReason.width), dpi);
    const auto warning = launcherStatusLabelText(text(TextId::NeedsSetup, locale));
    const int warningWidth = std::max(
        metrics.minimumTarget,
        base.gameList.width - metrics.space3 * 2 - metrics.statusMarker - metrics.space2);
    measured.gameRowHeight = metrics.space2 + scaledLogical(20, dpi) + metrics.space1 +
                             deterministicWrappedHeight(warning, warningWidth, dpi) +
                             metrics.space2;
    return launcherTextRequirements(measured, dpi);
}

void checkCriticalLauncherGeometry(const LauncherLayout& layout, int width, int height,
                                   unsigned dpi, bool expectNarrow) {
    const auto metrics = launcherThemeMetrics(dpi);
    check(layout.valid && layout.narrow == expectNarrow &&
              rectFitsClient(layout.hero, width, height) &&
              rectFitsClient(layout.playerNameLabel, width, height) &&
              rectFitsClient(layout.playerName, width, height) &&
              rectFitsClient(layout.addPlayer, width, height) &&
              rectFitsClient(layout.configure, width, height) &&
              rectFitsClient(layout.seat1Row, width, height) &&
              rectFitsClient(layout.seat2Row, width, height) &&
              rectFitsClient(layout.gameList, width, height) &&
              rectFitsClient(layout.refresh, width, height) &&
              rectFitsClient(layout.addExecutable, width, height) &&
              rectFitsClient(layout.launchReason, width, height) &&
              rectFitsClient(layout.play, width, height),
          "launcher geometry keeps game, Players, device setup, library, reason, and Play in the viewport");
    check(layout.play.height >= metrics.minimumTarget &&
              layout.play.width >= metrics.minimumTarget &&
              layout.playerName.height >= metrics.minimumTarget &&
              layout.addPlayer.height >= metrics.minimumTarget &&
              layout.configure.height >= metrics.minimumTarget &&
              layout.refresh.height >= metrics.minimumTarget &&
              layout.addExecutable.height >= metrics.minimumTarget &&
              metrics.gameRowHeight >= metrics.minimumTarget,
          "launcher geometry preserves 44-logical-pixel interactive targets");
    check(!overlaps(layout.heroEyebrow, layout.heroTitle) &&
              !overlaps(layout.heroTitle, layout.heroStatus) &&
              !overlaps(layout.playerNameLabel, layout.playerName) &&
              !overlaps(layout.playerName, layout.addPlayer) &&
              !overlaps(layout.addPlayer, layout.configure) &&
              !overlaps(layout.seat1Label, layout.seat1Player) &&
              !overlaps(layout.seat1Player, layout.seat1Status) &&
              !overlaps(layout.seat2Label, layout.seat2Player) &&
              !overlaps(layout.seat2Player, layout.seat2Status) &&
              !overlaps(layout.gameList, layout.refresh) &&
              !overlaps(layout.gameList, layout.addExecutable) &&
              !overlaps(layout.launchReason, layout.play),
          "launcher critical regions do not overlap at supported widths");
    check(layout.playerName.x >= layout.hero.x &&
              layout.addPlayer.right() <= layout.hero.right() &&
              layout.configure.x >= layout.hero.x &&
              layout.configure.right() <= layout.hero.right(),
          "rapid-test Player creation and device setup stay inside the selected-game card");
}

void testManagementMatrixAcrossLocalesAndDpi() {
    for (const auto locale : {Locale::EnglishUnitedStates, Locale::KoreanKorea,
                              Locale::ChineseSimplified}) {
        for (const auto dpi : {96u, 120u, 144u, 192u, 240u, 288u}) {
            LayoutRequest request;
            request.surface = Surface::ManagementGames;
            request.dpi = dpi;
            request.widthPx = (860u * dpi + 95u) / 96u;
            request.heightPx = (640u * dpi + 95u) / 96u;
            request.locale = locale;
            request.keyboardInput = true;
            request.protectionConfirmationRequired = true;
            request.recoveryActionRequired = true;
            const auto result = assessLayout(request);
            check(result.usable && !result.focusOrder.empty() &&
                      result.focusOrder.front() == FocusAction::GameList &&
                      hasAction(result, FocusAction::Play) &&
                      hasAction(result, FocusAction::ProtectionConfirmation) &&
                      hasAction(result, FocusAction::Recovery),
                  "management keyboard flow starts game-first and keeps Play/protection/recovery reachable");
        }
    }
}

void testRendererGeometryIsTheAccessibilitySourceOfTruth() {
    for (const auto dpi : {96u, 120u, 144u, 192u}) {
        const int standardWidth = scaledLogical(980, dpi);
        const int standardHeight = scaledLogical(720, dpi);
        checkCriticalLauncherGeometry(
            computeLauncherLayout(standardWidth, standardHeight, dpi),
            standardWidth, standardHeight, dpi, false);

        const int narrowWidth = scaledLogical(kLauncherMinimumClientWidthLogical, dpi);
        const int narrowHeight = scaledLogical(kLauncherMinimumClientHeightLogical, dpi);
        checkCriticalLauncherGeometry(
            computeLauncherLayout(narrowWidth, narrowHeight, dpi),
            narrowWidth, narrowHeight, dpi, true);
    }

    const auto clippedWidth = computeLauncherLayout(
        kLauncherMinimumClientWidthLogical - 1,
        kLauncherMinimumClientHeightLogical, 96u);
    const auto clippedHeight = computeLauncherLayout(
        kLauncherMinimumClientWidthLogical,
        kLauncherMinimumClientHeightLogical - 1, 96u);
    check(!clippedWidth.valid && !clippedHeight.valid,
          "launcher geometry rejects clients below the declared narrow minimum");
}

void testLocalizedCriticalTextDrivesLauncherLayout() {
    for (const auto locale : {Locale::EnglishUnitedStates, Locale::KoreanKorea,
                              Locale::ChineseSimplified}) {
        for (const auto dpi : {96u, 120u, 144u, 192u}) {
            const int standardWidth = scaledLogical(980, dpi);
            const int standardHeight = scaledLogical(720, dpi);
            const auto base = computeLauncherLayout(standardWidth, standardHeight, dpi);
            const auto requirements = localizedLauncherRequirements(locale, dpi, base);
            const auto standard = computeLauncherLayout(
                standardWidth, standardHeight, dpi, requirements);
            check(standard.valid && !standard.narrow &&
                      standard.heroEyebrow.height >= scaledLogical(22, dpi) &&
                      standard.heroEyebrow.width >= requirements.heroEyebrowMinimumWidth &&
                      standard.heroTitle.height >= requirements.heroTitleMinimumHeight &&
                      standard.heroStatus.height >= requirements.heroStatusMinimumHeight &&
                      standard.seat1Status.width >= requirements.playerStatusMinimumWidth &&
                      standard.seat1Status.height >= requirements.playerStatusMinimumHeight &&
                      standard.seat2Status.width >= requirements.playerStatusMinimumWidth &&
                      standard.seat2Status.height >= requirements.playerStatusMinimumHeight &&
                      standard.addPlayer.width >= requirements.addPlayerMinimumWidth &&
                      standard.addPlayer.height >= requirements.addPlayerMinimumHeight &&
                      standard.configure.width >= requirements.configureMinimumWidth &&
                      standard.configure.height >= requirements.configureMinimumHeight &&
                      standard.refresh.width >= requirements.refreshMinimumWidth &&
                      standard.refresh.height >= requirements.refreshMinimumHeight &&
                      standard.addExecutable.width >= requirements.addExecutableMinimumWidth &&
                      standard.addExecutable.height >= requirements.addExecutableMinimumHeight &&
                      standard.play.width >= requirements.playMinimumWidth &&
                      standard.play.height >= requirements.playMinimumHeight &&
                      standard.launchReason.height >= requirements.launchReasonMinimumHeight &&
                      standard.gameRowHeight >= requirements.gameRowMinimumHeight,
                  "980x720 launcher allocates measured localized critical text before secondary metadata");
            check(standard.playerName.height >= launcherThemeMetrics(dpi).minimumTarget &&
                      standard.addPlayer.width >= requirements.addPlayerMinimumWidth &&
                      standard.addPlayer.height >= requirements.addPlayerMinimumHeight &&
                      standard.configure.height >= requirements.configureMinimumHeight &&
                      rectFitsClient(standard.playerName, standardWidth, standardHeight) &&
                      rectFitsClient(standard.addPlayer, standardWidth, standardHeight),
                  "980x720 rapid-test Player creation remains keyboard-sized and unclipped");
            check(standard.headerTitle.height >= scaledLogical(30, dpi) &&
                      !overlaps(standard.heroEyebrow, standard.heroTitle) &&
                      !overlaps(standard.heroTitle, standard.heroStatus) &&
                      !overlaps(standard.playerNameLabel, standard.playerName) &&
                      !overlaps(standard.playerName, standard.addPlayer) &&
                      !overlaps(standard.addPlayer, standard.configure),
                  "localized standard launcher preserves title baselines and non-overlapping primary actions");

            const int narrowWidth = scaledLogical(kLauncherMinimumClientWidthLogical, dpi);
            const int narrowHeight = scaledLogical(kLauncherMinimumClientHeightLogical, dpi);
            const auto narrowBase = computeLauncherLayout(narrowWidth, narrowHeight, dpi);
            const auto narrowRequirements = localizedLauncherRequirements(
                locale, dpi, narrowBase);
            const auto narrow = computeLauncherLayout(
                narrowWidth, narrowHeight, dpi, narrowRequirements);
            const auto metrics = launcherThemeMetrics(dpi);
            check(narrow.valid && narrow.narrow &&
                      narrow.heroEyebrow.width >= narrowRequirements.heroEyebrowMinimumWidth &&
                      narrow.heroTitle.height >= narrowRequirements.heroTitleMinimumHeight &&
                      narrow.heroStatus.height >= narrowRequirements.heroStatusMinimumHeight &&
                      narrow.seat1Status.width >= narrowRequirements.playerStatusMinimumWidth &&
                      narrow.seat1Status.height >= narrowRequirements.playerStatusMinimumHeight &&
                      narrow.seat2Status.width >= narrowRequirements.playerStatusMinimumWidth &&
                      narrow.seat2Status.height >= narrowRequirements.playerStatusMinimumHeight &&
                      narrow.addPlayer.width >= narrowRequirements.addPlayerMinimumWidth &&
                      narrow.addPlayer.height >= narrowRequirements.addPlayerMinimumHeight &&
                      narrow.configure.width >= narrowRequirements.configureMinimumWidth &&
                      narrow.configure.height >= narrowRequirements.configureMinimumHeight &&
                      narrow.refresh.width >= narrowRequirements.refreshMinimumWidth &&
                      narrow.refresh.height >= narrowRequirements.refreshMinimumHeight &&
                      narrow.addExecutable.width >= narrowRequirements.addExecutableMinimumWidth &&
                      narrow.addExecutable.height >= narrowRequirements.addExecutableMinimumHeight &&
                      narrow.play.width >= narrowRequirements.playMinimumWidth &&
                      narrow.play.height >= narrowRequirements.playMinimumHeight &&
                      narrow.launchReason.height >= narrowRequirements.launchReasonMinimumHeight &&
                      narrow.gameRowHeight >= narrowRequirements.gameRowMinimumHeight,
                  "narrow launcher preserves measured selected-game copy and Player flow without clipping");
            check(narrow.playerName.height >= metrics.minimumTarget &&
                      narrow.addPlayer.width >= narrowRequirements.addPlayerMinimumWidth &&
                      narrow.addPlayer.height >= narrowRequirements.addPlayerMinimumHeight &&
                      narrow.configure.height >= narrowRequirements.configureMinimumHeight &&
                      rectFitsClient(narrow.playerName, narrowWidth, narrowHeight) &&
                      rectFitsClient(narrow.addPlayer, narrowWidth, narrowHeight),
                  "narrow rapid-test Player creation remains keyboard-sized and unclipped");
            check(!overlaps(narrow.playerNameLabel, narrow.playerName) &&
                      !overlaps(narrow.playerName, narrow.addPlayer) &&
                      !overlaps(narrow.addPlayer, narrow.configure) &&
                      rectFitsClient(narrow.play, narrowWidth, narrowHeight) &&
                      rectFitsClient(narrow.launchReason, narrowWidth, narrowHeight),
                  "narrow localization reflow keeps Player creation, Play, and the blocking reason visible");
        }
    }
}

void testLongBlockingReasonKeepsPrimaryFlowVisible() {
    for (const auto locale : {Locale::EnglishUnitedStates, Locale::KoreanKorea,
                              Locale::ChineseSimplified}) {
        for (const auto dpi : {96u, 120u, 144u, 192u}) {
            for (const int logicalWidth : {980, kLauncherMinimumClientWidthLogical}) {
                const int logicalHeight = logicalWidth == 980
                    ? 720 : kLauncherMinimumClientHeightLogical;
                const int width = scaledLogical(logicalWidth, dpi);
                const int height = scaledLogical(logicalHeight, dpi);
                const auto base = computeLauncherLayout(width, height, dpi);
                auto requirements = localizedLauncherRequirements(locale, dpi, base);
                requirements.launchReasonMinimumHeight = std::max(
                    requirements.launchReasonMinimumHeight, scaledLogical(80, dpi));
                const auto layout = computeLauncherLayout(width, height, dpi, requirements);
                const auto metrics = launcherThemeMetrics(dpi);
                check(layout.valid &&
                          layout.launchReason.height >= requirements.launchReasonMinimumHeight &&
                          layout.gameList.height >= metrics.minimumTarget &&
                          rectFitsClient(layout.launchReason, width, height) &&
                          rectFitsClient(layout.play, width, height),
                      "long blocking reason expands without hiding the library or Play action");
            }
        }
    }
}

void testExplicitMultilineActionsUseHeightInsteadOfEnglishWidth() {
    for (const auto dpi : {96u, 120u, 144u, 192u}) {
        check(launcherTextWidthFloor(L"AAAA\nBB", dpi) ==
                  launcherTextWidthFloor(L"AAAA", dpi),
              "explicit multiline width uses the widest line instead of summing lines");

        for (const int logicalWidth : {980, kLauncherMinimumClientWidthLogical}) {
            const int logicalHeight = logicalWidth == 980
                ? 720 : kLauncherMinimumClientHeightLogical;
            const int width = scaledLogical(logicalWidth, dpi);
            const int height = scaledLogical(logicalHeight, dpi);

            LauncherTextMeasurements measured;
            const int twoLineHeight = scaledLogical(40, dpi);
            measured.addPlayerWidth = launcherTextWidthFloor(L"Add\nPlayer", dpi);
            measured.addPlayerHeight = twoLineHeight;
            measured.configureWidth = launcherTextWidthFloor(L"Device\nSetup", dpi);
            measured.configureHeight = twoLineHeight;

            const auto requirements = launcherTextRequirements(measured, dpi);
            const auto layout = computeLauncherLayout(width, height, dpi, requirements);
            check(layout.valid &&
                      layout.addPlayer.height >= requirements.addPlayerMinimumHeight &&
                      layout.configure.height >= requirements.configureMinimumHeight &&
                      rectFitsClient(layout.addPlayer, width, height) &&
                      rectFitsClient(layout.configure, width, height),
                  "explicit two-line primary actions grow vertically without clipping or raising the window minimum");
        }
    }
}

void testPresentationSemanticsAreStateConsumersOnly() {
    const auto primary = launcherButtonPresentation(
        LauncherButtonRole::Primary, true, true, false, false);
    check(primary.surface == LauncherButtonSurface::Primary &&
              primary.drawFocusFrame && !primary.pressedOffset &&
              !primary.useSystemColors,
          "primary button presentation is dominant and keeps an explicit focus frame");

    const auto secondaryPressed = launcherButtonPresentation(
        LauncherButtonRole::Secondary, true, false, true, false);
    check(secondaryPressed.surface == LauncherButtonSurface::Raised &&
              secondaryPressed.pressedOffset,
          "secondary button press changes presentation without changing its semantic role");

    const auto danger = launcherButtonPresentation(
        LauncherButtonRole::Danger, true, false, false, false);
    check(danger.surface == LauncherButtonSurface::Danger,
          "destructive actions use the dedicated danger presentation");

    const auto disabledPrimary = launcherButtonPresentation(
        LauncherButtonRole::Primary, false, false, true, false);
    check(disabledPrimary.surface == LauncherButtonSurface::Disabled &&
              !disabledPrimary.pressedOffset,
          "disabled primary action cannot acquire a pressed presentation");

    const auto highContrast = launcherButtonPresentation(
        LauncherButtonRole::Primary, true, true, true, true);
    check(highContrast.surface == LauncherButtonSurface::System &&
              highContrast.useSystemColors && highContrast.drawFocusFrame,
          "High Contrast forces the system-color button path while retaining focus");

    const auto normalRow = launcherGameRowPresentation(false, false, false);
    const auto selectedRow = launcherGameRowPresentation(true, true, false);
    const auto highContrastRow = launcherGameRowPresentation(true, true, true);
    check(normalRow.surface == LauncherGameRowSurface::Open &&
              !normalRow.drawSelectionEdge &&
              selectedRow.surface == LauncherGameRowSurface::Selected &&
              selectedRow.drawSelectionEdge && selectedRow.drawFocusFrame &&
              highContrastRow.surface == LauncherGameRowSurface::System &&
              highContrastRow.useSystemColors,
          "game row selection and focus are explicit and High Contrast remains system-owned");

    const auto unconfigured = launcherSeatPresentation(false, false, false);
    const auto ready = launcherSeatPresentation(true, true, false);
    const auto attention = launcherSeatPresentation(true, false, false);
    const auto highContrastSeat = launcherSeatPresentation(true, false, true);
    check(unconfigured.state == LauncherSeatState::NotConfigured &&
              unconfigured.marker == LauncherStatusMarker::Ring &&
              ready.state == LauncherSeatState::Ready &&
              ready.marker == LauncherStatusMarker::Dot &&
              attention.state == LauncherSeatState::NeedsAttention &&
              attention.marker == LauncherStatusMarker::Triangle &&
              highContrastSeat.useSystemColors,
          "Seat readiness uses distinct state+shape presentation and never color alone");
}

void testSeatLauncherExpandedAndCompactSafety() {
    LayoutRequest expanded;
    expanded.surface = Surface::SeatLauncherExpanded;
    expanded.widthPx = 840u;
    expanded.heightPx = 720u;
    expanded.dpi = 192u;
    expanded.locale = Locale::KoreanKorea;
    expanded.keyboardInput = true;
    expanded.protectionConfirmationRequired = true;
    expanded.recoveryActionRequired = true;
    const auto expandedResult = assessLayout(expanded);
    check(expandedResult.usable && hasAction(expandedResult, FocusAction::EndPlaying) &&
              hasAction(expandedResult, FocusAction::Reconnect) &&
              hasAction(expandedResult, FocusAction::ProtectionConfirmation) &&
              hasAction(expandedResult, FocusAction::Recovery),
          "expanded Seat launcher exposes every critical safety action");

    auto compact = expanded;
    compact.surface = Surface::SeatLauncherCompact;
    compact.widthPx = 600u;
    compact.heightPx = 180u;
    const auto compactResult = assessLayout(compact);
    check(!compactResult.usable && has(compactResult, AccessibilityIssue::CriticalActionHidden),
          "compact playing surface refuses protection/recovery states that would hide actions");

    compact.protectionConfirmationRequired = false;
    compact.recoveryActionRequired = false;
    const auto normalCompact = assessLayout(compact);
    check(normalCompact.usable && normalCompact.focusOrder.size() == 1u &&
              normalCompact.focusOrder.front() == FocusAction::EndPlaying,
          "normal compact playing surface retains bounded End Playing action");
}

void testSmallSurfaceAndNoInputFailClosed() {
    LayoutRequest request;
    request.surface = Surface::SeatLauncherExpanded;
    request.widthPx = 300u;
    request.heightPx = 200u;
    request.dpi = 144u;
    request.pointerInput = false;
    request.keyboardInput = false;
    request.controllerInput = false;
    const auto result = assessLayout(request);
    check(!result.usable && has(result, AccessibilityIssue::SurfaceTooSmall) &&
              has(result, AccessibilityIssue::NoInputModality),
          "off-screen/sourceless interaction fails closed instead of hiding controls");
}

void testInvalidDpiAndStableMachineNames() {
    LayoutRequest request;
    request.surface = Surface::ManagementGames;
    request.widthPx = 3000u;
    request.heightPx = 2000u;
    request.dpi = 48u;
    const auto result = assessLayout(request);
    check(!result.usable && has(result, AccessibilityIssue::InvalidDpi),
          "unsupported DPI range fails closed for declared layout contract");
    check(focusActionName(FocusAction::EndPlaying) == "EndPlaying" &&
              accessibilityIssueName(AccessibilityIssue::CriticalActionHidden) ==
                  "CriticalActionHidden",
          "diagnostic identifiers remain stable English and are not localized");
}

} // namespace

int main() {
    testManagementMatrixAcrossLocalesAndDpi();
    testSeatLauncherExpandedAndCompactSafety();
    testSmallSurfaceAndNoInputFailClosed();
    testInvalidDpiAndStableMachineNames();
    testRendererGeometryIsTheAccessibilitySourceOfTruth();
    testLocalizedCriticalTextDrivesLauncherLayout();
    testLongBlockingReasonKeepsPrimaryFlowVisible();
    testExplicitMultilineActionsUseHeightInsteadOfEnglishWidth();
    testPresentationSemanticsAreStateConsumersOnly();
    if (failures != 0) {
        std::cerr << failures << " UI accessibility test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "UI accessibility readiness tests passed.\n";
    return EXIT_SUCCESS;
}
