#include "hydra/host_transport.hpp"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

#ifdef _WIN32

struct OwnedProcess {
    PROCESS_INFORMATION value{};
    ~OwnedProcess() {
        if (value.hProcess != nullptr &&
            WaitForSingleObject(value.hProcess, 0) == WAIT_TIMEOUT) {
            (void)TerminateProcess(value.hProcess, 0x50375348u);
            (void)WaitForSingleObject(value.hProcess, 2000u);
        }
        if (value.hThread != nullptr) CloseHandle(value.hThread);
        if (value.hProcess != nullptr) CloseHandle(value.hProcess);
    }
};

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path makeDirectory() {
    const auto path = std::filesystem::temp_directory_path() /
        ("hydraseat-p7-shell-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path writeProfile(const std::filesystem::path& directory) {
    const auto path = directory / "workspace_config.json";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << R"JSON({
  "schema_version": 2,
  "management_seat_id": 1,
  "shareable_resources": [],
  "seats": [
    {"id":1,"name":"Seat UI 1","active":true,"target_hwnd":0,"displays":[],"primary_display":null,"keyboards":[],"mice":[],"controllers":[],"audio_output":null,"audio_input":null},
    {"id":2,"name":"Seat UI 2","active":true,"target_hwnd":0,"displays":[],"primary_display":null,"keyboards":[],"mice":[],"controllers":[],"audio_output":null,"audio_input":null}
  ]
})JSON";
    return path;
}

bool launch(std::wstring command, OwnedProcess& process) {
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    return CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, executableDirectory().c_str(),
                          &startup, &process.value) != FALSE;
}

bool launchHost(const std::filesystem::path& profile, OwnedProcess& process) {
    const auto executable = executableDirectory() / L"hydra_host.exe";
    return launch(L"\"" + executable.wstring() + L"\" --serve --profile \"" +
                      profile.wstring() + L"\"", process);
}

bool launchSeatUi(std::wstring arguments, OwnedProcess& process) {
    const auto executable = executableDirectory() / L"hydra_seat_ui.exe";
    return launch(L"\"" + executable.wstring() + L"\" " + std::move(arguments), process);
}

DWORD waitExit(OwnedProcess& process) {
    if (process.value.hProcess == nullptr ||
        WaitForSingleObject(process.value.hProcess, 10000u) != WAIT_OBJECT_0) {
        return std::numeric_limits<DWORD>::max();
    }
    DWORD code = std::numeric_limits<DWORD>::max();
    (void)GetExitCodeProcess(process.value.hProcess, &code);
    return code;
}

bool waitForClient(hydra::hostipc::HostControlClient& client,
                   hydra::hostipc::ClientRole role, hydra::SeatId seatId) {
    for (int attempt = 0; attempt < 50; ++attempt) {
        std::string error;
        if (client.connectForSeat(role, seatId, 200u, &error)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::wstring reportArgument(const std::filesystem::path& path) {
    return L" --report \"" + path.wstring() + L"\"";
}

void testTwoSeatUiProcesses() {
    const auto directory = makeDirectory();
    const auto profile = writeProfile(directory);
    OwnedProcess host;
    check(launchHost(profile, host), "controlled host process launches");
    if (host.value.hProcess == nullptr) return;

    hydra::hostipc::HostControlClient management;
    check(waitForClient(management, hydra::hostipc::ClientRole::Control, 1),
          "Management Seat connects to controlled host");
    std::string error;
    const auto before = management.getSnapshot(2000u, &error);
    check(before && before->seatGames.size() == 2u,
          "baseline contains two authoritative idle Seat lifecycles");

    const auto hostOneReport = directory / "host-seat-1.json";
    const auto hostTwoReport = directory / "host-seat-2.json";
    OwnedProcess hostUiOne;
    OwnedProcess hostUiTwo;
    check(launchSeatUi(L"--host-self-test --seat 1 --expect idle --hold-ms 200" +
                           reportArgument(hostOneReport), hostUiOne) &&
              launchSeatUi(L"--host-self-test --seat 2 --expect idle --hold-ms 200" +
                           reportArgument(hostTwoReport), hostUiTwo),
          "two Seat UI processes connect concurrently to one authority");
    check(waitExit(hostUiOne) == 0 && waitExit(hostUiTwo) == 0,
          "both Seat UI processes accept only their authoritative Seat view");
    check(readFile(hostOneReport).find("\"seat_id\":1") != std::string::npos &&
              readFile(hostTwoReport).find("\"seat_id\":2") != std::string::npos,
          "independent process reports retain exact Seat identity");

    const auto after = management.getSnapshot(2000u, &error);
    check(before && after && before->seatGames == after->seatGames &&
              before->wholeMachineReturnRequested == after->wholeMachineReturnRequested,
          "connecting and closing Seat UIs does not mutate games or runtime return policy");

    OwnedProcess crashedUi;
    check(launchSeatUi(L"--host-self-test --seat 1 --expect idle --hold-ms 5000", crashedUi),
          "Seat UI process starts for crash/restart recovery fixture");
    if (crashedUi.value.hProcess != nullptr) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        check(TerminateProcess(crashedUi.value.hProcess, 0x50375243u) != FALSE &&
                  WaitForSingleObject(crashedUi.value.hProcess, 2000u) == WAIT_OBJECT_0,
              "forced Seat UI crash is bounded to that disposable client process");
    }
    const auto afterCrash = management.getSnapshot(2000u, &error);
    check(after && afterCrash && after->seatGames == afterCrash->seatGames &&
              after->wholeMachineReturnRequested == afterCrash->wholeMachineReturnRequested,
          "Seat UI crash leaves authoritative games and whole-machine policy unchanged");

    OwnedProcess startingUi;
    check(launchSeatUi(L"--controlled-self-test --seat 2 --phase starting --expect starting "
                       L"--hold-ms 5000", startingUi),
          "launch-progress presentation process starts independently");
    if (startingUi.value.hProcess != nullptr) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        check(TerminateProcess(startingUi.value.hProcess, 0x50375354u) != FALSE &&
                  WaitForSingleObject(startingUi.value.hProcess, 2000u) == WAIT_OBJECT_0,
              "Seat UI can crash during launch-progress presentation without recovery mutation");
    }

    OwnedProcess restartedUi;
    check(launchSeatUi(L"--host-self-test --seat 1 --expect idle" +
                           reportArgument(directory / "host-seat-1-restarted.json"),
                       restartedUi) &&
              waitExit(restartedUi) == 0,
          "a Seat UI restarts and resnapshots without affecting the other Seat");

    const auto playingReport = directory / "controlled-playing.json";
    const auto recoveryReport = directory / "controlled-recovery.json";
    OwnedProcess playingUi;
    OwnedProcess recoveryUi;
    check(launchSeatUi(L"--controlled-self-test --seat 1 --phase playing "
                       L"--expect playing --hold-ms 200" + reportArgument(playingReport),
                       playingUi) &&
              launchSeatUi(L"--controlled-self-test --seat 2 --phase recovery "
                       L"--expect recovery --hold-ms 200" + reportArgument(recoveryReport),
                       recoveryUi),
          "isolated processes exercise playing and recovery presentation states");
    check(waitExit(playingUi) == 0 && waitExit(recoveryUi) == 0 &&
              readFile(playingReport).find("\"phase\":\"playing\"") != std::string::npos &&
              readFile(recoveryReport).find("\"phase\":\"recovery\"") != std::string::npos,
          "controlled playing and recovery states remain independent and bounded");

    const auto exit = management.command(
        hydra::hostipc::MessageType::ExitHostWhenIdle, 2000u, &error);
    check(exit && exit->succeeded(), "controlled host exits through Management Seat only");
    management.close();
    check(WaitForSingleObject(host.value.hProcess, 5000u) == WAIT_OBJECT_0,
          "controlled host exits without an orphan process");

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

#endif

} // namespace

int main() {
#ifdef _WIN32
    testTwoSeatUiProcesses();
#else
    std::cout << "Seat UI process test is Windows-only.\n";
#endif
    if (failures != 0) {
        std::cerr << failures << " Seat UI process test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Seat UI process tests passed.\n";
    return EXIT_SUCCESS;
}
