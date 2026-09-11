#pragma once

#ifdef _WIN32

#include "hydra/profile_schema.hpp"
#include "hydra/provider_launch_plan.hpp"

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hydra::launcher_ui {

enum class LauncherExitAction {
    Closed,
    OpenHardwareSetup,
};

enum class LauncherActivationStatus {
    Success,
    Unavailable,
    TimedOut,
    InvalidData,
    Unsupported,
    PreflightFailed,
    RecoveryRequired,
    Failed,
};

struct LauncherActivationResult {
    LauncherActivationStatus status{LauncherActivationStatus::Unavailable};
    std::wstring message;

    bool succeeded() const noexcept {
        return status == LauncherActivationStatus::Success;
    }
};

// Bounded in-process navigation state. This survives only while the management
// app switches between Games and Hardware Setup; it is not runtime authority or
// a durable game/profile record.
struct LauncherNavigationState {
    std::optional<std::string> selectedGameId;
};

using LauncherActivate = std::function<LauncherActivationResult(
    const plan::ProviderAwareLaunchPlan&)>;

// Opens the normal game-first product flow as a modal owner window. Provider
// discovery is read-only. Success is displayed only after the injected narrow
// runtime activation boundary reports Success for the exact immutable plan.
LauncherExitAction showLauncherWindow(
    HWND owner,
    profile::SeatConfigDocument seats,
    std::vector<plan::GameRuntimeRequirement> requirements,
    LauncherActivate activate = {},
    LauncherNavigationState* navigationState = nullptr);

} // namespace hydra::launcher_ui

#endif
