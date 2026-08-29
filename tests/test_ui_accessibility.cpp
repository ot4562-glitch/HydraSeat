#include "hydra/ui_accessibility.hpp"

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

void testManagementMatrixAcrossLocalesAndDpi() {
    for (const auto locale : {Locale::EnglishUnitedStates, Locale::KoreanKorea,
                              Locale::ChineseSimplified}) {
        for (const auto dpi : {96u, 120u, 144u, 192u, 288u}) {
            LayoutRequest request;
            request.surface = Surface::ManagementGames;
            request.dpi = dpi;
            request.widthPx = (900u * dpi + 95u) / 96u;
            request.heightPx = (600u * dpi + 95u) / 96u;
            request.locale = locale;
            request.keyboardInput = true;
            request.protectionConfirmationRequired = true;
            request.recoveryActionRequired = true;
            const auto result = assessLayout(request);
            check(result.usable && hasAction(result, FocusAction::Play) &&
                      hasAction(result, FocusAction::ProtectionConfirmation) &&
                      hasAction(result, FocusAction::Recovery),
                  "management layout keeps Play/protection/recovery reachable across locale/DPI matrix");
        }
    }
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
    if (failures != 0) {
        std::cerr << failures << " UI accessibility test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "UI accessibility readiness tests passed.\n";
    return EXIT_SUCCESS;
}
