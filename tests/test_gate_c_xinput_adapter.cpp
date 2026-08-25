#include "hydra/gate_c_adapter.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

HydraGateCAdapterXInputMappingV4 mapping(
    std::uint64_t sourceKey, std::uint64_t generation,
    std::uint8_t runtimeSlot) {
    HydraGateCAdapterXInputMappingV4 value{};
    value.struct_size = sizeof(value);
    value.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    value.source_kind = HYDRA_GATE_C_ADAPTER_XINPUT_SOURCE_SYNTHETIC;
    value.runtime_xinput_slot_hint = runtimeSlot;
    value.source_key = sourceKey;
    value.source_generation = generation;
    return value;
}

HydraGateCAdapterXInputSourceStateV4 sourceState(
    std::uint64_t sourceKey, std::uint64_t generation,
    std::uint16_t buttons) {
    HydraGateCAdapterXInputSourceStateV4 value{};
    value.struct_size = sizeof(value);
    value.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    value.source_kind = HYDRA_GATE_C_ADAPTER_XINPUT_SOURCE_SYNTHETIC;
    value.runtime_xinput_slot_hint = 0u;
    value.source_key = sourceKey;
    value.source_generation = generation;
    value.buttons = buttons;
    value.left_trigger = 20u;
    value.right_trigger = 200u;
    value.thumb_lx = -12000;
    value.thumb_ly = 9000;
    return value;
}

HydraGateCAdapterXInputSourceCapabilitiesV4 capabilities(
    std::uint64_t sourceKey, std::uint64_t generation) {
    HydraGateCAdapterXInputSourceCapabilitiesV4 value{};
    value.struct_size = sizeof(value);
    value.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    value.source_kind = HYDRA_GATE_C_ADAPTER_XINPUT_SOURCE_SYNTHETIC;
    value.runtime_xinput_slot_hint = 0u;
    value.type = HYDRA_GATE_C_ADAPTER_XINPUT_CAPABILITY_GAMEPAD;
    value.subtype = 1u;
    value.vibration_supported = 1u;
    value.source_key = sourceKey;
    value.source_generation = generation;
    value.buttons = 0xffffu;
    value.left_trigger = 255u;
    value.right_trigger = 255u;
    value.thumb_lx = 32767;
    value.thumb_ly = 32767;
    value.thumb_rx = 32767;
    value.thumb_ry = 32767;
    value.left_motor_maximum = 65535u;
    value.right_motor_maximum = 65535u;
    return value;
}

HydraGateCAdapterXInputSourceBatteryV4 battery(
    std::uint64_t sourceKey, std::uint64_t generation,
    std::uint8_t level) {
    HydraGateCAdapterXInputSourceBatteryV4 value{};
    value.struct_size = sizeof(value);
    value.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    value.source_kind = HYDRA_GATE_C_ADAPTER_XINPUT_SOURCE_SYNTHETIC;
    value.runtime_xinput_slot_hint = 0u;
    value.available = 1u;
    value.device_type = 0u;
    value.battery_type = 2u;
    value.battery_level = level;
    value.source_key = sourceKey;
    value.source_generation = generation;
    return value;
}

void testAbiMigrationAndIndependentContexts() {
    check(hydra_gate_c_adapter_api_version() == 4u,
          "controller ABI addition advances the adapter to v4");
    auto first = hydra_gate_c_adapter_create();
    auto second = hydra_gate_c_adapter_create();
    check(first != nullptr && second != nullptr,
          "two adapter contexts are created");

    auto firstMapping = mapping(0xaaaau, 10u, 0u);
    auto secondMapping = mapping(0xbbbbu, 20u, 1u);
    check(hydra_gate_c_adapter_xinput_map_slot(
              first, 1u, &firstMapping) == HYDRA_GATE_C_ADAPTER_OK &&
              hydra_gate_c_adapter_xinput_map_slot(
                  second, 1u, &secondMapping) ==
                  HYDRA_GATE_C_ADAPTER_OK,
          "both process-local contexts map logical slot 0 independently");
    auto firstState = sourceState(0xaaaau, 10u, 0x1100u);
    auto secondState = sourceState(0xbbbbu, 20u, 0x2200u);
    secondState.runtime_xinput_slot_hint = 1u;
    secondState.left_trigger = 180u;
    secondState.right_trigger = 5u;
    check(hydra_gate_c_adapter_xinput_apply_state(
              first, 2u, &firstState) == HYDRA_GATE_C_ADAPTER_OK &&
              hydra_gate_c_adapter_xinput_apply_state(
                  second, 2u, &secondState) ==
                  HYDRA_GATE_C_ADAPTER_OK,
          "distinct normalized controller states apply through C ABI v4");

    HydraGateCAdapterXInputStateV4 firstQuery{};
    firstQuery.struct_size = sizeof(firstQuery);
    firstQuery.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    HydraGateCAdapterXInputStateV4 secondQuery = firstQuery;
    check(hydra_gate_c_adapter_xinput_get_state(
              first, 0u, &firstQuery) == HYDRA_GATE_C_ADAPTER_OK &&
              hydra_gate_c_adapter_xinput_get_state(
                  second, 0u, &secondQuery) == HYDRA_GATE_C_ADAPTER_OK &&
              firstQuery.source_key == 0xaaaau &&
              secondQuery.source_key == 0xbbbbu &&
              firstQuery.buttons == 0x1100u &&
              secondQuery.buttons == 0x2200u,
          "C ABI queries preserve process-local source and state separation");

    auto firstCaps = capabilities(0xaaaau, 10u);
    auto secondCaps = capabilities(0xbbbbu, 20u);
    secondCaps.runtime_xinput_slot_hint = 1u;
    secondCaps.subtype = 2u;
    auto firstBattery = battery(0xaaaau, 10u, 3u);
    auto secondBattery = battery(0xbbbbu, 20u, 1u);
    secondBattery.runtime_xinput_slot_hint = 1u;
    check(hydra_gate_c_adapter_xinput_apply_capabilities(
              first, 3u, &firstCaps) == HYDRA_GATE_C_ADAPTER_OK &&
              hydra_gate_c_adapter_xinput_apply_capabilities(
                  second, 3u, &secondCaps) ==
                  HYDRA_GATE_C_ADAPTER_OK &&
              hydra_gate_c_adapter_xinput_apply_battery(
                  first, 4u, &firstBattery) ==
                  HYDRA_GATE_C_ADAPTER_OK &&
              hydra_gate_c_adapter_xinput_apply_battery(
                  second, 4u, &secondBattery) ==
                  HYDRA_GATE_C_ADAPTER_OK,
          "capabilities and battery apply independently through C ABI v4");
    HydraGateCAdapterXInputCapabilitiesV4 queriedCaps{};
    queriedCaps.struct_size = sizeof(queriedCaps);
    queriedCaps.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    HydraGateCAdapterXInputBatteryV4 queriedBattery{};
    queriedBattery.struct_size = sizeof(queriedBattery);
    queriedBattery.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    check(hydra_gate_c_adapter_xinput_get_capabilities(
              first, 0u, &queriedCaps) == HYDRA_GATE_C_ADAPTER_OK &&
              queriedCaps.source_key == 0xaaaau &&
              queriedCaps.subtype == 1u &&
              hydra_gate_c_adapter_xinput_get_battery(
                  first, 0u, &queriedBattery) ==
                  HYDRA_GATE_C_ADAPTER_OK &&
              queriedBattery.source_key == 0xaaaau &&
              queriedBattery.battery_level == 3u,
          "capabilities and battery queries are mapping-consistent");

    HydraGateCAdapterXInputVibrationV4 vibration{};
    vibration.struct_size = sizeof(vibration);
    vibration.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    vibration.logical_slot = 0u;
    vibration.source_generation = firstQuery.source_generation;
    vibration.mapping_generation = firstQuery.mapping_generation;
    vibration.left_motor = 123u;
    vibration.right_motor = 456u;
    check(hydra_gate_c_adapter_xinput_route_vibration(
              first, 5u, &vibration) == HYDRA_GATE_C_ADAPTER_OK &&
              vibration.source_key == 0xaaaau &&
              vibration.route_count == 1u,
          "vibration returns only the validated source route descriptor");

    auto stale = firstState;
    stale.source_generation = 9u;
    check(hydra_gate_c_adapter_xinput_apply_state(
              first, 6u, &stale) ==
              HYDRA_GATE_C_ADAPTER_XINPUT_STALE_GENERATION,
          "stale source generation fails closed at the C boundary");
    auto staleRemap = mapping(0xaaaau, 9u, 1u);
    check(hydra_gate_c_adapter_xinput_map_slot(
              first, 6u, &staleRemap) ==
              HYDRA_GATE_C_ADAPTER_XINPUT_STALE_GENERATION,
          "explicit C ABI hint remap cannot resurrect a stale stable source");
    HydraGateCAdapterXInputStateV4 afterRejectedRemap{};
    afterRejectedRemap.struct_size = sizeof(afterRejectedRemap);
    afterRejectedRemap.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    check(hydra_gate_c_adapter_xinput_get_state(
              first, 0u, &afterRejectedRemap) ==
                  HYDRA_GATE_C_ADAPTER_OK &&
              afterRejectedRemap.runtime_xinput_slot_hint == 0u &&
              afterRejectedRemap.source_generation == 10u &&
              afterRejectedRemap.mapping_generation ==
                  firstQuery.mapping_generation &&
              afterRejectedRemap.buttons == 0x1100u,
          "rejected C ABI remap preserves mapping metadata and controller state");
    HydraGateCAdapterXInputSourceV4 disconnect{};
    disconnect.struct_size = sizeof(disconnect);
    disconnect.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    disconnect.source_kind = HYDRA_GATE_C_ADAPTER_XINPUT_SOURCE_SYNTHETIC;
    disconnect.runtime_xinput_slot_hint = 0u;
    disconnect.source_key = 0xaaaau;
    disconnect.source_generation = 10u;
    check(hydra_gate_c_adapter_xinput_disconnect(
              first, 6u, &disconnect) == HYDRA_GATE_C_ADAPTER_OK,
          "disconnect clears the mapped source at C ABI boundary");
    firstQuery = {};
    firstQuery.struct_size = sizeof(firstQuery);
    firstQuery.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    check(hydra_gate_c_adapter_xinput_get_state(
              first, 0u, &firstQuery) ==
                  HYDRA_GATE_C_ADAPTER_XINPUT_DISCONNECTED &&
              firstQuery.connected == 0u && firstQuery.buttons == 0u &&
              firstQuery.left_trigger == 0u &&
              firstQuery.thumb_lx == 0,
          "disconnected C ABI query returns no stale controller state");

    check(hydra_gate_c_adapter_reset(second) == HYDRA_GATE_C_ADAPTER_OK,
          "adapter reset succeeds with controller state present");
    secondQuery = {};
    secondQuery.struct_size = sizeof(secondQuery);
    secondQuery.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    check(hydra_gate_c_adapter_xinput_get_state(
              second, 0u, &secondQuery) ==
              HYDRA_GATE_C_ADAPTER_XINPUT_DISCONNECTED,
          "adapter reset clears all controller mappings");

    auto malformed = mapping(0xccccu, 1u, 0u);
    malformed.api_version = 3u;
    check(hydra_gate_c_adapter_xinput_map_slot(
              first, 7u, &malformed) ==
              HYDRA_GATE_C_ADAPTER_STRUCT_VERSION_MISMATCH,
          "old adapter ABI version fails closed");
    malformed = mapping(0xccccu, 1u, 0u);
    malformed.struct_size -= 1u;
    check(hydra_gate_c_adapter_xinput_map_slot(
              first, 7u, &malformed) ==
              HYDRA_GATE_C_ADAPTER_STRUCT_VERSION_MISMATCH,
          "malformed controller struct size fails closed");
    firstQuery = {};
    firstQuery.struct_size = sizeof(firstQuery);
    firstQuery.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    check(hydra_gate_c_adapter_xinput_get_state(
              first, 4u, &firstQuery) ==
              HYDRA_GATE_C_ADAPTER_XINPUT_INVALID_SLOT,
          "C ABI rejects logical slot 4");

    hydra_gate_c_adapter_destroy(first);
    hydra_gate_c_adapter_destroy(second);
}

} // namespace

int main() {
    testAbiMigrationAndIndependentContexts();
    std::cout << "Gate C XInput adapter ABI tests passed.\n";
    return EXIT_SUCCESS;
}
