# HydraSeat Architecture Specification

## System goal

HydraSeat makes one physical Windows PC feel like multiple local gaming PCs without requiring virtual machines, Remote Desktop, or streaming. The unit of composition is a **Seat**, which can own multiple displays, input devices, controllers, audio endpoints and processes.

```text
                         HydraSeat Host
                               │
        ┌──────────────────────┼──────────────────────┐
        │                      │                      │
 Hardware / Topology      Seat Composition     Compatibility Planner
        │                      │                      │
 Raw Input, SetupAPI       Displays[], Inputs     Game profile + available
 ConfigMgr, displays       Audio, Primary         backend capabilities
        │                      │                      │
        └───────────────┬──────┴───────────┬──────────┘
                        │                  │
                      Seat 1            Seat 2
                        │                  │
             Process/window group  Process/window group
                        │                  │
                  Backend plan        Backend plan
                        │                  │
                  Game / Apps          Game / Apps
```

## Target production process topology

The current development labs are intentionally separate executables. The planned production topology is:

```text
HydraSeat.exe
  Configuration UI, tray UI, profile editor, start/stop/reset commands
        │ versioned local control/state protocol
        ▼
hydra_host.exe
  Authoritative per-user runtime state
  Hardware/input/process/window/display/controller/audio routing
  Compatibility-plan execution and diagnostics
        │ leases / rollback manifest
        ├──────────────────────────────► hydra_watchdog.exe
        │                                 Independent timeout/crash recovery
        │
        ├─ Seat 1 process group ─► target A + optional adapter32/64.dll
        ├─ Seat 2 process group ─► target B + optional adapter32/64.dll
        ├─ Seat 1 shell ─────────► hydra_shell.exe
        └─ Seat 2 shell ─────────► hydra_shell.exe

hydra_reset.exe
  Independent emergency reset and verification path
```

The UI and shell are disposable clients. They are not runtime authority. Optional adapters, drivers, providers, and extensions are capability-planned, version/hash/trust checked, and can be absent without breaking physical-display core operation.

The packet-level construction order, ownership rules, rollback gates, and executable contracts are defined in [the master implementation roadmap](implementation/README.md) and [non-negotiable decisions](implementation/DECISIONS.md).

## Layer boundaries

### 1. Hardware Detector (`HardwareDetector`)

Responsibilities:

- enumerate attached monitors with `EnumDisplayMonitors` / `EnumDisplayDevices`;
- enumerate Raw Input keyboards, mice, touchpads and HID controllers;
- resolve stable physical identity through SetupAPI and ConfigMgr;
- enumerate XInput controller slots;
- classify virtual displays conservatively;
- expose diagnostics without changing device state.

Hardware detection does not provide isolation.

### 2. Seat Composition (`WorkspaceManager` / `SeatConfig`)

A Seat owns logical resources:

```json
{
  "id": 1,
  "name": "Player 1",
  "displays": ["Display:LG", "Display:Samsung"],
  "primary_display": "Display:LG",
  "keyboards": ["Keyboard:A"],
  "mice": ["Mouse:A"],
  "controllers": ["Controller:XInput:0"],
  "audio_output": "Audio:Headset",
  "audio_input": "Audio:Microphone",
  "target_hwnd": 0,
  "active": true
}
```

The manager enforces exclusive physical-device ownership by default, supports explicit sharing, and saves/loads transactional UTF-8 JSON profiles.

### 3. Raw Input Host (`InputRouter`)

Responsibilities:

- register a host Raw Input sink for keyboards, mice and Precision Touchpads;
- request `WM_INPUT_DEVICE_CHANGE` notifications;
- decode `WM_INPUT` safely into sequenced observations;
- preserve keyboard make/break information and mouse movement/button/wheel data;
- associate transient Raw Input handles with stable SetupAPI/ConfigMgr identities;
- cache online device descriptors and expose arrival/removal statistics;
- report decode and callback failures explicitly.

The router can create a hidden message window or use a caller-owned window. A caller-owned window must forward both `WM_INPUT` and `WM_INPUT_DEVICE_CHANGE`.

Limitations:

- observing Raw Input does not stop Windows or another game from receiving normal input;
- legacy target-window messages are a diagnostic compatibility aid, not Raw Input or key-state virtualization;
- a process may have only one Raw Input registration target per device class, so a future process adapter must interpose registration coherently;
- `InputRouter::setIsolationMode` records policy intent only and supplies no physical suppression guarantee.

### 4. Input Observation and Gate A/B Routing

The non-invasive Phase 3 observation path is separated from future game-process adapters.

#### `InputObservationLedger`

- aggregates events by stable physical device ID rather than transient Raw Input handle;
- tracks key down/up state, mouse buttons, movement, wheel totals and event sequence;
- tracks every online child handle belonging to a composite HID identity;
- marks a composite physical device offline only after its final child collection is removed;
- clears held key/button state when the final physical collection disappears;
- exposes deterministic snapshots for tests, UI and JSONL diagnostics.

#### `InputObservationSession`

- rebuilds keyboard/mouse bindings from `WorkspaceManager` and `SeatRoutingPolicy`;
- binds only a device with exactly one owning Seat;
- treats shared input as ambiguous instead of selecting one Seat silently;
- rejects unassigned devices, inactive Seats and missing target windows explicitly;
- records per-Seat routed-event and dispatch-failure metrics;
- accepts a dispatch callback so routing policy remains independent of Win32 UI code.

Controllers remain outside Gate A/B routing because the current `InputRouter` does not yet provide controller events. XInput/DirectInput/Raw-HID controller work stays profile- and backend-specific.

#### `hydra_input_lab`

- creates two HydraSeat-owned top-level Seat windows;
- loads a saved Seat profile or runs in observer-only mode;
- receives live Raw Input and hot-plug events from a hidden `InputRouter` window;
- posts a HydraSeat-specific diagnostic notification to one explicitly selected Seat window;
- writes UTF-8 JSON Lines for physical acceptance analysis;
- exposes assigned and observed stable IDs, online state, counters and failures;
- displays a permanent warning that normal Windows input is still active.

This is a Gate A/B diagnostic route, not a game compatibility backend. It does not use ordinary messages to impersonate native game input, suppress physical input, virtualize polling/cursor/focus state, or claim zero input bleed.

### 5. Gate C Controlled-Process Runtime

Gate C introduces a process boundary without attaching to a third-party game.

#### `hydra_gate_c_core`

- encodes and validates a fixed-width little-endian protocol rather than serializing C++ objects;
- bounds every payload/frame and enforces a monotonic input/control sequence;
- uses local Windows named pipes with overlapped timeouts and `PIPE_REJECT_REMOTE_CLIENTS`;
- authenticates each controlled child with a random session token and validates Seat ID, child PID and architecture;
- represents keyboard, mouse, virtual cursor, clip, foreground and capture state independently for each target process.

#### `hydra_gate_c_adapter`

The adapter is a normally loaded shared library with a versioned C ABI. It owns one process-local `VirtualInputState` per adapter context and exposes controlled-test equivalents for async/key/keyboard state, mouse buttons/wheel, cursor/clip and virtual foreground/capture queries. It does not install hooks or modify Windows APIs.

#### `hydra_gate_c_host` and `hydra_gate_c_target`

- the host starts only HydraSeat-owned target binaries and validates their handshake;
- each Seat receives a distinct target process and adapter context;
- Raw Input delivery uses one bounded writer queue per target so slow pipe writes do not block the Raw Input window procedure indefinitely;
- queue overflow, target failure and protocol failure remain visible and terminate the controlled session rather than rerouting input;
- synthetic Windows CI starts two processes and proves different A/B key edges, mouse state, cursor state and virtual foreground state do not cross between them;
- the target displays virtual state beside actual OS foreground state to make the current limitation visible.

The current target calls the adapter C ABI directly. Gate C remains incomplete until HydraSeat-owned probe binaries observe these values through the actual Raw Input, polling, cursor and focus API surfaces. No commercial-process injection, physical suppression or anti-cheat interaction is implemented.

See [PHASE3_GATE_C_TESTING.md](PHASE3_GATE_C_TESTING.md).

### 6. Compatibility Profile and Planner

The planner is the Phase 3 control plane. It receives:

- a `GameCompatibilityProfile` describing required and optional API guarantees;
- a `BackendEnvironment` describing user approval, anti-cheat state, recovery readiness and installed components;
- an inventory of `BackendDescriptor` objects.

It produces an `IsolationPlan` containing:

- selected backends and capability assignment;
- missing capabilities;
- rejected backends and reasons;
- warnings and consent/admin/recovery requirements;
- a status that can be supported, supported with warnings, observation-only or unsupported.

The planner fails closed. A low-compatibility message route cannot satisfy Raw Input, cursor, foreground or physical-suppression requirements.

### 7. Input Isolation Backends

Backends are optional, independently discoverable components.

#### Host observation backend

Provides Raw Input observation, stable device identity, Seat ownership and diagnostics. It is low risk and available with HydraSeat, but it supplies no suppression or per-process virtualization.

#### Legacy message router

Provides selected target-window message routing for HydraSeat-owned tests and simple applications. It never claims zero input bleed.

#### Per-process compatibility adapter

A future/optional process component can virtualize:

- Raw Input registration and data;
- keyboard/mouse polling state;
- cursor position, clip, visibility and capture;
- foreground/focus queries and messages;
- XInput/DirectInput behavior;
- window placement/style;
- selected named objects.

ProtoInput is the primary public reference and possible external adapter, but direct reuse is blocked until the project license and transitive dependencies are resolved.

#### HidHide session cloak

An optional installed-driver adapter may use HidHide's documented control API. It can hide selected physical devices but does not create replacement Seat-local input. Activation is forbidden without a recovery guard, explicit consent and Gate D evidence. The current descriptor advertises device cloaking only, not verified physical input suppression.

#### Controller visibility adapters

Separate XInput slot-remapping and DirectInput order/visibility components are selected according to the target game's actual input API.

### 8. Process and Window Manager

Planned responsibilities:

- launch a target suspended when a startup adapter is required;
- assign process/children to a Windows Job Object where compatible;
- discover and track target windows;
- keep windows inside the Seat display group;
- enforce primary-monitor and Seat-local coordinate policy;
- roll back helpers and state when a process exits;
- isolate named objects only when an explicit profile requires it.

### 9. Display Manager (`DisplayManager`)

Planned responsibilities:

- expose physical monitor bounds and Seat-local coordinates;
- manage primary/secondary display groups;
- position and restore windows;
- later integrate an IDD/IddCx or compatible local virtual-display adapter.

Virtual-display creation remains Phase 4.

### 10. Audio Manager

Planned responsibilities:

- enumerate Core Audio endpoints;
- associate endpoints with Seats;
- route supported per-application sessions to Seat outputs;
- expose microphone ownership and diagnostics.

## Runtime activation transaction

Risky activation must be transactional:

```text
Plan -> Validate -> Stage -> Handshake -> Enable routes -> Cloak last -> Verify
  \_______________________________________________________________/
                         rollback on any failure
```

Rollback includes clearing session cloaking, releasing cursor/focus state, stopping adapters, restoring process/window state and producing a diagnostic bundle.

The Gate A/B lab does not enter the risky activation transaction because it performs observation and diagnostic notification only.

## Recovery model

Any backend that can suppress or hide input requires:

- an independent recovery device or automatic timeout;
- a watchdog outside the target process group;
- crash-triggered rollback;
- a safe-mode startup marker;
- a command-line reset path;
- explicit user confirmation.

## Anti-cheat and protected processes

HydraSeat does not bypass anti-cheat, DRM or protected-process controls. Invasive backends are denied by default when anti-cheat/protection is detected or declared. The planner returns observation-only or unsupported unless a tested non-invasive plan covers every required capability.

## Source and license boundaries

See:

- [Master implementation roadmap](implementation/README.md)
- [Product requirement traceability](implementation/TRACEABILITY.md)
- [Non-negotiable decisions](implementation/DECISIONS.md)
- [RELATED_SYSTEMS_RESEARCH.md](RELATED_SYSTEMS_RESEARCH.md)
- [PHASE3_INPUT_ISOLATION_DESIGN.md](PHASE3_INPUT_ISOLATION_DESIGN.md)
- [PHASE3_GATE_A_B_TESTING.md](PHASE3_GATE_A_B_TESTING.md)
- [PHASE3_GATE_C_TESTING.md](PHASE3_GATE_C_TESTING.md)
- [CLEAN_ROOM_POLICY.md](CLEAN_ROOM_POLICY.md)

Reference repositories are not build inputs. GPL, unlicensed and proprietary implementation code must not be copied into the core. Permissive code reuse requires an explicit HydraSeat license, dependency audit, attribution and third-party notices.
