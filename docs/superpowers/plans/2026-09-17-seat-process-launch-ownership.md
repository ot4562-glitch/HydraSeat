# Seat Process Launch Ownership Implementation Plan

Date: 2026-09-17

## Objective

Replace the unsafe `CreateProcessW` + immediate handle close path in production `GameLauncher` with a fail-closed Seat-owned launch lifecycle tied to the existing `runtime::SessionController`.

## Task 1 — RED: controlled process ownership test

Files:
- `tests/game_launcher_child.cpp`
- `tests/test_game_launcher_process.cpp`
- `CMakeLists.txt`

Steps:
1. Add a Windows-only child executable that loads the existing `HYDRA_XINPUT_*` session environment, validates expected pipe/Seat/source arguments, and remains alive on success.
2. Add a Windows-only integration test executable that creates deterministic inventories/bindings for Seat 1 and Seat 2 and calls the desired new `GameLauncher` launch API.
3. Assert exact runtime process identity using PID + `GetProcessTimes` creation time.
4. Assert duplicate same-Seat launch rejection, concurrent Seat 2 survival when Seat 1 stops, and missing-executable activation rollback.
5. Register the test with CTest.
6. Build the new test target and confirm it fails because the safe launch API/behavior does not exist yet.

## Task 2 — GREEN: production launch ownership

Files:
- `include/hydra/game_launcher.hpp`
- `src/game_launcher.cpp`

Steps:
1. Change `GameLauncher::launchGameForWorkspace` to require the external `SessionController`, transient `SeatBinding`, current `InventorySnapshot`, and XInput pipe endpoint.
2. Add private `Impl` storage for at most two launcher-owned process sessions.
3. Build a bounded Unicode child environment preserving the parent environment and overriding only the four `HYDRA_XINPUT_*` keys.
4. Begin Seat activation and bind the transient controller before process creation.
5. Create the child suspended.
6. Read exact creation time from the retained process handle and publish `ProcessIdentity` before resume.
7. Transfer only the process handle into the Seat session after successful `ResumeThread`.
8. On every failure, terminate any created child, close handles, and end the activation.
9. Implement `stopWorkspaceGame` and destructor cleanup without touching the other Seat.
10. Build/run the focused integration test until green.

## Task 3 — Regression verification

1. Build Windows MSVC x64 Release targets:
   - `hydra_tests`
   - `game_launcher_child`
   - `game_launcher_process_test`
   - `xinput_probe_game`
   - `xinput_process_isolation`
   - `hydra_xinput_adapter`
   - `xinput_adapter_exports`
   - `xinput_abi_probe_game`
   - `xinput_adapter_process_isolation`
2. Run full Windows Release CTest and require all tests to pass.
3. Run `git diff --check`.
4. Inspect diff to confirm no audio files and no automatic injection/interposition code changed.

## Task 4 — Publish as Draft PR

1. Commit only #19 design/plan/tests/implementation.
2. Push `collab/seat-process-launch-ownership` to the user fork.
3. Open a Draft PR to `Pranshu45883/HydraSeat` depending on #18.
4. State clearly that the PR proves controlled process ownership/environment propagation only and does not claim real-game injection compatibility.
