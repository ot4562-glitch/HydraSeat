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
    result.capabilitiesAvailable = connected;
    result.batteryInformationAvailable = connected;
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

    auto runtimeQualityId = xinput(2, true, L"runtime-quality-id");
    runtimeQualityId.identityQuality = IdentityQuality::RuntimeOnly;
    runtimeQualityId.sourceGeneration = 1u;
    SourceSnapshot runtimeQualitySnapshot;
    runtimeQualitySnapshot.sources = {runtimeQualityId};
    const std::vector<SeatBindingRequest> persistentAgainstRuntimeQuality{
        {1, ApiSurface::XInput, L"runtime-quality-id", std::nullopt},
    };
    const auto runtimeQualityPlan =
        planSeatBindings(persistentAgainstRuntimeQuality, runtimeQualitySnapshot);
    check(!runtimeQualityPlan.valid && runtimeQualityPlan.bindings.empty(),
          "runtime-only identity quality cannot satisfy a persistent controller binding");

    auto stableAtExplicitSlot = xinput(3, true, L"stable-slot-device");
    stableAtExplicitSlot.sourceGeneration = 4u;
    SourceSnapshot explicitSlotSnapshot;
    explicitSlotSnapshot.sources = {stableAtExplicitSlot};
    const std::vector<SeatBindingRequest> explicitSlotRequest{
        {1, ApiSurface::XInput, std::nullopt, std::uint8_t{3}},
    };
    const auto explicitSlotPlan =
        planSeatBindings(explicitSlotRequest, explicitSlotSnapshot);
    check(explicitSlotPlan.valid && explicitSlotPlan.bindings.size() == 1u &&
              !explicitSlotPlan.bindings[0].persistentControllerId &&
              explicitSlotPlan.bindings[0].runtimeXInputSlotHint == 3u,
          "explicit runtime-slot authority is not silently upgraded to an incidental persistent ID");

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

    backend->records[0] = xinput(0, false);
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
    backend->records[0].capabilities.subtype = 1u;
    backend->records[0].battery.batteryType = gatec::XInputBatteryType::Alkaline;
    backend->records[1].capabilities.subtype = 2u;
    backend->records[1].battery.batteryType = gatec::XInputBatteryType::Nimh;
    backend->records[1].battery.batteryLevel = gatec::XInputBatteryLevel::Medium;
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
          "poll snapshots feed one coherent state/capability/battery generation to each Seat context");

    gatec::VirtualXInputState state1;
    gatec::VirtualXInputState state2;
    gatec::VirtualXInputCapabilities capabilities1;
    gatec::VirtualXInputCapabilities capabilities2;
    gatec::VirtualXInputBattery battery1;
    gatec::VirtualXInputBattery battery2;
    check(seat1Context.getState(0, state1) == gatec::VirtualXInputResult::Success &&
              seat2Context.getState(0, state2) == gatec::VirtualXInputResult::Success &&
              state1.gamepad.buttons != state2.gamepad.buttons,
          "two Seat contexts retain distinct controller state");
    check(seat1Context.getCapabilities(0, capabilities1) ==
                  gatec::VirtualXInputResult::Success &&
              seat2Context.getCapabilities(0, capabilities2) ==
                  gatec::VirtualXInputResult::Success &&
              seat1Context.getBattery(0, battery1) ==
                  gatec::VirtualXInputResult::Success &&
              seat2Context.getBattery(0, battery2) ==
                  gatec::VirtualXInputResult::Success &&
              capabilities1.mapping == state1.mapping &&
              battery1.mapping == state1.mapping &&
              capabilities2.mapping == state2.mapping &&
              battery2.mapping == state2.mapping &&
              capabilities1.capabilities.subtype == 1u &&
              capabilities2.capabilities.subtype == 2u &&
              battery1.battery.batteryType == gatec::XInputBatteryType::Alkaline &&
              battery2.battery.batteryType == gatec::XInputBatteryType::Nimh,
          "capabilities and battery data remain attached to the same Seat-owned identity and generation as state");

    check(runtime.requestVibration(1, seat1Context, 0, 1234, 5678, &error) &&
              backend->vibrations.size() == 1 &&
              std::get<0>(backend->vibrations[0]) == "xinput-slot:0" &&
              std::get<1>(backend->vibrations[0]) == 1234 &&
              std::get<2>(backend->vibrations[0]) == 5678,
          "vibration reaches only the exact current runtime source selected for Seat 1");
    check(!runtime.requestVibration(2, seat1Context, 0, 1, 2, &error),
          "Seat 1 context cannot be reused to vibrate Seat 2's runtime source");

    const auto seat2StateBefore = state2;
    const auto seat2CapabilitiesBefore = capabilities2;
    const auto seat2BatteryBefore = battery2;
    backend->records[0] = xinput(0, false);
    check(runtime.refresh(&error), "runtime observes controller disconnect");
    check(runtime.updateSeatContext(1, seat1Context) ==
              gatec::VirtualXInputResult::Success,
          "disconnect clears the Seat-local source state with the old exact generation");
    check(seat1Context.getState(0, state1) == gatec::VirtualXInputResult::Disconnected,
          "disconnected physical source is visible as disconnected rather than neutral success");
    check(seat2Context.getState(0, state2) == gatec::VirtualXInputResult::Success &&
              seat2Context.getCapabilities(0, capabilities2) ==
                  gatec::VirtualXInputResult::Success &&
              seat2Context.getBattery(0, battery2) ==
                  gatec::VirtualXInputResult::Success &&
              state2 == seat2StateBefore && capabilities2 == seat2CapabilitiesBefore &&
              battery2 == seat2BatteryBefore,
          "Seat A disconnect leaves Seat B state, metadata, and generation unchanged");

    backend->records[0] = xinput(0, true);
    backend->records[0].gamepad.buttons = 0x1000u;
    backend->records[0].capabilities.subtype = 3u;
    backend->records[0].battery.batteryType = gatec::XInputBatteryType::Wired;
    check(runtime.refresh(&error), "runtime observes controller reconnect");
    check(runtime.updateSeatContext(1, seat1Context) ==
              gatec::VirtualXInputResult::Success,
          "reconnect is accepted only as a newer source generation");
    check(seat1Context.getState(0, state1) == gatec::VirtualXInputResult::Success &&
              seat1Context.getCapabilities(0, capabilities1) ==
                  gatec::VirtualXInputResult::Success &&
              seat1Context.getBattery(0, battery1) ==
                  gatec::VirtualXInputResult::Success &&
              state1.mapping.sourceGeneration == 3u &&
              capabilities1.mapping == state1.mapping &&
              battery1.mapping == state1.mapping &&
              state1.gamepad.buttons == 0x1000u &&
              capabilities1.capabilities.subtype == 3u &&
              battery1.battery.batteryType == gatec::XInputBatteryType::Wired,
          "reconnect publishes state, capabilities, and battery atomically as generation three");
    check(runtime.requestVibration(1, seat1Context, 0, 9, 10, &error) &&
              backend->vibrations.size() == 2,
          "post-reconnect vibration is regenerated from the current mapping and generation");
}

void testMissingMetadataFailsExplicitlyWithoutStaleSuccess() {
    auto backend = std::make_shared<FakeBackend>();
    backend->records = {xinput(0, true)};
    auto worker = std::make_shared<PollWorker>(backend);
    SeatControllerRuntime runtime(worker, backend);
    const std::vector<SeatBindingRequest> requests{
        {1, ApiSurface::XInput, std::nullopt, std::uint8_t{0}},
    };
    std::string error;
    check(runtime.configure(requests, &error),
          "metadata availability fixture configures");
    gatec::VirtualXInputContext context;
    check(runtime.mapSeatToContext(1, context) == gatec::VirtualXInputResult::Success &&
              runtime.updateSeatContext(1, context) == gatec::VirtualXInputResult::Success,
          "initial complete XInput snapshot is published");

    gatec::VirtualXInputCapabilities capabilities;
    gatec::VirtualXInputBattery battery;
    check(context.getCapabilities(0, capabilities) == gatec::VirtualXInputResult::Success &&
              context.getBattery(0, battery) == gatec::VirtualXInputResult::Success,
          "initial capability and battery metadata are observable");

    backend->records[0].capabilitiesAvailable = false;
    backend->records[0].batteryInformationAvailable = false;
    backend->records[0].vibrationSupported = false;
    check(runtime.refresh(&error) &&
              runtime.updateSeatContext(1, context) == gatec::VirtualXInputResult::Success,
          "state remains connected when optional native metadata queries become unavailable");
    gatec::VirtualXInputState state;
    check(context.getState(0, state) == gatec::VirtualXInputResult::Success && state.connected &&
              context.getCapabilities(0, capabilities) ==
                  gatec::VirtualXInputResult::Disconnected &&
              context.getBattery(0, battery) ==
                  gatec::VirtualXInputResult::Disconnected,
          "missing capability or battery query data cannot reuse a previous successful payload");
    check(!runtime.requestVibration(1, context, 0, 1, 2, &error),
          "missing current capabilities fails rumble closed instead of assuming support");
}

void testScanFailureInvalidatesSeatAuthority() {
    auto backend = std::make_shared<FakeBackend>();
    backend->records = {xinput(0, true)};
    auto worker = std::make_shared<PollWorker>(backend);
    SeatControllerRuntime runtime(worker, backend);
    const std::vector<SeatBindingRequest> requests{
        {1, ApiSurface::XInput, std::nullopt, std::uint8_t{0}},
    };
    std::string error;
    check(runtime.configure(requests, &error),
          "scan failure fixture configures from an authoritative snapshot");
    gatec::VirtualXInputContext context;
    check(runtime.mapSeatToContext(1, context) == gatec::VirtualXInputResult::Success &&
              runtime.updateSeatContext(1, context) == gatec::VirtualXInputResult::Success,
          "scan failure fixture publishes its initial controller generation");
    check(runtime.requestVibration(1, context, 0, 10u, 20u, &error) &&
              backend->vibrations.size() == 1u,
          "scan failure fixture begins with valid output authority");

    backend->failScan = true;
    check(!runtime.refresh(&error) && !error.empty(),
          "a native inventory scan failure is surfaced to the Seat runtime");
    gatec::VirtualXInputState state;
    check(runtime.updateSeatContext(1, context) == gatec::VirtualXInputResult::Success &&
              context.getState(0, state) == gatec::VirtualXInputResult::Disconnected,
          "failed scan invalidates the last published Seat controller state instead of reusing it");
    check(!runtime.requestVibration(1, context, 0, 30u, 40u, &error) &&
              backend->vibrations.size() == 1u,
          "failed scan revokes output authority even if the backend could still accept a command");

    backend->failScan = false;
    check(runtime.refresh(&error) &&
              runtime.updateSeatContext(1, context) == gatec::VirtualXInputResult::Success &&
              context.getState(0, state) == gatec::VirtualXInputResult::Success &&
              state.mapping.sourceGeneration >= 3u,
          "scan recovery republishes the source only through a newer generation");
}

void testMalformedSourceInventoryFailsClosed() {
    const auto rejected = [](SourceDescriptor record) {
        auto backend = std::make_shared<FakeBackend>();
        backend->records = {std::move(record)};
        auto worker = std::make_shared<PollWorker>(backend);
        std::string error;
        return !worker->pollOnce(&error) && !error.empty();
    };

    auto invalidApi = xinput(0, true);
    invalidApi.api = static_cast<ApiSurface>(0xffu);
    check(rejected(std::move(invalidApi)),
          "unknown controller API enum is rejected before snapshot publication");

    auto invalidIdentity = xinput(0, true);
    invalidIdentity.identityQuality = static_cast<IdentityQuality>(0xffu);
    check(rejected(std::move(invalidIdentity)),
          "unknown controller identity enum is rejected before snapshot publication");

    auto invalidCapabilities = xinput(0, true);
    invalidCapabilities.capabilities.type =
        static_cast<gatec::XInputCapabilityType>(0xffu);
    check(rejected(std::move(invalidCapabilities)),
          "malformed advertised capability payload is rejected at inventory boundary");

    auto invalidBattery = xinput(0, true);
    invalidBattery.battery.batteryLevel =
        static_cast<gatec::XInputBatteryLevel>(0xffu);
    check(rejected(std::move(invalidBattery)),
          "malformed advertised battery payload is rejected at inventory boundary");

    auto disconnectedLeak = xinput(0, false);
    disconnectedLeak.stateAvailable = true;
    check(rejected(std::move(disconnectedLeak)),
          "disconnected inventory entry cannot advertise live controller state");
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

void testFailedRemapDoesNotCommitNewSourceGeneration() {
    auto backend = std::make_shared<FakeBackend>();
    backend->records = {xinput(0, true)};
    auto worker = std::make_shared<PollWorker>(backend);
    SeatControllerRuntime runtime(worker, backend);
    const std::vector<SeatBindingRequest> requests{
        {1, ApiSurface::XInput, std::nullopt, std::uint8_t{0}},
    };
    std::string error;
    check(runtime.configure(requests, &error),
          "stale-generation fixture configures its controller binding");

    gatec::VirtualXInputContext context;
    check(runtime.mapSeatToContext(1, context, 1) ==
              gatec::VirtualXInputResult::Success,
          "fixture maps the source to logical slot one");
    const auto initial = runtime.binding(1);
    check(initial && initial->sourceGeneration == 1u,
          "fixture begins with committed source generation one");

    backend->records[0] = xinput(0, false);
    check(runtime.refresh(&error), "fixture observes disconnect generation");
    backend->records[0] = xinput(0, true);
    check(runtime.refresh(&error), "fixture observes reconnect generation");

    check(runtime.mapSeatToContext(1, context, 0) ==
              gatec::VirtualXInputResult::DuplicateSource,
          "same physical source cannot be remapped into a second logical slot");
    const auto afterRejectedRemap = runtime.binding(1);
    check(afterRejectedRemap && afterRejectedRemap->sourceGeneration == 1u,
          "failed remap does not commit the unaccepted reconnect generation into Seat binding state");
}

void testBindingAuthoritySurvivesBackendIdentityDrift() {
    auto backend = std::make_shared<FakeBackend>();
    backend->records = {xinput(0, true, L"stable-pad-a")};
    auto worker = std::make_shared<PollWorker>(backend);
    SeatControllerRuntime runtime(worker, backend);
    const std::vector<SeatBindingRequest> requests{
        {1, ApiSurface::XInput, L"stable-pad-a", std::nullopt},
    };
    std::string error;
    check(runtime.configure(requests, &error),
          "stable identity drift fixture configures Seat 1");

    gatec::VirtualXInputContext context;
    check(runtime.mapSeatToContext(1, context) ==
              gatec::VirtualXInputResult::Success &&
              runtime.updateSeatContext(1, context) ==
                  gatec::VirtualXInputResult::Success,
          "stable identity drift fixture publishes the original source");
    gatec::VirtualXInputState state;
    check(context.getState(0, state) == gatec::VirtualXInputResult::Success,
          "original stable controller state is connected");

    backend->records[0] = xinput(0, true, L"stable-pad-b");
    backend->records[0].gamepad.buttons = 0x4000u;
    check(runtime.refresh(&error),
          "backend stable identity replacement is observed as a fresh poll");
    check(runtime.updateSeatContext(1, context) ==
              gatec::VirtualXInputResult::Success &&
              context.getState(0, state) ==
                  gatec::VirtualXInputResult::Disconnected,
          "a different stable identity behind the same runtime key disconnects the old Seat view instead of inheriting it");
    check(!runtime.requestVibration(1, context, 0, 1u, 2u, &error) &&
              backend->vibrations.empty(),
          "a replacement stable identity cannot inherit Seat 1 rumble authority");

    backend->records[0] = xinput(0, true, L"stable-pad-a");
    backend->records[0].gamepad.buttons = 0x2000u;
    check(runtime.refresh(&error) &&
              runtime.updateSeatContext(1, context) ==
                  gatec::VirtualXInputResult::Success &&
              context.getState(0, state) == gatec::VirtualXInputResult::Success &&
              state.gamepad.buttons == 0x2000u &&
              state.mapping.sourceGeneration >= 3u,
          "the original stable identity can return only through a newer source generation");

    auto runtimeBackend = std::make_shared<FakeBackend>();
    runtimeBackend->records = {xinput(0, true)};
    auto runtimeWorker = std::make_shared<PollWorker>(runtimeBackend);
    SeatControllerRuntime runtimeOnly(runtimeWorker, runtimeBackend);
    const std::vector<SeatBindingRequest> runtimeRequest{
        {1, ApiSurface::XInput, std::nullopt, std::uint8_t{0}},
    };
    check(runtimeOnly.configure(runtimeRequest, &error),
          "runtime-slot authority fixture configures explicit XInput slot zero");
    gatec::VirtualXInputContext runtimeContext;
    check(runtimeOnly.mapSeatToContext(1, runtimeContext) ==
              gatec::VirtualXInputResult::Success &&
              runtimeOnly.updateSeatContext(1, runtimeContext) ==
                  gatec::VirtualXInputResult::Success,
          "runtime-slot authority fixture publishes slot zero");

    runtimeBackend->records[0].runtimeXInputSlotHint = 1u;
    runtimeBackend->records[0].gamepad.buttons = 0x8000u;
    check(runtimeOnly.refresh(&error),
          "backend runtime-slot drift is observed as a newer generation");
    check(runtimeOnly.updateSeatContext(1, runtimeContext) ==
              gatec::VirtualXInputResult::Success &&
              runtimeContext.getState(0, state) ==
                  gatec::VirtualXInputResult::Disconnected,
          "an explicit runtime-only slot binding cannot silently follow a different slot hint");
    check(!runtimeOnly.requestVibration(1, runtimeContext, 0, 3u, 4u, &error) &&
              runtimeBackend->vibrations.empty(),
          "runtime-slot drift cannot inherit vibration authority");
}

} // namespace

int main() {
    testStableAndRuntimeBindingPolicy();
    testPollGenerationDisconnectReconnectAndWorker();
    testRuntimeMappingStateReconnectAndVibrationOwnership();
    testMissingMetadataFailsExplicitlyWithoutStaleSuccess();
    testScanFailureInvalidatesSeatAuthority();
    testMalformedSourceInventoryFailsClosed();
    testDirectInputStableIdentityAndApiSeparation();
    testFailedRemapDoesNotCommitNewSourceGeneration();
    testBindingAuthoritySurvivesBackendIdentityDrift();

    if (failures != 0) {
        std::cerr << failures << " controller runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Controller runtime tests passed.\n";
    return EXIT_SUCCESS;
}
