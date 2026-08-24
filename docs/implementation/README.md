# HydraSeat Master Implementation Roadmap

## 1. Purpose

This directory is the implementation source of truth for HydraSeat from the remaining Phase 3 work through a production-quality release.

The roadmap is written so a coding agent can implement one bounded work packet without inventing architecture, silently widening scope, or marking an unverified feature complete. A packet describes:

- the exact outcome;
- prerequisites;
- files and public types expected to exist;
- invariants that must remain true;
- automated tests;
- manual Windows/hardware acceptance where required;
- rollback and failure behavior;
- the phase exit gate.

The target product remains:

> One Windows PC should feel like multiple local gaming PCs. Each Seat may own one or more displays, keyboard/mouse/controller devices, audio endpoints, processes, windows, cursor state, launcher state, and eventually a lightweight Seat shell. The implementation must not require a virtual machine, Remote Desktop, or streaming for ordinary local monitors.

## 2. Source-of-truth order

When documents disagree, use this order:

1. direct user instructions for the current task;
2. `.agents/AGENTS.md`;
3. [DECISIONS.md](DECISIONS.md);
4. this master roadmap and [STATUS.md](STATUS.md);
5. the active phase specification;
6. `docs/ARCHITECTURE.md` and specialized design documents;
7. existing code and tests;
8. older comments, issue text, and historical implementation notes.

A coding agent must not resolve a real design conflict by guessing. It must either apply the higher-precedence decision or update the roadmap/decision document first.

## 3. Target executable and module topology

The intended product topology is:

```text
HydraSeat.exe
  On-demand Management Seat control console
  Configuration UI, profile editor, Start, Stop/Return to Windows, Reconfigure, diagnostics
  Default visible placement: Management Seat (Seat 1) primary display

hydra_host.exe
  Per-user background runtime
  Hardware observation, Seat runtime state, process/window/display/audio routing
  Compatibility-plan execution and diagnostics

hydra_watchdog.exe
  Independent recovery process
  Detect host/adapter failure and restore safe Windows state

hydra_reset.exe
  Emergency command-line reset that does not depend on the GUI or host

hydra_adapter32.dll / hydra_adapter64.dll
  Optional process-local compatibility layer selected by a game profile

hydra_shell.exe
  Optional per-Seat launcher/taskbar/desktop surface

hydra_plan.exe
  Read-only compatibility-plan diagnostics

hydra_diag.exe
  Read-only runtime and hardware diagnostics, support-bundle export

optional display/audio/controller adapters
  Capability-gated components that can be absent without breaking the core
```

The current Gate A/B/C labs remain development harnesses. Their proven components may be moved into the production host, but the lab executables must remain available as regression tools until equivalent production diagnostics exist.

The control plane is intentionally separate from the runtime. Closing `HydraSeat.exe` does not stop a validated active session. The user may run HydraSeat manually, keep host/watchdog silently idle at logon, or auto-activate one explicitly selected validated session after all recovery/topology/capability preflight passes. `Stop / Return to Windows` always performs verified rollback to ordinary one-PC Windows behavior, and `Reconfigure` performs that return first before changing monitor/input assignments.

## 4. Phase model

Phase numbers group product capability. They are mostly sequential, but reliability, security, diagnostics, and schema work are cross-cutting. A later-phase work packet may be pulled forward only when an earlier packet declares it as a prerequisite.

| Phase | Capability | Current state | Exit summary |
| --- | --- | --- | --- |
| 0 | Research and foundation | Complete | Architectural research and clean-room policy recorded |
| 1 | Hardware detection | Complete | Stable device identities and Windows CI |
| 2 | Seat composition | Complete | Multi-display Seat model and transactional profiles |
| 3 | Input compatibility and isolation | Current | Documented profiles can run without cross-Seat input merging and can roll back safely |
| 4 | Runtime host, control console, process/window/display routing | Planned | Background host stays authoritative; Management Seat controls Start/Return/Reconfigure; Seat-owned windows remain inside assigned display groups |
| 5 | Two-Seat gaming MVP | Planned | Two different supported games run with independent input/display/controller/audio and measured limits |
| 6 | Launcher and profile manager | Planned | Repeatable provider-aware launch plans and editable compatibility profiles |
| 7 | Seat shell and local-PC experience | Planned | Each Seat feels like a coherent local desktop/launcher environment |
| 8 | Reliability, watchdog, installer, updates | Planned | Crash-safe background runtime with emergency reset and reversible install/update |
| 9 | Compatibility SDK and ecosystem | Planned | Versioned adapter/profile SDK with trust and validation policy |
| 10 | Release hardening | Planned | Signed, documented, performance-tested release with explicit compatibility scope |

Detailed specifications:

- [Product requirement traceability](TRACEABILITY.md)
- [Non-negotiable decisions](DECISIONS.md)
- [Localization policy](../LOCALIZATION.md)
- [Codex implementation playbook](CODEX_PLAYBOOK.md)
- [Current execution status](STATUS.md)
- [PHASE3_INPUT_ISOLATION.md](PHASE3_INPUT_ISOLATION.md)
- [PHASE4_RUNTIME_DISPLAY.md](PHASE4_RUNTIME_DISPLAY.md)
- [PHASE5_TWO_SEAT_MVP.md](PHASE5_TWO_SEAT_MVP.md)
- [PHASE6_LAUNCHER_PROFILES.md](PHASE6_LAUNCHER_PROFILES.md)
- [PHASE7_SEAT_SHELL.md](PHASE7_SEAT_SHELL.md)
- [PHASE8_RELIABILITY_DISTRIBUTION.md](PHASE8_RELIABILITY_DISTRIBUTION.md)
- [PHASE9_COMPATIBILITY_SDK.md](PHASE9_COMPATIBILITY_SDK.md)
- [PHASE10_RELEASE_HARDENING.md](PHASE10_RELEASE_HARDENING.md)

## 5. Critical path

The default implementation order is:

```text
P3 controlled API probes
  -> P3 optional physical cloaking experiment
  -> P3 two-game zero-bleed proof
  -> P4 production host/process/window/display ownership
  -> P5 audio/controller/two-game MVP
  -> P6 repeatable launcher/profile system
  -> P7 Seat shell
  -> P8 installer/watchdog/update productization
  -> P9 SDK/ecosystem
  -> P10 release hardening
```

Some Phase 8 foundations are intentionally pulled forward:

- watchdog protocol and emergency reset design before any physical-device cloaking;
- crash journal before the first external game profile;
- privilege broker before driver or elevated configuration;
- signed manifest/hash verification before loading optional external binaries.

## 6. Work-packet states

Every packet has one state in [STATUS.md](STATUS.md):

- `BLOCKED`: a declared prerequisite is incomplete;
- `READY`: all prerequisites and required design decisions exist;
- `IN_PROGRESS`: one branch/task owns the packet;
- `CODE_COMPLETE`: implementation and automated tests pass, but required manual acceptance is pending;
- `VALIDATED`: all automated and declared manual acceptance passed;
- `MERGED`: validated commit is on the fork default branch;
- `UPSTREAMED`: an upstream PR contains the merged commit;
- `DEFERRED`: intentionally postponed with a written reason;
- `REJECTED`: research or experiment proved the approach unsuitable.

Rules:

1. Only one agent may own an `IN_PROGRESS` packet at a time.
2. A manual hardware checkbox cannot be completed from a unit test, synthetic fixture, or source inspection.
3. `CODE_COMPLETE` is not equivalent to `VALIDATED`.
4. A packet that changes a public schema, ABI, protocol, installation state, or driver policy needs explicit migration/rollback tests.
5. A packet remains incomplete when a required Windows CI job is absent, skipped, or allowed to fail.

## 7. Definition of done for a work packet

A packet is `VALIDATED` only when all applicable items pass:

### Design and scope

- packet prerequisites are already validated;
- implementation follows [DECISIONS.md](DECISIONS.md);
- public interfaces are versioned or explicitly internal;
- unsupported behavior fails closed and is visible;
- no unrelated refactor is bundled without a separate packet;
- documentation states what the feature does **not** guarantee.

### Code quality

- C++20 and C11/C ABI constraints are respected where applicable;
- ownership is RAII-based;
- Win32 handles, COM initialization, callbacks, threads, timers, hooks, pipes, events, and driver state have deterministic teardown;
- thread interaction has a documented lock/queue/ownership model;
- errors include operation, system code, component, Seat/process context, and remediation where meaningful;
- persistent state is written transactionally and migrated explicitly.

### Automated verification

- pure unit tests cover normal, boundary, malformed, stale, duplicate, and rollback paths;
- Windows integration tests cover the actual API/process boundary;
- x64 Release MSVC CI passes with `/W4 /permissive-`;
- x86 CI is required before any 32-bit adapter/profile is declared supported;
- `git diff --check` passes;
- generated files and traces are ignored;
- test names and failure messages identify the violated invariant.

### Manual acceptance

When the packet declares manual acceptance:

- exact hardware/Windows build/driver/game version is recorded;
- procedure is reproducible from a clean start;
- before/after traces or diagnostic bundle are retained;
- success and failure criteria are objective;
- recovery is exercised, not merely described;
- the result is added to the compatibility or hardware matrix.

### Repository integration

- packet state and evidence links are updated in [STATUS.md](STATUS.md);
- phase roadmap checkbox changes only after its true gate passes;
- README changes only when user-facing behavior changed;
- architecture changes are reflected in `docs/ARCHITECTURE.md`;
- commit title follows the packet's suggested prefix;
- branch is clean after commit;
- push/PR occurs only when explicitly authorized.

## 8. Cross-cutting engineering tracks

### 8.1 Runtime state machine

All runtime code converges on an explicit state machine:

```text
Stopped
  -> Planning
  -> Prepared
  -> Starting
  -> Active
  -> Degraded
  -> Stopping
  -> RollingBack
  -> Stopped

Any state -> Failed -> RollingBack -> Stopped or RecoveryRequired
```

A UI string or boolean such as `isIsolationMode` must not be the authoritative runtime state.

### 8.2 Capability and profile model

Every operation that can vary by game, Windows build, driver, API, or privilege must be capability-planned. A backend advertises only behavior that has test evidence. Missing required capabilities produce `Unsupported`, not a silent lower-quality fallback.

### 8.3 Versioned contracts

The following contracts require explicit version fields and compatibility tests:

- Seat profile JSON;
- game compatibility profile JSON;
- host/UI/watchdog IPC;
- host/adapter protocol;
- adapter C ABI;
- diagnostic bundle manifest;
- plugin package manifest;
- update manifest.

### 8.4 Diagnostics and evidence

Every phase must produce machine-readable diagnostics. Minimum fields:

- UTC and monotonic timestamps;
- component/build/version;
- session and Seat IDs;
- process/window/device stable identities;
- backend/profile/plan identifiers;
- operation/result/system error;
- sequence or correlation ID;
- explicit guarantee level;
- rollback result.

Logs must not contain credentials, tokens, private file contents, unrelated process memory, or raw user input text beyond explicitly enabled diagnostic key codes.

### 8.5 Safety and recovery

Physical hiding, input suppression, display topology mutation, process injection, system-wide hooks, explorer/shell changes, audio routing persistence, driver installation, and updates require:

- explicit user approval;
- preflight validation;
- independent watchdog or timeout;
- captured previous state;
- idempotent rollback;
- emergency reset path;
- crash recovery evidence;
- safe-mode startup after incomplete rollback.

### 8.6 Performance budgets

Initial budgets, to be refined by measurement:

- host Raw Input callback: no blocking external I/O and no unbounded allocation;
- input event enqueue path: target p99 below 250 microseconds on reference hardware;
- controlled adapter state query: target p99 below 100 microseconds;
- added local input latency for supported profiles: target p95 below 2 ms and p99 below 5 ms;
- host idle CPU: below 1% on reference hardware;
- host idle private working set: below 150 MB;
- Seat shell idle CPU per Seat: below 0.5%;
- rollback after helper failure: target below 3 seconds without driver reboot requirements;
- no unbounded queue, log, trace, or retry loop.

A packet may change a budget only with benchmark evidence and a documented reason.

### 8.7 Security and legal boundaries

- no anti-cheat, DRM, protected-process, credential, or security-product bypass;
- no stealth or persistence beyond documented user-approved autostart;
- no third-party source copying outside [CLEAN_ROOM_POLICY.md](../CLEAN_ROOM_POLICY.md);
- optional binaries/drivers require version, hash, origin, license, and trust review;
- driver/injection backends are disabled by default;
- unlicensed and proprietary systems remain behavior/documentation references only.

## 9. Test layers

Each phase uses the lowest layer that proves the invariant:

1. **Pure unit tests** — parsers, state machines, planners, coordinate math, migrations.
2. **Component tests** — DLL/ABI, IPC, queues, profile validation, fake backends.
3. **Windows integration tests** — real processes, windows, audio endpoints, display APIs.
4. **Controlled hardware tests** — two keyboards/mice/controllers/displays and hot-plug.
5. **Controlled application tests** — HydraSeat-owned probes and open-source test apps.
6. **Game compatibility tests** — explicit non-anti-cheat profiles only.
7. **Soak/recovery tests** — repeated start/stop, crash, disconnect, reboot, update/rollback.

A higher layer does not replace missing lower-layer tests. A synthetic test does not replace the manual physical gate when the requirement is about physical hardware or OS behavior.

## 10. Compatibility support levels

Each game/profile result uses one level:

- `ObservationOnly`: HydraSeat can diagnose APIs/devices but not isolate them.
- `Experimental`: controlled success with known manual steps and recovery requirements.
- `Supported`: automated preflight and documented manual acceptance pass on listed builds/hardware.
- `Blocked`: required capability conflicts with anti-cheat/protection or no safe backend exists.
- `Regressed`: a previously supported matrix entry failed and release is blocked or support is withdrawn.

No documentation may say “works with games” without naming the profile, version, Windows build, backend set, support level, and limitations.

## 11. Codex task entry point

A Codex implementation task must name exactly one packet ID and use [CODEX_PLAYBOOK.md](CODEX_PLAYBOOK.md).

Inspect or generate the task directly from the validated roadmap:

```text
python tools/show_implementation_packet.py --current
python tools/show_implementation_packet.py --current --prompt
python tools/show_implementation_packet.py --ready
```

`--prompt` is the preferred ready-to-paste Codex input because the tool validates the entire roadmap first, then includes the packet file, declared prerequisites, scope limits, required tests, status update, and manual-gate prohibition.

Equivalent manual prompt:

```text
Implement packet <PACKET-ID> exactly as specified in
  docs/implementation/<PHASE-FILE>.md

Read first:
  .agents/AGENTS.md
  docs/implementation/DECISIONS.md
  docs/implementation/CODEX_PLAYBOOK.md
  docs/implementation/STATUS.md
  the packet's phase document

Do not implement later packets. Do not mark manual acceptance complete.
Run every test required by the packet and update STATUS.md with evidence.
Do not push unless the user explicitly authorizes it.
```

## 12. Current next step

The default next packet is recorded in [STATUS.md](STATUS.md). At the time this master roadmap was introduced, the immediate engineering target is the remaining Phase 3 controlled API-interposition path for HydraSeat-owned probes, followed by physical acceptance and watchdog-backed recovery.
