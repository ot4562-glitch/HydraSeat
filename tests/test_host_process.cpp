#include "hydra/host_transport.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace hydra::hostipc;
using namespace hydra::runtime;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

#ifdef _WIN32

struct OwnedProcess {
    PROCESS_INFORMATION process{};
    ~OwnedProcess() {
        if (process.hProcess != nullptr &&
            WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
            (void)TerminateProcess(process.hProcess, 0x50344950u); // P4IP
            (void)WaitForSingleObject(process.hProcess, 2000);
        }
        if (process.hThread != nullptr) CloseHandle(process.hThread);
        if (process.hProcess != nullptr) CloseHandle(process.hProcess);
    }
};

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path writeProfile() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("hydraseat-p4-ipc-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "workspace_config.json";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << R"JSON({
  "schema_version": 2,
  "management_seat_id": 2,
  "shareable_resources": [],
  "seats": [
    {"id":1,"name":"IPC Seat 1","active":true,"target_hwnd":0,"displays":[],"primary_display":null,"keyboards":[],"mice":[],"controllers":[],"audio_output":null,"audio_input":null},
    {"id":2,"name":"IPC Seat 2","active":true,"target_hwnd":0,"displays":[],"primary_display":null,"keyboards":[],"mice":[],"controllers":[],"audio_output":null,"audio_input":null}
  ]
})JSON";
    stream.close();
    return path;
}

std::vector<hydra::SeatConfig> ipcSeats() {
    hydra::SeatConfig first;
    first.seatId = 1;
    first.name = L"IPC Seat 1";
    hydra::SeatConfig second;
    second.seatId = 2;
    second.name = L"IPC Seat 2";
    return {first, second};
}

bool launchHost(const std::filesystem::path& profile, OwnedProcess& owned) {
    const auto host = executableDirectory() / L"hydra_host.exe";
    std::wstring command = L"\"" + host.wstring() + L"\" --serve --profile \"" +
                           profile.wstring() + L"\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    return CreateProcessW(host.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, executableDirectory().c_str(),
                          &startup, &owned.process) != FALSE;
}

DWORD runHostCtl(std::wstring arguments) {
    const auto executable = executableDirectory() / L"hydra_hostctl.exe";
    std::wstring command = L"\"" + executable.wstring() + L"\" " + arguments;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr,
                       executableDirectory().c_str(), &startup, &process) == FALSE) {
        return std::numeric_limits<DWORD>::max();
    }
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, 5000u);
    DWORD exitCode = std::numeric_limits<DWORD>::max();
    if (wait == WAIT_OBJECT_0) (void)GetExitCodeProcess(process.hProcess, &exitCode);
    if (wait == WAIT_TIMEOUT) {
        (void)TerminateProcess(process.hProcess, 0x43544c54u); // CTLT
        (void)WaitForSingleObject(process.hProcess, 2000u);
    }
    CloseHandle(process.hProcess);
    return exitCode;
}

bool waitForClient(HostControlClient& client, ClientRole role, hydra::SeatId seatId = 0) {
    for (int attempt = 0; attempt < 50; ++attempt) {
        std::string error;
        const bool connected = role != ClientRole::ReadOnly
            ? client.connectForSeat(role, seatId, 200u, &error)
            : client.connect(role, 200u, &error);
        if (connected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

void testRealHostProcess() {
    check(currentHostPipeName().find(L"HydraSeat.Host.v3.") != std::wstring::npos,
          "host protocol v3 uses a distinct same-session pipe namespace");
    const auto profile = writeProfile();
    OwnedProcess hostProcess;
    check(launchHost(profile, hostProcess), "real hydra_host process launches");
    if (hostProcess.process.hProcess == nullptr) return;

    HostControlClient readOnly;
    HostControlClient control;
    check(waitForClient(readOnly, ClientRole::ReadOnly),
          "read-only UI-style client connects to same-user/session host");
    check(waitForClient(control, ClientRole::Control, 2),
          "Management Seat control client connects simultaneously");

    check(runHostCtl(L"--json snapshot") == 0,
          "hostctl snapshot reports protocol v3 state from a separate process");
    check(runHostCtl(L"--json seat-assign 1 cli-player cli-game") == 0,
          "hostctl discovers Management Seat 2 and assigns Seat 1 without hardcoded authority");
    std::string cliError;
    const auto cliPlanned = control.getSnapshot(2000u, &cliError);
    check(cliPlanned && cliPlanned->seatGames[0].phase == SeatGamePhase::Planning &&
              cliPlanned->seatGames[0].binding ==
                  SeatGameBinding{"cli-player", "cli-game"},
          "hostctl Seat assignment is authoritative and observable by another client");
    check(runHostCtl(L"--json seat-stop 1") == 0,
          "hostctl cancels a planned Seat binding");
    check(runHostCtl(L"--json seat-reconcile") == 0,
          "hostctl reconciles Seat lifecycle idempotently");
    const auto cliStopped = control.getSnapshot(2000u, &cliError);
    check(cliStopped && cliStopped->seatGames[0].phase == SeatGamePhase::Idle,
          "hostctl Seat stop returns the authoritative Seat to Idle");
    check(runHostCtl(L"seat-start 0") != 0,
          "hostctl rejects zero Seat ID before sending a mutation");

    HostControlClient otherSeatControl;
    std::string otherSeatError;
    check(!otherSeatControl.connectForSeat(ClientRole::Control, 1, 1000u, &otherSeatError) &&
              otherSeatError.find("Management Seat") != std::string::npos,
          "non-Management Seat control handshake is rejected before global commands");

    HostControlClient seatOneControl;
    HostControlClient seatTwoControl;
    check(waitForClient(seatOneControl, ClientRole::SeatControl, 1) &&
              waitForClient(seatTwoControl, ClientRole::SeatControl, 2),
          "two active Seats establish simultaneous scoped control channels");
    HostControlClient invalidSeatControl;
    check(!invalidSeatControl.connectForSeat(
              ClientRole::SeatControl, 3, 1000u, &otherSeatError),
          "unconfigured Seat cannot establish scoped control authority");

    SeatGameCommandPayload scopedAssignment;
    scopedAssignment.seatId = 1;
    scopedAssignment.binding = SeatGameBinding{"seat-ui-player", "seat-ui-game"};
    std::string scopedError;
    const auto scopedAssigned = seatOneControl.seatGameCommand(
        MessageType::AssignSeatGame, scopedAssignment, 2000u, &scopedError);
    check(scopedAssigned && scopedAssigned->succeeded() &&
              scopedAssigned->seats[0].phase == SeatGamePhase::Planning,
          "Seat-scoped client assigns only its own temporary game binding");

    std::optional<ErrorPayload> scopedPermission;
    SeatGameCommandPayload otherSeatStop;
    otherSeatStop.seatId = 1;
    const auto crossSeatDenied = seatTwoControl.seatGameCommand(
        MessageType::StopSeatGame, otherSeatStop, 2000u, &scopedError,
        &scopedPermission);
    check(!crossSeatDenied && scopedPermission &&
              scopedPermission->code == ErrorCode::PermissionDenied,
          "Seat-scoped client cannot stop another Seat lifecycle");

    scopedPermission.reset();
    const auto globalDenied = seatOneControl.command(
        MessageType::PlanSession, 2000u, &scopedError, &scopedPermission);
    check(!globalDenied && scopedPermission &&
              scopedPermission->code == ErrorCode::PermissionDenied,
          "Seat-scoped client has no whole-machine runtime authority");

    const auto scopedStopped = seatOneControl.seatGameCommand(
        MessageType::StopSeatGame, otherSeatStop, 2000u, &scopedError);
    check(scopedStopped && scopedStopped->succeeded() &&
              scopedStopped->seats[0].phase == SeatGamePhase::Idle &&
              scopedStopped->seats[1].phase == SeatGamePhase::Idle,
          "own-Seat stop preserves the other Seat state");

    std::string error;
    HostControlClient unsubscribed;
    check(waitForClient(unsubscribed, ClientRole::ReadOnly),
          "additional read-only client connects for subscription precondition test");
    error.clear();
    const auto withoutSubscription = unsubscribed.readSubscriptionEvent(10u, &error);
    check(!withoutSubscription.event && !withoutSubscription.error &&
              error == "host client has no active event subscription" && unsubscribed.connected(),
          "event reads require an explicit subscription without disconnecting the client");
    unsubscribed.close();

    for (int reconnect = 0; reconnect < 32; ++reconnect) {
        HostControlClient transient;
        check(waitForClient(transient, ClientRole::ReadOnly),
              "sequential reconnect remains available after completed client workers");
        const auto transientSnapshot = transient.getSnapshot(2000u, &error);
        check(transientSnapshot.has_value(),
              "sequential reconnect receives authoritative snapshot");
        transient.close();
    }

    const auto initial = readOnly.getSnapshot(2000u, &error);
    check(initial && initial->profileLoaded && initial->sessionPhase == SeatSessionPhase::Idle &&
              initial->seats.size() == 2u && initial->managementSeatId == 2 &&
              initial->connectedControlClients >= 2u,
          "simultaneous reader sees authoritative loaded Idle snapshot");
    check(control.ping(0x123456789abcdef0ull, 2000u, &error),
          "ping lease round-trip succeeds");

    HostControlClient lifecycleSubscriber;
    check(waitForClient(lifecycleSubscriber, ClientRole::ReadOnly),
          "Seat lifecycle event subscriber connects");
    const auto lifecycleSubscription = lifecycleSubscriber.beginSubscription(
        initial ? initial->transitionSequence : 0u, 16u, 2000u, &error);
    check(lifecycleSubscription.has_value(),
          "Seat lifecycle subscription begins from an authoritative snapshot");

    SeatGameCommandPayload seatOneAssignment;
    seatOneAssignment.seatId = 1;
    seatOneAssignment.binding = SeatGameBinding{"player-a", "controlled-game-a"};
    const auto assignedSeatOne = control.seatGameCommand(
        MessageType::AssignSeatGame, seatOneAssignment, 2000u, &error);
    check(assignedSeatOne && assignedSeatOne->succeeded() &&
              assignedSeatOne->seats.size() == 2u &&
              assignedSeatOne->seats[0].phase == SeatGamePhase::Planning,
          "Management client assigns a temporary Player/Game binding to Idle Seat 1");
    const auto seatEvent = lifecycleSubscriber.readSubscriptionEvent(2000u, &error);
    check(seatEvent.event && seatEvent.event->command == RuntimeCommand::AssignSeatGame &&
              seatEvent.event->seatId == 1,
          "subscribed UI receives ordered Seat-local mutation event with Seat identity");
    lifecycleSubscriber.close();

    HostControlClient lifecycleReconnect;
    check(waitForClient(lifecycleReconnect, ClientRole::ReadOnly),
          "UI-style reader reconnects after Seat-local mutation");
    const auto lifecycleSnapshot = lifecycleReconnect.getSnapshot(2000u, &error);
    check(lifecycleSnapshot && lifecycleSnapshot->seatGames.size() == 2u &&
              lifecycleSnapshot->seatGames[0].phase == SeatGamePhase::Planning &&
              lifecycleSnapshot->seatGames[0].binding == seatOneAssignment.binding,
          "reconnected reader receives authoritative two-Seat lifecycle snapshot");
    lifecycleReconnect.close();

    SeatGameCommandPayload seatOneOnly;
    seatOneOnly.seatId = 1;
    const auto unavailableStart = control.seatGameCommand(
        MessageType::StartSeatGame, seatOneOnly, 2000u, &error);
    check(unavailableStart && unavailableStart->code == SeatGameResultCode::InvalidState &&
              unavailableStart->seats[0].phase == SeatGamePhase::Planning,
          "Seat start fails closed until the whole-machine runtime is active");

    const auto cancelledSeatPlan = control.seatGameCommand(
        MessageType::StopSeatGame, seatOneOnly, 2000u, &error);
    check(cancelledSeatPlan && cancelledSeatPlan->succeeded() &&
              cancelledSeatPlan->seats[0].phase == SeatGamePhase::Idle,
          "Seat-local Stop cancels a planned binding without global rollback");

    SeatGameCommandPayload thirdSeat;
    thirdSeat.seatId = 3;
    thirdSeat.binding = SeatGameBinding{"player-c", "game-c"};
    const auto rejectedThird = control.seatGameCommand(
        MessageType::AssignSeatGame, thirdSeat, 2000u, &error);
    check(rejectedThird && rejectedThird->code == SeatGameResultCode::InvalidSeat,
          "IPC command rejects a third unconfigured v1 Seat without state change");

    std::optional<ErrorPayload> permissionError;
    const auto denied = readOnly.command(MessageType::PlanSession, 2000u, &error,
                                         &permissionError);
    check(!denied && permissionError &&
              permissionError->code == ErrorCode::PermissionDenied,
          "read-only client cannot mutate runtime state");

    const auto firstDuplicate = control.transact(MessageType::GetSnapshot, {}, 9001u, 2000u,
                                                  &error);
    const auto secondDuplicate = control.transact(MessageType::GetSnapshot, {}, 9001u, 2000u,
                                                   &error);
    const auto duplicateError = secondDuplicate && secondDuplicate->type == MessageType::Error
        ? decodeError(secondDuplicate->payload) : std::nullopt;
    check(firstDuplicate && firstDuplicate->type == MessageType::Snapshot && duplicateError &&
              duplicateError->code == ErrorCode::DuplicateCorrelation,
          "duplicate command correlation is rejected without replaying work");

    const auto planned = control.command(MessageType::PlanSession, 2000u, &error);
    check(planned && planned->succeeded() &&
              planned->snapshot.sessionPhase == SeatSessionPhase::Planning,
          "control client plans loaded profile");

    HostControlClient subscriber;
    check(waitForClient(subscriber, ClientRole::ReadOnly),
          "event subscriber connects alongside UI and CLI readers");
    const auto subscription = subscriber.beginSubscription(
        planned ? planned->snapshot.transitionSequence : 0u, 16u, 2000u, &error);
    check(subscription && subscription->snapshot.sessionPhase == SeatSessionPhase::Planning,
          "subscription begins with a full authoritative snapshot");

    const auto started = control.command(MessageType::StartSession, 2000u, &error);
    check(started && started->succeeded() &&
              started->snapshot.sessionPhase == SeatSessionPhase::Active,
          "StartSession prepares and activates through one correlated request");

    const auto reassignedSeatOne = control.seatGameCommand(
        MessageType::AssignSeatGame, seatOneAssignment, 2000u, &error);
    const auto unavailablePlannedGame = control.seatGameCommand(
        MessageType::StartSeatGame, seatOneOnly, 2000u, &error);
    check(reassignedSeatOne && reassignedSeatOne->succeeded() &&
              unavailablePlannedGame &&
              unavailablePlannedGame->code == SeatGameResultCode::BackendFailure &&
              unavailablePlannedGame->seats[0].phase == SeatGamePhase::Idle &&
              unavailablePlannedGame->seats[1].phase == SeatGamePhase::Idle,
          "active host without a validated game launch plan rolls Seat start back locally");
    const auto event = subscriber.readSubscriptionEvent(2000u, &error);
    check(event.event && event.event->sequence >
              (subscription ? subscription->snapshot.transitionSequence : 0u),
          "subscription receives ordered runtime event after full snapshot");

    const auto active = readOnly.getSnapshot(2000u, &error);
    check(active && active->sessionPhase == SeatSessionPhase::Active,
          "independent UI reader observes Active without owning runtime components");

    const auto reconfigure = control.command(MessageType::BeginReconfigure, 2000u, &error);
    check(reconfigure && reconfigure->succeeded() &&
              reconfigure->snapshot.sessionPhase == SeatSessionPhase::Idle &&
              reconfigure->snapshot.lastTransition &&
              reconfigure->snapshot.lastTransition->command == RuntimeCommand::BeginReconfigure,
          "BeginReconfigure performs a distinct verified rollback before editing");

    ProfilePayload editedProfile;
    editedProfile.managementSeatId = 2;
    editedProfile.seats = ipcSeats();
    editedProfile.seats[0].name = L"IPC Seat 1 Edited";
    const auto applied = control.applyProfile(editedProfile, 2000u, &error);
    check(applied && applied->succeeded() &&
              applied->snapshot.sessionPhase == SeatSessionPhase::Idle &&
              applied->snapshot.managementSeatId == 2,
          "edited profile applies only after reconfigure reaches Idle");

    const auto replanned = control.command(MessageType::PlanSession, 2000u, &error);
    check(replanned && replanned->succeeded() &&
              replanned->snapshot.sessionPhase == SeatSessionPhase::Planning &&
              (!planned || replanned->snapshot.generation > planned->snapshot.generation),
          "edited profile compiles a fresh immutable plan generation");
    const auto restartedSession = control.command(MessageType::StartSession, 2000u, &error);
    check(restartedSession && restartedSession->succeeded() &&
              restartedSession->snapshot.sessionPhase == SeatSessionPhase::Active,
          "edited profile restarts through fresh prepare/start");

    const auto applyWhileActive = control.applyProfile(editedProfile, 2000u, &error);
    check(applyWhileActive && applyWhileActive->code == RuntimeResultCode::InvalidState &&
              applyWhileActive->snapshot.sessionPhase == SeatSessionPhase::Active,
          "ApplyProfile is rejected while the session is Active");

    const auto reconfigureForTransfer = control.command(
        MessageType::BeginReconfigure, 2000u, &error);
    check(reconfigureForTransfer && reconfigureForTransfer->succeeded() &&
              reconfigureForTransfer->snapshot.sessionPhase == SeatSessionPhase::Idle,
          "second reconfigure safely returns edited session to Idle");

    ProfilePayload transferred = editedProfile;
    transferred.managementSeatId = 1;
    const auto transferredAuthority = control.applyProfile(transferred, 2000u, &error);
    check(transferredAuthority && transferredAuthority->succeeded() &&
              transferredAuthority->snapshot.managementSeatId == 1,
          "current Management Seat may explicitly transfer global authority in an Idle profile apply");

    permissionError.reset();
    const auto oldAuthorityDenied = control.command(
        MessageType::PlanSession, 2000u, &error, &permissionError);
    check(!oldAuthorityDenied && permissionError &&
              permissionError->code == ErrorCode::PermissionDenied,
          "old Management Seat loses mutation authority immediately after transfer");

    HostControlClient newManagementControl;
    check(waitForClient(newManagementControl, ClientRole::Control, 1),
          "new Management Seat can establish control authority after transfer");
    const auto transferredSnapshot = readOnly.getSnapshot(2000u, &error);
    check(transferredSnapshot && transferredSnapshot->managementSeatId == 1 &&
              transferredSnapshot->sessionPhase == SeatSessionPhase::Idle,
          "read-only clients observe the transferred authoritative Management Seat");

    subscriber.close();
    readOnly.close();
    control.close();
    const auto exited = newManagementControl.command(
        MessageType::ExitHostWhenIdle, 2000u, &error);
    check(exited && exited->succeeded() &&
              exited->snapshot.hostPhase == HostLifecyclePhase::ExitRequested,
          "only the current Management Seat may exit an Idle host");
    newManagementControl.close();
    check(WaitForSingleObject(hostProcess.process.hProcess, 5000u) == WAIT_OBJECT_0,
          "real host exits after accepted idle exit request");

    // A restarted host is a new authority instance. Clients must reconnect and
    // begin from a new full snapshot rather than inferring prior state.
    OwnedProcess restarted;
    check(launchHost(profile, restarted), "host restart launches cleanly");
    HostControlClient afterRestart;
    check(waitForClient(afterRestart, ClientRole::ReadOnly),
          "reader reconnects after host restart");
    const auto restartedSnapshot = afterRestart.getSnapshot(2000u, &error);
    check(restartedSnapshot && restartedSnapshot->profileLoaded &&
              restartedSnapshot->sessionPhase == SeatSessionPhase::Idle &&
              restartedSnapshot->managementSeatId == 2 &&
              restartedSnapshot->generation == 0u &&
              restartedSnapshot->transitionSequence == 1u,
          "reconnect resnapshots new host authority instead of reusing stale state");
    afterRestart.close();

    HostControlClient exitRestarted;
    check(waitForClient(exitRestarted, ClientRole::Control, 2),
          "control reconnects to restarted host");
    const auto exit2 = exitRestarted.command(MessageType::ExitHostWhenIdle, 2000u, &error);
    check(exit2 && exit2->succeeded(), "restarted idle host exits cleanly");
    exitRestarted.close();
    check(WaitForSingleObject(restarted.process.hProcess, 5000u) == WAIT_OBJECT_0,
          "restarted host process exits without orphaning");

    std::error_code ignored;
    std::filesystem::remove_all(profile.parent_path(), ignored);
}

#endif

} // namespace

int main() {
#ifdef _WIN32
    testRealHostProcess();
#else
    std::cout << "Host IPC process test is Windows-only; portable codec tests cover this platform.\n";
#endif
    if (failures != 0) {
        std::cerr << failures << " host IPC process test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Host IPC process tests passed.\n";
    return EXIT_SUCCESS;
}
