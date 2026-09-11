#include "hydra/hardware_detector.hpp"

#include "hydra/hardware_identity.hpp"
#include "hydra/hid_usage.hpp"
#include "hydra/raw_input_utils.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#endif

namespace hydra {

void HardwareDetector::recordError(std::wstring operation, uint32_t systemError) {
    m_lastError = DetectionError{std::move(operation), systemError};
}

#ifdef _WIN32
namespace {

std::wstring displayIdentity(const MONITORINFOEXW& monitorInfo, const DISPLAY_DEVICEW* monitor) {
    if (monitor != nullptr && monitor->DeviceID[0] != L'\0') {
        const auto identity = win32::resolveDeviceInterfaceIdentity(monitor->DeviceID);
        const std::wstring_view instanceId = identity.deviceInstanceId
                                                 ? *identity.deviceInstanceId
                                                 : std::wstring_view{};
        return hardware::selectInterfaceDeviceIdentity(instanceId, identity.interfacePath);
    }
    return hardware::normalizeDevicePath(monitorInfo.szDevice);
}

bool isLikelyVirtualDisplay(const MONITORINFOEXW& monitorInfo, const DISPLAY_DEVICEW* monitor) {
    std::wstring evidence = monitorInfo.szDevice;
    if (monitor != nullptr) {
        evidence.push_back(L' ');
        evidence.append(monitor->DeviceString);
        evidence.push_back(L' ');
        evidence.append(monitor->DeviceID);
    }
    return hardware::isLikelyVirtualDisplayIdentity(evidence);
}

struct DisplayEnumerationContext {
    std::vector<DeviceInfo>* devices{nullptr};
    std::unordered_set<std::wstring>* seen{nullptr};
};

BOOL CALLBACK collectMonitor(HMONITOR monitorHandle, HDC, LPRECT, LPARAM userData) {
    auto* context = reinterpret_cast<DisplayEnumerationContext*>(userData);
    if (context == nullptr || context->devices == nullptr || context->seen == nullptr) {
        return FALSE;
    }

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitorHandle, &monitorInfo)) {
        return TRUE;
    }

    DISPLAY_DEVICEW monitor{};
    monitor.cb = sizeof(monitor);
    const DISPLAY_DEVICEW* monitorDevice = nullptr;
    if (EnumDisplayDevicesW(
            monitorInfo.szDevice, 0, &monitor, EDD_GET_DEVICE_INTERFACE_NAME)) {
        monitorDevice = &monitor;
    }

    const auto identity = displayIdentity(monitorInfo, monitorDevice);
    if (identity.empty() || !context->seen->insert(identity).second) {
        return TRUE;
    }

    DeviceInfo info;
    info.id = L"Display:" + identity;
    info.name = monitorDevice != nullptr && monitorDevice->DeviceString[0] != L'\0'
                    ? monitorDevice->DeviceString
                    : monitorInfo.szDevice;
    info.devicePath = monitorDevice != nullptr && monitorDevice->DeviceID[0] != L'\0'
                          ? monitorDevice->DeviceID
                          : monitorInfo.szDevice;
    info.type = DeviceType::Display;
    info.nativeHandle = reinterpret_cast<uintptr_t>(monitorHandle);
    info.isLikelyVirtual = isLikelyVirtualDisplay(monitorInfo, monitorDevice);
    context->devices->push_back(std::move(info));
    return TRUE;
}

std::wstring inputName(DeviceType type, bool special, std::size_t ordinal) {
    switch (type) {
    case DeviceType::Keyboard:
        if (special) {
            return L"Laptop Internal Keyboard";
        }
        return L"Keyboard #" + std::to_wstring(ordinal);
    case DeviceType::Mouse:
        if (special) {
            return L"Touchpad #" + std::to_wstring(ordinal);
        }
        return L"Mouse #" + std::to_wstring(ordinal);
    default:
        return {};
    }
}

struct PhysicalInputAggregate {
    DeviceInfo device;
    bool special{false};
};

using PhysicalInputMap = std::map<std::wstring, PhysicalInputAggregate>;

std::wstring_view optionalView(const std::optional<std::wstring>& value) {
    return value ? std::wstring_view(*value) : std::wstring_view{};
}

bool isRemoteOrSynthetic(const win32::DeviceInterfaceIdentity& identity) {
    return hardware::isObviousRemoteOrSyntheticInputIdentity(
               identity.interfacePath,
               optionalView(identity.deviceInstanceId),
               optionalView(identity.parentDeviceInstanceId)) ||
           hardware::isObviousRemoteOrSyntheticInputPath(
               optionalView(identity.physicalAncestorInstanceId));
}

bool preferRepresentative(std::wstring_view candidatePath,
                          uintptr_t candidateHandle,
                          const DeviceInfo& current) {
    const auto candidate = hardware::normalizeDevicePath(candidatePath);
    const auto existing = hardware::normalizeDevicePath(current.devicePath);
    if (candidate != existing) {
        return hardware::isPreferredRepresentativePath(candidatePath, current.devicePath);
    }
    return candidateHandle < current.nativeHandle;
}

void addPhysicalInputCandidate(PhysicalInputMap& aggregate,
                               std::wstring stableId,
                               std::wstring_view path,
                               uintptr_t nativeHandle,
                               DeviceType type,
                               bool special) {
    auto [position, inserted] = aggregate.try_emplace(stableId);
    auto& entry = position->second;
    if (inserted) {
        entry.device.id = std::move(stableId);
        entry.device.devicePath = path;
        entry.device.type = type;
        entry.device.nativeHandle = nativeHandle;
        entry.special = special;
        return;
    }

    entry.special = entry.special || special;
    if (preferRepresentative(path, nativeHandle, entry.device)) {
        entry.device.devicePath = path;
        entry.device.nativeHandle = nativeHandle;
    }
}

void sortByIdentity(std::vector<DeviceInfo>& devices) {
    std::sort(devices.begin(), devices.end(), [](const DeviceInfo& left, const DeviceInfo& right) {
        return left.id < right.id;
    });
}

} // namespace
#endif

std::vector<DeviceInfo> HardwareDetector::detectDisplays() {
    beginQuery();
    std::vector<DeviceInfo> result;

#ifdef _WIN32
    std::unordered_set<std::wstring> seen;
    DisplayEnumerationContext context{&result, &seen};
    if (!EnumDisplayMonitors(
            nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&context))) {
        recordError(L"EnumDisplayMonitors", GetLastError());
        return result;
    }
    sortByIdentity(result);
#endif

    return result;
}

std::vector<DeviceInfo> HardwareDetector::detectKeyboards() {
    beginQuery();
    std::vector<DeviceInfo> result;

#ifdef _WIN32
    const auto rawDevices = win32::enumerateRawInputDevices();
    if (!rawDevices) {
        recordError(L"GetRawInputDeviceList", rawDevices.error);
        return result;
    }

    PhysicalInputMap aggregate;
    for (const auto& device : rawDevices.devices) {
        if (device.dwType != RIM_TYPEKEYBOARD) {
            continue;
        }
        const auto path = win32::rawInputDeviceName(device.hDevice);
        if (!path) {
            continue;
        }
        const auto identity = win32::resolveDeviceInterfaceIdentity(*path);
        if (isRemoteOrSynthetic(identity)) {
            continue;
        }
        auto stableId = win32::makeStableRawInputDeviceId(L"Keyboard", identity);
        if (stableId.empty()) {
            continue;
        }

        const bool internal = hardware::isLikelyInternalKeyboardPath(*path) ||
                              hardware::isLikelyInternalKeyboardPath(
                                  optionalView(identity.deviceInstanceId)) ||
                              hardware::isLikelyInternalKeyboardPath(
                                  optionalView(identity.parentDeviceInstanceId));
        addPhysicalInputCandidate(
            aggregate, std::move(stableId), *path,
            reinterpret_cast<uintptr_t>(device.hDevice), DeviceType::Keyboard, internal);
    }

    std::size_t externalOrdinal = 0;
    for (auto& [stableId, entry] : aggregate) {
        (void)stableId;
        entry.device.name = inputName(
            DeviceType::Keyboard, entry.special, entry.special ? 0 : ++externalOrdinal);
        result.push_back(std::move(entry.device));
    }
#endif

    return result;
}

std::vector<DeviceInfo> HardwareDetector::detectMice() {
    beginQuery();
    std::vector<DeviceInfo> result;

#ifdef _WIN32
    const auto rawDevices = win32::enumerateRawInputDevices();
    if (!rawDevices) {
        recordError(L"GetRawInputDeviceList", rawDevices.error);
        return result;
    }

    PhysicalInputMap aggregate;
    for (const auto& device : rawDevices.devices) {
        bool isMouse = device.dwType == RIM_TYPEMOUSE;
        bool isTouchpad = false;
        bool hasUsage = false;
        if (device.dwType == RIM_TYPEHID) {
            const auto details = win32::rawInputDeviceInfo(device.hDevice);
            if (details && details->dwType == RIM_TYPEHID) {
                hasUsage = true;
                const auto kind = hid::classifyCollection(
                    details->hid.usUsagePage, details->hid.usUsage);
                isMouse = kind == hid::CollectionKind::Mouse ||
                          kind == hid::CollectionKind::Touchpad;
                isTouchpad = kind == hid::CollectionKind::Touchpad;
            }
        }
        if (!isMouse) {
            continue;
        }

        const auto path = win32::rawInputDeviceName(device.hDevice);
        if (!path) {
            continue;
        }
        const auto identity = win32::resolveDeviceInterfaceIdentity(*path);
        if (isRemoteOrSynthetic(identity)) {
            continue;
        }
        if (!hasUsage) {
            isTouchpad = hardware::isLikelyTouchpadPath(*path) ||
                         hardware::isLikelyTouchpadPath(optionalView(identity.deviceInstanceId)) ||
                         hardware::isLikelyTouchpadPath(optionalView(identity.parentDeviceInstanceId));
        }

        auto stableId = win32::makeStableRawInputDeviceId(L"Mouse", identity);
        if (stableId.empty()) {
            continue;
        }

        addPhysicalInputCandidate(
            aggregate, std::move(stableId), *path,
            reinterpret_cast<uintptr_t>(device.hDevice), DeviceType::Mouse, isTouchpad);
    }

    std::size_t mouseOrdinal = 0;
    std::size_t touchpadOrdinal = 0;
    for (auto& [stableId, entry] : aggregate) {
        (void)stableId;
        entry.device.name = inputName(
            DeviceType::Mouse, entry.special,
            entry.special ? ++touchpadOrdinal : ++mouseOrdinal);
        result.push_back(std::move(entry.device));
    }
#endif

    return result;
}

std::vector<DeviceInfo> HardwareDetector::detectControllers() {
    beginQuery();
    std::vector<DeviceInfo> result;

#ifdef _WIN32
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE state{};
        if (XInputGetState(index, &state) == ERROR_SUCCESS) {
            DeviceInfo info;
            info.id = L"Controller:XInput:" + std::to_wstring(index);
            info.name = L"XInput Controller #" + std::to_wstring(index + 1);
            info.type = DeviceType::Controller;
            info.nativeHandle = index;
            result.push_back(std::move(info));
        }
    }

    const auto rawDevices = win32::enumerateRawInputDevices();
    if (!rawDevices) {
        recordError(L"GetRawInputDeviceList", rawDevices.error);
        return result;
    }

    std::unordered_set<std::wstring> seen;
    for (const auto& device : rawDevices.devices) {
        if (device.dwType != RIM_TYPEHID) {
            continue;
        }
        const auto details = win32::rawInputDeviceInfo(device.hDevice);
        if (!details || details->dwType != RIM_TYPEHID) {
            continue;
        }
        const auto kind = hid::classifyCollection(
            details->hid.usUsagePage, details->hid.usUsage);
        if (kind != hid::CollectionKind::Joystick && kind != hid::CollectionKind::Gamepad) {
            continue;
        }

        const auto path = win32::rawInputDeviceName(device.hDevice);
        if (!path || hardware::isObviousRemoteOrSyntheticInputPath(*path) ||
            hardware::isXInputShadowPath(*path)) {
            continue;
        }
        const auto stableId = win32::makeStableRawInputDeviceId(L"Controller:HID", *path);
        if (stableId.empty() || !seen.insert(stableId).second) {
            continue;
        }

        DeviceInfo info;
        info.id = stableId;
        info.name = kind == hid::CollectionKind::Gamepad
                        ? L"Generic HID Gamepad"
                        : L"Generic HID Joystick";
        info.devicePath = *path;
        info.type = DeviceType::Controller;
        info.nativeHandle = reinterpret_cast<uintptr_t>(device.hDevice);
        result.push_back(std::move(info));
    }
    sortByIdentity(result);
#endif

    return result;
}

void HardwareDetector::printReport() {
    const auto printCategory = [this](std::wstring_view title, const auto& query) {
        const auto devices = query();
        std::wcout << L"[" << title << L": " << devices.size() << L"]\n";
        for (const auto& device : devices) {
            std::wcout << L"  - " << device.name << L"\n"
                       << L"    ID: " << device.id << L"\n"
                       << L"    Native: " << device.nativeHandle << L"\n";
            if (!device.devicePath.empty()) {
                std::wcout << L"    Path: " << device.devicePath << L"\n";
            }
            if (device.type == DeviceType::Display) {
                std::wcout << L"    Likely virtual: "
                           << (device.isLikelyVirtual ? L"yes" : L"no") << L"\n";
            }
        }
        if (m_lastError) {
            std::wcout << L"  Query failed: " << m_lastError->operation
                       << L" (Win32 error " << m_lastError->systemError << L")\n";
        }
        std::wcout << L"\n";
    };

    std::wcout << L"HydraSeat Hardware Report\n=========================\n\n";
    printCategory(L"Displays", [this] { return detectDisplays(); });
    printCategory(L"Keyboards", [this] { return detectKeyboards(); });
    printCategory(L"Mice / touchpads", [this] { return detectMice(); });
    printCategory(L"Controllers", [this] { return detectControllers(); });
}

} // namespace hydra
