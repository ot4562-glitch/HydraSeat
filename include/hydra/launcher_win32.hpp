#pragma once

#ifdef _WIN32

#include "hydra/profile_schema.hpp"
#include "hydra/provider_launch_plan.hpp"

#include <windows.h>

#include <vector>

namespace hydra::launcher_ui {

// Opens the normal game-first product flow as a modal owner window. Provider
// discovery is read-only; no launch occurs unless the model produces a valid
// immutable plan from explicit local requirement evidence.
void showLauncherWindow(
    HWND owner,
    profile::SeatConfigDocument seats,
    std::vector<plan::GameRuntimeRequirement> requirements);

} // namespace hydra::launcher_ui

#endif
