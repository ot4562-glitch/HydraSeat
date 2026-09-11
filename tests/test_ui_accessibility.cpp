#include "hydra/ui_accessibility.hpp"
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

void testManagementSurfaceIsAPlainFunctionalContract() {
    for (const auto locale : {Locale::EnglishUnitedStates, Locale::KoreanKorea,
                              Locale::ChineseSimplified}) {
        for (const auto dpi : {96u, 120u, 144u, 192u, 240u, 288u}) {
            LayoutRequest request;
            request.surface = Surface::ManagementGames;
            request.dpi = dpi;
            request.widthPx = (720u * dpi + 95u) / 96u;
            request.heightPx = (500u * dpi + 95u) / 96u;
            request.locale = locale;
            request.keyboardInput = true;
            request.protectionConfirmationRequired = true;
            request.recoveryActionRequired = true;

            const auto result = assessLayout(request);
            check(result.usable &&
                      hasAction(result, FocusAction::PlayerName) &&
                      hasAction(result, FocusAction::HardwareSetup) &&
                      hasAction(result, FocusAction::GameList) &&
                      hasAction(result, FocusAction::Play) &&
                      hasAction(result, FocusAction::ProtectionConfirmation) &&
                      hasAction(result, FocusAction::Recovery),
                  "management surface exposes the functional controls and safety actions");
        }
    }
}

void testManagementSurfaceRejectsOnlyFunctionalMinimums() {
    LayoutRequest request;
    request.surface = Surface::ManagementGames;
    request.widthPx = 719u;
    request.heightPx = 500u;
    request.dpi = 96u;
    const auto narrow = assessLayout(request);
    check(!narrow.usable && has(narrow, AccessibilityIssue::SurfaceTooSmall),
          "management surface rejects clients below the plain functional minimum");

    request.widthPx = 720u;
    request.pointerInput = false;
    request.keyboardInput = false;
    request.controllerInput = false;
    const auto noInput = assessLayout(request);
    check(!noInput.usable && has(noInput, AccessibilityIssue::NoInputModality),
          "management surface requires at least one input modality");
}

void testSeatLauncherSafetyRemainsFailClosed() {
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
    check(expandedResult.usable &&
              hasAction(expandedResult, FocusAction::EndPlaying) &&
              hasAction(expandedResult, FocusAction::Reconnect) &&
              hasAction(expandedResult, FocusAction::ProtectionConfirmation) &&
              hasAction(expandedResult, FocusAction::Recovery),
          "expanded Seat launcher exposes critical safety actions");

    auto compact = expanded;
    compact.surface = Surface::SeatLauncherCompact;
    compact.widthPx = 600u;
    compact.heightPx = 180u;
    const auto unsafeCompact = assessLayout(compact);
    check(!unsafeCompact.usable && has(unsafeCompact, AccessibilityIssue::CriticalActionHidden),
          "compact Seat launcher refuses states that would hide safety actions");

    compact.protectionConfirmationRequired = false;
    compact.recoveryActionRequired = false;
    const auto normalCompact = assessLayout(compact);
    check(normalCompact.usable && normalCompact.focusOrder.size() == 1u &&
              normalCompact.focusOrder.front() == FocusAction::EndPlaying,
          "normal compact Seat launcher retains only its bounded action");
}

void testStableDiagnosticNames() {
    LayoutRequest request;
    request.surface = Surface::ManagementGames;
    request.widthPx = 3000u;
    request.heightPx = 2000u;
    request.dpi = 48u;
    const auto result = assessLayout(request);
    check(!result.usable && has(result, AccessibilityIssue::InvalidDpi),
          "unsupported DPI fails closed");
    check(focusActionName(FocusAction::HardwareSetup) == "HardwareSetup" &&
              focusActionName(FocusAction::EndPlaying) == "EndPlaying" &&
              accessibilityIssueName(AccessibilityIssue::CriticalActionHidden) ==
                  "CriticalActionHidden",
          "diagnostic identifiers remain stable English");
}

} // namespace

int main() {
    testManagementSurfaceIsAPlainFunctionalContract();
    testManagementSurfaceRejectsOnlyFunctionalMinimums();
    testSeatLauncherSafetyRemainsFailClosed();
    testStableDiagnosticNames();
    if (failures != 0) {
        std::cerr << failures << " UI accessibility test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "UI accessibility readiness tests passed.\n";
    return EXIT_SUCCESS;
}
