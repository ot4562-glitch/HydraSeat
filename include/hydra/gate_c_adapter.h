#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(HYDRA_GATE_C_ADAPTER_BUILD)
#    define HYDRA_GATE_C_ADAPTER_API __declspec(dllexport)
#  else
#    define HYDRA_GATE_C_ADAPTER_API __declspec(dllimport)
#  endif
#  define HYDRA_GATE_C_ADAPTER_CALL __cdecl
#else
#  define HYDRA_GATE_C_ADAPTER_API __attribute__((visibility("default")))
#  define HYDRA_GATE_C_ADAPTER_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define HYDRA_GATE_C_ADAPTER_API_VERSION 3u
#define HYDRA_GATE_C_ADAPTER_KEY_BYTES 32u
#define HYDRA_GATE_C_ADAPTER_KEYBOARD_STATE_BYTES 256u
#define HYDRA_GATE_C_ADAPTER_NO_PROBE_KEY 0xffffu
#define HYDRA_GATE_C_ADAPTER_INPUT_EVENT_V1_BYTES 36u
#define HYDRA_GATE_C_ADAPTER_CONTROL_STATE_V1_BYTES 32u
#define HYDRA_GATE_C_ADAPTER_SNAPSHOT_V1_BYTES 120u
#define HYDRA_GATE_C_ADAPTER_CLIP_RECT_V2_BYTES 24u
#define HYDRA_GATE_C_ADAPTER_WINDOW_STATE_V2_BYTES 56u
#define HYDRA_GATE_C_ADAPTER_RAW_REGISTRATION_V3_BYTES 28u
#define HYDRA_GATE_C_ADAPTER_RAW_REGISTRATION_ENTRY_V3_BYTES 36u
#define HYDRA_GATE_C_ADAPTER_RAW_DELIVERY_V3_BYTES 40u

typedef void* HydraGateCAdapterHandle;

typedef enum HydraGateCAdapterResult {
    HYDRA_GATE_C_ADAPTER_OK = 0,
    HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT = 1,
    HYDRA_GATE_C_ADAPTER_STRUCT_VERSION_MISMATCH = 2,
    HYDRA_GATE_C_ADAPTER_STALE_SEQUENCE = 3,
    HYDRA_GATE_C_ADAPTER_INVALID_STATE = 4,
    HYDRA_GATE_C_ADAPTER_BUFFER_TOO_SMALL = 5,
    HYDRA_GATE_C_ADAPTER_INTERNAL_ERROR = 6,
    HYDRA_GATE_C_ADAPTER_RAW_INPUT_FAILURE = 7
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

typedef struct HydraGateCAdapterClipRectV2 {
    uint32_t struct_size;
    uint8_t enabled;
    uint8_t reserved0[3];
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} HydraGateCAdapterClipRectV2;

/*
 * HWND values are transient process-local runtime values represented as
 * fixed-width integers. They are never persisted or transported as stable
 * window identity.
 */
typedef struct HydraGateCAdapterWindowStateV2 {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t process_id;
    uint32_t reserved0;
    uint64_t target_window;
    uint64_t logical_foreground_window;
    uint64_t logical_active_window;
    uint64_t logical_focus_window;
    uint64_t virtual_capture_window;
} HydraGateCAdapterWindowStateV2;

typedef struct HydraGateCAdapterRawRegistrationV3 {
    uint32_t struct_size;
    uint16_t usage_page;
    uint16_t usage;
    uint32_t flags;
    uint64_t target_window;
    uint8_t target_window_current_process;
    uint8_t reserved0[7];
} HydraGateCAdapterRawRegistrationV3;

typedef struct HydraGateCAdapterRawRegistrationEntryV3 {
    uint32_t struct_size;
    uint16_t usage_page;
    uint16_t usage;
    uint32_t requested_flags;
    uint32_t observable_flags;
    uint64_t target_window;
    uint64_t generation;
    uint8_t target_validated_at_registration;
    uint8_t device_notification_requested;
    uint8_t reserved0[2];
} HydraGateCAdapterRawRegistrationEntryV3;

typedef struct HydraGateCAdapterRawDeliveryV3 {
    uint32_t struct_size;
    uint32_t result;
    uint64_t token;
    uint64_t target_window;
    uint64_t registration_generation;
    uint32_t message_wparam;
    uint32_t reserved0;
} HydraGateCAdapterRawDeliveryV3;

#pragma pack(pop)

HYDRA_GATE_C_ADAPTER_API uint32_t HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_api_version(void);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterHandle HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_create(void);

HYDRA_GATE_C_ADAPTER_API void HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_destroy(HydraGateCAdapterHandle handle);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_reset(HydraGateCAdapterHandle handle);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_apply_input(
    HydraGateCAdapterHandle handle,
    uint64_t sequence,
    const HydraGateCAdapterInputEventV1* event_data);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_apply_control(
    HydraGateCAdapterHandle handle,
    uint64_t sequence,
    const HydraGateCAdapterControlStateV1* control_data);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_async_key_state(
    HydraGateCAdapterHandle handle,
    uint32_t vkey,
    uint16_t* value);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_key_state(
    HydraGateCAdapterHandle handle,
    uint32_t vkey,
    uint16_t* value);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_keyboard_state(
    HydraGateCAdapterHandle handle,
    uint8_t* state,
    size_t state_bytes);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_control_state(
    HydraGateCAdapterHandle handle,
    HydraGateCAdapterControlStateV1* control_state);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_mouse_state(
    HydraGateCAdapterHandle handle,
    uint32_t* buttons_down,
    int64_t* wheel_accumulator);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_snapshot(
    HydraGateCAdapterHandle handle,
    uint16_t probe_vkey,
    HydraGateCAdapterSnapshotV1* snapshot);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_set_virtual_cursor(
    HydraGateCAdapterHandle handle,
    int32_t x,
    int32_t y);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_set_virtual_clip(
    HydraGateCAdapterHandle handle,
    const HydraGateCAdapterClipRectV2* clip);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_configure_window_state(
    HydraGateCAdapterHandle handle,
    const HydraGateCAdapterWindowStateV2* window_state);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_window_state(
    HydraGateCAdapterHandle handle,
    HydraGateCAdapterWindowStateV2* window_state);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_set_virtual_capture(
    HydraGateCAdapterHandle handle,
    uint64_t window,
    uint64_t* previous_window);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_release_virtual_capture(
    HydraGateCAdapterHandle handle);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_invalidate_window(
    HydraGateCAdapterHandle handle,
    uint64_t window);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_raw_configure(
    HydraGateCAdapterHandle handle,
    uint32_t seat_id,
    uint32_t process_id,
    uint16_t architecture_bits);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_raw_register(
    HydraGateCAdapterHandle handle,
    const HydraGateCAdapterRawRegistrationV3* registrations,
    uint32_t registration_count);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_raw_get_registered(
    HydraGateCAdapterHandle handle,
    HydraGateCAdapterRawRegistrationEntryV3* registrations,
    uint32_t* registration_count);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_raw_take_delivery(
    HydraGateCAdapterHandle handle,
    HydraGateCAdapterRawDeliveryV3* delivery);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_raw_complete_delivery(
    HydraGateCAdapterHandle handle,
    uint64_t token,
    uint8_t target_currently_valid,
    uint8_t post_succeeded);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_raw_get_data(
    HydraGateCAdapterHandle handle,
    uint64_t token,
    uint32_t command,
    void* data,
    uint32_t* size,
    uint32_t header_size);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_raw_get_buffer(
    HydraGateCAdapterHandle handle,
    void* data,
    uint32_t* size,
    uint32_t header_size,
    uint32_t* packet_count);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_raw_begin_stopping(HydraGateCAdapterHandle handle);

HYDRA_GATE_C_ADAPTER_API HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_raw_reset(HydraGateCAdapterHandle handle);

#ifdef __cplusplus
}
#endif
