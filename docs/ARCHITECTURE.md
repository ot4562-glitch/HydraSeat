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

- register a host Raw Input sink;
- decode `WM_INPUT` safely;
- associate events with stable physical paths/handles;
- update diagnostic and future per-Seat state;
- notify routing policy subscribers.

Limitations:

- observing Raw Input does not stop Windows or another game from receiving normal input;
- `PostMessage` is a compatibility aid for simple targets, not full Raw Input or key-state virtualization;
- a process may have only one Raw Input registration target per device class, so a process adapter must interpose registration coherently.

### 4. Compatibility Profile and Planner

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

The planner must fail closed. It may not infer that a low-compatibility message route provides Raw Input, cursor, foreground or physical-suppression guarantees.

### 5. Input Isolation Backends

Backends are optional, independently discoverable components.

#### Host observation backend

Provides Raw Input observation, device identity, Seat ownership and diagnostics. Low risk and always available with HydraSeat.

#### Legacy message router

Provides selected target-window message routing for HydraSeat-owned tests and simple applications. It never claims zero input bleed.

#### Per-process compatibility adapter

Future/optional process component that can virtualize:

- Raw Input registration and data;
- keyboard/mouse polling state;
- cursor position, clip, visibility and capture;
- foreground/focus queries and messages;
- XInput/DirectInput behavior;
- window placement/style;
- selected named objects.

ProtoInput is the primary public reference and possible external adapter, but direct reuse is blocked until the project license and transitive dependencies are resolved.

#### HidHide session cloak

Optional installed-driver adapter using HidHide's documented control API. It hides original physical devices but does not create replacement Seat-local input. Activation is forbidden without a recovery guard and explicit consent.

#### Controller visibility adapters

Separate XInput slot-remapping and DirectInput order/visibility components selected according to the target game's actual API.

### 6. Process and Window Manager

Planned responsibilities:

- launch a target suspended when a startup adapter is required;
- assign process/children to a Windows Job Object where compatible;
- discover and track target windows;
- keep windows inside the Seat display group;
- enforce primary-monitor and local coordinate policy;
- roll back helpers and state when a process exits;
- isolate named objects only when an explicit profile requires it.

### 7. Display Manager (`DisplayManager`)

Planned responsibilities:

- expose physical monitor bounds and Seat-local coordinates;
- manage primary/secondary display groups;
- position and restore windows;
- later integrate an IDD/IddCx or compatible local virtual-display adapter.

Virtual-display creation remains Phase 4.

### 8. Audio Manager

Planned responsibilities:

- enumerate Core Audio endpoints;
- associate endpoints with Seats;
- route supported per-application sessions to Seat outputs;
- expose microphone ownership and diagnostics.

## Phase 3 runtime transaction

Risky activation must be transactional:

```text
Plan -> Validate -> Stage -> Handshake -> Enable routes -> Cloak last -> Verify
  \_______________________________________________________________/
                         rollback on any failure
```

Rollback includes clearing session cloaking, releasing cursor/focus state, stopping adapters, restoring process/window state and producing a diagnostic bundle.

## Recovery model

Any backend that can suppress or hide input requires:

- an independent recovery device or automatic timeout;
- a watchdog outside the target process group;
- crash-triggered rollback;
- safe-mode startup marker;
- a command-line reset path;
- explicit user confirmation.

## Anti-cheat and protected processes

HydraSeat does not bypass anti-cheat, DRM or protected-process controls. Invasive backends are denied by default when anti-cheat/protection is detected or declared. The planner returns observation-only or unsupported unless a tested non-invasive plan covers every required capability.

## Source and license boundaries

See:

- [RELATED_SYSTEMS_RESEARCH.md](RELATED_SYSTEMS_RESEARCH.md)
- [PHASE3_INPUT_ISOLATION_DESIGN.md](PHASE3_INPUT_ISOLATION_DESIGN.md)
- [CLEAN_ROOM_POLICY.md](CLEAN_ROOM_POLICY.md)

Reference repositories are not build inputs. GPL, unlicensed and proprietary implementation code must not be copied into the core. Permissive code reuse requires an explicit HydraSeat license, dependency audit, attribution and third-party notices.
