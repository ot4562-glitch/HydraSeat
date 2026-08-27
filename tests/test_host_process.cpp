#include "hydra/host_transport.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
  "shareable_resources": [],
  "seats": [
    {"id":1,"name":"IPC Seat 1","active":true,"target_hwnd":0,"displays":[],"primary_display":null,"keyboards":[],"mice":[],"controllers":[],"audio_output":null,"audio_input":null},
    {"id":2,"name":"IPC Seat 2","active":true,"target_hwnd":0,"displays":[],"primary_display":null,"keyboards":[],"mice":[],"controllers":[],"audio_output":null,"audio_input":null}
  ]
})JSON";
    stream.close();
    return path;
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

bool waitForClient(HostControlClient& client, ClientRole role) {
    for (int attempt = 0; attempt < 50; ++attempt) {
        std::string error;
        if (client.connect(role, 200u, &error)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

void testRealHostProcess() {
    const auto profile = writeProfile();
    OwnedProcess hostProcess;
    check(launchHost(profile, hostProcess), "real hydra_host process launches");
    if (hostProcess.process.hProcess == nullptr) return;

    HostControlClient readOnly;
    HostControlClient control;
    check(waitForClient(readOnly, ClientRole::ReadOnly),
          "read-only UI-style client connects to same-user/session host");
    check(waitForClient(control, ClientRole::Control),
          "control CLI-style client connects simultaneously");

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
              initial->seats.size() == 2u && initial->connectedControlClients >= 2u,
          "simultaneous reader sees authoritative loaded Idle snapshot");
    check(control.ping(0x123456789abcdef0ull, 2000u, &error),
          "ping lease round-trip succeeds");

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
    const auto event = subscriber.readSubscriptionEvent(2000u, &error);
    check(event.event && event.event->sequence >
              (subscription ? subscription->snapshot.transitionSequence : 0u),
          "subscription receives ordered runtime event after full snapshot");

    const auto active = readOnly.getSnapshot(2000u, &error);
    check(active && active->sessionPhase == SeatSessionPhase::Active,
          "independent UI reader observes Active without owning runtime components");

    const auto stopped = control.command(MessageType::StopAndReturnToWindows, 2000u, &error);
    check(stopped && stopped->succeeded() &&
              stopped->snapshot.sessionPhase == SeatSessionPhase::Idle,
          "StopAndReturnToWindows verifies Idle while host remains alive");

    subscriber.close();
    readOnly.close();
    const auto exited = control.command(MessageType::ExitHostWhenIdle, 2000u, &error);
    check(exited && exited->succeeded() &&
              exited->snapshot.hostPhase == HostLifecyclePhase::ExitRequested,
          "ExitHostWhenIdle is accepted only after Idle");
    control.close();
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
              restartedSnapshot->generation == 0u &&
              restartedSnapshot->transitionSequence == 1u,
          "reconnect resnapshots new host authority instead of reusing stale state");
    afterRestart.close();

    HostControlClient exitRestarted;
    check(waitForClient(exitRestarted, ClientRole::Control),
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
