#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace hydra {

enum class DeviceType {
    Display,
    Keyboard,
    Mouse,
    Controller
};

struct DeviceInfo {
    std::wstring id;
    std::wstring name;
    std::wstring devicePath;
    DeviceType type;
    uintptr_t nativeHandle{0};
    bool isLikelyVirtual{false};
};

struct DetectionError {
    std::wstring operation;
    uint32_t systemError{0};
};

class HardwareDetector {
public:
    HardwareDetector() = default;
    ~HardwareDetector() = default;

    // Detect all connected displays (Physical & Virtual)
    std::vector<DeviceInfo> detectDisplays();

    // Detect all physical keyboards separately
    std::vector<DeviceInfo> detectKeyboards();

    // Detect all physical mice / touchpads separately
    std::vector<DeviceInfo> detectMice();

    // Detect all connected gamepads/controllers
    std::vector<DeviceInfo> detectControllers();

    // Print summary of all detected hardware
    void printReport();

    // Error from the most recent category query, if enumeration itself failed.
    const std::optional<DetectionError>& lastError() const noexcept { return m_lastError; }

private:
    void beginQuery() noexcept { m_lastError.reset(); }
    void recordError(std::wstring operation, uint32_t systemError);

    std::optional<DetectionError> m_lastError;
};

} // namespace hydra
