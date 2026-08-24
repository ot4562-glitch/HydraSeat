# Phase 4 — Production Runtime, Process/Window Ownership, and Display Routing

## Phase objective

Move proven Phase 3 components into a background production runtime and make each Seat own a process tree, window set, and one-or-more-display group. Seat-owned windows must stay within their display group across launch, fullscreen transitions, DPI changes, display hot-plug, target restart, and host restart.

Physical displays are completed first. Virtual displays remain optional capability backends.

## Phase exit gate

Phase 4 is complete when:

1. the UI can close while `hydra_host.exe` keeps validated Seat runtime state;
2. UI/CLI/watchdog obtain the same authoritative state snapshot;
3. one configured Management Seat owns the visible control console, which opens on that Seat's primary display with a safe visible fallback;
4. the control console can Start, Stop/Return to Windows, and enter Reconfigure without owning runtime authority;
5. every launched process/child/window is attributed to one Seat or explicitly unowned;
6. Seat 1 may own multiple displays and Seat 2 a different display set;
7. window placement and restore survive DPI/topology changes;
8. display removal/return and target restart have deterministic recovery;
9. no unrelated window is moved;
10. physical-display support works without any virtual-display dependency;
11. optional virtual-display backends are capability-gated and reversible;
12. crash/restart tests leave ordinary Windows display topology usable.

## Dependency graph

```text
P3-CLOSE-01 -> P4-RUN-01 -> P4-IPC-01
                    |
                    +-> P4-CTRL-01 -> P4-CTRL-02
                    |
                    +-> P4-PROC-01 -> P4-WIN-01 -> P4-WIN-02
                    |
                    +-> P4-DIS-01 -> P4-DIS-02 -> P4-DIS-03

P4-WIN-01 + P4-DIS-02 -> P4-POL-01
P4-POL-01 + P4-DIS-03 + P4-CTRL-02 -> P4-REC-01
P4-DIS-01 -> P4-VID-01 -> P4-VID-02 -> optional P4-IDD-01

required packets -> P4-CLOSE-01
```

---

## P4-RUN-01 — Production `hydra_host.exe` runtime skeleton

**State:** BLOCKED

**Goal**

Extract runtime authority from the configuration GUI into a per-user background host process.

**Depends on**

- P3-CLOSE-01 or stable Phase 3 host/adapter contracts
- D-004, D-019

**Create/modify**

- `include/hydra/runtime_state.hpp`
- `include/hydra/runtime_host.hpp`
- `src/runtime_state.cpp`
- `src/runtime_host.cpp`
- `src/host_main.cpp`
- `hydra_host` CMake target
- tests with fake hardware/process/display backends

**Core types**

```cpp
enum class HostLifecyclePhase;
enum class SeatSessionPhase;
struct RuntimeSessionId;
struct SeatRuntimeState;
struct HostRuntimeSnapshot;
struct RuntimeTransition;
class RuntimeHost;
```

**Implementation skeleton**

1. Separate host-process lifecycle from Seat-session lifecycle. The host may be `Running` while the Seat session is `Idle`. Define `SeatSessionPhase` as `Idle -> Planning -> Prepared -> Starting -> Active -> Degraded -> Stopping -> RollingBack -> Idle`, with failures entering `RecoveryRequired` until verified recovery; do not use host process exit as the normal meaning of Stop.
2. load a validated Seat profile without starting any backend;
3. expose `Plan`, `Prepare`, `Start`, `StopAndReturnToWindows`, `Reset`, `ExitHostWhenIdle`, and read-only snapshot operations; keep closing a UI client separate from every session transition;
4. inject interfaces for hardware/input/process/window/display/audio backends;
5. ensure transitions are serialized and correlated;
6. own all worker threads and Phase 3 production routing components;
7. make start/stop idempotent;
8. keep the existing labs separate.

**Invariants**

- GUI presence is irrelevant to runtime continuity;
- closing or crashing the control UI never transitions the Seat session;
- `StopAndReturnToWindows` reaches session `Idle` only after rollback postconditions pass and does not require the host process to exit;
- `ExitHostWhenIdle` is rejected or converted to a Stop-first transaction while a Seat session is active;
- only the host mutates runtime state;
- one active mutation transaction at a time;
- a failed transition records rollback state;
- profile parsing alone does not advertise active isolation.

**Automated tests**

- every valid/invalid transition;
- concurrent command rejection/serialization;
- partial backend startup rollback;
- repeated start/StopAndReturn/reset while the host remains Running;
- close/reopen UI during Active leaves the Seat-session phase unchanged;
- ExitHostWhenIdle succeeds only from Idle and an Active request follows the declared Stop-first/reject policy;
- snapshot consistency under readers;
- host process self-test and clean exit.

**Done when**

A headless host can start a fake two-Seat session, publish state, stop, and recover independently of `HydraSeat.exe`.

**Suggested commit**

`feat: implement P4-RUN-01 background runtime host`

---

## P4-IPC-01 — UI/CLI/watchdog control and state protocol

**State:** BLOCKED

**Goal**

Provide a versioned local protocol so UI, tray, CLI, and watchdog control/observe the same host.

**Depends on**

- P4-RUN-01
- D-012, D-019

**Create/modify**

- `include/hydra/host_protocol.hpp`
- `src/host_protocol.cpp`
- `include/hydra/host_transport.hpp`
- `src/host_transport.cpp`
- `hydra_hostctl.exe`
- UI client adapter;
- protocol/process tests.

**Messages**

- `Hello` / `HelloAck`;
- `GetSnapshot` / `Snapshot`;
- `PlanSession` / `PlanResult`;
- `StartSession` / progress/result;
- `StopAndReturnToWindows`;
- `BeginReconfigure`;
- `ExitHostWhenIdle`;
- `EmergencyReset`;
- `SubscribeEvents` / runtime events;
- `Ping` / lease;
- `Error`.

**Implementation skeleton**

1. Reuse fixed-width/bounded protocol utilities where appropriate without coupling Gate C message semantics.
2. authenticate the same Windows user/session;
3. enforce one command correlation ID and response;
4. make event subscriptions bounded with reconnect/resnapshot;
5. provide CLI output in human and JSON form;
6. make protocol version mismatch explicit.

**Invariants**

- read-only clients cannot mutate;
- watchdog reset path remains available if UI protocol client fails;
- UI never infers state from a command timeout;
- reconnect starts from a full snapshot then applies events;
- no command contains credentials or raw input data.

**Automated tests**

- malformed/version/permission/reconnect;
- simultaneous UI and CLI readers;
- duplicate command correlation;
- host restart and resnapshot;
- subscription overflow recovery.

**Done when**

The main UI uses host IPC rather than owning runtime components directly.

**Suggested commit**

`feat: implement P4-IPC-01 host control protocol`

---

## P4-PROC-01 — Seat process tree and Job Object ownership

**State:** BLOCKED

**Goal**

Track each launched process and compatible descendants as one Seat-owned process group.

**Depends on**

- P4-RUN-01

**Create/modify**

- `include/hydra/process_group.hpp`
- `src/process_group.cpp`
- `include/hydra/process_launcher.hpp`
- `src/process_launcher.cpp`
- process test child executables;
- tests.

**Core types**

```cpp
struct ProcessIdentity;
struct ProcessLaunchSpec;
struct ProcessTreeSnapshot;
class SeatProcessGroup;
```

**Implementation skeleton**

1. launch controlled targets with explicit executable, arguments, environment, working directory, architecture, and policy;
2. create a Job Object before resume where compatible;
3. record root/child PIDs, creation time, executable identity, and exit code;
4. subscribe to process/job completion without polling the UI;
5. support profiles that cannot join a Job Object through an explicit weaker capability;
6. expose graceful stop then timeout/terminate policy;
7. prevent PID reuse confusion with creation identity.

**Invariants**

- a process belongs to at most one Seat;
- unowned processes are never terminated/moved;
- child tracking capability is explicit;
- target exit propagates to window/adapter cleanup;
- termination is profile/policy-controlled and logged.

**Automated tests**

- parent/child/grandchild tracking;
- root exits before child;
- Job Object breakaway supported/unsupported cases;
- graceful timeout and forced termination;
- PID reuse fixture;
- no orphan test processes.

**Done when**

Runtime snapshots show deterministic per-Seat process trees and cleanup.

**Suggested commit**

`feat: implement P4-PROC-01 Seat process groups`

---

## P4-WIN-01 — Window ownership tracker

**State:** BLOCKED

**Goal**

Attribute top-level/owned windows to Seat process groups and track their lifecycle without moving unrelated windows.

**Depends on**

- P4-PROC-01

**Create/modify**

- `include/hydra/window_tracker.hpp`
- `src/window_tracker.cpp`
- `include/hydra/window_identity.hpp`
- controlled multi-window test app;
- tests.

**Implementation skeleton**

1. enumerate initial top-level windows;
2. use WinEvent hooks or equivalent out-of-context notifications for create/show/hide/name/location/destroy;
3. map HWND to process identity and Seat;
4. classify primary game, launcher, dialog, overlay, child-owned popup, and ignored window by profile rules;
5. debounce duplicate events through a bounded queue;
6. validate HWND/process before every action;
7. publish window snapshots/events to the host.

**Invariants**

- callback performs minimal bounded enqueue work;
- stale/reused HWND is rejected with process identity check;
- system shell/security/other-user/unowned windows are never adopted;
- profile overrides are typed and diagnostic;
- destroy removes state idempotently.

**Automated tests**

- create/destroy/show/hide/title/location;
- multiple windows from one process;
- launcher spawning game process;
- fake/stale HWND;
- unrelated window remains untouched;
- callback queue overflow visible.

**Done when**

Each controlled target window is attributed to the correct Seat and unrelated windows are proven unaffected.

**Suggested commit**

`feat: implement P4-WIN-01 window ownership tracking`

---

## P4-DIS-01 — Display topology inventory and stable output identity

**State:** BLOCKED

**Goal**

Build one authoritative display topology combining Windows Display Configuration and DXGI data.

**Depends on**

- P3-CLOSE-01

**Create/modify**

- `include/hydra/display_topology.hpp`
- `src/display_topology.cpp`
- `include/hydra/display_identity.hpp`
- `hydra_display_diag.exe`;
- topology fixtures/tests.

**Data model**

```cpp
struct DisplayAdapterIdentity;
struct DisplayOutputIdentity;
struct DisplayMode;
struct DisplayOutput;
struct DisplayTopologySnapshot;
```

Fields include:

- stable target/adapter identity;
- GDI device name;
- connector/EDID-friendly metadata where available;
- physical/likely-virtual classification and confidence;
- desktop bounds/orientation/refresh/pixel format;
- DPI/effective scale;
- primary/active/attached state;
- adapter LUID and DXGI output relation;
- topology generation.

**Implementation skeleton**

1. query active/all paths with retry for topology changes;
2. enrich targets via display-config device info;
3. correlate with DXGI adapters/outputs;
4. preserve partial data with explicit confidence/errors;
5. sort deterministically by stable identity;
6. emit read-only diagnostics.

**Invariants**

- persistent identity never uses array index alone;
- topology query races retry with a bounded count;
- unavailable EDID/friendly name does not drop an output;
- likely-virtual is heuristic, not certainty;
- no display mode is changed in this packet.

**Automated tests**

- synthetic topology correlation;
- duplicate names/adapters;
- disconnected/disabled outputs;
- orientation/negative coordinates;
- topology-changing retry;
- actual Windows read-only integration.

**Manual acceptance**

Record three-monitor example including two displays in Seat 1 and one in Seat 2.

**Done when**

Seat profiles can reference stable output IDs independent of monitor enumeration order.

**Suggested commit**

`feat: implement P4-DIS-01 display topology inventory`

---

## P4-DIS-02 — Seat display groups and coordinate transforms

**State:** BLOCKED

**Goal**

Represent each Seat's display set, primary display, bounds, DPI, and reversible global/local coordinate transforms.

**Depends on**

- P4-DIS-01
- P2-SEAT-01

**Create/modify**

- `include/hydra/seat_display_layout.hpp`
- `src/seat_display_layout.cpp`
- `tests/test_seat_display_layout.cpp`

**Core types**

```cpp
struct SeatDisplayOutput;
struct SeatDisplayGroup;
struct CoordinateTransform;
struct DisplayLayoutValidation;
```

**Implementation skeleton**

1. resolve profile display IDs into active topology;
2. validate exclusivity/share policy;
3. compute union bounds and primary origin;
4. define point/rect transforms between global, Seat-local, output-local, and client coordinates;
5. handle negative coordinates, orientation, and per-output DPI;
6. report missing output/degraded layout explicitly.

**Invariants**

- multiple displays per Seat are normal;
- transform round trips within documented rounding tolerance;
- no assumption that Seat primary is Windows global primary;
- overlapping display assignment is rejected unless explicitly shareable;
- missing required output blocks or degrades according to profile.

**Automated tests**

- horizontal/vertical/L-shaped layouts;
- negative origin;
- mixed DPI/orientation;
- one/zero/missing outputs;
- transform round trip and rectangle clipping;
- two Seats with disjoint groups.

**Done when**

Window and cursor routing can consume one stable `SeatDisplayGroup` contract.

**Suggested commit**

`feat: implement P4-DIS-02 Seat display layouts`

---

## P4-WIN-02 — Window placement, mode, and restore engine

**State:** BLOCKED

**Goal**

Place and keep owned windows within the Seat display group according to a typed profile policy.

**Depends on**

- P4-WIN-01
- P4-DIS-02

**Create/modify**

- `include/hydra/window_policy.hpp`
- `src/window_policy.cpp`
- `include/hydra/window_placement.hpp`
- `src/window_placement.cpp`
- controlled fullscreen/DPI test app;
- tests.

**Policies**

- leave-native;
- place on primary output;
- span Seat group;
- borderless on selected output;
- restore last Seat-local rect;
- profile-specified delay/retry/stable-window selector;
- exclusive fullscreen allowed/blocked/unsupported.

**Implementation skeleton**

1. compute desired rect/style/mode from owned window + display group + profile;
2. apply only after ownership validation;
3. distinguish requested placement from observed final placement;
4. handle window recreation and launcher-to-game transitions;
5. save previous style/rect/show state for rollback;
6. rate-limit correction loops and detect fighting applications;
7. expose degraded/unsupported mode rather than infinite repositioning.

**Invariants**

- never move an unowned window;
- no infinite `SetWindowPos` loop;
- DPI context is explicit;
- rollback restores captured state when window still exists;
- exclusive fullscreen is not assumed controllable.

**Automated tests**

- controlled windows recreating/changing style;
- mixed DPI placement;
- target ignores/reverses position;
- borderless restore;
- stale HWND/process identity;
- unrelated window unchanged.

**Manual acceptance**

Two physical display groups, multiple windows, alt-tab, fullscreen/borderless transitions.

**Done when**

Owned windows remain inside assigned physical display groups for the tested policies.

**Suggested commit**

`feat: implement P4-WIN-02 Seat window placement`

---

## P4-POL-01 — Seat window/display runtime policy coordinator

**State:** BLOCKED

**Goal**

Combine process, window, and display events into one deterministic runtime policy without module-specific ad hoc reactions.

**Depends on**

- P4-WIN-01
- P4-WIN-02
- P4-DIS-02

**Create/modify**

- `include/hydra/seat_runtime_policy.hpp`
- `src/seat_runtime_policy.cpp`
- host integration;
- policy state-machine tests.

**Implementation skeleton**

1. consume serialized process/window/display events;
2. maintain desired versus observed Seat state;
3. calculate idempotent actions;
4. execute actions through interfaces with correlation IDs;
5. retry only retryable actions with bounded backoff;
6. enter `Degraded` on missing displays/windows;
7. initiate rollback on invariant violation.

**Invariants**

- deterministic event order and reconciliation;
- no direct window move from callbacks;
- duplicate event produces no duplicate mutation;
- action failure is visible in runtime snapshot;
- policy can be unit-tested with fake backends.

**Done when**

A synthetic event replay produces the same final state and actions every run.

**Suggested commit**

`feat: implement P4-POL-01 Seat runtime policy`

---

## P4-DIS-03 — Display hot-plug, degradation, and rollback

**State:** BLOCKED

**Goal**

Handle assigned display removal/reconnect/topology changes without losing windows or corrupting the desktop.

**Depends on**

- P4-DIS-02
- P4-POL-01

**Create/modify**

- display-change observer;
- topology generation/event contracts;
- safe fallback placement;
- tests and manual checklist.

**Behavior**

- debounce topology bursts;
- rebuild topology and Seat groups;
- if an optional secondary disappears, move owned windows to Seat primary and mark degraded;
- if required primary disappears, pause/stop according to profile and preserve process state;
- on reconnect, restore only when stable identity/mode is confirmed;
- never make another Seat's display the silent fallback;
- capture/restore pre-session Windows placement where possible.

**Automated tests**

- secondary removal;
- primary removal;
- reconnect with changed coordinates/DPI;
- monitor enumeration reordering;
- rapid disconnect/reconnect burst;
- host restart while display missing.

**Manual acceptance**

Physical unplug/replug and display disable/enable across two Seats.

**Done when**

Display change results are deterministic, visible, and recoverable.

**Suggested commit**

`feat: implement P4-DIS-03 display hotplug recovery`

---

## P4-VID-01 — Virtual display backend interface

**State:** BLOCKED

**Goal**

Define virtual display creation as an optional capability backend without requiring any one driver/tool.

**Depends on**

- P4-DIS-01

**Create/modify**

- `include/hydra/virtual_display_backend.hpp`
- `src/virtual_display_registry.cpp`
- fake backend/tests;
- planner capabilities/profile fields.

**Contract**

```cpp
struct VirtualDisplayRequest;
struct VirtualDisplayInstance;
struct VirtualDisplayBackendDescriptor;
class IVirtualDisplayBackend;
```

Operations:

- read-only probe;
- prepare/create;
- wait for topology appearance;
- update mode where supported;
- destroy;
- restore previous state;
- diagnostics/version/trust metadata.

**Invariants**

- backend absence does not break physical displays;
- create/destroy are correlated and idempotent;
- host confirms output in real topology before success;
- persistent driver state is explicit;
- unknown backend version is unsupported.

**Done when**

A fake backend and one read-only external probe pass planner/transaction tests.

**Suggested commit**

`feat: implement P4-VID-01 virtual display backend contract`

---

## P4-VID-02 — External virtual display adapter integration

**State:** BLOCKED

**Goal**

Integrate one user-supplied/licensed external virtual-display implementation through P4-VID-01 without bundling unclear binaries.

**Depends on**

- P4-VID-01
- P8-TRUST-01 optional component manifest/hash verification

**Implementation skeleton**

1. choose an adapter with documented automation/control and acceptable license;
2. require configured path/version/hash;
3. implement read-only probe first;
4. implement create/destroy under a transaction;
5. confirm topology identity and latency;
6. crash/host-exit cleanup;
7. document redistribution/install expectations.

**Manual acceptance**

Create/destroy/reboot/update/rollback and GPU/display latency matrix.

**Done when**

One virtual output can be assigned to a Seat while physical-only operation remains unaffected.

**Suggested commit**

`feat: implement P4-VID-02 external virtual display adapter`

---

## P4-IDD-01 — Custom IddCx/IDD feasibility gate

**State:** BLOCKED / OPTIONAL

**Goal**

Decide whether HydraSeat should own a custom indirect display driver.

**Depends on**

- P4-VID-01
- evidence from P4-VID-02
- P8 driver signing/install/recovery foundations

**Research/prototype outputs**

- official sample-based minimal driver in a separate component/repository area;
- WDK build/sign/test plan;
- user-mode control protocol;
- mode/EDID/adapter/GPU behavior;
- latency and resource use;
- install/update/uninstall/reboot/crash recovery;
- legal/signing/maintenance cost comparison.

**Decision**

- `ADOPT`: packets are added for production driver work;
- `DEFER`: external adapter remains recommended;
- `REJECT`: no custom driver in supported scope.

**Done when**

A written decision is based on measurements, not the roadmap assumption that a custom driver is automatically better.

**Suggested commit**

`docs: decide P4-IDD-01 custom display driver feasibility`

---

## P4-CTRL-01 — Management Seat control console placement and permissions

**State:** BLOCKED

**Goal**

Make `HydraSeat.exe` an on-demand management console that always opens on the configured Management Seat, default Seat 1, while `hydra_host.exe` continues to own the runtime in the background.

**Depends on**

- P4-IPC-01
- P4-DIS-02
- D-031, D-032

**Create/modify**

- `include/hydra/management_seat.hpp`
- `src/management_seat.cpp`
- `include/hydra/control_surface_model.hpp`
- `src/control_surface_model.cpp`
- `HydraSeat.exe` host-client/window placement integration;
- Seat/session profile field for `managementSeatId`;
- tests with fake host/display clients.

**Core types**

```cpp
struct ManagementSeatConfig;
struct ControlSurfacePlacement;
enum class ControlSurfaceFallback;
struct GlobalControlPermission;
class ControlSurfaceModel;
```

**Required visible controls**

- current runtime mode and session state;
- current Seat 1/Seat 2 display, keyboard, mouse, controller, and audio assignments;
- `Start`;
- `Stop / Return to Windows`;
- `Reconfigure`;
- identify/flash/test selected display and input device;
- startup mode selection: Manual, Background Idle, Auto-Activate Validated Session;
- diagnostics/export;
- recovery/reset entry point.

**Implementation skeleton**

1. Add `managementSeatId` to the typed Seat/session configuration with a deterministic default of Seat 1.
2. Resolve the Management Seat's active primary display through `SeatDisplayGroup` and stable display identity.
3. On launch/reopen, position the control window wholly inside that primary display's work area and restore only a still-valid Seat-local rectangle.
4. If the configured display is unavailable, place the window visibly on the current Windows primary display, mark the session degraded, and explain why fallback occurred.
5. Bind every button/view to the authoritative host snapshot and versioned host commands; never mutate runtime components from UI code.
6. Keep other Seat shells read-mostly for whole-machine operations unless an explicit permission policy grants otherwise.
7. Make closing the control window close only the client; the active host/watchdog/session continue according to runtime mode.
8. Allow reopening from the Management Seat shell shortcut, tray/recovery entry point, Start Menu shortcut, or `hydra_hostctl ui`-style command without creating a second host.
9. Keep one visible control-console instance per Windows user session and bring the existing instance to the Management Seat display on duplicate launch.

**Invariants**

- runtime authority never moves into the control UI;
- global Start/Stop/Reconfigure commands are accepted only from an authorized control client/session;
- the control window cannot become permanently off-screen after monitor removal, DPI change, or profile change;
- another Seat cannot accidentally terminate or reconfigure the whole machine through its default shell;
- closing/crashing/restarting `HydraSeat.exe` does not change active Seat isolation;
- all displayed assignments and state come from the host snapshot or validated inactive configuration, not stale UI memory;
- normal operation requires no console window or repeated administrator prompt.

**Automated tests**

- default Management Seat is Seat 1;
- explicit Management Seat 2 override;
- Seat 1 with two displays chooses its configured primary;
- primary display removal falls back visibly and reports degraded state;
- saved off-screen rectangle is clamped/rejected;
- duplicate UI launch activates existing instance;
- UI kill/restart/resnapshot while host remains Active;
- unauthorized other-Seat/global command rejection;
- host disconnect shows unknown/degraded state rather than optimistic Active/Stopped text.

**Manual acceptance**

- run Seat 1 on two monitors and Seat 2 on one monitor;
- verify the console always appears on Seat 1 primary while the split session is active;
- close and reopen the console several times without disturbing either game;
- unplug the Management Seat primary and verify visible fallback/recovery placement.

**Done when**

A user can leave HydraSeat running invisibly in the background, reopen one authoritative control console on Seat 1's primary display, and inspect/control the session without affecting runtime merely by opening or closing the window.

**Suggested commit**

`feat: implement P4-CTRL-01 Management Seat control console`

---

## P4-CTRL-02 — Return-to-Windows and safe reconfiguration workflow

**State:** BLOCKED

**Goal**

Make ending the split-PC experience and changing the hardware composition normal, obvious, verified UI operations instead of requiring process termination, reboot, or manual cleanup.

**Depends on**

- P4-CTRL-01
- P4-RUN-01
- P4-POL-01
- D-033, D-034

**Create/modify**

- `include/hydra/session_control_transition.hpp`
- `src/session_control_transition.cpp`
- host protocol commands/results for `StopAndReturnToWindows`, `BeginReconfigure`, and `ExitHostWhenIdle` where not already represented;
- control-surface button/state model;
- inactive configuration/editing gate;
- tests with fake rollback actions and UI client.

**User-visible flows**

### Stop / Return to Windows

```text
Active split session
  -> user presses Stop / Return to Windows
  -> host enters Stopping/RollingBack
  -> target launch/input acceptance stops
  -> session-specific input/device/window/display/audio/controller/shell actions roll back
  -> host verifies ordinary Windows postconditions
  -> state becomes Idle/Stopped
  -> all monitors and ordinary keyboard/mouse behavior are usable as one normal PC
```

### Reconfigure

```text
Active split session
  -> user presses Reconfigure
  -> save/snapshot current profile and requested edit intent
  -> verified Stop / Return to Windows
  -> open configuration mode on Management Seat display
  -> identify/flash/test monitors and input devices
  -> edit Seat assignments and primary displays
  -> Validate / Preflight
  -> Save
  -> Start Now OR remain in normal Windows mode
```

### Exit HydraSeat completely

```text
Active session
  -> Stop / Return to Windows first
  -> verify rollback
  -> stop idle host/watchdog if startup mode permits
  -> close control/tray clients
```

**Implementation skeleton**

1. Model Stop, Reconfigure, and Exit as host state-machine transitions, not button-specific UI procedures.
2. Have every active backend contribute captured state and an idempotent rollback/verify action.
3. Disable configuration mutation while runtime is Active except for explicitly capability-tested hot-reconfiguration operations.
4. `Reconfigure` records edit intent, requests the same verified stop transaction, and opens the editor only after the host reaches a safe inactive state.
5. Preserve the previous valid profile until the edited profile validates and writes transactionally.
6. On cancel, remain in normal Windows mode with the previous saved profile untouched.
7. On Save + Start, compile a new immutable plan and run normal preflight rather than patching the old active plan.
8. If any rollback postcondition fails, enter `RecoveryRequired`, keep Start/Reconfigure blocked where unsafe, and surface `hydra_reset`/watchdog recovery.
9. Distinguish `Stop Session`, `Exit Controller UI`, and `Exit Background Host` so users do not accidentally stop a split session by closing a window.

**Invariants**

- `Stopped` means rollback verification passed, not merely that games/windows exited;
- no new profile is activated until the old session is fully inactive;
- failed reconfiguration never overwrites the last valid profile;
- another Seat receives no global stop/reconfigure authority by default;
- Stop/Reconfigure are idempotent under duplicate clicks/IPC retries;
- the recovery/reset path remains available if the console disappears mid-transition;
- ordinary Windows remains the safe fallback state.

**Automated tests**

- Active -> Stop -> Idle with all fake rollback postconditions verified;
- duplicate Stop while Stopping/RollingBack;
- Active -> Reconfigure -> Idle -> editor-ready;
- reconfigure cancel preserves prior profile;
- invalid edited profile cannot Start;
- Save + Start compiles a new plan hash;
- rollback failure -> `RecoveryRequired` and no false Stopped state;
- UI crashes during stop, reconnects, and resnapshots transition/result;
- Exit Controller leaves Active session running;
- Exit Host while Active is rejected or converted to Stop-first flow;
- Management Seat display disappears during reconfigure and UI falls back visibly.

**Manual acceptance**

- from a physical two-Seat session, press `Stop / Return to Windows` and verify all monitors/input work as one normal PC without reboot;
- start again, press `Reconfigure`, change one monitor and one keyboard assignment, validate/save/start, and verify the new composition;
- close only the UI during Active and prove the split session continues;
- choose Exit HydraSeat completely and prove rollback finishes before host/watchdog exit.

**Done when**

A normal user can split the PC, close/reopen the controller, return the machine to ordinary Windows with one clear action, change the monitor/input composition with a few guided steps, and start the new session without manual cleanup or reboot.

**Suggested commit**

`feat: implement P4-CTRL-02 return and reconfigure workflow`

---

## P4-REC-01 — Runtime/window/display crash and restart matrix

**State:** BLOCKED

**Goal**

Prove the production host can restart or fail without losing control of owned processes/windows or damaging display placement.

**Depends on**

- P4-IPC-01
- P4-POL-01
- P4-DIS-03
- P8-WATCH-01
- P8-JOURNAL-01

**Failure matrix**

- UI killed;
- host killed and restarted;
- window tracker failure;
- target process crash/restart;
- display unplug during activation;
- topology query failure;
- optional virtual backend failure;
- logoff/shutdown.

**Automated/manual evidence**

- no unowned window moved;
- owned windows restored or session marked recovery-required;
- process groups reconciled;
- watchdog state/leases correct;
- previous user window state restored on stop;
- repeated reset safe.

**Done when**

The runtime survives the matrix on physical two-Seat displays.

**Suggested commit**

`test: implement P4-REC-01 runtime display recovery matrix`

---

## P4-CLOSE-01 — Phase 4 closure

**State:** BLOCKED

**Closure checklist**

- GUI no longer owns runtime authority;
- Management Seat control console placement, permissions, close/reopen behavior, and safe display fallback are proven;
- Stop / Return to Windows and Reconfigure workflows complete verified rollback before editing or exit;
- host IPC/state model stable and versioned;
- process/window ownership proven;
- multi-monitor Seat groups and DPI transforms proven;
- physical hot-plug/recovery evidence recorded;
- virtual displays remain optional and capability-gated;
- no unrelated windows moved in tests;
- Phase 5 receives stable launch/process/window/display contracts;
- architecture, status, and README agree.

**Done when**

Phase 4 is complete and Phase 5 becomes current.

**Suggested commit**

`docs: close Phase 4 runtime and display routing`
