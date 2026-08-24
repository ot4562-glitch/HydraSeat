#include "hydra/gate_c_shim_api.h"

#include "hydra/win32_iat_patch.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

static_assert(sizeof(HydraGateCShimConfigV1) ==
              HYDRA_GATE_C_SHIM_CONFIG_V1_BYTES);
static_assert(sizeof(HydraGateCShimStatusV1) ==
              HYDRA_GATE_C_SHIM_STATUS_V1_BYTES);

#ifdef _WIN32
using AsyncKeyStateFunction = SHORT(WINAPI*)(int);
using KeyStateFunction = SHORT(WINAPI*)(int);
using KeyboardStateFunction = BOOL(WINAPI*)(PBYTE);
#endif

struct ShimRuntime {
    std::mutex mutex;
    std::condition_variable condition;
    HydraGateCShimLifecycle lifecycle{HYDRA_GATE_C_SHIM_INACTIVE};
    HydraGateCShimResult lastResult{HYDRA_GATE_C_SHIM_OK};
    std::uint64_t generation{0};
    std::uint32_t systemError{0};
    std::uint32_t discoveredMask{0};
    std::uint32_t patchedMask{0};
    std::uint32_t restoredMask{0};
    std::uint32_t hookFailureCount{0};
    HydraGateCAdapterResult lastAdapterResult{HYDRA_GATE_C_ADAPTER_OK};
    bool adapterAvailable{false};
    bool rollbackComplete{true};
    std::uint32_t activeCalls{0};
    HydraGateCAdapterHandle adapter{nullptr};
    std::uint32_t seatId{0};
    std::uint32_t processId{0};
    hydra::gatec::PollingIatPatchSet patches;
    std::array<std::uintptr_t, hydra::gatec::kPollingImportCount> originals{};
};

ShimRuntime& runtime() {
    static ShimRuntime value;
    return value;
}

HydraGateCShimResult mapPatchResult(
    hydra::gatec::IatPatchStatus status) noexcept {
    using hydra::gatec::IatPatchStatus;
    switch (status) {
    case IatPatchStatus::Success: return HYDRA_GATE_C_SHIM_OK;
    case IatPatchStatus::AlreadyInstalled:
        return HYDRA_GATE_C_SHIM_ALREADY_INSTALLED;
    case IatPatchStatus::MissingImport:
        return HYDRA_GATE_C_SHIM_IMPORT_NOT_FOUND;
    case IatPatchStatus::DuplicateImport:
        return HYDRA_GATE_C_SHIM_DUPLICATE_IMPORT;
    case IatPatchStatus::AlreadyPatched:
        return HYDRA_GATE_C_SHIM_ALREADY_PATCHED;
    case IatPatchStatus::InvalidImage:
        return HYDRA_GATE_C_SHIM_INVALID_IMAGE;
    case IatPatchStatus::ProtectionFailure:
        return HYDRA_GATE_C_SHIM_PROTECTION_FAILURE;
    case IatPatchStatus::PatchFailure:
        return HYDRA_GATE_C_SHIM_PATCH_FAILURE;
    case IatPatchStatus::RollbackFailure:
        return HYDRA_GATE_C_SHIM_ROLLBACK_FAILURE;
    case IatPatchStatus::UnsupportedPlatform:
        return HYDRA_GATE_C_SHIM_UNSUPPORTED_PLATFORM;
    }
    return HYDRA_GATE_C_SHIM_INTERNAL_ERROR;
}

#ifdef _WIN32

struct HookCall {
    std::uintptr_t original{0};
    HydraGateCAdapterHandle adapter{nullptr};
    bool virtualize{false};
    bool failClosed{false};
};

HookCall beginHook(hydra::gatec::PollingImport function,
                   bool supportedDomain) noexcept {
    auto& state = runtime();
    std::scoped_lock lock(state.mutex);
    HookCall call;
    call.original = state.originals[static_cast<std::size_t>(function)];
    if (!supportedDomain || state.lifecycle == HYDRA_GATE_C_SHIM_INACTIVE ||
        state.lifecycle == HYDRA_GATE_C_SHIM_INSTALLING ||
        state.lifecycle == HYDRA_GATE_C_SHIM_UNINSTALLING) {
        return call;
    }
    if (state.lifecycle == HYDRA_GATE_C_SHIM_ACTIVE &&
        state.adapterAvailable && state.adapter != nullptr) {
        ++state.activeCalls;
        call.adapter = state.adapter;
        call.virtualize = true;
        return call;
    }
    call.failClosed = true;
    return call;
}

void finishHook(HydraGateCAdapterResult result) noexcept {
    auto& state = runtime();
    std::scoped_lock lock(state.mutex);
    if (state.activeCalls != 0) --state.activeCalls;
    if (result != HYDRA_GATE_C_ADAPTER_OK &&
        state.lifecycle == HYDRA_GATE_C_SHIM_ACTIVE) {
        ++state.hookFailureCount;
        state.lastAdapterResult = result;
        state.adapterAvailable = false;
        state.lifecycle = HYDRA_GATE_C_SHIM_FAIL_CLOSED;
        state.lastResult = HYDRA_GATE_C_SHIM_ADAPTER_UNAVAILABLE;
        state.systemError = ERROR_DEVICE_NOT_CONNECTED;
    }
    if (state.activeCalls == 0) state.condition.notify_all();
}

SHORT WINAPI shimGetAsyncKeyState(int vkey) noexcept {
    const auto call = beginHook(
        hydra::gatec::PollingImport::GetAsyncKeyState,
        vkey >= 0 && vkey < 256);
    const auto original = reinterpret_cast<AsyncKeyStateFunction>(call.original);
    if (!call.virtualize) {
        if (call.failClosed) SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        return call.failClosed || original == nullptr ? 0 : original(vkey);
    }
    std::uint16_t value = 0;
    const auto result = hydra_gate_c_adapter_get_async_key_state(
        call.adapter, static_cast<std::uint32_t>(vkey), &value);
    finishHook(result);
    if (result != HYDRA_GATE_C_ADAPTER_OK) {
        SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        return 0;
    }
    return static_cast<SHORT>(value);
}

SHORT WINAPI shimGetKeyState(int vkey) noexcept {
    const auto call = beginHook(
        hydra::gatec::PollingImport::GetKeyState,
        vkey >= 0 && vkey < 256);
    const auto original = reinterpret_cast<KeyStateFunction>(call.original);
    if (!call.virtualize) {
        if (call.failClosed) SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        return call.failClosed || original == nullptr ? 0 : original(vkey);
    }
    std::uint16_t value = 0;
    const auto result = hydra_gate_c_adapter_get_key_state(
        call.adapter, static_cast<std::uint32_t>(vkey), &value);
    finishHook(result);
    if (result != HYDRA_GATE_C_ADAPTER_OK) {
        SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        return 0;
    }
    // Toggle-state low bits are explicitly unsupported in shim ABI v1.
    return static_cast<SHORT>(value & 0x8000u);
}

BOOL WINAPI shimGetKeyboardState(PBYTE keyboardState) noexcept {
    const auto call = beginHook(
        hydra::gatec::PollingImport::GetKeyboardState,
        true);
    const auto original = reinterpret_cast<KeyboardStateFunction>(call.original);
    if (keyboardState == nullptr) {
        if (call.virtualize) finishHook(HYDRA_GATE_C_ADAPTER_OK);
        if (call.virtualize || call.failClosed) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return original == nullptr ? FALSE : original(keyboardState);
    }
    if (!call.virtualize) {
        if (call.failClosed) SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        if (call.failClosed) return FALSE;
        return original == nullptr ? FALSE : original(keyboardState);
    }
    std::array<std::uint8_t,
               HYDRA_GATE_C_ADAPTER_KEYBOARD_STATE_BYTES> temporary{};
    const auto result = hydra_gate_c_adapter_get_keyboard_state(
        call.adapter, temporary.data(), temporary.size());
    finishHook(result);
    if (result != HYDRA_GATE_C_ADAPTER_OK) {
        SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        return FALSE;
    }
    std::copy(temporary.begin(), temporary.end(), keyboardState);
    return TRUE;
}

#endif

} // namespace

extern "C" {

std::uint32_t HYDRA_GATE_C_SHIM_CALL hydra_gate_c_shim_api_version(void) {
    return HYDRA_GATE_C_SHIM_API_VERSION;
}

HydraGateCShimResult HYDRA_GATE_C_SHIM_CALL hydra_gate_c_shim_install(
    HydraGateCAdapterHandle adapter, const HydraGateCShimConfigV1* config) {
    try {
    if (adapter == nullptr || config == nullptr) {
        return HYDRA_GATE_C_SHIM_INVALID_ARGUMENT;
    }
    if (config->struct_size != sizeof(HydraGateCShimConfigV1) ||
        config->api_version != HYDRA_GATE_C_SHIM_API_VERSION) {
        return HYDRA_GATE_C_SHIM_STRUCT_VERSION_MISMATCH;
    }
    if (config->seat_id == 0 || config->process_id == 0 ||
        config->flags != 0 || config->reserved0 != 0) {
        return HYDRA_GATE_C_SHIM_INVALID_ARGUMENT;
    }
    if (hydra_gate_c_adapter_api_version() !=
        HYDRA_GATE_C_ADAPTER_API_VERSION) {
        return HYDRA_GATE_C_SHIM_ADAPTER_VERSION_MISMATCH;
    }
#ifdef _WIN32
    if (config->process_id != GetCurrentProcessId()) {
        return HYDRA_GATE_C_SHIM_INVALID_ARGUMENT;
    }
    auto& state = runtime();
    std::unique_lock lock(state.mutex);
    if (state.lifecycle == HYDRA_GATE_C_SHIM_ACTIVE &&
        state.adapter == adapter && state.seatId == config->seat_id &&
        state.processId == config->process_id) {
        state.lastResult = HYDRA_GATE_C_SHIM_ALREADY_INSTALLED;
        return state.lastResult;
    }
    if (state.lifecycle != HYDRA_GATE_C_SHIM_INACTIVE) {
        state.lastResult = HYDRA_GATE_C_SHIM_INVALID_ARGUMENT;
        return state.lastResult;
    }
    state.lifecycle = HYDRA_GATE_C_SHIM_INSTALLING;
    state.lastResult = HYDRA_GATE_C_SHIM_OK;
    state.systemError = 0;
    state.discoveredMask = 0;
    state.patchedMask = 0;
    state.restoredMask = 0;
    state.rollbackComplete = true;
    state.lastAdapterResult = HYDRA_GATE_C_ADAPTER_OK;
    state.originals.fill(0);

    const std::array<std::uintptr_t, hydra::gatec::kPollingImportCount>
        replacements{
            reinterpret_cast<std::uintptr_t>(&shimGetAsyncKeyState),
            reinterpret_cast<std::uintptr_t>(&shimGetKeyState),
            reinterpret_cast<std::uintptr_t>(&shimGetKeyboardState)};
    std::vector<hydra::gatec::PollingIatSlot> slots;
    const auto discovered = hydra::gatec::discoverCurrentProcessPollingImports(
        replacements, slots);
    state.discoveredMask = discovered.discoveredMask;
    if (!discovered) {
        state.lifecycle = HYDRA_GATE_C_SHIM_INACTIVE;
        state.lastResult = mapPatchResult(discovered.status);
        state.systemError = discovered.systemError;
        return state.lastResult;
    }
    for (const auto& slot : slots) {
        state.originals[static_cast<std::size_t>(slot.function)] =
            slot.original;
    }
    const auto patched = state.patches.install(
        slots, hydra::gatec::writeProcessIatSlot);
    state.discoveredMask = patched.discoveredMask;
    state.patchedMask = patched.patchedMask;
    state.restoredMask = patched.restoredMask;
    state.rollbackComplete = patched.rollbackComplete;
    state.systemError = patched.systemError;
    state.lastResult = mapPatchResult(patched.status);
    if (!patched) {
        state.lifecycle = patched.rollbackComplete
                              ? HYDRA_GATE_C_SHIM_INACTIVE
                              : HYDRA_GATE_C_SHIM_FAIL_CLOSED;
        return state.lastResult;
    }
    state.adapter = adapter;
    state.seatId = config->seat_id;
    state.processId = config->process_id;
    state.adapterAvailable = true;
    state.lifecycle = HYDRA_GATE_C_SHIM_ACTIVE;
    state.patchedMask = HYDRA_GATE_C_SHIM_POLLING_API_MASK;
    ++state.generation;
    state.lastResult = HYDRA_GATE_C_SHIM_OK;
    return state.lastResult;
#else
    (void)adapter;
    (void)config;
    return HYDRA_GATE_C_SHIM_UNSUPPORTED_PLATFORM;
#endif
    } catch (...) {
        auto& state = runtime();
        std::unique_lock lock(state.mutex);
        state.adapterAvailable = false;
        state.adapter = nullptr;
        state.seatId = 0;
        state.processId = 0;
        state.lifecycle = state.patches.installed()
                              ? HYDRA_GATE_C_SHIM_FAIL_CLOSED
                              : HYDRA_GATE_C_SHIM_INACTIVE;
        state.lastResult = HYDRA_GATE_C_SHIM_INTERNAL_ERROR;
        state.rollbackComplete = !state.patches.installed();
        return state.lastResult;
    }
}

HydraGateCShimResult HYDRA_GATE_C_SHIM_CALL
hydra_gate_c_shim_mark_adapter_unavailable(void) {
    try {
    auto& state = runtime();
    std::unique_lock lock(state.mutex);
    if (state.lifecycle != HYDRA_GATE_C_SHIM_ACTIVE &&
        state.lifecycle != HYDRA_GATE_C_SHIM_FAIL_CLOSED) {
        state.lastResult = HYDRA_GATE_C_SHIM_INVALID_ARGUMENT;
        return state.lastResult;
    }
    state.adapterAvailable = false;
    state.lifecycle = HYDRA_GATE_C_SHIM_FAIL_CLOSED;
    state.lastResult = HYDRA_GATE_C_SHIM_ADAPTER_UNAVAILABLE;
    state.lastAdapterResult = HYDRA_GATE_C_ADAPTER_INVALID_STATE;
#ifdef _WIN32
    state.systemError = ERROR_DEVICE_NOT_CONNECTED;
#endif
    state.condition.wait(lock, [&] { return state.activeCalls == 0; });
    return state.lastResult;
    } catch (...) {
        return HYDRA_GATE_C_SHIM_INTERNAL_ERROR;
    }
}

HydraGateCShimResult HYDRA_GATE_C_SHIM_CALL
hydra_gate_c_shim_uninstall(void) {
    try {
    auto& state = runtime();
    std::unique_lock lock(state.mutex);
    if (state.lifecycle == HYDRA_GATE_C_SHIM_INACTIVE &&
        !state.patches.installed()) {
        state.lastResult = HYDRA_GATE_C_SHIM_OK;
        return state.lastResult;
    }
    if (!state.patches.installed() && !state.rollbackComplete) {
        state.lifecycle = HYDRA_GATE_C_SHIM_FAIL_CLOSED;
        state.adapterAvailable = false;
        state.lastResult = HYDRA_GATE_C_SHIM_ROLLBACK_FAILURE;
        return state.lastResult;
    }
    state.lifecycle = HYDRA_GATE_C_SHIM_UNINSTALLING;
    state.adapterAvailable = false;
    state.condition.wait(lock, [&] { return state.activeCalls == 0; });
    const auto restored = state.patches.uninstall(
        hydra::gatec::writeProcessIatSlot);
    state.restoredMask = restored.restoredMask;
    state.patchedMask = restored.patchedMask;
    state.rollbackComplete = restored.rollbackComplete;
    state.systemError = restored.systemError;
    state.lastResult = mapPatchResult(restored.status);
    if (restored) {
        state.lifecycle = HYDRA_GATE_C_SHIM_INACTIVE;
        state.adapter = nullptr;
        state.seatId = 0;
        state.processId = 0;
        state.adapterAvailable = false;
        state.patchedMask = 0;
        return HYDRA_GATE_C_SHIM_OK;
    }
    state.lifecycle = HYDRA_GATE_C_SHIM_FAIL_CLOSED;
    return state.lastResult;
    } catch (...) {
        auto& state = runtime();
        std::scoped_lock lock(state.mutex);
        state.lifecycle = HYDRA_GATE_C_SHIM_FAIL_CLOSED;
        state.adapterAvailable = false;
        state.lastResult = HYDRA_GATE_C_SHIM_INTERNAL_ERROR;
        state.rollbackComplete = false;
        return state.lastResult;
    }
}

HydraGateCShimResult HYDRA_GATE_C_SHIM_CALL hydra_gate_c_shim_get_status(
    HydraGateCShimStatusV1* status) {
    try {
    if (status == nullptr) return HYDRA_GATE_C_SHIM_INVALID_ARGUMENT;
    if (status->struct_size != sizeof(HydraGateCShimStatusV1)) {
        return HYDRA_GATE_C_SHIM_STRUCT_VERSION_MISMATCH;
    }
    auto& state = runtime();
    std::scoped_lock lock(state.mutex);
    const auto requestedSize = status->struct_size;
    std::memset(status, 0, sizeof(*status));
    status->struct_size = requestedSize;
    status->api_version = HYDRA_GATE_C_SHIM_API_VERSION;
    status->lifecycle = static_cast<std::uint32_t>(state.lifecycle);
    status->last_result = static_cast<std::uint32_t>(state.lastResult);
    status->generation = state.generation;
    status->system_error = state.systemError;
    status->module_kind = HYDRA_GATE_C_SHIM_MODULE_CURRENT_EXECUTABLE;
    status->expected_api_mask = HYDRA_GATE_C_SHIM_POLLING_API_MASK;
    status->discovered_api_mask = state.discoveredMask;
    status->patched_api_mask = state.patchedMask;
    status->restored_api_mask = state.restoredMask;
    status->hook_failure_count = state.hookFailureCount;
    status->last_adapter_result =
        static_cast<std::uint32_t>(state.lastAdapterResult);
    status->adapter_available = state.adapterAvailable ? 1u : 0u;
    status->rollback_complete = state.rollbackComplete ? 1u : 0u;
    return HYDRA_GATE_C_SHIM_OK;
    } catch (...) {
        return HYDRA_GATE_C_SHIM_INTERNAL_ERROR;
    }
}

} // extern "C"
