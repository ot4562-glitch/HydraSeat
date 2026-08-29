#include "hydra/seat_notification_model.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace hydra;
using namespace hydra::seatui;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

SeatLauncherState launcher(SeatLauncherPhase phase,
                           std::uint64_t generation = 4u,
                           std::uint64_t sequence = 8u) {
    SeatLauncherState value;
    value.seatId = 2u;
    value.connected = phase != SeatLauncherPhase::Disconnected;
    value.phase = phase;
    value.authorityGeneration = generation;
    value.transitionSequence = sequence;
    value.warning = R"(C:\Private\account-token.txt bearer=secret)";
    return value;
}

void testStableCodeMappingAndPrivacy() {
    preflight::Summary summary;
    summary.canActivate = false;
    summary.messages = {
        {preflight::Severity::Blocking, 2u, "plan.MissingController",
         R"(Seat 2 needs C:\Private\controller.json)", "token=expert-secret"},
        {preflight::Severity::Blocking, 1u, "plan.ProviderUnavailable",
         "other Seat private message", "other-secret"},
        {preflight::Severity::Warning, 0u, "risk.protected",
         "raw warning", R"(C:\Users\Private)"}};
    SeatNotificationModel model(2u);
    std::string error;
    check(model.apply(launcher(SeatLauncherPhase::Idle), &summary, &error) &&
              model.state().notifications.size() == 2u &&
              model.state().notifications[0].messageId ==
                  "seat.requirements.devices" &&
              model.state().notifications[0].action ==
                  SeatNotificationAction::OpenSeatSettings,
          "stable preflight codes map to bounded Seat-local actions");
    std::string rendered;
    for (const auto& notification : model.state().notifications) {
        rendered += notification.messageId + notification.displayText;
    }
    check(rendered.find("Private") == std::string::npos &&
              rendered.find("secret") == std::string::npos &&
              rendered.find("token") == std::string::npos &&
              rendered.find("controller.json") == std::string::npos &&
              rendered.find("other Seat") == std::string::npos,
          "raw diagnostic, path, expert detail, and other-Seat text never enter output");
}

void testAuthoritativeRecoveryAndStaleClearing() {
    SeatNotificationModel model(2u);
    std::string error;
    check(model.apply(launcher(SeatLauncherPhase::Recovery), nullptr, &error) &&
              model.state().notifications.size() == 1u &&
              model.state().notifications[0].messageId == "seat.game.recovery" &&
              model.state().notifications[0].action ==
                  SeatNotificationAction::Resnapshot,
          "authoritative recovery phase produces one bounded recovery action");
    check(!model.apply(launcher(SeatLauncherPhase::Idle, 3u, 7u), nullptr, &error) &&
              model.state().notifications[0].messageId == "seat.game.recovery",
          "stale success cannot clear a newer recovery notification");
    check(model.apply(launcher(SeatLauncherPhase::Idle, 5u, 9u), nullptr, &error) &&
              model.state().notifications.empty(),
          "new authoritative success fully clears stale recovery state");
}

void testDisconnectResetsAuthorityEpoch() {
    SeatNotificationModel model(2u);
    std::string error;
    check(model.apply(launcher(SeatLauncherPhase::Warning, 20u, 30u), nullptr, &error) &&
              model.apply(launcher(SeatLauncherPhase::Disconnected, 20u, 30u),
                          nullptr, &error) &&
              model.state().notifications[0].messageId == "seat.host.disconnected" &&
              model.state().authorityGeneration == 0u,
          "disconnect replaces warning and resets the authority epoch");
    check(model.apply(launcher(SeatLauncherPhase::Idle, 0u, 1u), nullptr, &error) &&
              model.state().notifications.empty(),
          "full snapshot from a restarted host can clear disconnected state");
}

void testBoundsAndDeduplication() {
    preflight::Summary summary;
    summary.canActivate = false;
    for (std::size_t index = 0; index < kMaximumSeatNotifications * 4u; ++index) {
        summary.messages.push_back({preflight::Severity::Blocking, 2u,
                                    "unknown-" + std::to_string(index), {}, {}});
    }
    SeatNotificationModel model(2u);
    std::string error;
    check(model.apply(launcher(SeatLauncherPhase::Idle), &summary, &error) &&
              model.state().notifications.size() == 1u &&
              model.state().notifications[0].messageId == "seat.preflight.review",
          "unknown repeated failures deduplicate to one generic bounded action");
}

} // namespace

int main() {
    testStableCodeMappingAndPrivacy();
    testAuthoritativeRecoveryAndStaleClearing();
    testDisconnectResetsAuthorityEpoch();
    testBoundsAndDeduplication();
    if (failures != 0) {
        std::cerr << failures << " Seat notification model test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Seat notification model tests passed.\n";
    return EXIT_SUCCESS;
}
