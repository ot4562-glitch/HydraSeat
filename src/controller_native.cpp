#include "hydra/controller_runtime.hpp"

#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <xinput.h>
#endif

namespace hydra::controller {
namespace {

#if defined(_WIN32)

directinput::DirectInputInstanceId normalizeGuid(const GUID& value) noexcept {
    directinput::DirectInputInstanceId result;
    result.data1 = static_cast<std::uint32_t>(value.Data1);
    result.data2 = static_cast<std::uint16_t>(value.Data2);
    result.data3 = static_cast<std::uint16_t>(value.Data3);
    for (std::size_t index = 0; index < result.data4.size(); ++index) {
        result.data4[index] = static_cast<std::uint8_t>(value.Data4[index]);
    }
    return result;
}

std::wstring boundedWideString(const wchar_t* value, std::size_t capacity) {
    if (value == nullptr) return {};
    std::size_t length = 0;
    while (length < capacity && value[length] != L'\0') ++length;
    std::wstring result(value, length);
    if (result.size() > kMaxControllerDisplayNameChars) {
        result.resize(kMaxControllerDisplayNameChars);
    }
    return result;
}

std::string asciiRuntimeKey(std::wstring_view persistentId) {
    std::string result;
    result.reserve(persistentId.size());
    for (const wchar_t ch : persistentId) {
        if (ch > 0x7f) return {};
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

struct DirectInputEnumerationState {
    std::vector<SourceDescriptor>* sources{nullptr};
    bool failed{false};
};

BOOL CALLBACK directInputCallback(const DIDEVICEINSTANCEW* instance,
                                  void* reference) noexcept {
    auto* state = static_cast<DirectInputEnumerationState*>(reference);
    if (state == nullptr || state->sources == nullptr || instance == nullptr ||
        instance->dwSize < sizeof(DIDEVICEINSTANCEW) ||
        state->sources->size() >= kMaxControllerSources) {
        if (state != nullptr) state->failed = true;
        return DIENUM_STOP;
    }
    try {
        SourceDescriptor descriptor;
        descriptor.api = ApiSurface::DirectInput;
        descriptor.identityQuality = IdentityQuality::Stable;
        descriptor.directInputInstanceId = normalizeGuid(instance->guidInstance);
        descriptor.persistentId =
            formatDirectInputPersistentId(*descriptor.directInputInstanceId);
        descriptor.runtimeKey = asciiRuntimeKey(*descriptor.persistentId);
        descriptor.displayName = boundedWideString(instance->tszInstanceName, MAX_PATH);
        descriptor.connected = true;
        descriptor.stateAvailable = false;
        descriptor.vibrationSupported = false;
        if (descriptor.runtimeKey.empty()) {
            state->failed = true;
            return DIENUM_STOP;
        }
        state->sources->push_back(std::move(descriptor));
    } catch (...) {
        state->failed = true;
        return DIENUM_STOP;
    }
    return DIENUM_CONTINUE;
}

bool appendDirectInputSources(std::vector<SourceDescriptor>& sources,
                              std::string& error) noexcept {
    IDirectInput8W* directInput = nullptr;
    const HRESULT created = DirectInput8Create(
        GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
        reinterpret_cast<void**>(&directInput), nullptr);
    if (FAILED(created) || directInput == nullptr) {
        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%08lx",
                      static_cast<unsigned long>(created));
        error = std::string("DirectInput8Create failed: 0x") + buffer;
        return false;
    }

    DirectInputEnumerationState state{&sources, false};
    const HRESULT enumerated = directInput->EnumDevices(
        DI8DEVCLASS_GAMECTRL, directInputCallback, &state, DIEDFL_ATTACHEDONLY);
    directInput->Release();
    if (FAILED(enumerated) || state.failed) {
        error = "DirectInput attached-controller enumeration failed or exceeded bounds";
        return false;
    }
    return true;
}

gatec::XInputBatteryType batteryType(BYTE value) noexcept {
    switch (value) {
        case BATTERY_TYPE_DISCONNECTED:
            return gatec::XInputBatteryType::Disconnected;
        case BATTERY_TYPE_WIRED:
            return gatec::XInputBatteryType::Wired;
        case BATTERY_TYPE_ALKALINE:
            return gatec::XInputBatteryType::Alkaline;
        case BATTERY_TYPE_NIMH:
            return gatec::XInputBatteryType::Nimh;
        default:
            return gatec::XInputBatteryType::Unknown;
    }
}

gatec::XInputBatteryLevel batteryLevel(BYTE value) noexcept {
    switch (value) {
        case BATTERY_LEVEL_EMPTY: return gatec::XInputBatteryLevel::Empty;
        case BATTERY_LEVEL_LOW: return gatec::XInputBatteryLevel::Low;
        case BATTERY_LEVEL_MEDIUM: return gatec::XInputBatteryLevel::Medium;
        case BATTERY_LEVEL_FULL: return gatec::XInputBatteryLevel::Full;
        default: return gatec::XInputBatteryLevel::Empty;
    }
}

SourceDescriptor xInputSource(DWORD slot) noexcept {
    SourceDescriptor descriptor;
    descriptor.runtimeKey = "xinput-slot:" + std::to_string(slot);
    descriptor.displayName = L"XInput runtime slot " + std::to_wstring(slot);
    descriptor.api = ApiSurface::XInput;
    descriptor.identityQuality = IdentityQuality::RuntimeOnly;
    descriptor.runtimeXInputSlotHint = static_cast<std::uint8_t>(slot);

    XINPUT_STATE state{};
    const DWORD stateResult = XInputGetState(slot, &state);
    if (stateResult != ERROR_SUCCESS) {
        descriptor.connected = false;
        return descriptor;
    }

    descriptor.connected = true;
    descriptor.stateAvailable = true;
    descriptor.gamepad.buttons = state.Gamepad.wButtons;
    descriptor.gamepad.leftTrigger = state.Gamepad.bLeftTrigger;
    descriptor.gamepad.rightTrigger = state.Gamepad.bRightTrigger;
    descriptor.gamepad.thumbLX = state.Gamepad.sThumbLX;
    descriptor.gamepad.thumbLY = state.Gamepad.sThumbLY;
    descriptor.gamepad.thumbRX = state.Gamepad.sThumbRX;
    descriptor.gamepad.thumbRY = state.Gamepad.sThumbRY;

    XINPUT_CAPABILITIES capabilities{};
    if (XInputGetCapabilities(slot, 0, &capabilities) == ERROR_SUCCESS) {
        descriptor.capabilitiesAvailable = true;
        descriptor.capabilities.type = gatec::XInputCapabilityType::Gamepad;
        descriptor.capabilities.subtype = capabilities.SubType;
        descriptor.capabilities.flags = capabilities.Flags;
        descriptor.capabilities.gamepad.buttons = capabilities.Gamepad.wButtons;
        descriptor.capabilities.gamepad.leftTrigger = capabilities.Gamepad.bLeftTrigger;
        descriptor.capabilities.gamepad.rightTrigger = capabilities.Gamepad.bRightTrigger;
        descriptor.capabilities.gamepad.thumbLX = capabilities.Gamepad.sThumbLX;
        descriptor.capabilities.gamepad.thumbLY = capabilities.Gamepad.sThumbLY;
        descriptor.capabilities.gamepad.thumbRX = capabilities.Gamepad.sThumbRX;
        descriptor.capabilities.gamepad.thumbRY = capabilities.Gamepad.sThumbRY;
        descriptor.capabilities.leftMotorMaximum = capabilities.Vibration.wLeftMotorSpeed;
        descriptor.capabilities.rightMotorMaximum = capabilities.Vibration.wRightMotorSpeed;
        descriptor.vibrationSupported =
            capabilities.Vibration.wLeftMotorSpeed != 0 ||
            capabilities.Vibration.wRightMotorSpeed != 0;
        descriptor.capabilities.vibrationSupported = descriptor.vibrationSupported;
    }

    XINPUT_BATTERY_INFORMATION battery{};
    if (XInputGetBatteryInformation(slot, BATTERY_DEVTYPE_GAMEPAD, &battery) ==
        ERROR_SUCCESS) {
        descriptor.batteryInformationAvailable = true;
        if (battery.BatteryType != BATTERY_TYPE_DISCONNECTED) {
            descriptor.battery.available = true;
            descriptor.battery.deviceType = gatec::XInputBatteryDeviceType::Gamepad;
            descriptor.battery.batteryType = batteryType(battery.BatteryType);
            descriptor.battery.batteryLevel = batteryLevel(battery.BatteryLevel);
        }
    }
    return descriptor;
}

bool parseXInputRuntimeKey(std::string_view runtimeKey, DWORD& slot) noexcept {
    constexpr std::string_view prefix = "xinput-slot:";
    if (!runtimeKey.starts_with(prefix) || runtimeKey.size() != prefix.size() + 1u) {
        return false;
    }
    const char digit = runtimeKey.back();
    if (digit < '0' || digit > '3') return false;
    slot = static_cast<DWORD>(digit - '0');
    return true;
}

class NativeControllerSourceBackend final : public SourceBackend {
public:
    bool scan(std::vector<SourceDescriptor>& sources,
              std::string& error) noexcept override {
        try {
            sources.clear();
            sources.reserve(4u + directinput::kMaxDirectInputNativeDevices);
            for (DWORD slot = 0; slot < 4u; ++slot) {
                sources.push_back(xInputSource(slot));
            }
            if (!appendDirectInputSources(sources, error)) return false;
            error.clear();
            return true;
        } catch (...) {
            error = "native controller scan raised an unexpected exception";
            return false;
        }
    }

    bool setVibration(std::string_view runtimeKey,
                      std::uint16_t leftMotor,
                      std::uint16_t rightMotor,
                      std::string& error) noexcept override {
        DWORD slot = 0;
        if (!parseXInputRuntimeKey(runtimeKey, slot)) {
            error = "native vibration is supported only for an exact current XInput runtime source";
            return false;
        }
        XINPUT_STATE state{};
        if (XInputGetState(slot, &state) != ERROR_SUCCESS) {
            error = "XInput source disconnected before vibration routing";
            return false;
        }
        XINPUT_VIBRATION vibration{};
        vibration.wLeftMotorSpeed = leftMotor;
        vibration.wRightMotorSpeed = rightMotor;
        const DWORD result = XInputSetState(slot, &vibration);
        if (result != ERROR_SUCCESS) {
            error = "XInputSetState failed for the exact selected runtime slot";
            return false;
        }
        error.clear();
        return true;
    }
};

#else

class NativeControllerSourceBackend final : public SourceBackend {
public:
    bool scan(std::vector<SourceDescriptor>& sources,
              std::string& error) noexcept override {
        sources.clear();
        error = "native Windows controller source backend is unavailable on this platform";
        return false;
    }
    bool setVibration(std::string_view, std::uint16_t, std::uint16_t,
                      std::string& error) noexcept override {
        error = "native Windows controller vibration backend is unavailable on this platform";
        return false;
    }
};

#endif

} // namespace

std::shared_ptr<SourceBackend> makeNativeControllerSourceBackend() {
    return std::make_shared<NativeControllerSourceBackend>();
}

} // namespace hydra::controller
