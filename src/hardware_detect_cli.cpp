#include "hydra/hardware_detector.hpp"

#include <iostream>
#include <string_view>
#include <vector>

namespace {

bool printCategory(
    hydra::HardwareDetector& detector,
    std::wstring_view title,
    const std::vector<hydra::DeviceInfo>& devices) {
    std::wcout << L"[" << title << L": " << devices.size() << L"]\n";
    for (const auto& device : devices) {
        std::wcout << L"  Name: " << device.name << L"\n"
                   << L"  ID: " << device.id << L"\n"
                   << L"  Path: " << (device.devicePath.empty() ? L"<unavailable>" : device.devicePath) << L"\n"
                   << L"  Native/index: " << device.nativeHandle << L"\n";
        if (device.type == hydra::DeviceType::Display) {
            std::wcout << L"  Likely virtual (heuristic): "
                       << (device.isLikelyVirtual ? L"yes" : L"no") << L"\n";
        }
        std::wcout << L"\n";
    }
    if (const auto& error = detector.lastError()) {
        std::wcerr << L"Detection failure in " << error->operation
                   << L" (Win32 error " << error->systemError << L")\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    hydra::HardwareDetector detector;
    bool success = true;

    const auto displays = detector.detectDisplays();
    success = printCategory(detector, L"Displays", displays) && success;
    const auto keyboards = detector.detectKeyboards();
    success = printCategory(detector, L"Keyboards", keyboards) && success;
    const auto mice = detector.detectMice();
    success = printCategory(detector, L"Mice / touchpads", mice) && success;
    const auto controllers = detector.detectControllers();
    success = printCategory(detector, L"Controllers", controllers) && success;

    return success ? 0 : 1;
}
