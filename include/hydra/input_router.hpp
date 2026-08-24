#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hydra {

inline constexpr std::uint32_t kUnknownRawInputDeviceType =
    ~std::uint32_t{0};

enum class RawKeyTransition {
    None,
    Down,
    Up
};

enum class RawInputDeviceChangeKind {
    Arrival,
    Removal
};

struct RawInputEvent {
    std::uint64_t sequence{0};
    std::uint64_t monotonicTimestampMicros{0};

    std::uintptr_t deviceHandle{0};
    std::wstring deviceId;
    std::wstring devicePath;

    std::uint32_t messageType{0};
    std::uint32_t rawDevType{0};

    std::uint32_t vkey{0};
    std::uint16_t scanCode{0};
    std::uint16_t keyboardFlags{0};
    RawKeyTransition keyTransition{RawKeyTransition::None};

    std::int32_t deltaX{0};
    std::int32_t deltaY{0};
    std::uint16_t mouseFlags{0};
    std::uint16_t mouseButtonFlags{0};
    std::uint16_t mouseButtonData{0};
    std::int16_t wheelDelta{0};
    bool isAbsoluteMouse{false};
    bool isTouchpad{false};

    // Kept for older callers while Workspace terminology migrates to Seat.
    std::uint32_t assignedWorkspaceId{0};
};

struct RawInputDeviceDescriptor {
    std::uintptr_t deviceHandle{0};
    std::wstring deviceId;
    std::wstring devicePath;
    std::uint32_t rawDevType{0};
    bool isTouchpad{false};
    bool online{true};

    bool operator==(const RawInputDeviceDescriptor&) const = default;
};

struct RawInputDeviceChange {
    std::uint64_t sequence{0};
    std::uint64_t monotonicTimestampMicros{0};
    RawInputDeviceChangeKind kind{RawInputDeviceChangeKind::Arrival};
    RawInputDeviceDescriptor device;
};

struct InputRouterError {
    std::wstring operation;
    std::uint32_t systemError{0};
};

struct InputRouterStatistics {
    std::uint64_t decodedEvents{0};
    std::uint64_t droppedEvents{0};
    std::uint64_t deviceArrivals{0};
    std::uint64_t deviceRemovals{0};
};

using InputCallback = std::function<void(const RawInputEvent&)>;
using DeviceChangeCallback = std::function<void(const RawInputDeviceChange&)>;

class InputRouter {
public:
    InputRouter() = default;
    ~InputRouter();

    InputRouter(const InputRouter&) = delete;
    InputRouter& operator=(const InputRouter&) = delete;

    // Initialize a Raw Input sink. Passing zero creates an internal hidden
    // message window. Passing an HWND uses the caller-owned window; that caller
    // must forward WM_INPUT and WM_INPUT_DEVICE_CHANGE to this instance.
    bool initialize(std::uint64_t targetHwnd = 0);

    // Register keyboards, mice, and Precision Touchpads with device-change
    // notifications. This observes physical sources; it does not suppress the
    // normal Windows input path.
    bool registerRawInputDevices(bool backgroundSink = true);

    // Re-enumerate currently connected Raw Input devices and refresh the
    // handle-to-stable-ID cache. When notifyExisting is true, online devices are
    // emitted as arrival records for an observation harness.
    bool refreshConnectedDevices(bool notifyExisting = false);
    std::vector<RawInputDeviceDescriptor> connectedDevices() const;

    void subscribeDevice(std::uintptr_t deviceHandle, InputCallback callback);
    void unsubscribeDevice(std::uintptr_t deviceHandle);
    void setGlobalCallback(InputCallback callback);
    void setDeviceChangeCallback(DeviceChangeCallback callback);

    // This flag is policy intent only. InputRouter does not claim physical
    // suppression or zero-bleed isolation.
    void setIsolationMode(bool enabled) noexcept { m_isolationMode = enabled; }
    bool isIsolationMode() const noexcept { return m_isolationMode; }

    // Best-effort legacy window-message delivery for controlled test targets.
    // It is not Raw Input virtualization and does not block native OS input.
    static bool postInputToWindow(std::uint64_t hwnd, const RawInputEvent& event);

    void processMessages();

#ifdef _WIN32
    void handleRawInput(HRAWINPUT rawInput);
    void handleDeviceChange(WPARAM change, HANDLE deviceHandle);
#endif

    void stop();

    bool isRunning() const noexcept { return m_running; }
    const std::optional<InputRouterError>& lastError() const noexcept {
        return m_lastError;
    }
    const InputRouterStatistics& statistics() const noexcept {
        return m_statistics;
    }

private:
#ifdef _WIN32
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam,
                                    LPARAM lParam);
    RawInputDeviceDescriptor describeDevice(HANDLE deviceHandle,
                                            std::uint32_t knownType = kUnknownRawInputDeviceType);
    RawInputDeviceDescriptor cachedOrDescribeDevice(HANDLE deviceHandle,
                                                    std::uint32_t knownType);
    void cacheDevice(const RawInputDeviceDescriptor& device);
    void recordError(std::wstring operation, std::uint32_t systemError);
    void recordDroppedEvent() noexcept;
    static std::uint64_t monotonicTimestampMicros() noexcept;

    HWND m_hwnd{nullptr};
    bool m_ownsWindow{false};
#endif

    bool m_running{false};
    bool m_isolationMode{false};
    std::uint64_t m_nextSequence{1};

    InputCallback m_globalCallback;
    DeviceChangeCallback m_deviceChangeCallback;
    std::unordered_map<std::uintptr_t, InputCallback> m_deviceCallbacks;
    std::unordered_map<std::uintptr_t, RawInputDeviceDescriptor> m_deviceCache;

    std::optional<InputRouterError> m_lastError;
    InputRouterStatistics m_statistics;
};

} // namespace hydra
