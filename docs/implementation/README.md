# HydraSeat Master Implementation Roadmap

## 1. Purpose

This directory is the packet-level implementation source of truth for HydraSeat from the remaining Phase 3 work through a usable v1 release.

The canonical product contract is [`../PRODUCT_V1.md`](../PRODUCT_V1.md). A packet may refine implementation detail, but it must not silently widen that product scope.

HydraSeat v1 exists for one concrete reason:

> Modern gaming PCs often have useful CPU/GPU/memory/I/O headroom left while one person plays. HydraSeat aims to let two people use that spare capacity as two local Windows gaming Seats instead of requiring a second complete desktop solely for simultaneous local gaming.

v1 therefore targets **exactly two active Seats**, not a generic N-Seat platform.

## 2. Product concepts agents must keep separate

```text
Seat
  physical station hardware only

Player
  lightweight person profile independent from Seat

Game
  installed/discovered title independent from Seat and Player

Two-player setup
  optional same-game/two-instance compatibility recipe

Runtime Session
  temporary Seat + Player + Game bindings
```

A Seat does not permanently own a person, game, account, save, process, or window.

Normal users should think in `Game`, `Seat`, `Player`, and `Play`. Internal capability/backend/profile/protocol details remain in Diagnostics or Expert paths.

## 3. v1 scope rules

Every packet must preserve these rules unless the user explicitly changes the product spec:

- at most two active Seats in v1;
- one Windows interactive session is the normal path; no required VM/RDP/streaming;
- game-only product scope;
- minimal per-Seat idle launcher, not a full independent Windows desktop shell;
- one Seat can stop/change games while the other continues;
- saved Seat hardware may be incomplete and first-run setup may be skipped/deferred;
- launch preflight checks only what the selected game actually requires;
- installed-game discovery is automatic where supported with manual EXE fallback;
- same-game two-instance setup has automatic and guided manual paths;
- HydraSeat never bypasses anti-cheat, DRM, protected processes, account/launcher policy, credentials, or deliberate single-instance restrictions;
- protected-title experiments require explicit warning and never imply anti-cheat safety;
- compatibility is evidence-driven, not a maintainer certification badge;
- compatibility collection is local-first; community submission is explicit opt-in and redacted/previewable;
- core operation is offline-first;
- compatibility/setup catalog updates are separate from executable/runtime/driver updates;
- executable/runtime/driver updates require clear user approval;
- least privilege is mandatory;
- a real Windows installer/repair/uninstaller is part of v1;
- project license/contribution terms must be resolved before the release is described as open source.

See [`DECISIONS.md`](DECISIONS.md), especially D-039 through D-050.

## 4. Source-of-truth order

When documents disagree:

1. the user's current explicit instruction;
2. `.agents/AGENTS.md`;
3. `docs/PRODUCT_V1.md`;
4. [`DECISIONS.md`](DECISIONS.md);
5. this master roadmap and [`STATUS.md`](STATUS.md);
6. the active packet in its `PHASE*.md` file;
7. `docs/ARCHITECTURE.md` and specialized test/design documents;
8. existing code/tests;
9. older issue text/comments/historical notes.

Do not guess through a material conflict. Apply the higher-precedence source and update stale lower-precedence documentation in scope.

## 5. Target process/module topology

```text
HydraSeat.exe
  game-first Management UI
  game library / Player selection / two Seat cards
  Seat settings / two-player setup / diagnostics / recovery
        │
        │ versioned local control/state protocol
        ▼
hydra_host.exe
  authoritative per-user runtime
  whole-machine split state + independent Seat game lifecycles
  process/window/display/input/controller/audio ownership
        │
        ├────────────── hydra_watchdog.exe
        │               independent recovery lease/rollback
        │
        ├─ Seat 1 runtime -> selected game/launcher process tree
        └─ Seat 2 runtime -> selected game/launcher process tree

hydra_seat_ui.exe
  minimal idle/start/warning/error/End Playing surface for one Seat
  not a general desktop shell

hydra_reset.exe
  emergency reset independent from normal UI/host

hydra_adapter32.dll / hydra_adapter64.dll
  optional controlled/game compatibility layer selected by exact plan

hydra_plan.exe / hydra_diag.exe
  expert diagnostics and evidence tools
```

Phase 3 lab binaries remain regression tools until equivalent production diagnostics exist.

## 6. Two lifecycle levels

### 6.1 Whole-machine runtime state

The host owns whether HydraSeat's split environment is prepared/active and whether ordinary Windows has been fully restored.

A whole-machine control flow may still expose:

```text
Idle -> Planning -> Prepared -> Starting -> Active
  -> Degraded
  -> Stopping -> RollingBack -> Idle
  -> RecoveryRequired on unverified recovery
```

Closing `HydraSeat.exe` does not change this state.

### 6.2 Per-Seat game lifecycle

v1 additionally requires an independent lifecycle per Seat:

```text
Idle -> Planning -> Starting -> Playing -> Stopping -> Idle
```

with explicit degraded/recovery states where needed.

Critical invariant:

```text
Seat 1 = Playing
Seat 2 = Idle
```

is a normal healthy v1 state.

A normal game exit on Seat 2 must not stop Seat 1. Seat 2 may select another Player/game and start again. While one Seat remains active, the idle Seat stays under the minimal HydraSeat Seat Launcher/waiting surface instead of returning to an unrestricted ordinary Windows desktop.

Both Seats ending, or explicit Management UI `Return to Windows`, triggers verified whole-machine rollback.

## 7. Phase model

A phase is not complete because individual packets compile. Every numbered phase ends with a separate phase-close verification covering the whole phase, cross-packet behavior, declared Windows CI, physical/manual gates, recovery, performance claims, and documentation truth. Under D-051, however, an unperformed manual/physical gate blocks validation/phase closure rather than later automated implementation: agents may continue building downstream code against controlled/fake evidence while keeping the missing real evidence explicit.

| Phase | Product capability | State | Exit summary |
| --- | --- | --- | --- |
| 0 | Research / clean-room foundation | Complete | Architecture/research policy established |
| 1 | Hardware detection | Complete | Stable device identities validated |
| 2 | Two-Seat hardware composition foundation | Complete foundation | Multi-display Seat persistence works; end-user wizard remains later UX work |
| 3 | Input compatibility / isolation | Current | Physical two-input and real-game zero-bleed path is proven with recovery |
| 4 | Production runtime / independent Seat lifecycle / display-window ownership | Early foundation in progress | Background host owns two independent Seat game lifecycles safely |
| 5 | Real two-Seat gaming MVP | Planned | Two real games run concurrently and one Seat can exit/change independently |
| 6 | Game library / Player profiles / two-player setup | Planned | Normal users can discover games, choose Players, and create automatic/manual same-game setups |
| 7 | Minimal Seat Launcher / game-first UX | Planned | Full user flow works without exposing low-level implementation concepts |
| 8 | Reliability / installer / privilege / updates | Planned, prerequisites partly validated | Non-developer install/recovery/update/offline operation is safe |
| 9 | Community compatibility/setup ecosystem | Planned | Local-first evidence can be optionally shared and aggregated transparently |
| 10 | v1 release hardening | Planned | Real hardware/product/legal/security/performance release gates pass |

Detailed packet files:

- [`PHASE3_INPUT_ISOLATION.md`](PHASE3_INPUT_ISOLATION.md)
- [`PHASE4_RUNTIME_DISPLAY.md`](PHASE4_RUNTIME_DISPLAY.md)
- [`PHASE5_TWO_SEAT_MVP.md`](PHASE5_TWO_SEAT_MVP.md)
- [`PHASE6_LAUNCHER_PROFILES.md`](PHASE6_LAUNCHER_PROFILES.md)
- [`PHASE7_SEAT_SHELL.md`](PHASE7_SEAT_SHELL.md)
- [`PHASE8_RELIABILITY_DISTRIBUTION.md`](PHASE8_RELIABILITY_DISTRIBUTION.md)
- [`PHASE9_COMPATIBILITY_SDK.md`](PHASE9_COMPATIBILITY_SDK.md)
- [`PHASE10_RELEASE_HARDENING.md`](PHASE10_RELEASE_HARDENING.md)

## 8. Critical path

```text
P3 physical acceptance / guarded suppression / real-game zero-bleed
  -> P3 phase-close verification
  -> P4 background host + IPC + process/window/display ownership
  -> P4 independent per-Seat game lifecycle
  -> P4 phase-close verification
  -> P5 complete real two-game two-Seat MVP
  -> P6 game discovery + Player profiles + two-player setup
  -> P7 minimal launcher/game-first UX
  -> P8 installer + least privilege + offline/update productization
  -> P9 community compatibility evidence/catalog
  -> P10 release hardening and legal release gate
```

This remains the **validation/release** critical path. The automated implementation frontier may move ahead of it under D-051 when the only blocker is manual/physical/game/install/reboot evidence. Cross-phase recovery prerequisites and other downstream foundations may therefore be implemented early; early implementation never closes the phase that owns them and never upgrades synthetic evidence into physical evidence.

## 9. Work-packet states

Packets use:

- `BLOCKED`
- `READY`
- `IN_PROGRESS`
- `CODE_COMPLETE`
- `VALIDATED`
- `MERGED`
- `UPSTREAMED`
- `DEFERRED`
- `REJECTED`

`CODE_COMPLETE` means implementation/automated evidence exists but a required manual/physical/game/install/reboot gate may still be pending. D-051 intentionally allows downstream automated work to consume such code-complete foundations while the corresponding validation claim remains blocked.

A packet is `VALIDATED` only when all declared evidence actually exists. Never infer manual acceptance from source inspection, synthetic tests, or a green unrelated CI job.

## 10. Packet definition of done

### Scope and design

- prerequisites are satisfied or the phase explicitly permits early foundation work;
- D-039 through D-050 and all relevant earlier decisions are preserved;
- unsupported behavior fails closed and is visible;
- public/persisted contracts are versioned and bounded;
- documentation states limitations and unproven gates;
- unrelated broad refactors are not bundled.

### Code quality

- C++20/C11 ABI rules are followed where applicable;
- Win32/COM/thread/handle ownership is deterministic and RAII-based;
- PID/HWND reuse is handled with stronger runtime identity;
- every OS/persistent mutation captures previous state and has idempotent rollback;
- latency-sensitive callbacks do not block on disk/network/UI/pipe waits/unbounded allocation;
- bounded queues document overflow and teardown.

### Verification

- unit/component tests cover normal, malformed, stale, duplicate, boundary, rollback, teardown, and no-cross-Seat cases where applicable;
- real Windows process/API tests are used when the packet promises Windows behavior;
- x64 and x86 are tested before claiming both architectures;
- relevant physical/manual acceptance remains explicit;
- roadmap validator and `git diff --check` pass;
- generated/private/reference-repository files are not committed.

### Integration

- [`STATUS.md`](STATUS.md) records exact state/evidence;
- the active phase packet text matches implementation truth;
- `docs/ARCHITECTURE.md` is updated for contract changes;
- README changes only describe actual/clearly planned product behavior and never falsely claim implementation;
- remote actions occur only when authorized by the user.

## 11. Cross-cutting engineering contracts

### 11.1 Hardware identity

Persist stable SetupAPI/ConfigMgr/display/audio identities, not enumeration order, friendly name, runtime handle, PID, HWND, or XInput slot hint.

### 11.2 Capability planning

Game/API/Windows/backend differences are explicit capabilities. Missing required capability is `Unsupported`/cannot-start for that exact plan, never silent cross-Seat fallback.

### 11.3 Player/provider credentials

HydraSeat stores lightweight provider account references only when required. Passwords/tokens/cookies remain owned by the original provider whenever practical.

### 11.4 Same-game two-player setup

Automatic setup generation is read-only until reviewed. Any mutation is typed, bounded, user-visible, reversible, and scoped. Guided manual setup is a supported path, not a hidden developer-only escape hatch.

### 11.5 Protected games

A protected title may be an explicit advanced experiment, but no code or docs may implement/teach evasion or imply anti-cheat safety. Experimental success is just technical compatibility evidence for that run.

### 11.6 Compatibility evidence

A local result should eventually record bounded facts such as game/provider/version, HydraSeat version, Windows environment, scenario, launch/instance result, receiver-verified input/bleed evidence, controller/audio result where relevant, and clean stop/rollback.

Community submission is opt-in and previewable/redacted. Aggregated success percentage is segmented by materially relevant version/environment and is not a guarantee.

### 11.7 Offline-first

Core launch/runtime/recovery must not depend on a HydraSeat cloud service. Initial community catalog distribution should work as versioned static JSON/artifacts.

### 11.8 Updates

Compatibility/setup catalog refresh and executable/runtime/driver update are separate trust domains. Program/runtime/driver updates require user approval and staged trust/health/rollback checks.

### 11.9 Privilege

Normal UI/runtime runs unelevated whenever possible. Any privileged broker is narrowly typed/allowlisted and cannot execute arbitrary commands.

### 11.10 Performance

Initial measurable budgets remain targets, not assumptions:

- no blocking external I/O on Raw Input callback path;
- bounded input queues;
- low added input latency;
- low host idle CPU/memory;
- recovery completion within bounded time where no reboot/driver constraint exists.

Exact thresholds may be refined only with measured evidence.

## 12. Test layers

Use the lowest layer that proves the promise:

1. pure unit tests;
2. component/ABI/protocol tests;
3. Windows process/API integration tests;
4. controlled physical hardware tests;
5. open-source application tests;
6. explicit real-game compatibility tests;
7. full two-Seat lifecycle tests;
8. installer/reboot/update/soak/recovery tests;
9. community evidence-schema/privacy/contribution tests.

A higher layer does not excuse missing lower-layer correctness, and a synthetic test does not replace required physical evidence.

## 13. Compatibility terminology

Internal engineering may retain exact states needed by the planner, but normal user-facing product communication prefers evidence over badges.

Useful user-facing concepts:

- `Untested` — no useful current evidence;
- `Community results available` — show sample size/success/failure/sub-results;
- `Protected / Experimental` — explicit risk warning required.

Do not imply universal support from one successful run.

## 14. Codex task entry point

Every implementation task names one packet ID.

Before coding:

```text
python tools/show_implementation_packet.py --current
python tools/show_implementation_packet.py --current --prompt
python tools/show_implementation_packet.py --ready
python tools/validate_implementation_roadmap.py
```

Required reading includes:

```text
.agents/AGENTS.md
docs/PRODUCT_V1.md
docs/implementation/DECISIONS.md
docs/implementation/STATUS.md
the active PHASE*.md packet
docs/ARCHITECTURE.md
```

Do not implement later packets opportunistically and do not mark manual gates complete without real evidence.

## 15. Current next step

[`STATUS.md`](STATUS.md) is authoritative for the current default packet.

As of the current development line, Phase 3 physical Gate A/B/C acceptance remains pending while selected Phase 4 runtime foundations have already been implemented early on a development branch. Those Phase 4 foundations are useful but do not waive Phase 3 close requirements and do not yet prove the complete independent per-Seat v1 game lifecycle.