# HydraSeat Development Roadmap

This file is the phase-level summary. The implementation source of truth is the packetized master roadmap under [`docs/implementation/`](implementation/README.md). Coding agents must read [`STATUS.md`](implementation/STATUS.md), [`DECISIONS.md`](implementation/DECISIONS.md), and the active phase packet before editing code.

Current default implementation packet:

```text
P3-CTRL-02 — DirectInput enumeration and visibility adapter
```

Validate the roadmap with:

```text
python tools/validate_implementation_roadmap.py
```

---

## Phase 0 — Research and Foundation (Complete)

- [x] Establish C++20 / optional Qt 6 project architecture
- [x] Configure agent workflow and autonomous verification rules
- [x] Document Windows input/display architecture
- [x] Evaluate Interception, HidHide, ViGEmBus, IddCx/IDD, ProtoInput, Universal Split Screen, Nucleus Co-op, devreorder, Duo, and ASTER public behavior
- [x] Record clean-room, source-license, and third-party research policy

Research:

- [Phase 0 research](PHASE0_RESEARCH.md)
- [Related systems research](RELATED_SYSTEMS_RESEARCH.md)
- [Clean-room policy](CLEAN_ROOM_POLICY.md)

---

## Phase 1 — Hardware Detection (Complete)

- [x] Implement hardware detector and diagnostic CLI
- [x] Detect physical monitors and likely virtual displays
- [x] Resolve distinct keyboards/mice/touchpads through Raw Input plus SetupAPI/ConfigMgr identity
- [x] Detect XInput and generic HID controllers
- [x] Add deterministic identity tests and Windows/MSVC CI

---

## Phase 2 — Seat Composition and Assignment UI (Complete)

- [x] Replace single-display workspace assumptions with multi-display Seat composition
- [x] Support explicit Seat primary display
- [x] Assign keyboard, mouse, controller, render, and capture endpoint identities
- [x] Enforce exclusive ownership by default with explicit sharing
- [x] Save/load validated transactional UTF-8 JSON profiles
- [x] Restore assignments in the current Win32 drag/drop prototype

---

## Phase 3 — Input Compatibility and Isolation (Current)

### Completed implementation foundations

- [x] Raw Input observation and stable physical-device identity
- [x] Composite-HID-aware hot-plug ledger
- [x] Fail-closed exclusive keyboard/mouse Seat routing
- [x] Gate A/B two-window diagnostic lab and JSONL traces
- [x] Capability planner, backend descriptors, profile templates, and `hydra_plan`
- [x] Versioned Gate C host/target protocol and local named-pipe transport
- [x] Process-local adapter DLL with versioned C ABI
- [x] Independent synthetic keyboard/mouse/cursor/focus state in two controlled processes
- [x] Bounded per-target writer queues
- [x] Fail-closed stale-sequence and child lifecycle tests

### Remaining critical path

- [x] Controlled probe records real OS polling/cursor/focus baseline (`P3-API-01`)
- [x] Startup-loaded polling shim for HydraSeat-owned probes
- [x] Cursor/focus/capture shim for HydraSeat-owned probes
- [x] Raw Input registration/data behavior probe (`P3-RAW-01`)
- [x] Raw Input virtualization shim (`P3-RAW-02`)
- [x] x86/x64 controlled adapter matrix
- [x] Controlled normalized XInput state/remapping correctness remediation (`P3-CTRL-01`; fork PR #15 run `32832036967` validates native x64/x86 36/36 and x64-host-to-x64/x86 zero-cross controller acceptance)
- [ ] Controlled DirectInput enumeration/order/visibility adapter (`P3-CTRL-02`; READY)
- [ ] Production controller polling/routing and physical vibration evidence (later profile/runtime work)
- [ ] Input latency, queue, drop, and cross-Seat metrics
- [ ] Physical Gate A/B/C acceptance with two input sets
- [ ] Watchdog/crash rollback acceptance
- [ ] Read-only HidHide probe, then guarded session-cloak experiment
- [ ] Open-source application profile
- [ ] First non-anti-cheat game profile
- [ ] Two different target/game zero-bleed proof

Detailed implementation:

- [Phase 3 packet roadmap](implementation/PHASE3_INPUT_ISOLATION.md)
- [Phase 3 design](PHASE3_INPUT_ISOLATION_DESIGN.md)
- [Gate A/B testing](PHASE3_GATE_A_B_TESTING.md)
- [Gate C testing](PHASE3_GATE_C_TESTING.md)

Phase 3 closes only after controlled probes observe Seat-local values through the actual Windows API surfaces required by supported profiles, physical/recovery gates pass, and two different explicit targets produce objective zero-bleed evidence.

---

## Phase 4 — Production Runtime, Process/Window Ownership, and Display Routing (Planned)

- [ ] Move runtime authority from GUI into `hydra_host.exe`
- [ ] Add versioned UI/CLI/watchdog host protocol
- [ ] Add Management Seat control console on Seat 1 primary by default, with safe visible fallback
- [ ] Add verified `Stop / Return to Windows` and `Reconfigure` workflows that restore ordinary one-PC use before editing/restarting
- [ ] Track Seat-owned process trees with Job Objects where compatible
- [ ] Track Seat-owned windows without touching unrelated windows
- [ ] Build DisplayConfig/DXGI topology and stable output identities
- [ ] Implement multi-monitor Seat display groups and DPI-aware coordinate transforms
- [ ] Place/restore owned windows and capability-gate fullscreen modes
- [ ] Reconcile process/window/display events through one deterministic policy engine
- [ ] Handle display removal/reconnect and host restart safely
- [ ] Define optional virtual-display backend contract
- [ ] Integrate one lawful external virtual-display adapter
- [ ] Decide custom IddCx/IDD adoption only after measured feasibility

Detailed implementation: [Phase 4 packet roadmap](implementation/PHASE4_RUNTIME_DISPLAY.md)

---

## Phase 5 — Two-Seat Gaming MVP (Planned)

- [ ] Enumerate and validate stable audio endpoints
- [ ] Implement capability-gated per-process audio routing
- [ ] Move controller routing into production host/profile path
- [ ] Compile one immutable two-Seat activation plan
- [ ] Apply/verify/rollback all process/window/display/input/controller/audio actions transactionally
- [ ] Produce integrated latency/drop/bleed/resource report
- [ ] Validate an end-to-end controlled/open-source two-Seat session
- [ ] Validate two different non-anti-cheat game profiles
- [ ] Test target restart and input/display/controller/audio reconnect
- [ ] Provide truthful start/stop/status/reset UI
- [ ] Publish machine-readable compatibility/hardware matrix

Detailed implementation: [Phase 5 packet roadmap](implementation/PHASE5_TWO_SEAT_MVP.md)

---

## Phase 6 — Launcher and Profile Manager (Planned)

- [ ] Define versioned Seat/target/compatibility/session profile schemas
- [ ] Add transactional migration and backup
- [ ] Build provider-neutral application catalog
- [ ] Define launcher provider interface
- [ ] Implement custom executable and Steam path first
- [ ] Add Epic, EA, and GOG one provider packet at a time
- [ ] Compile immutable provider-aware launch plans and plan hashes
- [ ] Show exact preflight risks, mutations, capabilities, and recovery requirements
- [ ] Provide typed profile editor/manager UI
- [ ] Provide profile/catalog/plan CLI
- [ ] Add portable import/export with redaction and hardware identity remapping
- [ ] Maintain provider/profile regression fixture corpus

Detailed implementation: [Phase 6 packet roadmap](implementation/PHASE6_LAUNCHER_PROFILES.md)

---

## Phase 7 — Seat Shell and Local-PC Experience (Planned)

- [ ] Launch one scoped shell process per Seat
- [ ] Keep an always-available HydraSeat management-console entry on the configured Management Seat shell; non-management Seats remain read-mostly for whole-machine controls
- [ ] Add shared UI localization catalogs with English default plus Korean (`ko-KR`) and Simplified Chinese (`zh-CN`), while source comments and machine-readable identifiers remain English
- [ ] Add Seat launcher and pinned profiles
- [ ] Add Seat-owned task/window surface
- [ ] Add per-Seat multi-monitor wallpaper and desktop zones
- [ ] Render independent software cursors inside Seat display groups
- [ ] Add Seat-scoped launcher/window/recovery hotkeys
- [ ] Add Seat-scoped runtime notifications
- [ ] Define truthful optional clipboard policy
- [ ] Complete DPI/accessibility/localized-layout readiness across English/Korean/Chinese UI
- [ ] Create internal shell extension boundary
- [ ] Prove shell crash/restart and Explorer coexistence

Detailed implementation: [Phase 7 packet roadmap](implementation/PHASE7_SEAT_SHELL.md)

---

## Phase 8 — Reliability, Watchdog, Installer, and Updates (Planned; selected prerequisites may start early)

- [ ] Independent watchdog lease and allowlisted rollback protocol
- [ ] Emergency reset CLI
- [ ] Crash journal and safe-mode startup marker
- [ ] Narrow elevated privilege broker
- [ ] User-selectable runtime modes: Manual, hidden BackgroundIdle, and validated AutoActivate-on-logon; controller remains reopenable from the Management Seat
- [ ] Optional component manifest/hash/signature/trust policy
- [ ] Production code/driver signing pipeline
- [ ] Reversible installer/repair/uninstaller
- [ ] Signed staged update with health check and rollback
- [ ] Redacted diagnostic/support bundle
- [ ] Long soak, reboot, and fault-injection campaign

Detailed implementation: [Phase 8 packet roadmap](implementation/PHASE8_RELIABILITY_DISTRIBUTION.md)

---

## Phase 9 — Compatibility SDK and Extension Ecosystem (Planned)

- [ ] Freeze public C/schema/IPC SDK boundary and compatibility policy
- [ ] Add capability/permission negotiation
- [ ] Add signed/hash-verified extension packages
- [ ] Run normal extensions out of process with bounded IPC/resources
- [ ] Publish backend/provider/profile/shell/diagnostic extension contracts
- [ ] Add local extension registry and optional signed catalog contract
- [ ] Build extension conformance kit and samples
- [ ] Complete extension threat model and security review
- [ ] Keep core fully functional offline with all extensions disabled

Detailed implementation: [Phase 9 packet roadmap](implementation/PHASE9_COMPATIBILITY_SDK.md)

---

## Phase 10 — Release Hardening and 1.0 Readiness (Planned)

- [ ] Freeze exact supported platform/hardware/profile scope
- [ ] Qualify latency, CPU, memory, queue, rollback, and scalability budgets
- [ ] Run complete compatibility regression matrix
- [ ] Complete product threat model, hardening, privacy, and dependency review
- [ ] Run long-duration release soak/fault/reboot/update campaign
- [ ] Complete clean-machine onboarding, accessibility, help, and recovery docs
- [ ] Resolve project license/contribution terms and third-party notices
- [ ] Produce signed reproducible artifacts, checksums, SBOM, and provenance
- [ ] Establish support/regression/security intake policy
- [ ] Pass release-candidate stabilization gate
- [ ] Publish and immediately revalidate 1.0 artifacts
- [ ] Publish maintenance/EOL/retest policy

Detailed implementation: [Phase 10 packet roadmap](implementation/PHASE10_RELEASE_HARDENING.md)

---

## Product-defining traceability

The original Seat-first requirements and the packets/evidence that prove them are mapped in [Product Requirement Traceability](implementation/TRACEABILITY.md).

The complete execution rules are in:

- [Master implementation roadmap](implementation/README.md)
- [Non-negotiable decisions](implementation/DECISIONS.md)
- [Codex implementation playbook](implementation/CODEX_PLAYBOOK.md)
- [Current packet status](implementation/STATUS.md)
