#include "hydra/controller_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using namespace hydra;
using namespace hydra::controller;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

gatec::NormalizedXInputCapabilities vibrationCapabilities() {
    gatec::NormalizedXInputCapabilities result;
    result.vibrationSupported = true;
    result.leftMotorMaximum = 65535u;
    result.rightMotorMaximum = 65535u;
    return result;
}

SourceDescriptor xinput(std::uint8_t slot, bool connected = true,
                        std::optional<std::wstring> persistentId = std::nullopt) {
    SourceDescriptor result;
    result.runtimeKey = "xinput-slot:" + std::to_string(slot);
    result.persistentId = std::move(persistentId);
    result.displayName = L"XInput fixture";
    result.api = ApiSurface::XInput;
    result.identityQuality = result.persistentId ? IdentityQuality::Stable
                                                 : IdentityQuality::RuntimeOnly;
    result.runtimeXInputSlotHint = slot;
    result.connected = connected;
    result.stateAvailable = connected;
    result.vibrationSupported = connected;
    result.capabilities = vibrationCapabilities();
    result.gamepad.buttons = static_cast<std::uint16_t>(1u << slot);
    result.battery.available = connected;
    result.battery.deviceType = gatec::XInputBatteryDeviceType::Gamepad;
    result.battery.batteryType = connected ? gatec::XInputBatteryType::Wired
                                           : gatec::XInputBatteryType::Disconnected;
    result.battery.batteryLevel = connected ? gatec::XInputBatteryLevel::Full
                                            : gatec::XInputBatteryLevel::Empty;
    return result;
}

directinput::DirectInputInstanceId directInputId(std::uint32_t value) {
    directinput::DirectInputInstanceId result;
    result.data1 = value;
    result.data2 = static_cast<std::uint16_t>(value >> 16u);
    result.data3 = static_cast<std::uint16_t>(0x4000u | (value & 0x0fffu));
    result.data4 = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    return result;
}

SourceDescriptor directInput(std::uint32_t value, bool connected = true) {
    SourceDescriptor result;
    result.api = ApiSurface::DirectInput;
    result.identityQuality = IdentityQuality::Stable;
    result.directInputInstanceId = directInputId(value);
    result.persistentId = formatDirectInputPersistentId(*result.directInputInstanceId);
    result.runtimeKey = "directinput-fixture:" + std::to_string(value);
    result.displayName = L"DirectInput fixture";
    result.connected = connected;
    return result;
}

class FakeBackend final : public SourceBackend {
public:
    std::vector<SourceDescriptor> records;
    bool failScan{false};
    bool failVibration{false};
    std::vector<std::tuple<std::string, std::uint16_t, std::uint16_t>> vibrations;
    int scans{0};

    bool scan(std::vector<SourceDescriptor>& sources,
              std::string& error) noexcept override {
        ++scans;
        if (failScan) {
            error = "injected controller scan failure";
            return false;
        }
        sources = records;
        error.clear();
        return true;
    }

    bool setVibration(std::string_view runtimeKey,
                      std::uint16_t leftMotor,
                      std::uint16_t rightMotor,
                      std::string& error) noexcept override {
        if (failVibration) {
            error = "injected vibration failure";
            return false;
        }
        vibrations.emplace_back(std::string(runtimeKey), leftMotor, rightMotor);
        error.clear();
        return true;
    }
};

void testStableAndRuntimeBindingPolicy() {
    SourceSnapshot snapshot;
    auto runtime = xinput(0);
    runtime.sourceGeneration = 2;
    auto stable = xinput(1, true, L"gameinput-device:alpha");
    stable.sourceGeneration = 7;
    auto direct = directInput(42);
    direct.sourceGeneration = 3;
    snapshot.sources = {direct, stable, runtime};

    const std::vector<SeatBindingRequest> requests{
        {1, ApiSurface::XInput, L"gameinput-device:alpha", std::nullopt},
        {2, ApiSurface::XInput, std::nullopt, std::uint8_t{0}},
    };
    const auto plan = planSeatBindings(requests, snapshot);
    check(plan.valid && plan.bindings.size() == 2,
          "stable controller ID and explicit session-only XInput slot can coexist");
    check(plan.bindings[0].seatId == 1 &&
              plan.bindings[0].persistentControllerId == L"gameinput-device:alpha" &&
              plan.bindings[0].sourceGeneration == 7,
          "stable binding preserves persistent ID and source generation");
    check(plan.bindings[1].seatId == 2 &&
              !plan.bindings[1].persistentControllerId &&
              plan.bindings[1].runtimeXInputSlotHint == 0,
          "runtime-only XInput mapping is explicitly session-local");

    const std::vector<SeatBindingRequest> missingIdentity{
        {1, ApiSurface::XInput, std::nullopt, std::nullopt},
    };
    const auto missing = planSeatBindings(missingIdentity, snapshot);
    check(!missing.valid && missing.issues.size() == 1 &&
              missing.issues[0].code == BindingIssueCode::MissingPersistentIdentity,
          "XInput slot is never silently persisted or auto-selected without explicit session hint");

    const std::vector<SeatBindingRequest> duplicate{
        {1, ApiSurface::XInput, L"gameinput-device:alpha", std::nullopt},
        {2, ApiSurface::XInput, L"gameinput-device:alpha", std::nullopt},
    };
    const auto duplicatePlan = planSeatBindings(duplicate, snapshot);
    check(!duplicatePlan.valid &&
              std::any_of(duplicatePlan.issues.begin(), duplicatePlan.issues.end(),
                          [](const BindingIssue& issue) {
                              return issue.code == BindingIssueCode::SourceAlreadyAssigned;
                          }),
          "one exclusive controller source cannot be silently shared across two Seats");

    std::vector<SeatBindingRequest> three{
        {1, ApiSurface::XInput, std::nullopt, std::uint8_t{0}},
        {2, ApiSurface::XInput, L"gameinput-device:alpha", std::nullopt},
        {3, ApiSurface::DirectInput, direct.persistentId, std::nullopt},
    };
    const auto limited = planSeatBindings(three, snapshot);
    check(!limited.valid && limited.issues[0].code == BindingIssueCode::V1SeatLimitExceeded,
          "controller binding layer preserves the v1 two-active-Seat boundary");
}

void testPollGenerationDisconnectReconnectAndWorker() {
    auto backend = std::make_shared<FakeBackend>();
    backend->records = {xinput(0, true)};
    auto worker = std::make_shared<PollWorker>(backend);
    std::string error;
    check(worker->pollOnce(&error), "first controller poll succeeds");
    const auto first = worker->snapshot();
    check(first.sources.size() == 1 && first.sources[0].sourceGeneration == 1,
          "first observed controller source begins at generation one");

    backend->records[0].connected = false;
    backend->records[0].stateAvailable = false;
    check(worker->pollOnce(&error), "disconnect poll succeeds");
    const auto disconnected = worker->snapshot();
    check(disconnected.sources[0].sourceGeneration == 2,
          "disconnect advances source generation");

    backend->records[0] = xinput(0, true);
    check(worker->pollOnce(&error), "reconnect poll succeeds");
    const auto reconnected = worker->snapshot();
    check(reconnected.sources[0].sourceGeneration == 3,
          "reconnect requires a new source generation");

    check(!worker->start(std::chrono::milliseconds(1), &error),
          "poll worker rejects unbounded high-frequency polling");
    check(worker->start(std::chrono::milliseconds(5), &error),
          "poll worker starts outside the latency-sensitive callback path");
    std::this_thread::sleep_for(std::chrono::milliseconds(18));
    check(worker->running(), "controller poll worker remains live during bounded polling");
    worker->stop();
    check(!worker->running() && backend->scans >= 4,
          "controller poll worker stops deterministically after multiple scans");
}

void testRuntimeMappingStateReconnectAndVibrationOwnership() {
    auto backend = std::make_shared<FakeBackend>();
    backend->records = {xinput(0, true), xinput(1, true, L"stable-pad-b")};
    auto worker = std::make_shared<PollWorker>(backend);
    SeatControllerRuntime runtime(worker, backend);
    const std::vector<SeatBindingRequest> requests{
        {1, ApiSurface::XInput, std::nullopt, std::uint8_t{0}},
        {2, ApiSurface::XInput, L"stable-pad-b", std::nullopt},
    };
    std::string error;
    check(runtime.configure(requests, &error),
          "two controller Seat bindings configure from one bounded source snapshot");

    gatec::VirtualXInputContext seat1Context;
    gatec::VirtualXInputContext seat2Context;
    check(runtime.mapSeatToContext(1, seat1Context) == gatec::VirtualXInputResult::Success &&
              runtime.mapSeatToContext(2, seat2Context) == gatec::VirtualXInputResult::Success,
          "each Seat maps only its selected controller source into its process-local context");
    check(runtime.updateSeatContext(1, seat1Context) == gatec::VirtualXInputResult::Success &&
              runtime.updateSeatContext(2, seat2Context) == gatec::VirtualXInputResult::Success,
          "poll snapshots feed normalized state/capabilities to independent Seat contexts");

    gatec::VirtualXInputState state1;
    gatec::VirtualXInputState state2;
    check(seat1Context.getState(0, state1) == gatec::VirtualXInputResult::Success &&
              seat2Context.getState(0, state2) == gatec::VirtualXInputResult::Success &&
              state1.gamepad.buttons != state2.gamepad.buttons,
          "two Seat contexts retain distinct controller state");

    check(runtime.requestVibration(1, seat1Context, 0, 1234, 5678, &error) &&
              backend->vibrations.size() == 1 &&
              std::get<0>(backend->vibrations[0]) == "xinput-slot:0" &&
              std::get<1>(backend->vibrations[0]) == 1234 &&
              std::get<2>(backend->vibrations[0]) == 5678,
          "vibration reaches only the exact current physical source selected for Seat 1");
    check(!runtime.requestVibration(2, seat1Context, 0, 1, 2, &error),
          "Seat 1 context cannot be reused to vibrate Seat 2's physical source");

    backend->records[0] = xinput(0, false);
    check(runtime.refresh(&error), "runtime observes controller disconnect");
    check(runtime.updateSeatContext(1, seat1Context) ==
              gatec::VirtualXInputResult::Success,
          "disconnect clears the Seat-local source state with the old exact generation");
    check(seat1Context.getState(0, state1) == gatec::VirtualXInputResult::Disconnected,
          "disconnected physical source is visible as disconnected to the Seat context");

    backend->records[0] = xinput(0, true);
    backend->records[0].gamepad.buttons = 0x1000u;
    check(runtime.refresh(&error), "runtime observes controller reconnect");
    check(runtime.updateSeatContext(1, seat1Context) ==
              gatec::VirtualXInputResult::Success,
          "reconnect is accepted only as a newer source generation");
    check(seat1Context.getState(0, state1) == gatec::VirtualXInputResult::Success &&
              state1.mapping.sourceGeneration == 3 && state1.gamepad.buttons == 0x1000u,
          "reconnected source replaces stale state with generation three");
    check(runtime.requestVibration(1, seat1Context, 0, 9, 10, &error) &&
              backend->vibrations.size() == 2,
          "post-reconnect vibration is regenerated from the current mapping and generation");
}

void testDirectInputStableIdentityAndApiSeparation() {
    const auto id = directInputId(0x12345678u);
    const auto formatted = formatDirectInputPersistentId(id);
    check(formatted == L"directinput:{12345678-1234-4678-0102-030405060708}",
          "DirectInput persistent identity has deterministic fixed-width GUID text");

    SourceSnapshot snapshot;
    auto source = directInput(0x12345678u);
    source.sourceGeneration = 1;
    snapshot.sources = {source};
    const std::vector<SeatBindingRequest> requests{
        {1, ApiSurface::DirectInput, source.persistentId, std::nullopt},
    };
    const auto plan = planSeatBindings(requests, snapshot);
    check(plan.valid && plan.bindings.size() == 1 &&
              plan.bindings[0].api == ApiSurface::DirectInput,
          "DirectInput instance GUID can persist independently from XInput slot hints");

    auto backend = std::make_shared<FakeBackend>();
    backend->records = {source};
    auto worker = std::make_shared<PollWorker>(backend);
    SeatControllerRuntime runtime(worker, backend);
    check(runtime.configure(requests), "DirectInput stable binding configures");
    gatec::VirtualXInputContext context;
    check(runtime.mapSeatToContext(1, context) == gatec::VirtualXInputResult::InvalidState,
          "DirectInput source is never silently projected as XInput state without the matching API backend");
}

} // namespace

int main() {
    testStableAndRuntimeBindingPolicy();
    testPollGenerationDisconnectReconnectAndWorker();
    testRuntimeMappingStateReconnectAndVibrationOwnership();
    testDirectInputStableIdentityAndApiSeparation();

    if (failures != 0) {
        std::cerr << failures << " controller runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Controller runtime tests passed.\n";
    return EXIT_SUCCESS;
}
