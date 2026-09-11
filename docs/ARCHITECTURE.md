# HydraSeat architecture

HydraSeat turns one Windows gaming PC into at most two local gaming Seats. Both Seats remain inside the same interactive Windows session. The architecture is deliberately game-focused: it is not a replacement Windows shell, a VM manager, or a general multi-user desktop system.

`PRODUCT_V1.md` defines the user-facing v1 scope. This document defines the implementation boundaries we want contributors to build around.

## Design goals

1. **Independent Seats.** Seat 1 stopping or changing a game must not tear down a healthy Seat 2.
2. **One runtime authority.** Mutable session and hardware state has one owner: `hydra_host.exe`.
3. **Recoverable Windows changes.** Every risky mutation has a rollback path and a way to verify that ordinary Windows behavior was restored.
4. **Stable hardware identity.** Seat ownership is based on stable device/display identity, never enumeration order or friendly names alone.
5. **Fail closed.** If HydraSeat cannot prove that a requested isolation or compatibility path is supported, launch is rejected instead of silently degrading isolation.
6. **Boring internal structure.** Prefer concrete code and clear ownership over factories, managers, registries, policies, and adapters that exist only for hypothetical future flexibility.

## Non-goals

HydraSeat v1 does not provide independent Windows logons, virtual machines, RDP/streaming sessions, arbitrary per-Seat desktop applications, or security/protection bypasses. It does not bypass anti-cheat, DRM, protected processes, launcher/account restrictions, deliberate single-instance restrictions, or security products.

## Process model

```text
HydraSeat.exe
management UI
     |
     | bounded local IPC
     v
hydra_host.exe
runtime authority
     |
     +-- Seat 1 runtime ---- game / launcher process tree
     |
     +-- Seat 2 runtime ---- game / launcher process tree
     |
     +-- recovery journal / rollback ownership

hydra_seat_ui.exe   optional minimal Seat-local controls
hydra_watchdog.exe  crash/recovery supervision
hydra_reset.exe     independent emergency cleanup
```

The UIs are clients. Closing a UI must not implicitly destroy a running Seat. The host owns the session, the two Seat runtimes, process trees, and any temporary Windows mutations.

## Domain model

The persisted concepts stay separate:

- `SeatConfig` describes physical resources assigned to a station.
- `PlayerProfile` stores lightweight local player preferences.
- `GameRecord` identifies an installed/discovered game.
- `TwoPlayerSetup` describes an optional same-game/two-instance compatibility recipe.
- `LaunchPlan` is immutable data produced after validation for one launch.
- `RuntimeSession` is temporary host-owned state binding Seats, Players, games, and live resources.

PIDs, HWNDs, Raw Input handles, process handles, and other transient Windows identifiers are runtime state and are never stable profile identity.

## Runtime ownership

The desired runtime shape is intentionally small:

```text
SessionController
    owns RuntimeSession
    owns SeatRuntime[0..2]
    owns shared rollback/recovery state

SeatRuntime
    owns one Seat's launch plan
    owns its process tree
    owns its window/display mutations
    owns its input/controller/audio activation
    owns Seat-local rollback
```

`SessionController` coordinates operations that really are machine-wide. `SeatRuntime` owns operations that can be isolated to one Seat. State must not be mirrored in parallel lifecycle objects unless an external protocol genuinely requires a projection.

The current code has `RuntimeHost`, `SeatGameLifecycle`, launch-plan/resource objects, and production activation components that overlap with this responsibility split. Migration should collapse those layers incrementally rather than replacing them with another framework.

## Launch lifecycle

A launch follows one understandable transaction:

```text
validate request
    -> compile immutable LaunchPlan
    -> reserve/recovery journal
    -> launch process tree
    -> identify owned game window
    -> apply required display/input/controller/audio state
    -> Playing
```

Stopping or a failed start walks owned state back in reverse order. A rollback is successful only when the corresponding safe-state checks also succeed. If cleanup cannot be verified, the Seat/session remains visibly recovery-required.

Compatibility hooks may run at real lifecycle boundaries when a game needs them, but compatibility code must not become a second runtime state machine.

## Interfaces and test seams

An interface is justified when at least one of these is true:

- it wraps an operating-system boundary that tests must replace;
- there are multiple meaningful implementations in the product today;
- it is a stable cross-process/plugin ABI boundary.

Internal object construction, one-off orchestration, and speculative future backends do not justify an interface by themselves. Prefer a concrete owner with injected OS services over factory-of-factory graphs.

Cross-process messages and persisted formats remain explicit, bounded, versioned, and pointer-free. Those boundaries are intentionally stricter than ordinary in-process C++ code.

## Platform boundary

Windows-specific work belongs behind a small platform layer. Examples include:

- process creation and Job Objects;
- window discovery/placement;
- display topology;
- Raw Input and HID identity;
- controller/XInput handling;
- Core Audio endpoint/session routing;
- privilege/UAC operations.

The domain/runtime layer should traffic in HydraSeat identities and plans rather than leaking raw Win32 handles through unrelated modules.

## Providers and compatibility

Steam and custom executable discovery produce normal game records and launch inputs. Provider code does not own a Seat runtime.

Game-specific compatibility is optional. Gate-C/shim work, materialized instances, and community compatibility data should live at the edge of the runtime and be invoked only when the selected game's validated requirements call for them. They must not be prerequisites for understanding the normal launch path.

## Recovery

Recovery is part of normal architecture, not release tooling. Before an externally visible risky mutation, HydraSeat records enough owned state to undo that mutation. The watchdog/reset path must be able to restore HydraSeat-owned changes without terminating unrelated user processes or claiming success it cannot verify.

## Target source layout

The repository should converge toward product responsibilities instead of roadmap phases:

```text
src/
  app/                 HydraSeat.exe and presentation glue
  host/                host process, IPC, SessionController
  runtime/             SeatRuntime, launch plans, process/window ownership
  platform/windows/    Win32 device/display/input/audio/controller services
  providers/           Steam and custom executable discovery/launch metadata
  compatibility/       optional game-specific adapters and Gate-C work
  recovery/            journal, watchdog, reset and rollback primitives

tools/                 diagnostics, acceptance and release utilities
tests/                 focused unit/integration tests
include/hydra/          small reusable/public product API only
```

Most implementation-only headers should eventually move next to their owning source rather than remaining in `include/hydra/`.

## Target build shape

The current build graph is much more fragmented than the product. We should converge from one static library per small implementation unit toward a handful of meaningful libraries, for example:

- `hydra_core` — domain values, profiles, game/provider-neutral plans;
- `hydra_runtime` — session/Seat lifecycle and process ownership;
- `hydra_windows` — Windows platform services;
- `hydra_compatibility` — optional compatibility machinery;
- `hydra_ui` — UI models/shared presentation support.

Executables such as `HydraSeat.exe`, `hydra_host.exe`, `hydra_watchdog.exe`, and diagnostics link those components. Tests should usually link the component they test instead of causing a new production library to exist for every source file.

The exact final number of targets is not important. The rule is that a build target represents a real ownership/dependency boundary, not an implementation packet.

## Dependency direction

Preferred direction:

```text
app / tools
    -> host
    -> runtime
    -> core

runtime -> windows abstractions
runtime -> compatibility (only through explicit optional activation)
providers -> core
windows -> core types only where needed
```

Avoid cycles and avoid a generic `production` layer that imports every subsystem. Composition belongs near executable startup or the host owner.

## Small-PR migration plan

The large experimental implementation should not be proposed upstream as one architectural rewrite. Preserve working behavior while moving in reviewable steps:

1. **Repository cleanup.** Remove development-agent artifacts and stale roadmap authority; keep product/build documentation concise.
2. **Build cleanup.** Split the root CMake file and merge targets that do not represent real component boundaries without moving behavior.
3. **Runtime ownership.** Consolidate overlapping host/Seat lifecycle and launch-resource ownership around `SessionController`/`SeatRuntime` responsibilities.
4. **Windows platform boundary.** Group process/window/display/input/controller/audio services and keep Win32 details out of domain code.
5. **Compatibility isolation.** Move Gate-C and game-specific materialization behind the normal runtime path instead of beside it.
6. **UI/provider cleanup.** Keep game discovery and presentation simple clients of the host/runtime contracts.
7. **Release/recovery validation.** Retain the valuable tests and recovery guarantees, but move acceptance/release orchestration out of product architecture.

Each step should compile and pass its focused regression tests before the next one begins. Compatibility behavior and recovery guarantees should not be weakened merely to reduce code size.
