#include "hydra/gate_c_adapter.h"

#include "hydra/gate_c_protocol.hpp"
#include "hydra/virtual_input_state.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <new>

namespace {

struct AdapterContext {
    std::mutex mutex;
    hydra::gatec::VirtualInputState state;
};

static_assert(sizeof(HydraGateCAdapterInputEventV1) ==
              HYDRA_GATE_C_ADAPTER_INPUT_EVENT_V1_BYTES);
static_assert(sizeof(HydraGateCAdapterControlStateV1) ==
              HYDRA_GATE_C_ADAPTER_CONTROL_STATE_V1_BYTES);
static_assert(sizeof(HydraGateCAdapterSnapshotV1) ==
              HYDRA_GATE_C_ADAPTER_SNAPSHOT_V1_BYTES);

AdapterContext* contextOf(HydraGateCAdapterHandle handle) noexcept {
    return static_cast<AdapterContext*>(handle);
}

bool validBoolean(std::uint8_t value) noexcept {
    return value <= 1u;
}

HydraGateCAdapterResult validateInput(
    const HydraGateCAdapterInputEventV1* eventData) noexcept {
    if (eventData == nullptr) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    if (eventData->struct_size != sizeof(HydraGateCAdapterInputEventV1)) {
        return HYDRA_GATE_C_ADAPTER_STRUCT_VERSION_MISMATCH;
    }
    if (eventData->reserved0 != 0 || !validBoolean(eventData->is_touchpad)) {
        return HYDRA_GATE_C_ADAPTER_INVALID_STATE;
    }
    if (eventData->kind == HYDRA_GATE_C_ADAPTER_INPUT_KEYBOARD) {
        if (eventData->vkey >= 256u ||
            (eventData->key_transition != HYDRA_GATE_C_ADAPTER_KEY_DOWN &&
             eventData->key_transition != HYDRA_GATE_C_ADAPTER_KEY_UP)) {
            return HYDRA_GATE_C_ADAPTER_INVALID_STATE;
        }
    } else if (eventData->kind == HYDRA_GATE_C_ADAPTER_INPUT_MOUSE) {
        if (eventData->key_transition != HYDRA_GATE_C_ADAPTER_KEY_NONE) {
            return HYDRA_GATE_C_ADAPTER_INVALID_STATE;
        }
    } else {
        return HYDRA_GATE_C_ADAPTER_INVALID_STATE;
    }
    return HYDRA_GATE_C_ADAPTER_OK;
}

HydraGateCAdapterResult validateControl(
    const HydraGateCAdapterControlStateV1* controlData) noexcept {
    if (controlData == nullptr) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    if (controlData->struct_size != sizeof(HydraGateCAdapterControlStateV1)) {
        return HYDRA_GATE_C_ADAPTER_STRUCT_VERSION_MISMATCH;
    }
    if (controlData->reserved0 != 0 ||
        !validBoolean(controlData->clip_enabled) ||
        !validBoolean(controlData->virtual_foreground) ||
        !validBoolean(controlData->virtual_capture)) {
        return HYDRA_GATE_C_ADAPTER_INVALID_STATE;
    }
    if (controlData->clip_enabled != 0 &&
        (controlData->clip_right <= controlData->clip_left ||
         controlData->clip_bottom <= controlData->clip_top)) {
        return HYDRA_GATE_C_ADAPTER_INVALID_STATE;
    }
    return HYDRA_GATE_C_ADAPTER_OK;
}

hydra::gatec::InputEventMessage toMessage(
    const HydraGateCAdapterInputEventV1& eventData) noexcept {
    hydra::gatec::InputEventMessage message;
    message.kind = eventData.kind == HYDRA_GATE_C_ADAPTER_INPUT_KEYBOARD
                       ? hydra::gatec::InputKind::Keyboard
                       : hydra::gatec::InputKind::Mouse;
    message.keyTransition = static_cast<hydra::gatec::KeyTransition>(
        eventData.key_transition);
    message.isTouchpad = eventData.is_touchpad != 0;
    message.timestampMicros = eventData.timestamp_micros;
    message.vkey = eventData.vkey;
    message.scanCode = eventData.scan_code;
    message.keyboardFlags = eventData.keyboard_flags;
    message.deltaX = eventData.delta_x;
    message.deltaY = eventData.delta_y;
    message.mouseButtonFlags = eventData.mouse_button_flags;
    message.wheelDelta = eventData.wheel_delta;
    return message;
}

hydra::gatec::ControlStateMessage toMessage(
    const HydraGateCAdapterControlStateV1& controlData) noexcept {
    hydra::gatec::ControlStateMessage message;
    message.cursorX = controlData.cursor_x;
    message.cursorY = controlData.cursor_y;
    message.clipEnabled = controlData.clip_enabled != 0;
    message.virtualForeground = controlData.virtual_foreground != 0;
    message.virtualCapture = controlData.virtual_capture != 0;
    message.clipLeft = controlData.clip_left;
    message.clipTop = controlData.clip_top;
    message.clipRight = controlData.clip_right;
    message.clipBottom = controlData.clip_bottom;
    return message;
}

void copySnapshot(const hydra::gatec::StateSnapshotMessage& source,
                  HydraGateCAdapterSnapshotV1& destination) noexcept {
    destination.last_applied_sequence = source.lastAppliedSequence;
    std::copy(source.keyDownBits.begin(), source.keyDownBits.end(),
              destination.key_down_bits);
    std::copy(source.keyPressedEdgeBits.begin(),
              source.keyPressedEdgeBits.end(),
              destination.key_pressed_edge_bits);
    destination.mouse_buttons_down = source.mouseButtonsDown;
    destination.wheel_accumulator = source.wheelAccumulator;
    destination.probe_vkey = source.probeVkey;
    destination.async_key_state_value = source.asyncKeyStateValue;
    destination.keyboard_state_byte = source.keyboardStateByte;
    destination.clip_enabled = source.clipEnabled ? 1u : 0u;
    destination.virtual_foreground = source.virtualForeground ? 1u : 0u;
    destination.virtual_capture = source.virtualCapture ? 1u : 0u;
    destination.cursor_x = source.cursorX;
    destination.cursor_y = source.cursorY;
    destination.clip_left = source.clipLeft;
    destination.clip_top = source.clipTop;
    destination.clip_right = source.clipRight;
    destination.clip_bottom = source.clipBottom;
}

} // namespace

extern "C" {

std::uint32_t HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_api_version(void) {
    return HYDRA_GATE_C_ADAPTER_API_VERSION;
}

HydraGateCAdapterHandle HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_create(void) {
    try {
        return new (std::nothrow) AdapterContext{};
    } catch (...) {
        return nullptr;
    }
}

void HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_destroy(HydraGateCAdapterHandle handle) {
    delete contextOf(handle);
}

HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL hydra_gate_c_adapter_reset(
    HydraGateCAdapterHandle handle) {
    auto* context = contextOf(handle);
    if (context == nullptr) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    try {
        std::scoped_lock lock(context->mutex);
        context->state.reset();
        return HYDRA_GATE_C_ADAPTER_OK;
    } catch (...) {
        return HYDRA_GATE_C_ADAPTER_INTERNAL_ERROR;
    }
}

HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_apply_input(
    HydraGateCAdapterHandle handle, std::uint64_t sequence,
    const HydraGateCAdapterInputEventV1* eventData) {
    auto* context = contextOf(handle);
    if (context == nullptr || sequence == 0) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    const auto validation = validateInput(eventData);
    if (validation != HYDRA_GATE_C_ADAPTER_OK) {
        return validation;
    }
    try {
        std::scoped_lock lock(context->mutex);
        if (sequence <= context->state.lastAppliedSequence()) {
            return HYDRA_GATE_C_ADAPTER_STALE_SEQUENCE;
        }
        return context->state.applyInput(sequence, toMessage(*eventData))
                   ? HYDRA_GATE_C_ADAPTER_OK
                   : HYDRA_GATE_C_ADAPTER_INVALID_STATE;
    } catch (...) {
        return HYDRA_GATE_C_ADAPTER_INTERNAL_ERROR;
    }
}

HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_apply_control(
    HydraGateCAdapterHandle handle, std::uint64_t sequence,
    const HydraGateCAdapterControlStateV1* controlData) {
    auto* context = contextOf(handle);
    if (context == nullptr || sequence == 0) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    const auto validation = validateControl(controlData);
    if (validation != HYDRA_GATE_C_ADAPTER_OK) {
        return validation;
    }
    try {
        std::scoped_lock lock(context->mutex);
        if (sequence <= context->state.lastAppliedSequence()) {
            return HYDRA_GATE_C_ADAPTER_STALE_SEQUENCE;
        }
        return context->state.applyControl(sequence, toMessage(*controlData))
                   ? HYDRA_GATE_C_ADAPTER_OK
                   : HYDRA_GATE_C_ADAPTER_INVALID_STATE;
    } catch (...) {
        return HYDRA_GATE_C_ADAPTER_INTERNAL_ERROR;
    }
}

HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_async_key_state(
    HydraGateCAdapterHandle handle, std::uint32_t vkey,
    std::uint16_t* value) {
    auto* context = contextOf(handle);
    if (context == nullptr || value == nullptr || vkey >= 256u) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    try {
        std::scoped_lock lock(context->mutex);
        *value = context->state.consumeAsyncKeyState(vkey);
        return HYDRA_GATE_C_ADAPTER_OK;
    } catch (...) {
        return HYDRA_GATE_C_ADAPTER_INTERNAL_ERROR;
    }
}

HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_key_state(
    HydraGateCAdapterHandle handle, std::uint32_t vkey,
    std::uint16_t* value) {
    auto* context = contextOf(handle);
    if (context == nullptr || value == nullptr || vkey >= 256u) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    try {
        std::scoped_lock lock(context->mutex);
        *value = context->state.keyDown(vkey) ? 0x8000u : 0u;
        return HYDRA_GATE_C_ADAPTER_OK;
    } catch (...) {
        return HYDRA_GATE_C_ADAPTER_INTERNAL_ERROR;
    }
}

HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_keyboard_state(
    HydraGateCAdapterHandle handle, std::uint8_t* state,
    std::size_t stateBytes) {
    auto* context = contextOf(handle);
    if (context == nullptr || state == nullptr) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    if (stateBytes < HYDRA_GATE_C_ADAPTER_KEYBOARD_STATE_BYTES) {
        return HYDRA_GATE_C_ADAPTER_BUFFER_TOO_SMALL;
    }
    try {
        std::scoped_lock lock(context->mutex);
        const auto keyboard = context->state.keyboardState();
        std::copy(keyboard.begin(), keyboard.end(), state);
        return HYDRA_GATE_C_ADAPTER_OK;
    } catch (...) {
        return HYDRA_GATE_C_ADAPTER_INTERNAL_ERROR;
    }
}

HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_control_state(
    HydraGateCAdapterHandle handle,
    HydraGateCAdapterControlStateV1* controlState) {
    auto* context = contextOf(handle);
    if (context == nullptr || controlState == nullptr) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    if (controlState->struct_size != sizeof(HydraGateCAdapterControlStateV1)) {
        return HYDRA_GATE_C_ADAPTER_STRUCT_VERSION_MISMATCH;
    }
    try {
        std::scoped_lock lock(context->mutex);
        const auto requestedSize = controlState->struct_size;
        std::memset(controlState, 0, sizeof(*controlState));
        controlState->struct_size = requestedSize;
        controlState->cursor_x = context->state.cursorX();
        controlState->cursor_y = context->state.cursorY();
        controlState->clip_enabled = context->state.clipEnabled() ? 1u : 0u;
        controlState->virtual_foreground =
            context->state.virtualForeground() ? 1u : 0u;
        controlState->virtual_capture =
            context->state.virtualCapture() ? 1u : 0u;
        controlState->clip_left = context->state.clipLeft();
        controlState->clip_top = context->state.clipTop();
        controlState->clip_right = context->state.clipRight();
        controlState->clip_bottom = context->state.clipBottom();
        return HYDRA_GATE_C_ADAPTER_OK;
    } catch (...) {
        return HYDRA_GATE_C_ADAPTER_INTERNAL_ERROR;
    }
}

HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_mouse_state(
    HydraGateCAdapterHandle handle, std::uint32_t* buttonsDown,
    std::int64_t* wheelAccumulator) {
    auto* context = contextOf(handle);
    if (context == nullptr || buttonsDown == nullptr ||
        wheelAccumulator == nullptr) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    try {
        std::scoped_lock lock(context->mutex);
        *buttonsDown = context->state.mouseButtonsDown();
        *wheelAccumulator = context->state.wheelAccumulator();
        return HYDRA_GATE_C_ADAPTER_OK;
    } catch (...) {
        return HYDRA_GATE_C_ADAPTER_INTERNAL_ERROR;
    }
}

HydraGateCAdapterResult HYDRA_GATE_C_ADAPTER_CALL
hydra_gate_c_adapter_get_snapshot(
    HydraGateCAdapterHandle handle, std::uint16_t probeVkey,
    HydraGateCAdapterSnapshotV1* snapshot) {
    auto* context = contextOf(handle);
    if (context == nullptr || snapshot == nullptr) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    if (snapshot->struct_size != sizeof(HydraGateCAdapterSnapshotV1)) {
        return HYDRA_GATE_C_ADAPTER_STRUCT_VERSION_MISMATCH;
    }
    if (probeVkey != HYDRA_GATE_C_ADAPTER_NO_PROBE_KEY && probeVkey >= 256u) {
        return HYDRA_GATE_C_ADAPTER_INVALID_ARGUMENT;
    }
    try {
        std::scoped_lock lock(context->mutex);
        auto state = context->state.snapshot();
        state.probeVkey = probeVkey;
        if (probeVkey < 256u) {
            const auto keyboard = context->state.keyboardState();
            state.keyboardStateByte =
                keyboard[static_cast<std::size_t>(probeVkey)];
            state.asyncKeyStateValue =
                context->state.consumeAsyncKeyState(probeVkey);
        }
        const auto requestedSize = snapshot->struct_size;
        std::memset(snapshot, 0, sizeof(*snapshot));
        snapshot->struct_size = requestedSize;
        copySnapshot(state, *snapshot);
        return HYDRA_GATE_C_ADAPTER_OK;
    } catch (...) {
        return HYDRA_GATE_C_ADAPTER_INTERNAL_ERROR;
    }
}

} // extern "C"
