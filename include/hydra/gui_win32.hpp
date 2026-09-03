#pragma once

#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include "hydra/hardware_detector.hpp"
#include "hydra/host_transport.hpp"
#include "hydra/control_surface_model.hpp"
#include "hydra/session_control_transition.hpp"
#include "hydra/display_topology.hpp"
#include "hydra/seat_display_layout.hpp"
#include "hydra/management_seat.hpp"
#include "hydra/workspace_manager.hpp"
#include "hydra/input_router.hpp"
#include "hydra/input_observation.hpp"
#include "hydra/launcher_win32.hpp"

namespace hydra {
namespace gui {

enum class PartitionOwner {
    Pool = 0,
    Player1 = 1,
    Player2 = 2
};

enum class DeviceCategory {
    Display,
    Keyboard,
    Mouse,
    Touchpad,
    Gamepad
};

struct VisualDeviceTile {
    HWND hwndControl{nullptr};
    std::wstring name;
    std::wstring displayLabel; // e.g. "1.1", "1.2", "KBD 1", "MOU 1"
    DeviceCategory type{DeviceCategory::Keyboard};
    uintptr_t nativeHandle{0};
    std::wstring deviceId;
    std::wstring devicePath;
    PartitionOwner owner{PartitionOwner::Pool};
    uint64_t flashUntil{0};
    bool isPrimaryDisplay{false};
    bool isSelected{false};
    bool pointerPressed{false};
    bool isDragging{false};
    POINT dragStartScreen{};
};

class Win32App {
public:
    Win32App();
    ~Win32App();

    bool initialize(HINSTANCE hInstance, int nCmdShow);
    int run();

    void triggerDeviceFlash(uintptr_t handle, const std::wstring& devPath, uint32_t rawDevType, bool isTouchpad);
    void dropTileAtScreenPos(VisualDeviceTile* tile, POINT screenPt);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK DeviceTileProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void setupUI();
    void refreshHardware(bool preserveIdentification = false);
    void layoutDeviceTiles();
    void layoutHardwareSetup();
    void selectDeviceTile(VisualDeviceTile* tile);
    void assignSelectedDevice(PartitionOwner owner);
    bool assignTileToOwner(VisualDeviceTile* tile, PartitionOwner owner);
    void updateDeviceAssignmentUi();
    void beginInputIdentification(InputIdentificationKind kind);
    void observeIdentificationInput(const RawInputEvent& event);
    void saveWorkspaceProfile();
    void loadWorkspaceProfile();
    void applyWorkspaceProfileToTiles(const WorkspaceManager* source = nullptr);
    bool captureWorkspaceFromTiles(WorkspaceManager& candidate) const;
    void toggleIsolationMode();
    void launchMultiseat();
    void launchGateCControlledLab();
    bool initializeControlSurface();
    bool connectBackgroundHost(SeatId requestedSeat, bool allowBootstrap,
                               std::string& error);
    bool bootstrapBackgroundHost(std::string& error) const;
    void refreshControlSurface();
    void updateControlSurfaceUi();
    void applyManagementSeatPlacement();
    bool configurationEditingAllowed() const noexcept;
    void stopSessionAndReturnToWindows();
    void beginReconfigure();
    launcher_ui::LauncherExitAction openGameLibrary();
    launcher_ui::LauncherActivationResult activateLauncherPlan(
        const plan::ProviderAwareLaunchPlan& plan,
        const hostipc::ProfilePayload& profile);
    bool applyCurrentProfileToHost(bool showErrors);

    HWND m_hwnd{nullptr};
    HWND m_poolGroup{nullptr};
    HWND m_p1Group{nullptr};
    HWND m_p2Group{nullptr};

    HWND m_saveProfileBtn{nullptr};
    HWND m_loadProfileBtn{nullptr};
    HWND m_refreshBtn{nullptr};
    HWND m_isolationBtn{nullptr};
    HWND m_launchBtn{nullptr};
    HWND m_gateCBtn{nullptr};
    HWND m_headerLabel{nullptr};
    HWND m_deviceStatusLabel{nullptr};
    HWND m_runtimeStatusLabel{nullptr};
    HWND m_selectedDeviceLabel{nullptr};
    HWND m_assignSeat1Btn{nullptr};
    HWND m_assignSeat2Btn{nullptr};
    HWND m_unassignDeviceBtn{nullptr};
    HWND m_identifyKeyboardBtn{nullptr};
    HWND m_identifyMouseBtn{nullptr};
    HWND m_stopSessionBtn{nullptr};
    HWND m_reconfigureBtn{nullptr};
    HWND m_gameLibraryBtn{nullptr};

    VisualDeviceTile* m_selectedDeviceTile{nullptr};
    std::vector<std::unique_ptr<VisualDeviceTile>> m_deviceTiles;
    std::unordered_map<uintptr_t, size_t> m_handleToTileIndex;

    HardwareDetector m_hardwareDetector;
    WorkspaceManager m_workspaceManager;
    InputRouter m_inputRouter;
    InputIdentificationCapture m_identificationCapture;
    InputIdentificationKind m_identificationKind{InputIdentificationKind::Keyboard};
    std::uint64_t m_lastInputSequence{0};
    hostipc::HostControlClient m_hostClient;
    control::ControlSurfaceModel m_controlSurfaceModel;
    control::SessionControlTransition m_sessionControlTransition;
    HANDLE m_singleInstanceMutex{nullptr};
    bool m_duplicateLaunch{false};
    bool m_profileOutOfSync{false};
    launcher_ui::LauncherNavigationState m_launcherNavigationState;

    std::vector<DeviceInfo> m_displays;
    std::vector<DeviceInfo> m_keyboards;
    std::vector<DeviceInfo> m_mice;
    std::vector<DeviceInfo> m_controllers;
};

} // namespace gui
} // namespace hydra
#endif
