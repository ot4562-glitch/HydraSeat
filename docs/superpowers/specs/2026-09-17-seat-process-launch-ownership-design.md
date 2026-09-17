# Seat Process Launch Ownership Design

Date: 2026-09-17

## Goal

Make production `GameLauncher` participate in HydraSeat's single runtime-authority model instead of calling `CreateProcessW()` and immediately discarding the process handle. A launched game must become exact Seat-owned runtime state before its primary thread is allowed to run.

## Constraints

- `hydra::runtime::SessionController` remains the sole Seat runtime authority.
- `GameLauncher` must not create or mirror a second `SessionController`.
- At most Seat 1 and Seat 2 are supported.
- Seat A launch/stop/failure must not mutate Seat B.
- Controller binding must be validated against the current authoritative inventory before process launch.
- XInput adapter configuration is child-process-local through `HYDRA_XINPUT_*` environment variables.
- No automatic DLL injection, IAT patching, proxy replacement of system XInput DLLs, remote threads, HidHide, anti-cheat/DRM/protected-process bypass, or audio changes.
- Win32 handles are RAII-owned and all failure paths roll back the Seat activation.

## Public launch contract

Keep `GameLauncher` as the product-facing launch owner. Replace the unsafe two-argument launch call with a call that receives the existing runtime authority and the already-resolved transient controller binding/inventory:

```cpp
bool launchGameForWorkspace(
    const GameProfile& game,
    const WorkspaceConfig& workspace,
    runtime::SessionController& sessionController,
    const controller::SeatBinding& controllerBinding,
    const controller::InventorySnapshot& inventory,
    std::wstring xinputPipeEndpoint);
```

`WorkspaceConfig.workspaceId` is the v1 Seat identifier and must be 1 or 2. `GameLauncher` stores only transient launch ownership; persisted configuration remains unchanged.

## Launch sequence

For a free Seat slot:

1. Validate Seat ID, executable path, pipe endpoint, and source generation.
2. `beginSeatActivation(seatId)`.
3. `bindController(token, binding, inventory)`.
4. Ask `SessionController` for the immutable `VirtualXInputMapping`.
5. Build a Unicode child environment derived from the parent environment plus:
   - `HYDRA_XINPUT_PIPE`
   - `HYDRA_XINPUT_SEAT_ID`
   - `HYDRA_XINPUT_ACTIVATION_GENERATION`
   - `HYDRA_XINPUT_SOURCE_GENERATION`
6. Call `CreateProcessW` with `CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT` (and preserve the existing new-console behavior).
7. Read the exact process creation time from the retained process handle and construct `runtime::ProcessIdentity`.
8. `publishProcess(token, identity)`.
9. Resume the primary thread only after publication succeeds.
10. Close the thread handle and transfer the exact process handle + activation token + authority reference into the launcher's Seat session.

The process must never execute user code before its exact identity is accepted by `SessionController`.

## Failure rollback

Any failure after activation begins performs reverse cleanup:

- if a process exists, terminate the still-suspended/failed child and wait boundedly;
- close all created handles;
- call `endSeatActivation(token)`;
- leave no launcher session for the Seat.

A duplicate launch for a Seat with a live launcher-owned session fails closed and does not replace the existing session.

## Stop and destruction

`stopWorkspaceGame(seatId)` owns teardown for launcher-created processes. For this PR it is intentionally minimal and deterministic: if the exact process is still active, terminate it, wait boundedly, close the handle, then end the matching activation token. Already-exited children are simply reaped before ending the activation.

`GameLauncher` destruction invokes the same cleanup for Seat 1 and Seat 2 so a launcher-owned process handle is never abandoned. Graceful WM_CLOSE/provider-specific shutdown is a later product layer, not part of this ownership PR.

## Internal representation

Use a private `GameLauncher::Impl` so Win32 HANDLE types and transient session details do not leak into the public header. The implementation holds at most two optional sessions. Each session contains:

- Seat ID;
- exact `runtime::ProcessIdentity`;
- `runtime::ActivationToken`;
- retained process HANDLE;
- non-owning pointer to the external `SessionController` that outlives the launch session.

No new manager/registry/factory abstraction is introduced.

## Test strategy

Add a Windows-only controlled child executable and integration CTest.

The child loads `HYDRA_XINPUT_*` using the existing `xinput_adapter_session` parser, validates expected Seat/source/pipe values, then remains alive. The parent test:

1. creates deterministic authoritative inventories and bindings for Seat 1 and Seat 2;
2. launches Seat 1 and proves the runtime snapshot contains a live exact PID + matching creation time;
3. proves a duplicate Seat 1 launch is rejected without replacing the first process;
4. launches Seat 2 concurrently;
5. stops Seat 1 and proves Seat 2 remains active/alive;
6. stops Seat 2;
7. attempts a launch with a missing executable and proves activation rollback leaves Seat 1 inactive.

Existing controller/XInput process tests must remain green.

## Non-goals

- process-tree/job-object ownership;
- launcher handoff tracking;
- automatic adapter injection/interposition;
- real-game compatibility claims;
- graceful provider-specific game shutdown;
- audio integration.
