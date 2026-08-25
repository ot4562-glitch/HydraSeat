#include "hydra/gate_c_adapter.h"

#include <stdint.h>
#include <stdio.h>

_Static_assert(sizeof(HydraGateCAdapterInputEventV1) ==
                   HYDRA_GATE_C_ADAPTER_INPUT_EVENT_V1_BYTES,
               "Gate C input ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterControlStateV1) ==
                   HYDRA_GATE_C_ADAPTER_CONTROL_STATE_V1_BYTES,
               "Gate C control ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterSnapshotV1) ==
                   HYDRA_GATE_C_ADAPTER_SNAPSHOT_V1_BYTES,
               "Gate C snapshot ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterClipRectV2) ==
                   HYDRA_GATE_C_ADAPTER_CLIP_RECT_V2_BYTES,
               "Gate C clip ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterWindowStateV2) ==
                   HYDRA_GATE_C_ADAPTER_WINDOW_STATE_V2_BYTES,
               "Gate C window ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterRawRegistrationV3) ==
                   HYDRA_GATE_C_ADAPTER_RAW_REGISTRATION_V3_BYTES,
               "Gate C raw registration ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterRawRegistrationEntryV3) ==
                   HYDRA_GATE_C_ADAPTER_RAW_REGISTRATION_ENTRY_V3_BYTES,
               "Gate C raw entry ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterRawDeliveryV3) ==
                   HYDRA_GATE_C_ADAPTER_RAW_DELIVERY_V3_BYTES,
               "Gate C raw delivery ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterXInputSourceV4) ==
                   HYDRA_GATE_C_ADAPTER_XINPUT_SOURCE_V4_BYTES,
               "Gate C XInput source ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterXInputMappingV4) ==
                   HYDRA_GATE_C_ADAPTER_XINPUT_MAPPING_V4_BYTES,
               "Gate C XInput mapping ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterXInputSourceStateV4) ==
                   HYDRA_GATE_C_ADAPTER_XINPUT_SOURCE_STATE_V4_BYTES,
               "Gate C XInput source state ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterXInputStateV4) ==
                   HYDRA_GATE_C_ADAPTER_XINPUT_STATE_V4_BYTES,
               "Gate C XInput state ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterXInputSourceCapabilitiesV4) ==
                   HYDRA_GATE_C_ADAPTER_XINPUT_SOURCE_CAPABILITIES_V4_BYTES,
               "Gate C XInput source capabilities ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterXInputCapabilitiesV4) ==
                   HYDRA_GATE_C_ADAPTER_XINPUT_CAPABILITIES_V4_BYTES,
               "Gate C XInput capabilities ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterXInputSourceBatteryV4) ==
                   HYDRA_GATE_C_ADAPTER_XINPUT_SOURCE_BATTERY_V4_BYTES,
               "Gate C XInput source battery ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterXInputBatteryV4) ==
                   HYDRA_GATE_C_ADAPTER_XINPUT_BATTERY_V4_BYTES,
               "Gate C XInput battery ABI size changed");
_Static_assert(sizeof(HydraGateCAdapterXInputVibrationV4) ==
                   HYDRA_GATE_C_ADAPTER_XINPUT_VIBRATION_V4_BYTES,
               "Gate C XInput vibration ABI size changed");

int main(void) {
    if (hydra_gate_c_adapter_api_version() !=
        HYDRA_GATE_C_ADAPTER_API_VERSION) {
        fputs("Gate C adapter C ABI version mismatch.\n", stderr);
        return 1;
    }

    HydraGateCAdapterHandle handle = hydra_gate_c_adapter_create();
    if (handle == NULL) {
        fputs("Gate C adapter context creation failed.\n", stderr);
        return 2;
    }

    HydraGateCAdapterWindowStateV2 windows = {0};
    windows.struct_size = (uint32_t)sizeof(windows);
    windows.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    windows.process_id = 1234u;
    windows.target_window = 0x1234u;
    if (hydra_gate_c_adapter_configure_window_state(handle, &windows) !=
        HYDRA_GATE_C_ADAPTER_OK) {
        hydra_gate_c_adapter_destroy(handle);
        return 6;
    }

    HydraGateCAdapterControlStateV1 control = {0};
    control.struct_size = (uint32_t)sizeof(control);
    control.cursor_x = 12;
    control.cursor_y = 34;
    control.virtual_foreground = 1;
    if (hydra_gate_c_adapter_apply_control(handle, 1, &control) !=
        HYDRA_GATE_C_ADAPTER_OK) {
        hydra_gate_c_adapter_destroy(handle);
        return 3;
    }

    uint16_t key_state = 0xffffu;
    if (hydra_gate_c_adapter_get_key_state(handle, 0x41u, &key_state) !=
            HYDRA_GATE_C_ADAPTER_OK ||
        key_state != 0u) {
        hydra_gate_c_adapter_destroy(handle);
        return 4;
    }

    HydraGateCAdapterControlStateV1 queried = {0};
    queried.struct_size = (uint32_t)sizeof(queried);
    if (hydra_gate_c_adapter_get_control_state(handle, &queried) !=
            HYDRA_GATE_C_ADAPTER_OK ||
        queried.cursor_x != 12 || queried.cursor_y != 34 ||
        queried.virtual_foreground != 1u) {
        hydra_gate_c_adapter_destroy(handle);
        return 5;
    }

    windows = (HydraGateCAdapterWindowStateV2){0};
    windows.struct_size = (uint32_t)sizeof(windows);
    if (hydra_gate_c_adapter_get_window_state(handle, &windows) !=
            HYDRA_GATE_C_ADAPTER_OK ||
        windows.api_version != HYDRA_GATE_C_ADAPTER_API_VERSION ||
        windows.target_window != 0x1234u ||
        windows.logical_foreground_window != 0x1234u) {
        hydra_gate_c_adapter_destroy(handle);
        return 7;
    }

    if (hydra_gate_c_adapter_raw_configure(
            handle, 1u, 1234u, (uint16_t)(sizeof(void*) * 8u)) !=
        HYDRA_GATE_C_ADAPTER_OK) {
        hydra_gate_c_adapter_destroy(handle);
        return 8;
    }
    HydraGateCAdapterRawRegistrationV3 raw = {0};
    raw.struct_size = (uint32_t)sizeof(raw);
    raw.usage_page = 0x01u;
    raw.usage = 0x06u;
    raw.flags = 0x00000100u;
    raw.target_window = 0x1234u;
    raw.target_window_current_process = 1u;
    if (hydra_gate_c_adapter_raw_register(handle, &raw, 1u) !=
        HYDRA_GATE_C_ADAPTER_OK) {
        hydra_gate_c_adapter_destroy(handle);
        return 9;
    }
    uint32_t raw_count = 0;
    if (hydra_gate_c_adapter_raw_get_registered(
            handle, NULL, &raw_count) != HYDRA_GATE_C_ADAPTER_OK ||
        raw_count != 1u) {
        hydra_gate_c_adapter_destroy(handle);
        return 10;
    }

    HydraGateCAdapterXInputMappingV4 mapping = {0};
    mapping.struct_size = (uint32_t)sizeof(mapping);
    mapping.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    mapping.logical_slot = 0u;
    mapping.source_kind = HYDRA_GATE_C_ADAPTER_XINPUT_SOURCE_SYNTHETIC;
    mapping.runtime_xinput_slot_hint = 0u;
    mapping.source_key = 0x1111u;
    mapping.source_generation = 1u;
    if (hydra_gate_c_adapter_xinput_map_slot(
            handle, 1u, &mapping) != HYDRA_GATE_C_ADAPTER_OK ||
        mapping.mapping_generation == 0u) {
        hydra_gate_c_adapter_destroy(handle);
        return 11;
    }
    HydraGateCAdapterXInputSourceStateV4 xinput_state = {0};
    xinput_state.struct_size = (uint32_t)sizeof(xinput_state);
    xinput_state.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    xinput_state.source_kind = HYDRA_GATE_C_ADAPTER_XINPUT_SOURCE_SYNTHETIC;
    xinput_state.runtime_xinput_slot_hint = 0u;
    xinput_state.source_key = 0x1111u;
    xinput_state.source_generation = 1u;
    xinput_state.buttons = 0x1000u;
    if (hydra_gate_c_adapter_xinput_apply_state(
            handle, 2u, &xinput_state) != HYDRA_GATE_C_ADAPTER_OK) {
        hydra_gate_c_adapter_destroy(handle);
        return 12;
    }
    HydraGateCAdapterXInputStateV4 queried_xinput = {0};
    queried_xinput.struct_size = (uint32_t)sizeof(queried_xinput);
    queried_xinput.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    if (hydra_gate_c_adapter_xinput_get_state(
            handle, 0u, &queried_xinput) != HYDRA_GATE_C_ADAPTER_OK ||
        queried_xinput.connected != 1u ||
        queried_xinput.buttons != 0x1000u ||
        queried_xinput.source_key != 0x1111u) {
        hydra_gate_c_adapter_destroy(handle);
        return 13;
    }

    hydra_gate_c_adapter_destroy(handle);
    puts("Gate C adapter pure-C ABI smoke test passed.");
    return 0;
}
