#include "hydra/ui_localization.hpp"

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
        check(localizationEntryComplete(id),
              "every shipping UI string has explicit en-US/ko-KR/zh-CN catalog text");
        check(localizationPlaceholdersValid(id),
              "every shipping UI string preserves placeholder parity across locales");
        check(!text(id, Locale::EnglishUnitedStates).empty() &&
                  !text(id, Locale::KoreanKorea).empty() &&
                  !text(id, Locale::ChineseSimplified).empty(),
              "every shipping UI string resolves in all declared locales");
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
    const auto koHardware = formatOne(TextId::SeatHardwareLabel, Locale::KoreanKorea, L"2");
    const auto zhAssign = formatOne(TextId::AssignToSeat, Locale::ChineseSimplified, L"1");
    const auto koConnected = formatOne(
        TextId::ConnectedHardwareCount, Locale::KoreanKorea, L"4");
    check(koSeat.find(L"2") != std::wstring::npos &&
              koSeat.find(L"{0}") == std::wstring::npos,
          "localized Seat title substitutes bounded runtime value");
    check(zhPlayer.find(L"Mario") != std::wstring::npos &&
              zhPlayer.find(L"{0}") == std::wstring::npos,
          "localized dynamic labels substitute selected identity");
    check(koHardware.find(L"2") != std::wstring::npos &&
              koHardware.find(L"{0}") == std::wstring::npos &&
              zhAssign.find(L"1") != std::wstring::npos &&
              zhAssign.find(L"{0}") == std::wstring::npos &&
              koConnected.find(L"4") != std::wstring::npos &&
              koConnected.find(L"{0}") == std::wstring::npos,
          "hardware setup labels preserve and substitute Seat placeholders");
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

void testCriticalActionCopyStaysConcise() {
    constexpr std::size_t kMaxCriticalActionCharacters = 40u;
    constexpr TextId kCriticalActions[] = {
        TextId::Play,
        TextId::EndPlaying,
        TextId::Reconnect,
        TextId::RecoveryAction,
        TextId::SeatHardwareSetup,
        TextId::UseSeatOne,
        TextId::UseSeatTwo,
        TextId::UseBothSeats,
        TextId::ReturnToWindows,
        TextId::Reconfigure,
        TextId::ApplySetup,
        TextId::ReloadSetup,
        TextId::AssignToSeat,
        TextId::UnassignDevice,
        TextId::ExportSelectedResult,
        TextId::DeleteSelectedResult,
        TextId::ClearLocalResults,
    };

    for (const auto locale : {Locale::EnglishUnitedStates, Locale::KoreanKorea,
                              Locale::ChineseSimplified}) {
        for (const auto id : kCriticalActions) {
            const auto value = text(id, locale);
            check(!value.empty() && value.size() <= kMaxCriticalActionCharacters,
                  "critical action copy stays present and semantically concise in every locale");
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
    testCriticalActionCopyStaysConcise();
    if (failures != 0) {
        std::cerr << failures << " localization test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "UI localization tests passed.\n";
    return EXIT_SUCCESS;
}
