#include "hydra/ui_localization.hpp"
#include "hydra/launcher_layout.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace hydra::ui;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testCompleteThreeLocaleCatalog() {
    for (std::uint16_t raw = 0; raw < static_cast<std::uint16_t>(TextId::Count); ++raw) {
        const auto id = static_cast<TextId>(raw);
        check(!text(id, Locale::EnglishUnitedStates).empty(),
              "every UI string has canonical English text");
        check(!text(id, Locale::KoreanKorea).empty(),
              "every UI string has Korean text or safe English fallback");
        check(!text(id, Locale::ChineseSimplified).empty(),
              "every UI string has Simplified Chinese text or safe English fallback");
    }
}

void testLocaleSelectionAndSafeFallback() {
    check(localeFromTag("ko-KR") == Locale::KoreanKorea &&
              localeFromTag("ko") == Locale::KoreanKorea,
          "Korean locale aliases resolve deterministically");
    check(localeFromTag("zh-CN") == Locale::ChineseSimplified &&
              localeFromTag("zh-Hans") == Locale::ChineseSimplified,
          "Simplified Chinese locale aliases resolve deterministically");
    check(localeFromTag("fr-FR") == Locale::EnglishUnitedStates &&
              localeFromTag("") == Locale::EnglishUnitedStates,
          "unsupported or missing locale falls back to canonical English");
    check(localeTag(Locale::EnglishUnitedStates) == "en-US" &&
              localeTag(Locale::KoreanKorea) == "ko-KR" &&
              localeTag(Locale::ChineseSimplified) == "zh-CN",
          "stable locale tags remain machine-readable English identifiers");
}

void testFormattingAndSafetyActionsStayVisible() {
    const auto koSeat = formatOne(TextId::SeatLauncherTitle, Locale::KoreanKorea, L"2");
    const auto zhPlayer = formatOne(TextId::CurrentSelectedPlayer,
                                    Locale::ChineseSimplified, L"Mario");
    check(koSeat.find(L"2") != std::wstring::npos &&
              koSeat.find(L"{0}") == std::wstring::npos,
          "localized Seat title substitutes bounded runtime value");
    check(zhPlayer.find(L"Mario") != std::wstring::npos &&
              zhPlayer.find(L"{0}") == std::wstring::npos,
          "localized dynamic labels substitute selected identity");
    for (const auto locale : {Locale::EnglishUnitedStates, Locale::KoreanKorea,
                              Locale::ChineseSimplified}) {
        check(!text(TextId::EndPlaying, locale).empty() &&
                  !text(TextId::Reconnect, locale).empty() &&
                  !text(TextId::RecoveryAction, locale).empty() &&
                  !text(TextId::ProtectedExperimentConfirmation, locale).empty() &&
                  !text(TextId::CommunitySharing, locale).empty() &&
                  !text(TextId::SavePrivacy, locale).empty(),
              "critical end/reconnect/recovery/protection/privacy actions remain present in every locale");
    }
}

void testStableNotificationAndPreflightIdsLocalizeWithoutRawText() {
    const auto koRecovery = notificationText("seat.game.recovery", Locale::KoreanKorea);
    const auto zhDevice = preflightText("plan.MissingController", Locale::ChineseSimplified);
    check(!koRecovery.empty() && koRecovery != notificationText(
              "seat.game.recovery", Locale::EnglishUnitedStates),
          "stable recovery message id selects localized presentation");
    check(!zhDevice.empty() && zhDevice != preflightText(
              "plan.MissingController", Locale::EnglishUnitedStates),
          "stable preflight code selects localized presentation");
    check(notificationText("protocol.v3.internal", Locale::KoreanKorea).empty(),
          "unknown machine identifier is not guessed or translated");
    check(preflightText("requires.controller", Locale::KoreanKorea) ==
              text(TextId::NotificationPreflightInformation, Locale::KoreanKorea),
          "known stable informational code maps without copying raw diagnostic text");
}

void testLongLocalizedSafetyCopyDoesNotCollapseToEmpty() {
    for (const auto locale : {Locale::EnglishUnitedStates, Locale::KoreanKorea,
                              Locale::ChineseSimplified}) {
        const auto optionalSetup = text(TextId::OptionalSeatSetup, locale);
        const auto protectedCopy = text(TextId::ProtectedExperimentConfirmation, locale);
        check(optionalSetup.size() >= 8u && protectedCopy.size() >= 8u,
              "long safety/setup copy remains renderable in every declared locale");
    }
}

void testOwnerDrawStatusUsesOneShapeCue() {
    for (const auto locale : {Locale::EnglishUnitedStates, Locale::KoreanKorea,
                              Locale::ChineseSimplified}) {
        const auto notSelected = text(TextId::StatusNotSelected, locale);
        const auto setupWarning = text(TextId::NeedsSetup, locale);
        const auto strippedNotSelected = launcherStatusLabelText(notSelected);
        const auto strippedSetupWarning = launcherStatusLabelText(setupWarning);
        check(!strippedNotSelected.empty() && strippedNotSelected != notSelected &&
                  strippedNotSelected.front() != L'○' && strippedNotSelected.front() != L'●',
              "owner-drawn Seat status removes the legacy glyph before painting its shape marker");
        check(!strippedSetupWarning.empty() && strippedSetupWarning != setupWarning &&
                  strippedSetupWarning.front() != L'○' && strippedSetupWarning.front() != L'●',
              "owner-drawn blocking warning removes the legacy glyph before painting its triangle");
        check(launcherStatusLabelText(text(TextId::StatusReady, locale)) ==
                  text(TextId::StatusReady, locale),
              "ordinary localized status text without a marker is not altered");
    }
}

void testLocalizedLauncherWidthFloorIsNotEnglishSized() {
    const int englishBoth = launcherTextWidthFloor(
        text(TextId::UseBothSeats, Locale::EnglishUnitedStates), 96u);
    const int koreanBoth = launcherTextWidthFloor(
        text(TextId::UseBothSeats, Locale::KoreanKorea), 96u);
    const int chineseBoth = launcherTextWidthFloor(
        text(TextId::UseBothSeats, Locale::ChineseSimplified), 96u);
    check(englishBoth > 0 && koreanBoth > englishBoth && chineseBoth > englishBoth,
          "Seat action width floors come from each localized label rather than the English literal");

    for (const auto locale : {Locale::EnglishUnitedStates, Locale::KoreanKorea,
                              Locale::ChineseSimplified}) {
        for (const auto id : {TextId::SelectedGame, TextId::UseSeatOne, TextId::UseSeatTwo,
                              TextId::UseBothSeats, TextId::SeatHardwareSetup}) {
            const int at96 = launcherTextWidthFloor(text(id, locale), 96u);
            const int at120 = launcherTextWidthFloor(text(id, locale), 120u);
            const int at144 = launcherTextWidthFloor(text(id, locale), 144u);
            const int at192 = launcherTextWidthFloor(text(id, locale), 192u);
            check(at96 > 0 && at120 >= at96 && at144 >= at120 && at192 >= at144 &&
                      at192 >= at96 * 2 - 1,
                  "localized critical text sizing floor scales monotonically through 96/120/144/192 DPI");
        }
    }
}

} // namespace

int main() {
    testCompleteThreeLocaleCatalog();
    testLocaleSelectionAndSafeFallback();
    testFormattingAndSafetyActionsStayVisible();
    testStableNotificationAndPreflightIdsLocalizeWithoutRawText();
    testLongLocalizedSafetyCopyDoesNotCollapseToEmpty();
    testOwnerDrawStatusUsesOneShapeCue();
    testLocalizedLauncherWidthFloorIsNotEnglishSized();
    if (failures != 0) {
        std::cerr << failures << " localization test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "UI localization tests passed.\n";
    return EXIT_SUCCESS;
}
