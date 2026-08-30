#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hydra {

using SeatId = std::uint32_t;

enum class SeatDeviceType { Display, Keyboard, Mouse, Controller, AudioOutput, AudioInput };

struct SeatConfig {
    SeatId seatId{0};
    std::wstring name;
    std::vector<std::wstring> displayIds;
    std::optional<std::wstring> primaryDisplayId;
    std::vector<std::wstring> keyboardIds;
    std::vector<std::wstring> mouseIds;
    std::vector<std::wstring> controllerIds;
    std::optional<std::wstring> audioOutputEndpointId;
    std::optional<std::wstring> audioInputEndpointId;
    // Runtime-only window association. Legacy schema v2 keeps a target_hwnd field
    // for compatibility, but persistence must write zero and loading must discard it.
    std::uint64_t targetHwnd{0};
    bool active{true};

    bool operator==(const SeatConfig&) const = default;
};

// Keep the public Phase 1 type name while callers migrate to Seat terminology.
using WorkspaceConfig = SeatConfig;

class WorkspaceManager {
public:
    SeatId createSeat(const std::wstring& name = {});
    bool removeSeat(SeatId seatId);
    bool renameSeat(SeatId seatId, const std::wstring& name);

    bool assignDisplay(SeatId seatId, const std::wstring& displayId,
                       bool makePrimary = false, bool shareable = false);
    bool unassignDisplay(SeatId seatId, const std::wstring& displayId);
    bool setPrimaryDisplay(SeatId seatId, const std::wstring& displayId);
    bool assignKeyboard(SeatId seatId, const std::wstring& deviceId,
                        bool shareable = false);
    bool unassignKeyboard(SeatId seatId, const std::wstring& deviceId);
    bool assignMouse(SeatId seatId, const std::wstring& deviceId,
                     bool shareable = false);
    bool unassignMouse(SeatId seatId, const std::wstring& deviceId);
    bool assignController(SeatId seatId, const std::wstring& controllerId,
                          bool shareable = false);
    bool unassignController(SeatId seatId, const std::wstring& controllerId);

    // Compatibility overloads for existing XInput-index callers.
    bool assignController(SeatId seatId, std::uint32_t xinputIndex,
                          bool shareable = false);
    bool unassignController(SeatId seatId, std::uint32_t xinputIndex);

    bool assignAudioOutput(SeatId seatId, const std::wstring& endpointId,
                           bool shareable = false);
    bool unassignAudioOutput(SeatId seatId);
    bool assignAudioInput(SeatId seatId, const std::wstring& endpointId,
                          bool shareable = false);
    bool unassignAudioInput(SeatId seatId);
    bool assignTargetWindow(SeatId seatId, std::uint64_t hwnd);
    bool setActive(SeatId seatId, bool active);

    // The visible whole-machine control plane belongs to one Management Seat.
    // Seat 1 is the deterministic default for backward-compatible profiles.
    bool setManagementSeatId(SeatId seatId);
    SeatId managementSeatId() const noexcept { return m_managementSeatId; }

    bool setDeviceShareable(SeatDeviceType type, const std::wstring& deviceId,
                            bool shareable);
    bool isDeviceShareable(SeatDeviceType type, const std::wstring& deviceId) const;
    std::vector<SeatId> findDeviceOwners(SeatDeviceType type,
                                         const std::wstring& deviceId) const;
    std::optional<SeatId> findDisplayOwner(const std::wstring& displayId) const;
    std::optional<SeatId> findKeyboardOwner(const std::wstring& deviceId) const;
    std::optional<SeatId> findMouseOwner(const std::wstring& deviceId) const;
    std::optional<SeatId> findControllerOwner(const std::wstring& controllerId) const;
    std::optional<SeatId> findAudioOutputOwner(const std::wstring& endpointId) const;
    std::optional<SeatId> findAudioInputOwner(const std::wstring& endpointId) const;

    const SeatConfig* getSeat(SeatId seatId) const;
    std::vector<SeatConfig> getAllSeats() const;

    bool saveToFile(const std::string& filePath = "workspace_config.json") const;
    // Loading is transactional: any parse or validation failure leaves state intact.
    bool loadFromFile(const std::string& filePath = "workspace_config.json");
    const std::string& lastError() const noexcept { return m_lastError; }

    // Compatibility wrappers for existing WorkspaceManager callers.
    SeatId createWorkspace(const std::wstring& name = {}) { return createSeat(name); }
    bool removeWorkspace(SeatId id) { return removeSeat(id); }
    const WorkspaceConfig* getWorkspace(SeatId id) const { return getSeat(id); }
    std::vector<WorkspaceConfig> getAllWorkspaces() const { return getAllSeats(); }
    SeatId findWorkspaceByKeyboardPath(const std::wstring& path) const;
    SeatId findWorkspaceByMousePath(const std::wstring& path) const;

private:
    static std::wstring normalizeId(const std::wstring& value);
    static std::wstring resourceKey(SeatDeviceType type, const std::wstring& deviceId);
    bool canAssign(SeatId seatId, SeatDeviceType type, const std::wstring& deviceId,
                   bool explicitlyShareable);
    bool assignToList(SeatId seatId, SeatDeviceType type, const std::wstring& deviceId,
                      std::vector<std::wstring> SeatConfig::*member, bool shareable);
    bool unassignFromList(SeatId seatId, SeatDeviceType type, const std::wstring& deviceId,
                          std::vector<std::wstring> SeatConfig::*member);
    void removeUnusedShareableResources();

    SeatId m_nextId{1};
    SeatId m_managementSeatId{1};
    std::unordered_map<SeatId, SeatConfig> m_seats;
    std::unordered_set<std::wstring> m_shareableResources;
    mutable std::string m_lastError;
};

} // namespace hydra
