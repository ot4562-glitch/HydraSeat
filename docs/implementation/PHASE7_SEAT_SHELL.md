# Phase 7 — Seat Shell and Local-PC Experience

## Phase objective

Make each Seat feel like a coherent local computer rather than a collection of routed game windows. The Seat shell provides a launcher, owned-window list, wallpaper/desktop zones, software cursor surface, Seat status, notifications, and safe controls while leaving the real Windows shell recoverable.

The first supported shell is an overlay/application per Seat, not a replacement Windows shell and not a second Windows desktop/session.

## Phase exit gate

Phase 7 is complete when:

1. one shell instance is owned by one Seat and stays inside that Seat display group;
2. each shell lists/activates only owned applications/windows;
3. profile launcher actions call the production host, not direct process creation;
4. wallpaper/zones/task surface span the Seat's multi-monitor group correctly;
5. Seat software cursors stay within Seat bounds and do not require multiple global Windows cursors;
6. notifications and optional clipboard policy do not leak by default;
7. shell crash/restart does not stop or corrupt the active Seat runtime;
8. DPI/accessibility/localization basics pass;
9. UI/tray/shell all show the same host state;
10. Explorer and ordinary single-user Windows behavior are restored on stop/reset.

## Dependency graph

```text
P6-CLOSE-01 + P4-RUN/IPC/WIN/DIS
          |
          +-> P7-SHELL-01 -> P7-LAUNCH-01 -> P7-TASK-01
          +-> P7-DESK-01
          +-> P7-CURSOR-01
          +-> P7-HOTKEY-01

P7-SHELL-01 -> P7-NOTIFY-01 -> optional P7-CLIP-01
all UI paths -> P7-A11Y-01 -> P7-REC-01 -> P7-CLOSE-01
```

---

## P7-SHELL-01 — Per-Seat shell process and model

**State:** BLOCKED

**Goal**

Create one lightweight shell process per active Seat with a read-mostly host client and no runtime authority.

**Depends on**

- P4-IPC-01
- P4-DIS-02
- P6 profile/session contracts

**Create/modify**

- `include/hydra/shell_protocol.hpp` only if host protocol extensions are required;
- `include/hydra/seat_shell_model.hpp`
- `src/seat_shell_model.cpp`
- `src/shell_main.cpp`
- `hydra_shell.exe`;
- fake host-client tests.

**Shell identity**

- Seat ID and runtime session ID;
- display group snapshot/generation;
- owned process/window snapshot;
- profile/app launcher entries;
- input/cursor/status/recovery summary;
- no hardware/backend mutation privileges.

**Implementation skeleton**

1. host launches shell with Seat/session identity and a scoped read/control token;
2. shell verifies the host handshake and fetches a full snapshot;
3. shell subscribes to bounded runtime events and resnapshots on gaps;
4. shell creates top-level surfaces only inside its Seat display group;
5. shell sends typed launch/activate/stop requests to host;
6. shell shutdown is independent of active target processes;
7. host can restart/relaunch the shell.

**Invariants**

- shell cannot mutate another Seat;
- shell never launches target processes directly;
- host state is authoritative;
- no global shell replacement/registry change;
- shell crash leaves runtime active and recoverable;
- unassigned display does not receive a shell.

**Automated tests**

- two shell processes with disjoint Seat snapshots;
- spoofed Seat/session/token;
- event gap/resnapshot;
- host disconnect/reconnect;
- shell crash/relaunch;
- missing display/degraded state.

**Done when**

Two shell processes render independent Seat status and controls without owning runtime state.

**Suggested commit**

`feat: implement P7-SHELL-01 per-Seat shell process`

---

## P7-LAUNCH-01 — Seat launcher and pinned applications

**State:** BLOCKED

**Goal**

Show profile-approved applications and session actions within each Seat shell.

**Depends on**

- P7-SHELL-01
- P6 application catalog/profile manager

**Create/modify**

- `include/hydra/seat_launcher_model.hpp`
- `src/seat_launcher_model.cpp`
- shell launcher UI;
- tests.

**Features**

- pinned target/session profiles;
- recent applications owned by the Seat;
- support/risk badge from compatibility matrix;
- missing provider/backend/device warning;
- launch progress and cancel;
- no arbitrary executable browsing in normal mode;
- expert/custom profile link through the main UI.

**Invariants**

- launch request references immutable profile/plan IDs;
- unsupported target cannot be launched by UI-only override;
- another Seat's recent/pinned entries remain hidden unless shared policy says otherwise;
- provider credentials never enter shell state;
- icons/metadata are treated as untrusted bounded input.

**Automated tests**

- filtering by Seat/profile;
- stale/missing pinned entry;
- launch progress/error/cancel;
- compatibility status update;
- malicious title/icon metadata.

**Done when**

Each Seat can launch its validated profiles from its own display group.

**Suggested commit**

`feat: implement P7-LAUNCH-01 Seat launcher`

---

## P7-TASK-01 — Seat-owned window/task surface

**State:** BLOCKED

**Goal**

Provide a taskbar-like list and activation/minimize/close controls for Seat-owned windows only.

**Depends on**

- P7-SHELL-01
- P4-WIN-01/P4-WIN-02

**Create/modify**

- `include/hydra/seat_task_model.hpp`
- `src/seat_task_model.cpp`
- shell task surface;
- host window command validation;
- tests.

**Behavior**

- group by application/process profile;
- display owned primary windows and profile-approved dialogs;
- activate/minimize/restore/close through host after ownership revalidation;
- show unresponsive/exited/degraded state;
- optional per-display task surface within one Seat;
- never enumerate unrelated windows directly in UI.

**Invariants**

- command includes Seat/session/window/process identity generation;
- stale/reused HWND rejected;
- shell cannot close/move another Seat or system window;
- activation request respects Phase 3 virtual focus policy;
- no infinite focus war.

**Automated tests**

- two Seat window sets;
- stale/destroyed/recreated window;
- launcher/game/dialog classification;
- host rejection;
- shell restart/resnapshot.

**Done when**

Each shell controls only its owned windows and accurately reflects lifecycle.

**Suggested commit**

`feat: implement P7-TASK-01 Seat task surface`

---

## P7-DESK-01 — Seat wallpaper, desktop zones, and display-aware surfaces

**State:** BLOCKED

**Goal**

Give each multi-monitor Seat a coherent background and layout surface without changing the global Windows desktop wallpaper or shell.

**Depends on**

- P7-SHELL-01
- P4-DIS-02/P4-DIS-03

**Create/modify**

- `include/hydra/seat_desktop_layout.hpp`
- `src/seat_desktop_layout.cpp`
- shell background/zone surfaces;
- profile fields and tests.

**Features**

- per-Seat wallpaper/color/slideshow policy;
- one logical canvas across Seat displays;
- per-display and cross-display launcher/widget zones;
- primary-display task/launcher placement;
- safe area/reserved zone model;
- topology/DPI hot-plug relayout;
- no global desktop icon migration in the initial scope.

**Invariants**

- shell surfaces remain behind Seat-owned target windows unless explicitly overlayed;
- no surface crosses into another Seat display group;
- global Windows wallpaper/settings remain unchanged;
- missing display degrades/relayouts deterministically;
- image inputs are bounded/decoded safely.

**Automated/manual tests**

- dual-monitor Seat + single-monitor Seat;
- mixed DPI/orientation;
- secondary disconnect/reconnect;
- wallpaper decode failure;
- global wallpaper unchanged.

**Done when**

Each Seat visibly has its own multi-monitor environment without replacing Explorer.

**Suggested commit**

`feat: implement P7-DESK-01 Seat desktop surfaces`

---

## P7-CURSOR-01 — Software cursor rendering per Seat

**State:** BLOCKED

**Goal**

Render one independent software cursor per Seat on shell/overlay surfaces using the process-local virtual cursor state.

**Depends on**

- P3-API-03/P3 virtual cursor contract
- P4-DIS-02
- P7-SHELL-01

**Create/modify**

- `include/hydra/seat_cursor_model.hpp`
- `src/seat_cursor_model.cpp`
- cursor overlay renderer;
- profile/theme/visibility fields;
- tests/benchmarks.

**Implementation skeleton**

1. consume virtual cursor state from host/adapter metrics;
2. convert Seat-local coordinates to physical output surfaces;
3. render low-latency transparent cursor overlay;
4. confine to Seat display union and handle display gaps;
5. switch cursor shape/visibility from controlled adapter/profile where supported;
6. hide/leave global cursor only under a tested profile policy;
7. teardown overlays on stop/crash.

**Invariants**

- one Seat cursor never appears on another Seat display;
- overlay cannot steal focus/input;
- global cursor state is unchanged unless an explicit proven backend controls it;
- rendering queue bounded and latest-state preferred;
- latency measured.

**Automated/manual tests**

- two moving cursors simultaneously;
- mixed DPI and monitor gap;
- target window fullscreen/borderless;
- overlay crash/restart;
- latency/CPU budget;
- no click interception.

**Done when**

Two Seat cursors are independently visible within their display groups at acceptable latency.

**Suggested commit**

`feat: implement P7-CURSOR-01 Seat software cursors`

---

## P7-HOTKEY-01 — Seat-scoped shell and recovery hotkeys

**State:** BLOCKED

**Goal**

Provide deterministic Seat actions without relying on the single global Windows foreground window.

**Depends on**

- Phase 3 Seat input routing
- P7-SHELL-01
- P8 reset/watchdog contracts for emergency actions

**Actions**

- open/close Seat launcher;
- cycle Seat-owned windows;
- show Seat status overlay;
- request target stop/restart;
- pause/resume Seat input where safe;
- emergency reset chord using an independent policy/device.

**Invariants**

- hotkey evaluated from physical Seat-owned device state;
- collisions with game input are profile-configurable;
- emergency chord cannot be disabled by target process;
- action request revalidates Seat/runtime ownership;
- no global low-level hook unless a later explicit backend is approved.

**Tests**

- two Seats trigger different shell actions simultaneously;
- held/repeat/modifier semantics;
- target consumes same keys;
- shared/ambiguous device fails closed;
- emergency reset works when shell/UI target is hung.

**Done when**

Seat controls are usable without changing another Seat's focus or input.

**Suggested commit**

`feat: implement P7-HOTKEY-01 Seat hotkeys`

---

## P7-NOTIFY-01 — Seat-scoped runtime notifications

**State:** BLOCKED

**Goal**

Display HydraSeat/session/application notifications only on the owning Seat surfaces where ownership is known.

**Depends on**

- P7-SHELL-01
- P4 process/window ownership

**Scope**

Initial notifications are HydraSeat-generated:

- launch/start/stop/progress;
- device/display/audio disconnect;
- degraded/recovery required;
- compatibility warning;
- target exit/crash;
- update availability later.

Third-party Windows toast interception is not in initial scope.

**Invariants**

- notification includes Seat/session/correlation ID;
- no notification leaks another Seat's private title/path by default;
- bounded queue/expiry;
- critical recovery notification cannot be silently suppressed;
- shell restart can resnapshot active critical notifications.

**Automated tests**

- Seat filtering;
- duplicate/coalesce/expiry;
- shell disconnect/reconnect;
- critical versus informational policy;
- redaction.

**Done when**

Runtime events appear only in the correct Seat shell and main control UI.

**Suggested commit**

`feat: implement P7-NOTIFY-01 Seat notifications`

---

## P7-CLIP-01 — Optional Seat clipboard policy

**State:** BLOCKED / OPTIONAL

**Goal**

Provide explicit clipboard behavior instead of pretending the single Windows clipboard is automatically isolated.

**Depends on**

- P7-SHELL-01
- a controlled input/API shim capable of clipboard interposition, or a shell-only scoped workflow

**Policy options**

- shared Windows clipboard (default, clearly disclosed);
- shell-managed per-Seat text clipboard for HydraSeat-aware apps;
- no clipboard hotkeys routed to target;
- future controlled process shim for specific clipboard APIs.

**Rules**

- no claim of universal clipboard isolation without API coverage;
- formats and size bounded;
- sensitive data not logged;
- cross-Seat transfer requires explicit action;
- failure falls back only according to profile, not silently.

**Done when**

One limited policy is implemented/tested or the packet is formally deferred with truthful UI disclosure.

**Suggested commit**

`feat: implement P7-CLIP-01 Seat clipboard policy`

---

## P7-A11Y-01 — DPI, accessibility, input modality, and localization readiness

**State:** BLOCKED

**Goal**

Ensure the shell/control surfaces are usable across mixed-DPI displays and common accessibility needs.

**Depends on**

- primary Phase 7 UI packets

**Coverage**

- per-monitor DPI awareness and relayout;
- keyboard-only navigation within a Seat;
- screen-reader labels for main UI/shell where toolkit permits;
- high contrast and scalable text;
- color not sole status indicator;
- localization-ready strings/resource IDs;
- left-to-right/long string layout;
- controller navigation optional after controller UI policy.

**Invariants**

- no fixed-pixel assumption across outputs;
- shell remains within Seat bounds after scale change;
- emergency/reset control remains identifiable;
- diagnostic IDs remain stable while display text is localizable.

**Automated/manual tests**

- 100/125/150/200% DPI;
- mixed DPI Seat displays;
- long localized strings;
- keyboard navigation;
- high contrast;
- scale change/hot-plug.

**Done when**

Reference shell workflows pass the accessibility/DPI checklist.

**Suggested commit**

`feat: implement P7-A11Y-01 shell accessibility readiness`

---

## P7-EXT-01 — Shell extension points, not public SDK yet

**State:** BLOCKED

**Goal**

Separate shell widgets/actions from the core so Phase 9 can publish a stable SDK later.

**Depends on**

- P7-SHELL-01
- P7-LAUNCH/TASK/NOTIFY models

**Create/modify**

- internal `IShellPanel`, `IShellAction`, `IShellDataSource` interfaces;
- built-in extension registry;
- fake/built-in extension tests;
- no arbitrary third-party DLL loading yet.

**Invariants**

- extensions receive Seat-scoped read-only data and typed commands;
- built-ins cannot bypass host policy;
- lifecycle/timeout/error isolated;
- interface explicitly internal/version-unstable until Phase 9.

**Done when**

Built-in launcher/task/status panels use the same internal extension boundary.

**Suggested commit**

`refactor: implement P7-EXT-01 shell extension boundary`

---

## P7-REC-01 — Shell crash/restart and Explorer coexistence

**State:** BLOCKED

**Goal**

Prove shells are disposable UI processes and ordinary Windows remains usable.

**Depends on**

- Phase 7 core packets
- P8-WATCH-01

**Failure matrix**

- one Seat shell killed;
- both shells killed;
- main UI killed;
- host restart;
- display disconnect;
- shell rendering hang;
- logoff/shutdown;
- repeated shell relaunch.

**Acceptance**

- targets/runtime remain correct or stop by policy;
- shell relaunch resnapshots state;
- no invisible overlay steals clicks/focus;
- Explorer/global taskbar/wallpaper/settings remain usable;
- stop/reset removes all shell windows/overlays;
- no orphan process.

**Done when**

The failure matrix passes on a physical multi-display system.

**Suggested commit**

`test: implement P7-REC-01 shell recovery matrix`

---

## P7-CLOSE-01 — Phase 7 closure

**State:** BLOCKED

**Closure checklist**

- one shell per Seat, scoped host permissions;
- launcher/task/background/cursor/status work across multi-monitor Seats;
- no global Explorer replacement;
- notifications and clipboard policy truthful;
- DPI/accessibility checklist passes;
- shell crashes/restarts safely;
- user experiences distinct Seat environments while runtime remains host-owned;
- Phase 8 receives stable host/shell/watchdog/install boundaries.

**Done when**

Phase 7 is complete and Phase 8 becomes current.

**Suggested commit**

`docs: close Phase 7 Seat shell experience`
