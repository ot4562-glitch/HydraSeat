#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "hydra/controller_inventory.hpp"
#include "hydra/runtime_authority.hpp"
#include "hydra/workspace_manager.hpp"

namespace hydra {

enum class GamePlatform {
    Steam,
    Epic,
    EA,
    GOG,
    CustomExecutable
};

struct GameProfile {
    std::wstring title;
    GamePlatform platform{GamePlatform::CustomExecutable};
    std::wstring executablePath;
    std::wstring launchArguments;
    std::wstring workingDirectory;
    uint32_t appId{0}; // Steam AppID or Epic Launch ID
};

class GameLauncher {
public:
    GameLauncher();
    ~GameLauncher();

    GameLauncher(const GameLauncher&) = delete;
    GameLauncher& operator=(const GameLauncher&) = delete;
    GameLauncher(GameLauncher&&) = delete;
    GameLauncher& operator=(GameLauncher&&) = delete;

    // Launch one v1 Seat process only after controller ownership and the exact
    // process identity have been accepted by the shared runtime authority.
    bool launchGameForWorkspace(
        const GameProfile& game,
        const WorkspaceConfig& workspace,
        runtime::SessionController& sessionController,
        const controller::SeatBinding& controllerBinding,
        const controller::InventorySnapshot& inventory,
        std::wstring xinputPipeEndpoint);

    // Stop and reap the launcher-owned process for one Seat without touching
    // the other Seat, then end the matching runtime activation.
    bool stopWorkspaceGame(uint32_t workspaceId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hydra
