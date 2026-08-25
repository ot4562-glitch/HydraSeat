# Phase 3 Input Isolation Design

## Status

Phase 3 is a **feasibility and compatibility phase**. HydraSeat can currently identify physical input devices and associate them with Seats. It cannot yet guarantee that the normal Windows input path is hidden from non-owning games, nor can it make every game observe an independent foreground window, keyboard state and cursor.

This design deliberately distinguishes:

- **observation** — HydraSeat knows which device produced an event;
- **routing** — HydraSeat selects the owning Seat and target process/window;
- **state virtualization** — a target process observes Seat-local keyboard/mouse/controller/focus state;
- **physical suppression** — non-owning applications cannot read the original device;
- **verified isolation** — measured zero cross-seat bleed for a defined game/profile pair.

Only the last state satisfies the Phase 3 exit gate.

## Goals

1. Route each physical keyboard, mouse and controller to its owning Seat.
2. Support two different games with different input API requirements.
3. Select interventions per game instead of enabling a monolithic global hook set.
4. Fail closed: if required capabilities are absent, report `Unsupported` instead of silently leaking input.
5. Keep every risky operation reversible and observable.
6. Deny process injection for anti-cheat/protected games by default.
7. Make recovery possible even when assigned keyboards/mice are hidden or clipped.
8. Preserve a clean architecture so a ProtoInput adapter, a clean-room hook runtime, HidHide, or a future session backend can coexist.

## Non-goals for the first Phase 3 implementation

- Supporting every Windows game.
- Bypassing anti-cheat, DRM, process protection or access controls.
- Installing a custom kernel keyboard/mouse driver.
- Claiming a normal `PostMessage` call is equivalent to Raw Input or polled state.
- Hiding devices without an independent recovery input path.
- Automatically injecting unknown processes.

## Why a capability planner is required

Games read input through different paths. Example profiles:

| Game behavior | Required compatibility surfaces |
| --- | --- |
| Simple window-message test app | Selected `WM_KEY*` / `WM_MOUSE*` delivery and target-window routing |
| Raw Input game | Raw Input registration interposition, synthetic `WM_INPUT`, `GetRawInputData` virtualization |
| Polling-heavy legacy game | Raw Input or messages plus `GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState`, cursor state |
| Mouse-capture game | Cursor position, clip, capture and visibility virtualization |
| Background-sensitive game | Focus-query virtualization and/or controlled focus-message synthesis |
| XInput game | Per-process XInput slot remapping |
| DirectInput game | Per-process enumeration/order/visibility |
| Single-instance game | Named mutex/event/pipe namespace compatibility |
| Protected/anti-cheat game | Usually no injection; supported only if a documented non-invasive backend covers all required behavior |

A backend that covers one row is not necessarily suitable for another. Therefore the planner consumes a typed `GameCompatibilityProfile` and an inventory of available `BackendDescriptor` objects, then produces an explicit `IsolationPlan`.

## Capability model

The initial capability vocabulary should include:

### Host and observation

- `RawInputObservation`
- `StablePhysicalDeviceIdentity`
- `SeatOwnershipResolution`
- `TargetWindowMessageRouting`
- `InputDiagnostics`

### Per-process keyboard/mouse state

- `RawInputRegistrationInterposition`
- `RawInputDataVirtualization`
- `WindowMessageFiltering`
- `KeyboardAsyncStateVirtualization`
- `KeyboardStateArrayVirtualization`
- `MouseButtonStateVirtualization`
- `CursorPositionVirtualization`
- `CursorClipVirtualization`
- `CursorVisibilityVirtualization`
- `CaptureVirtualization`

### Focus and windows

- `ForegroundQueryVirtualization`
- `FocusMutationVirtualization`
- `FocusMessageSynthesis`
- `WindowPlacementControl`
- `WindowStyleControl`

### Controllers

- `XInputSlotRemapping`
- `DirectInputVisibility`
- `DirectInputOrdering`
- `RawHidControllerRouting`

### Device and process isolation

- `PhysicalDeviceCloaking`
- `PhysicalInputSuppression`
- `VirtualInputInjection`
- `NamedObjectIsolation`
- `ProcessLifecycleTracking`
- `ChildProcessTracking`

Capabilities describe observable guarantees, not implementation names. A ProtoInput adapter and a future HydraSeat hook runtime may provide the same capability through different code.

## Backend descriptors

Every backend publishes metadata before planning:

```cpp
struct BackendDescriptor {
    std::string id;
    std::wstring displayName;
    IsolationCapability capabilities;
    BackendAvailability availability;
    BackendRisk risk;
    bool requiresProcessInjection;
    bool requiresAdministrator;
    bool usesKernelDriver;
    bool modifiesPersistentSystemState;
    bool antiCheatSensitive;
    bool reversible;
    int priority;
    std::wstring unavailableReason;
};
```

`availability` is runtime data. A backend must not advertise capabilities merely because support code exists in the repository.

### Planned built-in descriptors

#### `hydra.raw-input-host`

Provides stable observation, Seat ownership resolution and diagnostics. It is available when the HydraSeat host is running. It does not provide physical suppression or process-local state.

#### `hydra.legacy-message-router`

Uses selected target-window messages for a controlled test harness and simple applications. It is low risk but low compatibility and must not count as Raw Input virtualization.

#### `external.protoinput`

Represents a separately supplied, version-pinned ProtoInput loader/runtime. It may provide Raw Input, keyboard polling, cursor, focus, message and controller capabilities. It requires injection, architecture matching and explicit consent. It is unavailable until binaries and expected hashes are configured.

#### `external.hidhide-session`

Represents an installed HidHide control device and session-blacklist support. It provides physical-device cloaking only after an explicit guarded activation step. Initial implementation should probe availability without modifying state.

#### `hydra.directinput-adapter`

Future clean-room per-process DirectInput order/visibility adapter. Initially unavailable.

#### `hydra.unsupported`

Provides no enforcement and returns an actionable diagnostic. It is the safe fallback.

## Game compatibility profile

A game profile should state requirements and policy separately:

```cpp
struct GameCompatibilityProfile {
    std::string id;
    std::wstring name;
    IsolationCapability requiredCapabilities;
    IsolationCapability optionalCapabilities;

    InjectionPolicy injectionPolicy;
    DriverPolicy driverPolicy;
    AntiCheatPolicy antiCheatPolicy;
    RecoveryPolicy recoveryPolicy;

    bool antiCheatDetected;
    bool allowGlobalInputSuppression;
    bool requireZeroBleed;
    std::vector<std::string> preferredBackends;
};
```

Recommended policy enums:

```cpp
enum class InjectionPolicy {
    Forbidden,
    UserApproved,
    Required
};

enum class DriverPolicy {
    Forbidden,
    InstalledOnly,
    UserApprovedInstallation
};

enum class AntiCheatPolicy {
    DenyInvasiveBackends,
    ObservationOnly,
    ExplicitExperimentalOverride
};

enum class RecoveryPolicy {
    Required,
    Recommended,
    NotApplicable
};
```

The initial profile library should contain only transparent templates:

- `observation-harness`
- `legacy-message-test`
- `raw-input-game`
- `polled-keyboard-mouse-game`
- `focus-cursor-game`
- `xinput-controller-game`
- `directinput-controller-game`
- `protected-game-observation-only`

These are technical templates, not claims that a specific commercial game is supported.

## Planning algorithm

1. Start with required capability bits.
2. Remove backends that are unavailable.
3. Apply policy filters:
   - injection forbidden;
   - kernel/driver forbidden;
   - anti-cheat-sensitive backend denied;
   - persistent mutation denied;
   - recovery-required backend without a recovery guard denied.
4. Rank remaining backends using:
   - explicit profile preference;
   - number of uncovered required capabilities supplied;
   - risk and reversibility;
   - backend priority.
5. Select a deterministic set until no backend adds required coverage.
6. Optionally select low-risk backends for optional capabilities.
7. Report:
   - selected backends and their exact assigned capabilities;
   - missing required capabilities;
   - rejected backends and reasons;
   - required user consent/admin/reboot/recovery steps;
   - whether the plan is `Supported`, `SupportedWithWarnings`, `ObservationOnly`, or `Unsupported`.

A plan is `Supported` only if every required capability is covered and every selected backend passes policy checks.

## Deployment transaction

Planning and activation are separate. Activation should use a reversible transaction:

```text
Prepare
├─ verify target process and architecture
├─ verify Seat assignments and stable device IDs
├─ verify backend hashes/versions
├─ verify recovery guard
└─ capture current state

Stage
├─ create process group / Job Object
├─ start observation and diagnostics
├─ prepare process compatibility adapter
├─ prepare virtual state channels
└─ prepare optional HidHide session entries (not active yet)

Commit
├─ attach/inject per-process backend
├─ verify handshake and capabilities
├─ enable selected device routes
├─ activate physical cloaking last
└─ run zero-bleed self-test

Rollback on any failure
├─ clear session cloaking
├─ detach/stop adapters
├─ remove cursor clips
├─ release capture/focus loops
├─ restore Explorer/taskbar if modified
├─ terminate orphan helper processes
└─ write a diagnostic bundle
```

Physical hiding or global suppression must be the final activation step, never the first.

## Recovery guard

A plan requiring `PhysicalDeviceCloaking` or `PhysicalInputSuppression` is invalid unless all of these are satisfied:

1. A recovery input path not included in the hidden set, such as a spare keyboard/controller or a time-based automatic rollback.
2. A watchdog process outside the target process group.
3. Automatic rollback when the HydraSeat host, adapter or target process exits unexpectedly.
4. An emergency hotkey recognized by the watchdog or a visible timeout confirmation.
5. A persisted crash marker that triggers safe-mode startup on the next HydraSeat launch.
6. A command-line reset utility that does not depend on the main GUI.

For HidHide, session-scoped entries are preferred because the driver removes entries owned by a dead process. HydraSeat must still explicitly clear them on orderly shutdown and verify the expected driver behavior before production use.

## Diagnostics model

Every plan and runtime session should emit structured diagnostics:

```cpp
enum class IsolationDiagnosticCode {
    BackendUnavailable,
    CapabilityMissing,
    InjectionForbidden,
    AntiCheatConflict,
    ArchitectureMismatch,
    DriverUnavailable,
    RecoveryGuardMissing,
    HandshakeFailed,
    RouteRejected,
    PhysicalCloakFailed,
    CrossSeatBleedDetected,
    RollbackIncomplete
};
```

A diagnostic record should include timestamp, Seat, process ID, profile, backend, capability, severity, Win32 error where relevant, and human-readable remediation.

No log should contain credentials, tokens, private user documents or unrelated process memory.

## Runtime channel design

A per-process adapter needs a narrow versioned protocol instead of arbitrary shared memory:

- host creates a uniquely named, access-controlled channel;
- adapter authenticates using a random session identifier inherited at launch, not a global predictable name;
- protocol version and architecture are exchanged before activation;
- host sends Seat device identities, target window and capability configuration;
- adapter reports installed hooks and failures individually;
- input events contain sequence number, device identity, event class and state transition;
- heartbeat expiry triggers rollback;
- the adapter receives only data for its Seat.

The protocol should support 32-bit and 64-bit adapters without assuming pointer sizes in serialized messages.

## Raw Input strategy

For a game that registers Raw Input, a compatible process adapter generally needs to:

1. intercept `RegisterRawInputDevices` and `GetRegisteredRawInputDevices`;
2. preserve the game's requested usage pages/usages and flags;
3. register the host/adapter sink without allowing the game to overwrite it;
4. deliver a synthetic `WM_INPUT` token only for the owning Seat's events;
5. intercept `GetRawInputData` and possibly `GetRawInputBuffer` so the token resolves to Seat-local data;
6. preserve correct structure sizes, keyboard make/break flags, mouse relative/absolute semantics, wheel and button flags;
7. support child windows or multiple registered windows when the game requires them;
8. avoid pointer-shaped synthetic handles that collide with real handles.

The initial implementation should target a HydraSeat-owned test process before any commercial game.

## Polled keyboard/mouse state

The host should maintain per-Seat state machines from Raw Input events:

- key-down bitmap;
- transition/edge bitmap;
- mouse button bitmap;
- relative delta accumulator;
- wheel accumulator;
- cursor position and clip rectangle;
- focus/capture view.

The adapter then answers `GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState` and cursor APIs from the Seat-local state. Edge semantics must be tested because games may consume the low-order `GetAsyncKeyState` bit.

## Cursor and focus

The real Windows cursor remains global. A process adapter should expose a virtual cursor in the target's Seat-local coordinates and optionally draw a software cursor. It must not call global `SetCursorPos` or `ClipCursor` on behalf of multiple Seats.

Focus support has two levels:

1. **Message synthesis** for games that only react to activation messages.
2. **API query virtualization** for games that poll foreground/focus/capture APIs.

The planner records these as separate capabilities because one does not imply the other.

## Controllers

### XInput

A process adapter maps the game's requested logical slot to a selected physical slot or a future virtual controller. It must also map capabilities and vibration consistently.

### DirectInput

A separate adapter controls enumeration and visibility. Ordering by friendly name is insufficient; use stable instance identity where possible. DirectInput wrapper deployment is per process/profile and must not modify system DLLs.

### Raw HID and SDL

These may bypass XInput/DirectInput adapters. Profiles must report them explicitly as unsupported until a tested backend exists.

## Named objects and multiple instances

Some games use named mutexes, events, shared memory or pipes to prevent multiple instances. Namespace workarounds are separate from input isolation and must be profile-controlled. HydraSeat must not automatically rename arbitrary handles because that can alter security or application behavior.

## Anti-cheat and protected-process policy

Default policy:

- Do not inject, hook, patch, hide modules from, or alter protected/anti-cheat processes.
- Do not provide bypass instructions.
- Run observation-only diagnostics if permitted.
- Mark the profile unsupported when zero-bleed requirements cannot be met non-invasively.
- Experimental override, if ever added, must remain a developer-only build and never imply anti-cheat compatibility.

## Feasibility program and exit gates

### Gate A — observation harness

Target: two HydraSeat-owned test windows.

Pass criteria:

- stable device identity survives repeated enumeration;
- every event records exactly one physical source;
- no crash on hot-plug/removal;
- diagnostic trace reconstructs key/button state.

### Gate B — explicit target routing

Target: two HydraSeat-owned message test windows.

Pass criteria:

- keyboard/mouse A reaches only test window A through the HydraSeat route;
- keyboard/mouse B reaches only test window B through the HydraSeat route;
- ordinary Windows input may still leak, and the report must state that this is not isolation.

### Gate C — controlled process state and API virtualization

Target: two HydraSeat-owned target processes first, followed by HydraSeat-owned API probe binaries. No third-party process is used until the controlled probes and rollback path pass.

Implemented foundation:

- a versioned little-endian host/target protocol with bounded frames and monotonic sequences;
- local named-pipe transport with timeouts, remote-client rejection and a token/Seat/PID/architecture handshake;
- a normally loaded process-local adapter DLL with a fixed-width versioned C ABI;
- key down state, `GetAsyncKeyState`-style one-shot edges, `GetKeyState` and `GetKeyboardState`-style high bits;
- mouse button/wheel state, virtual cursor/clip, virtual foreground and virtual capture state;
- two separate target processes with synthetic Windows CI evidence that their A/B key, mouse, cursor and virtual-focus state do not cross;
- explicit target-process detection through `IsWow64Process2` with a bounded
  legacy fallback, plus PE-machine preflight before child launch;
- a bounded schema-v1 artifact manifest and deterministic architecture-neutral
  target/adapter/API-probe/polling-shim names under `gate-c/x86` and
  `gate-c/x64`;
- a code-complete startup-loaded polling shim that transactionally patches
  only the controlled probe's three allowlisted IAT entries and restores them
  before unload;
- a code-complete cursor/clip/logical-focus/capture extension in that same
  startup-loaded shim, pending native Windows validation;
- bounded per-target writer queues for interactive Raw Input routing;
- orderly shutdown and force-termination cleanup for controlled child processes.

Remaining Gate C pass criteria:

- run physical Seat-owned keyboards/mice through the controlled target processes;
- validate x64/x86 HydraSeat-owned probe processes observing Seat-local values
  through the ordinary polling APIs;
- validate controlled probes observing Seat-local values through the actual
  cursor/focus API surface, then implement the separate Raw Input packets;
- prove adapter/host/target failure leaves no orphan child or persistent global state;
- confirm no global cursor clipping and no physical suppression are performed during Gate C;
- keep anti-cheat/protected and commercial targets out of scope until all controlled criteria pass.

The direct adapter C ABI is a testable state boundary, not itself Windows API interposition.

### Gate D — optional physical-device cloaking

Target: controlled test devices and test processes.

Pass criteria:

- the HydraSeat host retains required device access;
- non-owning test process cannot enumerate/read cloaked device;
- session entries are cleared on normal exit and host crash;
- emergency recovery works without the cloaked devices;
- composite HID behavior is documented.

Keyboard/mouse cloaking is not enabled for users until this gate passes on supported Windows versions.

### Gate E — two-game zero-bleed proof

Target: two different non-anti-cheat games with explicit profiles.

Pass criteria over a defined test duration:

- 0 cross-seat key/button transitions;
- 0 cross-seat mouse movement/wheel events;
- each game retains its Seat-local cursor/focus behavior;
- independent controller and audio routing where required;
- target restart and device reconnect recover correctly;
- performance overhead and latency stay within declared limits;
- rollback restores normal single-user Windows behavior.

Only after Gate E may Phase 3 be marked complete.

## Test matrix

| Test class | Pure unit | Windows integration | Physical hardware | Target process injection |
| --- | ---: | ---: | ---: | ---: |
| Capability bit operations | Yes | No | No | No |
| Planner deterministic selection | Yes | No | No | No |
| Policy rejection/diagnostics | Yes | No | No | No |
| Profile parsing/validation | Yes | No | No | No |
| Backend availability probe | Partial | Yes | No | No |
| Raw Input event state machine | Yes with fixtures | Yes | Gate A acceptance pending | No |
| Explicit Seat target routing | Yes | Yes | Gate B acceptance pending | No |
| JSONL hot-plug/route diagnostics | Yes | Yes | Gate A/B acceptance pending | No |
| Gate C wire protocol and parser | Yes | Yes | No | No |
| Gate C adapter C ABI | Yes | Yes | No | Controlled process only |
| Gate C x86/x64 manifest, PE, and ABI matrix | Yes | CI pending | No | Controlled process only |
| Two-process synthetic state separation | Partial | Yes | No | Controlled process only |
| Bounded per-target writer queues | Partial | Yes | Gate C physical acceptance pending | Controlled process only |
| Raw Input behavior trace/parser | Yes | Windows x64/x86 validated (`32800513365`) | P3-HW-01 physical trace pending | Controlled probe only |
| Raw Input API virtualization | No | P3-RAW-02 READY | Pending | Controlled probe only |
| Polling API interposition | Yes | CI pending | No | Controlled probe only |
| Cursor/focus API interposition | No | Pending | Pending | Controlled probe only |
| HidHide session lifecycle | No | Yes | Yes | No |
| Two-game zero-bleed | No | Yes | Yes | Profile-dependent |

## Implemented non-invasive baseline

The non-invasive Phase 3 foundation now contains five implementation slices.

### Capability-planning slice

1. The capability vocabulary is represented by a typed 64-bit capability set.
2. `GameCompatibilityProfile`, `BackendDescriptor`, `BackendEnvironment`, `IsolationPlanner`, `IsolationPlan` and structured diagnostics are implemented.
3. Built-in descriptors cover the current host, legacy message router, optional ProtoInput adapter metadata, optional HidHide session-cloak metadata and the future DirectInput adapter.
4. Deterministic planner tests prove that unsupported capabilities remain visible and safety policies fail closed.
5. `hydra_plan` prints selected, rejected, covered and missing capabilities without activating any backend.
6. HidHide advertises **device cloaking only**, not verified physical input suppression. Therefore zero-bleed profiles remain unsupported until Gate D proves a real suppression path.

### Gate A/B implementation slice

1. `InputRouter` now emits sequenced Raw Input observations with stable physical IDs, keyboard make/break state, mouse movement/button/wheel data, device-arrival/removal events, decode statistics and explicit errors.
2. `InputObservationLedger` maintains per-physical-device key/button state and aggregates composite HID child handles so one child removal does not falsely mark the whole device offline.
3. `InputObservationSession` rebuilds fail-closed keyboard/mouse bindings from the Seat profile and records unassigned, shared/ambiguous, inactive, missing-target and dispatch-failure outcomes.
4. `InputTraceWriter` produces UTF-8 JSON Lines whose records explicitly state that native Windows input is not suppressed.
5. `hydra_input_lab` opens two HydraSeat-owned Seat windows, shows live device/route/hot-plug diagnostics, supports observer-only and saved-profile modes, and never performs process injection or device cloaking.
6. Pure regression tests and `hydra_input_lab --self-test` validate deterministic Gate A/B behavior without claiming physical acceptance.

Gate A/B **implementation** is complete, but physical acceptance remains pending until the checklist is executed with the target PC's two keyboards and two pointing devices. See [PHASE3_GATE_A_B_TESTING.md](PHASE3_GATE_A_B_TESTING.md).

### Gate C controlled-process slice

1. `hydra_gate_c_core` implements the protocol, transport and process-local state model.
2. `hydra_gate_c_adapter` provides a normally loaded, versioned C ABI for key, keyboard, async-edge, mouse, cursor, clip, foreground and capture state.
3. `hydra_gate_c_host` authenticates and manages one controlled target per Seat and uses bounded writer queues for physical Raw Input delivery.
4. `hydra_gate_c_target` loads the adapter normally and exposes controlled snapshots without modifying Windows APIs.
5. Windows CI starts two distinct processes and proves their synthetic states remain independent.
6. Cryptographic Windows session tokens, explicit timeouts, protocol validation and child cleanup are implemented.

### Gate C architecture-matrix slice

1. Runtime architecture detection uses `IsWow64Process2`; only API absence may
   activate the `IsWow64Process` plus `GetNativeSystemInfo` fallback.
2. The host parses target and adapter PE headers before launch, then checks the
   actual child process architecture and the fixed-width Hello value.
3. The schema-v1 manifest is bounded and rejects unknown/future/duplicate,
   missing, non-canonical, oversized, absolute, and traversal descriptors.
4. CMake places runtime artifacts under deterministic `gate-c/x86` or
   `gate-c/x64` directories while preserving architecture-neutral filenames.
5. The adapter C ABI explicitly declares `__cdecl` on Windows and publishes
   v1 size constants asserted from both C11 and C++ builds.
6. Windows CI declares native x86/x64 full test jobs and a separate x64 host
   job that selects and launches an actual x86 controlled target/adapter pair.

This slice is `VALIDATED` by Windows CI run `32727711605`, including native
x64/x86 and x64-host-to-x64/x86 controlled target/probe evidence.

### Gate C controlled polling-shim slice

1. `hydra_gate_c_shim.dll` is normally loaded at controlled probe startup; no
   already-running process is injected or remotely modified.
2. Only the current executable's named `GetAsyncKeyState`, `GetKeyState`, and
   `GetKeyboardState` imports from allowlisted USER32/NTUSER module families
   are eligible.
3. Install requires all three unique entries and performs reverse rollback on
   any write/protection failure; uninstall restores the exact saved pointers.
4. A fixed-width versioned C ABI reports lifecycle, generation, masks,
   adapter/system failures, and rollback completion in both architectures.
5. The probe owns the sole adapter context; the shim borrows it and maintains
   no duplicate input state. Adapter loss is fail-closed for the supported
   polling domain.
6. Portable strict-GCC transaction and ABI tests pass. Windows CI run
   `32780563364` also passes native x64/x86 and x64-host-to-x64/x86
   polling-probe execution, so P3-API-02 is `VALIDATED`.

Gate C remains incomplete until Windows validation confirms the cursor/focus
surface and later packets cover Raw Input while preserving the validated
polling-shim behavior. Commercial games, physical device cloaking and
anti-cheat targets remain out of scope. See
[PHASE3_GATE_C_TESTING.md](PHASE3_GATE_C_TESTING.md).

### Gate C controlled cursor/focus-shim slice

1. The same `hydra_gate_c_shim.dll` owns polling plus cursor/focus lifecycle;
   there is no competing DLL patching the controlled probe.
2. A closed ten-function USER32/NTUSER named-import allowlist covers
   `GetCursorPos`, `SetCursorPos`, `ClipCursor`, `GetClipCursor`,
   `GetForegroundWindow`, `GetActiveWindow`, `GetFocus`, `GetCapture`,
   `SetCapture`, and `ReleaseCapture`. `ShowCursor` is deferred.
3. Adapter ABI v2 retains the existing process-local cursor/clip state and
   adds fixed-width transient target/foreground/active/focus/capture runtime
   window values. No raw pointer is persisted or transported as identity.
4. Controlled v1 uses signed 32-bit logical screen coordinates exactly as
   supplied by the probe. It performs no inferred DPI, physical-pixel, or
   client-coordinate conversion. Clip right/bottom are exclusive; invalid
   rectangles fail without mutation and enabled clips clamp the cursor.
5. Logical foreground, active, and focus share one current-process-owned
   controlled target in v1. Capture may use another validated window owned by
   that process. Stale, destroyed, or foreign windows never fall back to the
   native global state.
6. Active setters mutate adapter state only. Native global cursor, clip,
   foreground, and capture are host-side diagnostics and are not changed by
   the shim.
7. The polling and cursor/focus patch sets install as one all-or-rollback
   capability and uninstall in reverse order after in-flight calls drain.
8. Strict portable component tests pass, and Windows CI run `32792381573`
   validates x64/x86 24/24 CTest plus x64-host-to-x64/x86 ordinary-API,
   no-cross-Seat, teardown, polling-regression, and global-state-preservation execution.

### Gate C controlled Raw Input behavior-probe slice

1. `hydra_gate_c_raw_input_probe` is a standalone controlled process and never
   shares process-level registration state with `InputRouter` or the production
   Gate C host.
2. Controlled Window A/Window B experiments record keyboard/mouse foreground
   registration, per-usage target replacement, independent registration,
   `RIDEV_INPUTSINK`, `RIDEV_DEVNOTIFY`, removal, and the destroyed-target path.
3. Schema v1 uses deterministic UTF-8 JSON, fixed-width runtime diagnostic
   values, strict old/future version rejection, and hard limits for events,
   registrations, packet bytes, buffered packets, and fixture bytes.
4. `WM_INPUT` and `WM_INPUT_DEVICE_CHANGE` use a pre-reserved bounded callback
   ledger and aligned fixed scratch buffer. Serialization and optional stable
   identity resolution occur after message processing.
5. `GetRawInputData` records header/input query and read sizes independently;
   pure contract tests reject zero, truncated, oversized, inconsistent,
   unknown-type, API-sentinel, and query/read-disagreement cases.
6. `GetRawInputBuffer` uses native pointer fields plus an explicitly recorded
   block alignment (including the documented WOW64 8-byte rule), with byte,
   progress, overflow, and packet-count checks.
7. The committed fixture is explicitly synthetic. Windows CI run `32800513365`
   validates native x64/x86 28/28 CTest and retains separate observed-Windows
   traces. The traces agree on per-usage replacement/removal, accepted-but-not-
   echoed `RIDEV_DEVNOTIFY`, retained destroyed-HWND runtime values until valid
   replacement, architecture-specific structure sizes, and 8-byte buffer alignment.
   Physical input and hot-plug evidence remain P3-HW-01.
8. No registration/data hook, synthetic handle/message, virtual queue, physical
   suppression, or third-party process work is present.

## Related documents

- [RELATED_SYSTEMS_RESEARCH.md](RELATED_SYSTEMS_RESEARCH.md)
- [CLEAN_ROOM_POLICY.md](CLEAN_ROOM_POLICY.md)
- [PHASE0_RESEARCH.md](PHASE0_RESEARCH.md)
- [PHASE3_GATE_A_B_TESTING.md](PHASE3_GATE_A_B_TESTING.md)
- [PHASE3_GATE_C_TESTING.md](PHASE3_GATE_C_TESTING.md)
- [ROADMAP.md](ROADMAP.md)
