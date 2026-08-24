#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(HYDRA_GATE_C_ADAPTER_BUILD)
#    define HYDRA_GATE_C_ADAPTER_API __declspec(dllexport)
#  else
#    define HYDRA_GATE_C_ADAPTER_API __declspec(dllimport)
#  endif
#else
#  define HYDRA_GATE_C_ADAPTER_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define HYDRA_GATE_C_ADAPTER_API_VERSION 1u
#define HYDRA_GATE_C_ADAPTER_KEY_BYTES 32u
#define HYDRA_GATE_C_ADAPTER_KEYBOARD_STATE_BYTES 256u
#define HYDRA_GATE_C_ADAPTER_NO_PROBE_KEY 0xffffu

typedef void* HydraGateCAdapterHandle;

typedef enum HydraGateCAdapterResult {
    HYDRA_GATE_C_ADAPTER_OK = 0,
    HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT = 1,
    HYDRA_GATE_C_ADAPTER_STRUCT_VERSION_MISMATCH = 2,
    HYDRA_GATE_C_ADAPTER_STALE_SEQUENCE = 3,
    HYDRA_GATE_C_ADAPTER_INVALID_STATE = 4,
    HYDRA_GATE_C_ADAPTER_BUFFER_TOO_SMALL = 5,
    HYDRA_GATE_C_ADAPTER_INTERNAL_ERROR = 6
} HydraGateCAdapterResult;

typedef enum HydraGateCAdapterInputKind {
    HYDRA_GATE_C_ADAPTER_INPUT_KEYBOARD = 1,
    HYDRA_GATE_C_ADAPTER_INPUT_MOUSE = 2
} HydraGateCAdapterInputKind;

typedef enum HydraGateCAdapterKeyTransition {
    HYDRA_GATE_C_ADAPTER_KEY_NONE = 0,
    HYDRA_GATE_C_ADAPTER_KEY_DOWN = 1,
    HYDRA_GATE_C_ADAPTER_KEY_UP = 2
} HydraGateCAdapterKeyTransition;

#pragma pack(push, 1)

typedef struct HydraGateCAdapterInputEventV1 {
    uint32_t struct_size;
    uint8_t kind;
    uint8_t key_transition;
    uint8_t is_touchpad;
    uint8_t reserved0;
    uint64_t timestamp_micros;
    uint32_t vkey;
    uint16_t scan_code;
    uint16_t keyboard_flags;
    int32_t delta_x;
    int32_t delta_y;
    uint16_t mouse_button_flags;
    int16_t wheel_delta;
} HydraGateCAdapterInputEventV1;

typedef struct HydraGateCAdapterControlStateV1 {
    uint32_t struct_size;
    int32_t cursor_x;
    int32_t cursor_y;
    uint8_t clip_enabled;
    uint8_t virtual_foreground;
    uint8_t virtual_capture;
    uint8_t reserved0;
    int32_t clip_left;
    int32_t clip_top;
    int32_t clip_right;
    int32_t clip_bottom;
} HydraGateCAdapterControlStateV1;

typedef struct HydraGateCAdapterSnapshotV1 {
    uint32_t struct_size;
    uint64_t last_applied_sequence;
    uint8_t key_down_bits[HYDRA_GATE_C_ADAPTER_KEY_BYTES];
    uint8_t key_pressed_edge_bits[HYDRA_GATE_C_ADAPTER_KEY_BYTES];
    uint32_t mouse_buttons_down;
    int64_t wheel_accumulator;
    uint16_t probe_vkey;
    uint16_t async_key_state_value;
    uint8_t keyboard_state_byte;
    uint8_t clip_enabled;
    uint8_t virtual_foreground;
    uint8_t virtual_capture;
    int32_t cursor_x;
    int32_t cursor_y;
    int32_t clip_left;
    int32_t clip_top;
    int32_t clip_right;
    int32_t clip_bottom;
} HydraGateCAdapterSnapshotV1;

#pragma pack(pop)

HYDRA_GATE_C_ADAPTER_API uint32_t
hydra_gate_c_adapter_api_version(void);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterHandle
hydra_gate_c_adapter_create(void);

HYDRA_GATE_C_ADAPTER_API void
hydra_gate_c_adapter_destroy(HydraGateCAdapterHandle handle);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult
hydra_gate_c_adapter_reset(HydraGateCAdapterHandle handle);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult
hydra_gate_c_adapter_apply_input(
    HydraGateCAdapterHandle handle,
    uint64_t sequence,
    const HydraGateCAdapterInputEventV1* event_data);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult
hydra_gate_c_adapter_apply_control(
    HydraGateCAdapterHandle handle,
    uint64_t sequence,
    const HydraGateCAdapterControlStateV1* control_data);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult
hydra_gate_c_adapter_get_async_key_state(
    HydraGateCAdapterHandle handle,
    uint32_t vkey,
    uint16_t* value);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult
hydra_gate_c_adapter_get_key_state(
    HydraGateCAdapterHandle handle,
    uint32_t vkey,
    uint16_t* value);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult
hydra_gate_c_adapter_get_keyboard_state(
    HydraGateCAdapterHandle handle,
    uint8_t* state,
    size_t state_bytes);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult
hydra_gate_c_adapter_get_control_state(
    HydraGateCAdapterHandle handle,
    HydraGateCAdapterControlStateV1* control_state);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult
hydra_gate_c_adapter_get_mouse_state(
    HydraGateCAdapterHandle handle,
    uint32_t* buttons_down,
    int64_t* wheel_accumulator);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult
hydra_gate_c_adapter_get_snapshot(
    HydraGateCAdapterHandle handle,
    uint16_t probe_vkey,
    HydraGateCAdapterSnapshotV1* snapshot);

#ifdef __cplusplus
}
#endif
