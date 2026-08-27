# Phase 4 — Production Runtime, Independent Seat Lifecycle, and Display/Window Ownership

## Phase objective

Move the validated Phase 3 foundations into an authoritative background runtime and establish the production ownership boundaries needed by the two-Seat v1 product.

Phase 4 does **not** complete Phase 3 on behalf of the still-pending physical input gate. Selected Phase 4 foundation packets were intentionally implemented early because their contracts are useful and independently testable.

The v1 product contract comes from `docs/PRODUCT_V1.md` and D-039 through D-050:

- at most two active Seats;
- Seat is hardware, not Player/Game identity;
- one Seat can be Playing while the other is Idle;
- one Seat may stop/change games without stopping the other;
- an idle Seat remains under a minimal HydraSeat launcher/wait state while the other Seat is active;
- a whole-machine `Return to Windows` is a separate verified rollback transaction;
- physical displays are the required v1 path; virtual displays are optional/deferred.

## Phase exit gate

Phase 4 closes only when:

1. `hydra_host.exe` remains authoritative when the visible UI closes/reopens;
2. UI/CLI/watchdog use the same versioned state/control boundary;
3. exactly two v1 Seats can be represented and more than two active Seats fail closed;
4. Seat-owned process trees and windows never adopt unrelated processes/windows;
5. physical display groups and window placement are deterministic and recoverable;
6. one Seat can stop its own game and become Idle while the other remains Playing;
7. the idle Seat can select another Player/game and start again without restarting the other Seat;
8. both Seats ending, or explicit `Return to Windows`, reaches ordinary Windows only after rollback verification;
9. display removal/reconnect and target restart preserve ownership boundaries;
10. Windows x64/x86 regression matrices pass;
11. declared physical display/manual gates pass;
12. a dedicated Phase-close verification finds no false completion or rollback gap.

## Dependency overview

```text
P4-RUN-01 -> P4-IPC-01
     |
     +-> P4-PROC-01 -> P4-WIN-01
     |
     +-> P4-DIS-01 -> P4-DIS-02 -> P4-WIN-02 -> P4-POL-01 -> P4-DIS-03
     |
     +-> P4-CTRL-01 -> P4-CTRL-02

P4-IPC-01 + P4-PROC-01 + P4-WIN-01 + P4-CTRL-02 -> P4-SEAT-01
P4-SEAT-01 + P4-DIS-03 -> P4-REC-01 -> P4-CLOSE-01

P4-DIS-01 -> P4-VID-01 -> P4-VID-02 -> P4-IDD-01   (optional/deferred v1 path)
```

---

## P4-RUN-01 — Production `hydra_host.exe` runtime skeleton

**State:** VALIDATED

**Goal**

Make a headless per-user process the authoritative owner of runtime state rather than tying active state to the configuration UI.

**Depends on**

- P3-STATE-01
- P8-WATCH-01

**Implemented surface**

- `include/hydra/runtime_state.hpp`
- `include/hydra/runtime_host.hpp`
- `src/runtime_state.cpp`
- `src/runtime_host.cpp`
- `src/host_main.cpp`
- `hydra_host.exe`
- runtime state-machine and process self-tests

**Evidence**

Fork PR #26, exact head `0fdaf80`, run `33062975789` passed Windows x64, Win32/x86, Gate C cross-architecture, and P3-E regression jobs. The packet implementation commit is `c139354`.

**Invariants**

- GUI lifetime does not own runtime lifetime;
- host and session lifecycle are explicit typed state;
- one mutation transaction at a time;
- start/stop/reset are idempotent where declared;
- failed startup records/executes rollback rather than pretending Active;
- profile parsing alone never claims active isolation.

**Done when**

A headless host can load a validated two-Seat profile, exercise fake backend activation/rollback, publish a stable snapshot, and exit cleanly without the GUI owning authority.

---

## P4-IPC-01 — UI/CLI/watchdog control and state protocol

**State:** VALIDATED

**Goal**

Expose one bounded/versioned local control protocol so UI and CLI observe/mutate the same authoritative host state.

**Depends on**

- P4-RUN-01

**Implemented surface**

- `include/hydra/host_protocol.hpp`
- `include/hydra/host_transport.hpp`
- `src/host_protocol.cpp`
- `src/host_transport.cpp`
- `hydra_hostctl.exe`
- protocol and real host-process IPC tests

**Evidence**

Fork PR #26 run `33062975789` passed exact head `0fdaf80` across native x64/x86 and existing cross-architecture regressions. Packet implementation commit: `1813d39`.

**Invariants**

- protocol is bounded/versioned and rejects malformed/future/stale input;
- same-user local boundary is explicit;
- clients resnapshot authoritative state rather than inferring success from timeouts;
- no command transports credentials or raw typed input;
- UI is a client, not runtime authority.

**Done when**

A separate client can connect, read snapshots, issue declared host commands, survive reconnect/host restart semantics, and receive explicit failures without corrupting host state.

---

## P4-PROC-01 — Seat process tree and Job Object ownership

**State:** VALIDATED

**Goal**

Represent a launched game/launcher tree as temporary Seat-owned runtime state and clean it without touching unrelated processes.

**Depends on**

- P4-RUN-01

**Implemented surface**

- process identity/group/launcher library
- controlled child tree executable
- PID + creation-identity validation
- Job Object ownership where compatible
- process-group tests

**Evidence**

Fork PR #26 run `33062975789` passed exact head `0fdaf80` on x64/x86. Packet implementation commit: `e0e7334`.

**Invariants**

- a live process belongs to at most one Seat;
- Seat configuration does not persist PID/process ownership;
- PID reuse cannot silently transfer ownership;
- unrelated/unowned processes are never terminated;
- weaker non-Job capability is explicit rather than silently assumed equivalent.

**Done when**

Controlled parent/child/grandchild trees are assigned to one Seat, observed through lifecycle changes, and torn down deterministically with zero owned test-process orphans.

---

## P4-WIN-01 — Window ownership tracker

**State:** VALIDATED

**Goal**

Attribute controlled top-level/owned windows to validated Seat process ownership without adopting unrelated windows.

**Depends on**

- P4-PROC-01

**Implemented surface**

- `include/hydra/window_tracker.hpp`
- `src/window_tracker.cpp`
- controlled test window application
- stale HWND/process validation and tests

**Evidence**

Fork PR #26 run `33062975789` passed exact head `0fdaf80` on Windows x64/x86. Packet implementation commit: `0fdaf80`.

**Invariants**

- process ownership precedes window ownership;
- HWND reuse is rejected with process identity validation;
- unrelated/system/unowned windows are not adopted;
- callbacks do bounded event work only;
- destroy/removal is idempotent.

**Done when**

Controlled windows are deterministically attributed to the correct Seat process group and unrelated windows remain untouched through create/show/hide/destroy cycles.

---

## P4-DIS-01 — Display topology inventory and stable output identity

**State:** CODE_COMPLETE

**Goal**

Build a read-only authoritative physical-display topology with stable output identity independent from enumeration order.

**Depends on**

- P1-HW-01

**Implemented surface**

- `include/hydra/display_identity.hpp`
- `include/hydra/display_topology.hpp`
- `src/display_topology.cpp`
- `src/display_diag_main.cpp`
- `DisplayTopologyTests`

**Local automated evidence — 2026-08-27**

- MSVC x64 full build passes;
- MSVC x64 CTest passes 70/70;
- MSVC Win32/x86 full build passes;
- MSVC Win32/x86 CTest passes 70/70;
- topology unit/native read-only paths are included in those suites.

**Manual acceptance**

Record the user's real physical monitor topology, stable identities, reconnect behavior, and intended Seat 1/Seat 2 grouping. This remains pending.

**Invariants**

- persistent identity does not use index/friendly name alone;
- topology races retry only a bounded number of times;
- missing optional metadata does not erase an output;
- virtual classification is reported as confidence, not certainty;
- this packet does not mutate display modes.

**Done when**

Automated topology tests pass and real physical-display identities/grouping have been recorded without relying on enumeration order.

---

## P4-DIS-02 — Seat display groups and coordinate transforms

**State:** CODE_COMPLETE

**Goal**

Resolve physical outputs into at most two v1 Seat display groups and provide deterministic global/Seat-local coordinate transforms.

**Depends on**

- P4-DIS-01
- P2-SEAT-01

**Implemented surface**

- `include/hydra/seat_display_layout.hpp`
- `src/seat_display_layout.cpp`
- `SeatDisplayLayoutTests`

**Local automated evidence — 2026-08-27**

x64 and Win32/x86 full CTest each pass 70/70.

**Invariants**

- multiple displays per Seat are valid;
- v1 activation rejects more than two active Seat groups;
- Seat primary need not equal Windows global primary;
- overlapping exclusive assignment fails closed;
- transform round-trips are tested across negative coordinates/DPI/orientation;
- missing required output is explicit.

**Done when**

The same stable `SeatDisplayGroup` contract can be consumed by window placement and Management/Seat UI placement on x64/x86, with physical acceptance still tracked by the owning display gates.

---

## P4-WIN-02 — Window placement, mode, and restore engine

**State:** CODE_COMPLETE

**Goal**

Compute/apply bounded policies that keep owned game/launcher windows inside their Seat display group and restore captured state safely.

**Depends on**

- P4-WIN-01
- P4-DIS-02

**Implemented surface**

- `include/hydra/window_policy.hpp`
- `include/hydra/window_placement.hpp`
- `src/window_policy.cpp`
- `src/window_placement.cpp`
- `WindowPolicyTests`

**Local automated evidence — 2026-08-27**

MSVC x64 and Win32/x86 full build + 70/70 CTest pass after the complete local Phase 4 foundation was linked into the shared GUI test target.

**Manual acceptance**

Two real physical display groups, game-like borderless/fullscreen transitions, DPI changes, alt-tab, and restore behavior remain pending.

**Invariants**

- never move an unowned window;
- no unbounded correction loop;
- observed final placement is distinct from requested placement;
- rollback uses captured previous state when the exact owned window still exists;
- exclusive fullscreen is capability-specific, never assumed.

**Done when**

Automated ownership/placement tests pass and the declared real physical display/window acceptance is recorded with no unrelated-window movement.

---

## P4-POL-01 — Seat window/display runtime policy coordinator

**State:** CODE_COMPLETE

**Goal**

Serialize process/window/display events into deterministic desired-versus-observed Seat policy rather than module-specific ad hoc mutation.

**Depends on**

- P4-WIN-02
- P4-DIS-02

**Implemented surface**

- `include/hydra/seat_runtime_policy.hpp`
- `src/seat_runtime_policy.cpp`
- `SeatRuntimePolicyTests`

**Local automated evidence — 2026-08-27**

MSVC x64 and Win32/x86 full CTest each pass 70/70.

**Invariants**

- serialized deterministic event order;
- duplicate events do not duplicate mutation;
- callbacks do not directly move windows;
- retries are bounded and only for retryable outcomes;
- failed invariants become visible degraded/recovery state.

**Done when**

Deterministic event replay produces identical actions/final policy state and failures cannot silently mutate unrelated resources.

---

## P4-DIS-03 — Display hot-plug, degradation, and rollback

**State:** CODE_COMPLETE

**Goal**

Handle assigned display disappearance/reappearance without silently moving a Seat onto another Seat's display or corrupting the ordinary desktop.

**Depends on**

- P4-DIS-02
- P4-POL-01

**Implemented surface**

- `include/hydra/display_recovery.hpp`
- `src/display_recovery.cpp`
- `DisplayRecoveryTests`

**Local automated evidence — 2026-08-27**

MSVC x64 and Win32/x86 full CTest each pass 70/70.

**Manual acceptance**

Physical unplug/replug, disable/enable, coordinate/DPI change, and stable-identity reappearance across the user's two Seat display groups remain pending.

**Invariants**

- topology bursts are bounded/debounced;
- missing optional secondary is degraded explicitly;
- missing required primary never silently steals another Seat's output;
- reconnect uses stable identity, not index;
- rollback preserves captured ordinary Windows state where possible.

**Done when**

Automated recovery tests plus physical unplug/replug acceptance demonstrate deterministic, visible, recoverable behavior.

---

## P4-VID-01 — Virtual display backend interface

**State:** DEFERRED

**Goal**

Retain a future capability boundary for virtual displays without making them part of the v1 physical-monitor critical path.

**Depends on**

- P4-DIS-01

**v1 decision**

Physical local displays are sufficient for the intended household two-Seat product. A virtual-display API may be implemented later only if a real v1/user scenario needs it and the extra driver/install/recovery burden is justified.

**Done when**

Deferred for v1. If reactivated, it requires a fake backend, bounded lifecycle, trust/version checks, and proof that physical-only operation remains unaffected.

---

## P4-VID-02 — External virtual display adapter integration

**State:** DEFERRED

**Goal**

Potential future integration of one lawful user-supplied/licensed virtual-display implementation.

**Depends on**

- P4-VID-01
- P8-TRUST-01

**Done when**

Deferred for v1 unless a concrete product requirement reactivates it with install/create/destroy/reboot/rollback evidence.

---

## P4-IDD-01 — Custom IddCx/IDD feasibility gate

**State:** DEFERRED

**Goal**

Avoid committing a one-developer v1 project to a custom display driver unless measured product need justifies signing, WDK, update, crash, and maintenance cost.

**Depends on**

- P4-VID-01

**Done when**

Deferred for v1. A future `ADOPT`, `DEFER`, or `REJECT` decision must be based on measurements and support burden rather than architectural ambition.

---

## P4-CTRL-01 — Management Seat control console placement and permissions

**State:** CODE_COMPLETE

**Goal**

Make `HydraSeat.exe` an on-demand game-first Management UI whose placement/permissions are derived from the authoritative two-Seat configuration/runtime state.

**Depends on**

- P4-RUN-01
- P4-IPC-01
- P4-DIS-02

**Implemented surface**

- `include/hydra/management_seat.hpp`
- `src/management_seat.cpp`
- `include/hydra/control_surface_model.hpp`
- `src/control_surface_model.cpp`
- Win32 GUI host-client integration
- `ManagementSeatTests`
- `ControlSurfaceModelTests`

**Local automated evidence — 2026-08-27**

MSVC x64 and Win32/x86 full build + 70/70 CTest pass. A discovered linker defect in `hydra_tests` was fixed by linking the same control/display dependency surface used by `HydraSeat.exe`.

**Invariants**

- UI close/crash does not stop runtime;
- runtime snapshot is authoritative;
- Management Seat defaults to Seat 1 but remains explicit configuration;
- visible fallback never silently changes Seat hardware ownership;
- normal UI presents user concepts rather than backend/protocol jargon;
- whole-machine mutation permission is explicit.

**Done when**

The UI can reconnect to a running host, resolve Management placement/permissions deterministically, and its model remains consistent on x64/x86. Real display placement acceptance remains part of the Phase 4 physical gate.

---

## P4-CTRL-02 — Return-to-Windows and safe reconfiguration workflow

**State:** CODE_COMPLETE

**Goal**

Separate UI close, Seat/game stop, reconfiguration, host exit, and whole-machine `Return to Windows` into explicit state transitions.

**Depends on**

- P4-CTRL-01
- P4-IPC-01

**Implemented surface**

- `include/hydra/session_control_transition.hpp`
- `src/session_control_transition.cpp`
- host/profile control protocol additions
- GUI control integration
- `SessionControlTransitionTests`

**Local automated evidence — 2026-08-27**

MSVC x64 and Win32/x86 full build + 70/70 CTest pass.

**Important limitation**

The current transition foundation is still centered on several whole-session operations. It does **not** yet prove the independent per-Seat game lifecycle required by D-042. That gap is deliberately isolated into P4-SEAT-01 rather than falsely declaring Phase 4 complete.

**Invariants**

- reconfiguration edits do not occur while risky active state remains unrolled-back;
- `Return to Windows` is not UI close;
- host exit is not normal Seat/game stop;
- failed rollback enters RecoveryRequired rather than enabling configuration over unknown state;
- saved profile replacement is transactional/validated.

**Done when**

Whole-machine stop/reconfigure transitions are deterministic on x64/x86 and the remaining per-Seat lifecycle is delegated explicitly to P4-SEAT-01.

---

## P4-SEAT-01 — Independent per-Seat game lifecycle and v1 two-Seat limit

**State:** BLOCKED

**Goal**

Implement the v1 product behavior where each Seat owns an independent temporary game lifecycle while sharing one recoverable HydraSeat whole-machine runtime.

**Depends on**

- P4-IPC-01
- P4-PROC-01
- P4-WIN-01
- P4-CTRL-02

**Required model**

Introduce explicit per-Seat runtime state separate from whole-machine session state, for example:

```text
SeatGamePhase = Idle | Planning | Starting | Playing | Stopping | Degraded | RecoveryRequired
```

The exact type/version may differ after implementation review, but the observable contract is fixed.

**Required commands/events**

- assign/change Player for an Idle Seat;
- plan/start game on Seat 1 or Seat 2 independently;
- stop one Seat game only;
- observe normal target exit and return that Seat to Idle;
- start another game on the idle Seat without stopping the other;
- end playing for one Seat;
- when both Seats have ended, expose/execute the declared whole-machine return policy;
- reject a third active Seat in v1;
- preserve explicit emergency whole-machine reset/return path.

**Invariants**

- `Seat 1 = Playing, Seat 2 = Idle` is healthy;
- one Seat's normal game exit never becomes whole-machine Stop;
- Seat-local stop kills/cleans only exact owned process/window/input/audio/controller state for that Seat;
- global resources required by the remaining active Seat are preserved;
- an idle Seat remains under minimal HydraSeat Seat Launcher control while another Seat is active;
- more than two active Seats fail closed;
- Player/Game identity is runtime binding, not Seat persistence;
- both Seats ended or explicit Management return can trigger verified whole-machine rollback.

**Automated tests**

- Seat 1 playing / Seat 2 idle and the reverse;
- stop Seat 2 while Seat 1 process remains alive and unchanged;
- restart Seat 2 with another synthetic/controlled game;
- one Seat target exits unexpectedly but cleanly;
- one Seat fault becomes Seat degraded/recovery without killing healthy Seat unless a shared global invariant is lost;
- simultaneous commands/correlation/duplicate commands;
- third-Seat activation rejected;
- both Seats end -> correct global rollback trigger;
- host/UI reconnect retains both Seat states.

**Done when**

Two controlled Seat process trees can independently start/stop/restart while the other remains active, a third active Seat is rejected, and whole-machine rollback occurs only under the declared both-ended/explicit-return/recovery rules.

---

## P4-REC-01 — Runtime/window/display/Seat-lifecycle crash and restart matrix

**State:** BLOCKED

**Goal**

Prove the complete Phase 4 runtime can recover from host/UI/Seat target/window/display faults without crossing ownership or leaving ordinary Windows unusable.

**Depends on**

- P4-SEAT-01
- P4-DIS-03
- P8-WATCH-01
- P8-JOURNAL-01
- P8-RESET-01

**Required evidence**

- UI crash/reopen while both Seats active;
- one Seat target crash while other Seat continues where global safety permits;
- host crash -> watchdog/reset path;
- display loss/reconnect;
- stale PID/HWND rejection;
- repeated start/Seat-stop/restart/global-return cycles;
- sign-out/restart regression of the already validated recovery ordering;
- zero owned orphan process/window helpers;
- ordinary Windows postcondition after forced global recovery.

**Done when**

The complete Phase 4 runtime fault matrix passes x64/x86 and required physical/manual display/session-end acceptance with exact ownership and rollback evidence.

---

## P4-CLOSE-01 — Phase 4 closure

**State:** BLOCKED

**Goal**

Perform the independent Phase-close verification required by D-038.

**Depends on**

- P4-REC-01
- P4-CTRL-02
- P4-SEAT-01
- P4-DIS-03

**Verify**

- all non-deferred required packet implementations and tests;
- two-Seat limit and concept separation;
- independent Seat lifecycle;
- process/window/display ownership;
- UI/host authority separation;
- whole-machine return/reconfigure semantics;
- x64/x86 regressions;
- physical display gates;
- recovery/no-orphan postconditions;
- docs/README/product claims against actual implementation.

**Done when**

A dedicated review records a passing Phase 4 closeout in `STATUS.md`; otherwise the owning packet is reopened and Phase 4 remains incomplete.