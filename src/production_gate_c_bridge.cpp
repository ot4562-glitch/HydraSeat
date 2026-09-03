#include "production_gate_c_bridge_protocol.hpp"

#include "hydra/gate_c_adapter.h"
#include "hydra/gate_c_external_profile.hpp"
#include "hydra/gate_c_shim_api.h"
#include "hydra/gate_c_transport.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

using hydra::production::detail::ProductionBridgeDllState;
using hydra::production::detail::ProductionBridgeMappingV1;
constexpr wchar_t kBridgeWindowClass[] = L"HydraSeat.ProductionGateC.Window";
constexpr std::uint32_t kBridgeIoTimeoutMs = 250u;

LRESULT CALLBACK bridgeWindowProc(HWND window, UINT message,
                                  WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

HWND createBootstrapWindow() {
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = bridgeWindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = kBridgeWindowClass;
    const ATOM atom = RegisterClassExW(&cls);
    if (atom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    return CreateWindowExW(0, kBridgeWindowClass,
                           L"HydraSeat production Gate C bridge",
                           WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr,
                           cls.hInstance, nullptr);
}

struct WindowSearch {
    DWORD processId{0};
    HWND bootstrap{nullptr};
    HWND found{nullptr};
};

BOOL CALLBACK findWindowCallback(HWND window, LPARAM opaque) {
    auto& search = *reinterpret_cast<WindowSearch*>(opaque);
    DWORD processId = 0;
    if (window == search.bootstrap ||
        GetWindowThreadProcessId(window, &processId) == 0 ||
        processId != search.processId || IsWindowVisible(window) == FALSE ||
        GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }
    search.found = window;
    return FALSE;
}

HWND findApplicationWindow(HWND bootstrap) {
    WindowSearch search{GetCurrentProcessId(), bootstrap, nullptr};
    (void)EnumWindows(findWindowCallback, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

struct MappingView {
    HANDLE handle{nullptr};
    ProductionBridgeMappingV1* value{nullptr};
    MappingView() = default;
    MappingView(const MappingView&) = delete;
    MappingView& operator=(const MappingView&) = delete;
    MappingView(MappingView&& other) noexcept
        : handle(std::exchange(other.handle, nullptr)),
          value(std::exchange(other.value, nullptr)) {}
    ~MappingView() {
        if (value != nullptr) UnmapViewOfFile(value);
        if (handle != nullptr) CloseHandle(handle);
    }
};

std::optional<MappingView> openBridgeMapping() {
    MappingView result;
    const auto name = hydra::production::detail::productionBridgeMappingName(
        GetCurrentProcessId());
    result.handle = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE,
                                     name.c_str());
    if (result.handle == nullptr) return std::nullopt;
    result.value = static_cast<ProductionBridgeMappingV1*>(
        MapViewOfFile(result.handle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                      sizeof(ProductionBridgeMappingV1)));
    if (result.value == nullptr) return std::nullopt;
    const auto& config = *result.value;
    if (config.structSize != sizeof(config) ||
        config.magic != hydra::production::detail::kProductionBridgeMappingMagic ||
        config.version != hydra::production::detail::kProductionBridgeMappingVersion ||
        config.seatId == 0 || config.reserved0 != 0 || config.reserved1 != 0 ||
        !hydra::gatec::validProfiledShimMask(config.requiredApiMask) ||
        config.pipeName[0] == L'\0' ||
        config.pipeName[hydra::production::detail::kProductionBridgePipeNameChars - 1u] != L'\0') {
        return std::nullopt;
    }
    return result;
}

HydraGateCAdapterInputEventV1 adapterInput(
    const hydra::gatec::InputEventMessage& input) {
    HydraGateCAdapterInputEventV1 value{};
    value.struct_size = sizeof(value);
    value.kind = static_cast<std::uint8_t>(
        input.kind == hydra::gatec::InputKind::Keyboard
            ? HYDRA_GATE_C_ADAPTER_INPUT_KEYBOARD
            : HYDRA_GATE_C_ADAPTER_INPUT_MOUSE);
    switch (input.keyTransition) {
        case hydra::gatec::KeyTransition::Down:
            value.key_transition = HYDRA_GATE_C_ADAPTER_KEY_DOWN;
            break;
        case hydra::gatec::KeyTransition::Up:
            value.key_transition = HYDRA_GATE_C_ADAPTER_KEY_UP;
            break;
        default:
            value.key_transition = HYDRA_GATE_C_ADAPTER_KEY_NONE;
            break;
    }
    value.is_touchpad = input.isTouchpad ? 1u : 0u;
    value.timestamp_micros = input.timestampMicros;
    value.vkey = input.vkey;
    value.scan_code = input.scanCode;
    value.keyboard_flags = input.keyboardFlags;
    value.delta_x = input.deltaX;
    value.delta_y = input.deltaY;
    value.mouse_button_flags = input.mouseButtonFlags;
    value.wheel_delta = input.wheelDelta;
    return value;
}

HydraGateCAdapterControlStateV1 adapterControl(
    const hydra::gatec::ControlStateMessage& input) {
    HydraGateCAdapterControlStateV1 value{};
    value.struct_size = sizeof(value);
    value.cursor_x = input.cursorX;
    value.cursor_y = input.cursorY;
    value.clip_enabled = input.clipEnabled ? 1u : 0u;
    value.virtual_foreground = input.virtualForeground ? 1u : 0u;
    value.virtual_capture = input.virtualCapture ? 1u : 0u;
    value.clip_left = input.clipLeft;
    value.clip_top = input.clipTop;
    value.clip_right = input.clipRight;
    value.clip_bottom = input.clipBottom;
    return value;
}

bool configureWindow(HydraGateCAdapterHandle adapter, HWND target,
                     bool foreground, bool capture) {
    if (adapter == nullptr || target == nullptr) return false;
    HydraGateCAdapterWindowStateV2 state{};
    state.struct_size = sizeof(state);
    state.api_version = HYDRA_GATE_C_ADAPTER_API_VERSION;
    state.process_id = GetCurrentProcessId();
    state.target_window = reinterpret_cast<std::uint64_t>(target);
    if (foreground) {
        state.logical_foreground_window = reinterpret_cast<std::uint64_t>(target);
        state.logical_active_window = reinterpret_cast<std::uint64_t>(target);
        state.logical_focus_window = reinterpret_cast<std::uint64_t>(target);
    }
    if (capture) state.virtual_capture_window = reinterpret_cast<std::uint64_t>(target);
    return hydra_gate_c_adapter_configure_window_state(adapter, &state) ==
           HYDRA_GATE_C_ADAPTER_OK;
}

bool writeStateSnapshot(hydra::gatec::PipeChannel& channel,
                        std::uint64_t sequence,
                        HydraGateCAdapterHandle adapter,
                        std::uint16_t probeVkey) {
    HydraGateCAdapterSnapshotV1 native{};
    native.struct_size = sizeof(native);
    if (hydra_gate_c_adapter_get_snapshot(adapter, probeVkey, &native) !=
        HYDRA_GATE_C_ADAPTER_OK) {
        return false;
    }
    hydra::gatec::StateSnapshotMessage snapshot{};
    snapshot.lastAppliedSequence = native.last_applied_sequence;
    std::copy(std::begin(native.key_down_bits), std::end(native.key_down_bits),
              snapshot.keyDownBits.begin());
    std::copy(std::begin(native.key_pressed_edge_bits),
              std::end(native.key_pressed_edge_bits),
              snapshot.keyPressedEdgeBits.begin());
    snapshot.mouseButtonsDown = native.mouse_buttons_down;
    snapshot.wheelAccumulator = native.wheel_accumulator;
    snapshot.probeVkey = native.probe_vkey;
    snapshot.asyncKeyStateValue = native.async_key_state_value;
    snapshot.keyboardStateByte = native.keyboard_state_byte;
    snapshot.cursorX = native.cursor_x;
    snapshot.cursorY = native.cursor_y;
    snapshot.clipEnabled = native.clip_enabled != 0;
    snapshot.virtualForeground = native.virtual_foreground != 0;
    snapshot.virtualCapture = native.virtual_capture != 0;
    snapshot.clipLeft = native.clip_left;
    snapshot.clipTop = native.clip_top;
    snapshot.clipRight = native.clip_right;
    snapshot.clipBottom = native.clip_bottom;
    std::string error;
    return channel.writeFrame(
        hydra::gatec::encodeStateSnapshot(sequence, snapshot),
        kBridgeIoTimeoutMs, &error);
}

DWORD WINAPI bridgeWorker(void*) {
    auto mapping = openBridgeMapping();
    if (!mapping) return 10u;
    auto* state = mapping->value;
    hydra::production::detail::publishBridgeState(
        state, ProductionBridgeDllState::Starting, 0, false);

    HWND bootstrap = createBootstrapWindow();
    if (bootstrap == nullptr) {
        hydra::production::detail::publishBridgeState(
            state, ProductionBridgeDllState::Failed, 11, false);
        return 11u;
    }
    HydraGateCAdapterHandle adapter = hydra_gate_c_adapter_create();
    if (adapter == nullptr) {
        DestroyWindow(bootstrap);
        hydra::production::detail::publishBridgeState(
            state, ProductionBridgeDllState::Failed, 12, false);
        return 12u;
    }

    HydraGateCShimConfigV3 shim{};
    shim.struct_size = sizeof(shim);
    shim.api_version = HYDRA_GATE_C_SHIM_API_VERSION;
    shim.seat_id = state->seatId;
    shim.process_id = GetCurrentProcessId();
    shim.required_api_mask = state->requiredApiMask;
    shim.target_window = reinterpret_cast<std::uint64_t>(bootstrap);
    if ((state->requiredApiMask & HYDRA_GATE_C_SHIM_CURSOR_FOCUS_API_MASK) != 0) {
        shim.flags |= HYDRA_GATE_C_SHIM_ENABLE_CURSOR_FOCUS;
    }
    if ((state->requiredApiMask & HYDRA_GATE_C_SHIM_RAW_INPUT_API_MASK) != 0) {
        shim.flags |= HYDRA_GATE_C_SHIM_ENABLE_RAW_INPUT;
    }
    if (hydra_gate_c_shim_install_v3(adapter, &shim) != HYDRA_GATE_C_SHIM_OK) {
        hydra_gate_c_adapter_destroy(adapter);
        DestroyWindow(bootstrap);
        hydra::production::detail::publishBridgeState(
            state, ProductionBridgeDllState::Failed, 13, false);
        return 13u;
    }

    std::string error;
    auto channel = hydra::gatec::connectGateCClient(state->pipeName, 5'000u, &error);
    if (!channel.valid()) {
        const bool restored = hydra_gate_c_shim_uninstall() == HYDRA_GATE_C_SHIM_OK;
        hydra_gate_c_adapter_destroy(adapter);
        DestroyWindow(bootstrap);
        hydra::production::detail::publishBridgeState(
            state, ProductionBridgeDllState::Failed, 14, restored);
        return 14u;
    }

    hydra::gatec::HelloMessage hello{};
    hello.token = state->token;
    hello.seatId = state->seatId;
    hello.processId = GetCurrentProcessId();
    hello.architectureBits = static_cast<std::uint16_t>(sizeof(void*) * 8u);
    hello.targetWindow = reinterpret_cast<std::uint64_t>(bootstrap);
    if (!channel.writeFrame(hydra::gatec::encodeHello(1u, hello), 5'000u, &error)) {
        channel.close();
        const bool restored = hydra_gate_c_shim_uninstall() == HYDRA_GATE_C_SHIM_OK;
        hydra_gate_c_adapter_destroy(adapter);
        DestroyWindow(bootstrap);
        hydra::production::detail::publishBridgeState(
            state, ProductionBridgeDllState::Failed, 15, restored);
        return 15u;
    }
    const auto ackFrame = channel.readFrame(5'000u);
    hydra::gatec::HelloAckMessage ack{};
    if (!ackFrame || !ackFrame.frame ||
        !hydra::gatec::decodeHelloAck(*ackFrame.frame, ack, &error) ||
        !ack.accepted) {
        channel.close();
        const bool restored = hydra_gate_c_shim_uninstall() == HYDRA_GATE_C_SHIM_OK;
        hydra_gate_c_adapter_destroy(adapter);
        DestroyWindow(bootstrap);
        hydra::production::detail::publishBridgeState(
            state, ProductionBridgeDllState::Failed, 16, restored);
        return 16u;
    }

    hydra::production::detail::publishBridgeState(
        state, ProductionBridgeDllState::Active, 0, false);
    HWND applicationWindow = nullptr;
    bool virtualForeground = true;
    bool virtualCapture = false;
    std::uint64_t lastSequence = 1u;
    bool shutdownRequested = false;
    bool loopHealthy = true;
    while (!shutdownRequested) {
        HWND discovered = findApplicationWindow(bootstrap);
        if (discovered != nullptr && discovered != applicationWindow) {
            applicationWindow = discovered;
            if (!configureWindow(adapter, applicationWindow,
                                 virtualForeground, virtualCapture)) {
                loopHealthy = false;
                break;
            }
        }
        const auto read = channel.readFrame(kBridgeIoTimeoutMs);
        if (read.status == hydra::gatec::TransportStatus::Timeout) continue;
        if (!read || !read.frame || read.frame->sequence <= lastSequence) {
            loopHealthy = false;
            break;
        }
        const auto& frame = *read.frame;
        lastSequence = frame.sequence;
        if (frame.type == hydra::gatec::MessageType::InputEvent) {
            hydra::gatec::InputEventMessage input{};
            if (!hydra::gatec::decodeInputEvent(frame, input, &error)) {
                loopHealthy = false;
                break;
            }
            const auto native = adapterInput(input);
            if (hydra_gate_c_adapter_apply_input(adapter, frame.sequence, &native) !=
                HYDRA_GATE_C_ADAPTER_OK) {
                loopHealthy = false;
                break;
            }
            if ((state->requiredApiMask & HYDRA_GATE_C_SHIM_RAW_INPUT_API_MASK) != 0 &&
                hydra_gate_c_shim_dispatch_raw_input() != HYDRA_GATE_C_SHIM_OK) {
                loopHealthy = false;
                break;
            }
            continue;
        }
        if (frame.type == hydra::gatec::MessageType::ControlState) {
            hydra::gatec::ControlStateMessage control{};
            if (!hydra::gatec::decodeControlState(frame, control, &error)) {
                loopHealthy = false;
                break;
            }
            virtualForeground = control.virtualForeground;
            virtualCapture = control.virtualCapture;
            const auto native = adapterControl(control);
            if (hydra_gate_c_adapter_apply_control(adapter, frame.sequence, &native) !=
                HYDRA_GATE_C_ADAPTER_OK) {
                loopHealthy = false;
                break;
            }
            HWND target = applicationWindow != nullptr ? applicationWindow : bootstrap;
            if (!configureWindow(adapter, target, virtualForeground, virtualCapture)) {
                loopHealthy = false;
                break;
            }
            continue;
        }
        if (frame.type == hydra::gatec::MessageType::QuerySnapshot) {
            hydra::gatec::QuerySnapshotMessage query{};
            if (!hydra::gatec::decodeQuerySnapshot(frame, query, &error) ||
                !writeStateSnapshot(channel, frame.sequence, adapter, query.probeVkey)) {
                loopHealthy = false;
                break;
            }
            continue;
        }
        if (frame.type == hydra::gatec::MessageType::Shutdown) {
            if (!hydra::gatec::decodeShutdown(frame, &error)) {
                loopHealthy = false;
                break;
            }
            shutdownRequested = true;
            continue;
        }
        loopHealthy = false;
        break;
    }

    hydra::production::detail::publishBridgeState(
        state, ProductionBridgeDllState::Stopping, loopHealthy ? 0 : 20, false);
    (void)hydra_gate_c_shim_mark_adapter_unavailable();
    const bool restored = hydra_gate_c_shim_uninstall() == HYDRA_GATE_C_SHIM_OK;
    channel.close();
    hydra_gate_c_adapter_destroy(adapter);
    DestroyWindow(bootstrap);
    hydra::production::detail::publishBridgeState(
        state,
        restored && shutdownRequested ? ProductionBridgeDllState::Stopped
                                      : ProductionBridgeDllState::Failed,
        restored ? (loopHealthy ? 0 : 20) : 21,
        restored);
    return restored && shutdownRequested ? 0u : 21u;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE worker = CreateThread(nullptr, 0, bridgeWorker, nullptr, 0, nullptr);
        if (worker == nullptr) return FALSE;
        CloseHandle(worker);
    }
    return TRUE;
}
