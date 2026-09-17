#include "hydra/controller_inventory.hpp"
#include "hydra/game_launcher.hpp"
#include "hydra/runtime_authority.hpp"

#if defined(_WIN32)
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>

namespace {

[[noreturn]] void failCheck(const char* expression, int line) {
    std::fprintf(stderr, "CHECK failed at line %d: %s\n", line, expression);
    std::fflush(stderr);
    std::exit(1);
}

void check(bool condition, const char* expression, int line) {
    if (!condition) failCheck(expression, line);
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

class ScopedHandle final {
public:
    explicit ScopedHandle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~ScopedHandle() {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_{nullptr};
};

std::uint64_t creationIdentity(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    CHECK(GetProcessTimes(process, &creation, &exit, &kernel, &user) != FALSE);
    ULARGE_INTEGER value{};
    value.LowPart = creation.dwLowDateTime;
    value.HighPart = creation.dwHighDateTime;
    return value.QuadPart;
}

hydra::controller::InventorySnapshot inventoryFor(
    std::uint8_t slot,
    const std::wstring& persistentId,
    std::uint64_t sourceGeneration) {
    hydra::controller::InventorySnapshot inventory;
    inventory.authoritative = true;
    inventory.sources.push_back({
        "xinput-slot:" + std::to_string(slot), std::nullopt,
        L"Runtime XInput Slot", hydra::controller::ApiSurface::XInput,
        hydra::controller::IdentityQuality::RuntimeOnly, slot, true,
        sourceGeneration});
    inventory.physicalControllers.push_back(
        {persistentId, L"Physical Controller", L"hid-test-path"});
    return inventory;
}

hydra::controller::SeatBinding bindingFor(
    std::uint32_t seatId,
    std::uint8_t slot,
    const std::wstring& persistentId,
    const hydra::controller::InventorySnapshot& inventory) {
    const auto paired = hydra::controller::pairPhysicalControllerToXInput(
        seatId, persistentId, slot, inventory);
    CHECK(paired.status == hydra::controller::PairingStatus::Ok);
    CHECK(paired.binding.has_value());
    return *paired.binding;
}

std::wstring eventName(std::uint32_t seatId) {
    return L"HydraSeat.GameLauncher.Ready." + std::to_wstring(GetCurrentProcessId()) +
           L"." + std::to_wstring(seatId);
}

hydra::GameProfile childProfile(
    const std::wstring& childPath,
    const std::wstring& pipeEndpoint,
    std::uint32_t seatId,
    std::uint64_t sourceGeneration,
    const std::wstring& readyEvent) {
    hydra::GameProfile profile;
    profile.title = L"HydraSeat launch ownership probe";
    profile.executablePath = childPath;
    profile.launchArguments = pipeEndpoint + L" " + std::to_wstring(seatId) + L" " +
                              std::to_wstring(sourceGeneration) + L" " + readyEvent;
    return profile;
}

void assertProcessAliveAndExact(const hydra::runtime::ProcessIdentity& identity) {
    ScopedHandle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                     FALSE, identity.pid));
    CHECK(process.get() != nullptr);
    CHECK(creationIdentity(process.get()) == identity.creationIdentity);
    CHECK(WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT);
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    CHECK(argc == 2);
    const std::wstring childPath = argv[1];

    hydra::runtime::SessionController runtime;
    hydra::GameLauncher launcher;

    hydra::WorkspaceConfig seat1;
    seat1.workspaceId = 1;
    hydra::WorkspaceConfig seat2;
    seat2.workspaceId = 2;

    const auto inventory1 = inventoryFor(0, L"container-seat-1", 7);
    const auto inventory2 = inventoryFor(1, L"container-seat-2", 9);
    const auto binding1 = bindingFor(1, 0, L"container-seat-1", inventory1);
    const auto binding2 = bindingFor(2, 1, L"container-seat-2", inventory2);

    const std::wstring pipe1 = L"\\\\.\\pipe\\HydraSeat-Launch-Test-1";
    const std::wstring pipe2 = L"\\\\.\\pipe\\HydraSeat-Launch-Test-2";
    const std::wstring readyName1 = eventName(1);
    const std::wstring readyName2 = eventName(2);
    ScopedHandle ready1(CreateEventW(nullptr, TRUE, FALSE, readyName1.c_str()));
    ScopedHandle ready2(CreateEventW(nullptr, TRUE, FALSE, readyName2.c_str()));
    CHECK(ready1.get() != nullptr);
    CHECK(ready2.get() != nullptr);

    const auto profile1 = childProfile(childPath, pipe1, 1, 7, readyName1);
    const auto profile2 = childProfile(childPath, pipe2, 2, 9, readyName2);

    CHECK(launcher.launchGameForWorkspace(
        profile1, seat1, runtime, binding1, inventory1, pipe1));
    CHECK(WaitForSingleObject(ready1.get(), 5000) == WAIT_OBJECT_0);

    const auto snapshot1 = runtime.snapshot(1);
    CHECK(snapshot1.has_value());
    CHECK(snapshot1->active);
    CHECK(snapshot1->process.has_value());
    CHECK(snapshot1->controllerBinding == binding1);
    assertProcessAliveAndExact(*snapshot1->process);

    // A second launch must not silently replace the already-owned Seat process.
    CHECK(!launcher.launchGameForWorkspace(
        profile1, seat1, runtime, binding1, inventory1, pipe1));
    const auto duplicateSnapshot = runtime.snapshot(1);
    CHECK(duplicateSnapshot.has_value());
    CHECK(duplicateSnapshot->process == snapshot1->process);

    CHECK(launcher.launchGameForWorkspace(
        profile2, seat2, runtime, binding2, inventory2, pipe2));
    CHECK(WaitForSingleObject(ready2.get(), 5000) == WAIT_OBJECT_0);
    const auto snapshot2 = runtime.snapshot(2);
    CHECK(snapshot2.has_value());
    CHECK(snapshot2->active);
    CHECK(snapshot2->process.has_value());
    CHECK(snapshot2->controllerBinding == binding2);
    assertProcessAliveAndExact(*snapshot2->process);

    // Seat 1 teardown must not disturb Seat 2.
    CHECK(launcher.stopWorkspaceGame(1));
    const auto stopped1 = runtime.snapshot(1);
    CHECK(stopped1.has_value());
    CHECK(!stopped1->active);
    const auto stillRunning2 = runtime.snapshot(2);
    CHECK(stillRunning2.has_value());
    CHECK(stillRunning2->active);
    CHECK(stillRunning2->process == snapshot2->process);
    assertProcessAliveAndExact(*stillRunning2->process);

    CHECK(launcher.stopWorkspaceGame(2));
    const auto stopped2 = runtime.snapshot(2);
    CHECK(stopped2.has_value());
    CHECK(!stopped2->active);

    // Process creation failure must roll back the activation and binding.
    hydra::GameProfile missing = profile1;
    missing.executablePath = L"C:\\HydraSeat\\definitely-missing-game.exe";
    CHECK(!launcher.launchGameForWorkspace(
        missing, seat1, runtime, binding1, inventory1, pipe1));
    const auto rolledBack = runtime.snapshot(1);
    CHECK(rolledBack.has_value());
    CHECK(!rolledBack->active);
    CHECK(!rolledBack->process.has_value());
    CHECK(!rolledBack->controllerBinding.has_value());

    // Destruction must reap any remaining launcher-owned process and end its activation.
    hydra::runtime::SessionController destructorRuntime;
    CHECK(ResetEvent(ready1.get()) != FALSE);
    {
        hydra::GameLauncher scopedLauncher;
        CHECK(scopedLauncher.launchGameForWorkspace(
            profile1, seat1, destructorRuntime, binding1, inventory1, pipe1));
        CHECK(WaitForSingleObject(ready1.get(), 5000) == WAIT_OBJECT_0);
        const auto duringLifetime = destructorRuntime.snapshot(1);
        CHECK(duringLifetime.has_value());
        CHECK(duringLifetime->active);
        CHECK(duringLifetime->process.has_value());
        assertProcessAliveAndExact(*duringLifetime->process);
    }
    const auto afterDestruction = destructorRuntime.snapshot(1);
    CHECK(afterDestruction.has_value());
    CHECK(!afterDestruction->active);
    CHECK(!afterDestruction->process.has_value());
    CHECK(!afterDestruction->controllerBinding.has_value());

    return 0;
}
#else
int main() {
    return 0;
}
#endif
