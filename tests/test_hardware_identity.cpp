#include "hydra/hardware_identity.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using hydra::hardware::makeStableDeviceId;
    using hydra::hardware::normalizeDevicePath;
    using hydra::hardware::selectInterfaceDeviceIdentity;
    using hydra::hardware::selectPhysicalIdentity;
    using namespace std::string_view_literals;

    const std::wstring withTrailingNulls(
        L"//?/hid#vid_046d&pid_c31c&col01#7&abc&0&0000\0\0"sv);
    check(normalizeDevicePath(withTrailingNulls) ==
              L"\\\\?\\HID#VID_046D&PID_C31C&COL01#7&ABC&0&0000",
          "normalization removes trailing NULs and canonicalizes ASCII path syntax");
    check(normalizeDevicePath(L"\\??\\hid#vid_046d&pid_c31c#instance") ==
              L"\\\\?\\HID#VID_046D&PID_C31C#INSTANCE",
          "equivalent NT path prefixes normalize to the Raw Input spelling");
    check(normalizeDevicePath(L"").empty(), "an empty path stays empty");

    constexpr std::wstring_view firstInterface =
        L"\\\\?\\HID#VID_046D&PID_C31C&MI_00&COL01#7&111111&0&0000#{GUID}";
    constexpr std::wstring_view siblingInterface =
        L"\\\\?\\HID#VID_046D&PID_C31C&MI_00&COL02#7&111111&0&0001#{GUID}";
    constexpr std::wstring_view otherPhysicalUnit =
        L"\\\\?\\HID#VID_046D&PID_C31C&MI_00&COL01#8&222222&0&0000#{GUID}";

    constexpr std::wstring_view firstDeviceInstance =
        L"HID\\VID_046D&PID_C31C&MI_00&COL01\\7&111111&0&0000";
    constexpr std::wstring_view siblingDeviceInstance =
        L"HID\\VID_046D&PID_C31C&MI_00&COL02\\7&111111&0&0001";
    constexpr std::wstring_view firstParent =
        L"USB\\VID_046D&PID_C31C&MI_00\\7&111111&0&0000";
    constexpr std::wstring_view otherParent =
        L"USB\\VID_046D&PID_C31C&MI_00\\8&222222&0&0000";

    check(selectPhysicalIdentity(firstParent, firstDeviceInstance, firstInterface) ==
              L"USB\\VID_046D&PID_C31C&MI_00\\7&111111&0&0000",
          "resolved parent instance ID is the preferred physical identity");
    check(selectPhysicalIdentity(firstParent, siblingDeviceInstance, siblingInterface) ==
              selectPhysicalIdentity(firstParent, firstDeviceInstance, firstInterface),
          "different HID collections with the same resolved parent deduplicate");
    check(selectPhysicalIdentity(firstParent, firstDeviceInstance, firstInterface) !=
              selectPhysicalIdentity(otherParent, firstDeviceInstance, otherPhysicalUnit),
          "identical VID/PID units with different resolved parents remain distinct");

    check(selectPhysicalIdentity(L"", firstDeviceInstance, firstInterface) ==
              L"HID\\VID_046D&PID_C31C&MI_00&COL01\\7&111111&0&0000",
          "device instance ID is used when parent resolution is unavailable");
    check(selectPhysicalIdentity(L"", L"", firstInterface) ==
              normalizeDevicePath(firstInterface),
          "full interface path is the conservative final fallback");
    check(selectPhysicalIdentity(L"", L"", firstInterface) !=
              selectPhysicalIdentity(L"", L"", siblingInterface),
          "fallback does not guess that distinct symbolic paths are one device");

    check(selectInterfaceDeviceIdentity(firstDeviceInstance, firstInterface) ==
              L"HID\\VID_046D&PID_C31C&MI_00&COL01\\7&111111&0&0000",
          "interface resolution prefers its device instance over its symbolic path");
    check(selectInterfaceDeviceIdentity(L"", firstInterface) ==
              normalizeDevicePath(firstInterface),
          "an unresolved interface keeps the complete symbolic path as fallback");

    check(makeStableDeviceId(L"Mouse", firstParent, firstDeviceInstance, firstInterface) !=
              makeStableDeviceId(L"Keyboard", firstParent, firstDeviceInstance, firstInterface),
          "different input categories do not suppress each other even with one parent");
    check(makeStableDeviceId(L"Keyboard", L"", L"", L"").empty(),
          "missing identity data cannot produce a stable ID");

    check(hydra::hardware::isObviousRemoteOrSyntheticInputPath(
              L"\\\\?\\ROOT#RDP_MOU#0000"),
          "obvious remote input paths are filtered");
    check(!hydra::hardware::isObviousRemoteOrSyntheticInputPath(firstInterface),
          "ordinary local HID paths are retained");
    check(hydra::hardware::isLikelyInternalKeyboardPath(
              L"\\\\?\\ACPI#PNP0303#3&ABC&0"),
          "internal keyboard paths are recognized only as a label heuristic");
    check(hydra::hardware::isLikelyTouchpadPath(
              L"\\\\?\\HID#ELAN&PNP0C50#INSTANCE"),
          "touchpad path heuristic remains a fallback classifier");
    check(hydra::hardware::isXInputShadowPath(
              L"\\\\?\\HID#VID_045E&PID_028E&IG_00#INSTANCE"),
          "XInput shadow HID paths are recognized");
    check(hydra::hardware::isLikelyVirtualDisplayIdentity(
              L"Example Indirect Display Adapter"),
          "virtual-display classification remains explicitly heuristic");
    check(!hydra::hardware::isLikelyVirtualDisplayIdentity(
              L"DISPLAY\\DEL40A9\\5&12345&0&UID4352"),
          "ordinary monitor instance IDs are not marked virtual");

    std::cout << "Hardware identity tests passed.\n";
    return EXIT_SUCCESS;
}
