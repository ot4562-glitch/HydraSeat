# HydraSeat Implementation Status

## Current program state

- Current phase: **Phase 3 — Input Compatibility & Isolation**
- Current default packet: **P3-API-01 — Controlled Win32 API probe baseline**
- Current validated fork baseline when this ledger was created: `01aef66`
- Current validated Windows CI evidence: fork main run `32689630892`, 12/12 tests passed
- Manual physical acceptance: still pending for Gate A, Gate B, and Gate C
- Upstream state: the integrated development line is carried by upstream PR #4

This file is an execution ledger, not a marketing status page. Agents update it after each packet with truthful evidence and leave manual gates pending until a human records results.

## State legend

`BLOCKED`, `READY`, `IN_PROGRESS`, `CODE_COMPLETE`, `VALIDATED`, `MERGED`, `UPSTREAMED`, `DEFERRED`, `REJECTED`.

## Completed foundation

| Packet | State | Title and evidence |
| --- | --- | --- |
| P0-RES-01 | UPSTREAMED | Research and clean-room policy — `PHASE0_RESEARCH.md`, related systems research, clean-room policy |
| P1-HW-01 | UPSTREAMED | Stable hardware detection — hardware identity tests and Windows CI |
| P2-SEAT-01 | UPSTREAMED | Multi-display Seat composition — transactional JSON, exclusive ownership, UI/profile tests |
| P3-PLAN-01 | UPSTREAMED | Capability planner and `hydra_plan` — planner/CLI tests; unsupported profiles fail closed |
| P3-OBS-01 | UPSTREAMED | Gate A observation implementation — Raw Input ledger, hot-plug, composite HID, JSONL tests |
| P3-ROUTE-01 | UPSTREAMED | Gate B controlled routing implementation — two HydraSeat-owned windows, fail-closed routing tests |
| P3-IPC-01 | UPSTREAMED | Gate C versioned protocol/transport — parser, named-pipe transport, handshake tests |
| P3-STATE-01 | UPSTREAMED | Process-local adapter state/C ABI — C++ adapter tests and pure C11 ABI smoke test |
| P3-PROC-01 | UPSTREAMED | Two-process synthetic state separation — GateCProcessSelfTest on Windows CI |
| P3-QUEUE-01 | UPSTREAMED | Bounded interactive target writer queues — callback path no blocking pipe writes |
| P3-FAIL-01 | UPSTREAMED | Fail-closed protocol lifecycle — GateCProtocolErrorSelfTest verifies Error frame and child exit |

## Phase 3 active queue

| Packet | State | Depends on | Immediate evidence required |
| --- | --- | --- | --- |
| P3-API-01 | IN_PROGRESS | P3-STATE-01 | Controlled Win32 API probe baseline — source and portable tests complete; Windows/MSVC probe-process validation still required |
| P3-API-02 Startup-loaded polling shim for controlled probe | BLOCKED | P3-API-01 | `GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState` return Seat-local values in the probe |
| P3-API-03 Cursor/focus/capture shim for controlled probe | BLOCKED | P3-API-02 | Probe sees Seat-local cursor/clip/focus/capture while global state remains unchanged |
| P3-RAW-01 Raw Input registration/data probe | BLOCKED | P3-API-01 | Controlled probe records real registration/data behavior before interposition |
| P3-RAW-02 Raw Input virtualization shim | BLOCKED | P3-RAW-01, P3-API-02 | Two probes receive only their Seat synthetic Raw Input stream |
| P3-ARCH-01 x86 Gate C build and cross-architecture launcher selection | READY | P3-IPC-01, P3-STATE-01 | Windows x86 CI and x64 host launching x86 controlled target |
| P3-REC-01 Host/target/adapter crash and watchdog recovery | BLOCKED | P8-WATCH-01, P3-API-03 | No orphan target/helper, shim uninstalled, state reset after forced failures |
| P3-HW-01 Gate A/B/C physical acceptance runner | READY | P3-QUEUE-01 | User-run two-keyboard/two-pointing-device traces and report |
| P3-CTRL-01 XInput controlled adapter state | READY | P3-STATE-01 | Two controlled targets see different mapped XInput slots/states |
| P3-CTRL-02 DirectInput enumeration/visibility controlled adapter | BLOCKED | P3-CTRL-01 | Controlled DirectInput probe sees only declared devices/order |
| P3-D-01 HidHide read-only availability/capability probe | READY | P3-PLAN-01 | No mutation; exact installed/version/control-interface report |
| P3-D-02 Guarded HidHide session-cloak lab | BLOCKED | P3-REC-01, P3-D-01 | Spare input/watchdog/timeout/crash rollback and physical evidence |
| P3-E-01 Open-source non-protected application profile | BLOCKED | P3-RAW-02, P3-API-03 | Reproducible profile and measured no-cross-state result |
| P3-E-02 First non-anti-cheat game profile | BLOCKED | P3-E-01, P3-D-02 or proven non-cloak suppression path | Explicit experimental compatibility entry |
| P3-E-03 Two different game zero-bleed proof | BLOCKED | P3-E-02, P3-CTRL-01 | Measured keyboard/mouse/controller bleed and latency report |
| P3-MET-01 Input latency and bleed measurement harness | READY | P3-QUEUE-01 | Machine-readable sequence/time/cross-Seat metrics |

## Cross-phase prerequisites allowed to start early

| Packet | State | Reason |
| --- | --- | --- |
| P8-WATCH-01 Watchdog protocol and lease model | READY | Required before device cloaking and crash acceptance |
| P8-RESET-01 Emergency reset CLI contract | BLOCKED | Depends on watchdog rollback registry/contracts |
| P8-JOURNAL-01 Crash journal and safe-mode marker | READY | Required before external game experiments |
| P4-RUN-01 Production host service/process skeleton | BLOCKED | Start after P3 controlled shim contract stabilizes |

## Manual gates

| Gate | State | Required evidence |
| --- | --- | --- |
| Gate A physical device observation | PENDING | Two keyboards, two pointing devices, composite HID, repeated hot-plug trace |
| Gate B physical Seat routing | PENDING | Saved profile, per-Seat route counters/target notifications, shared/unassigned failure |
| Gate C physical controlled-process routing | PENDING | Two physical input sets driving two controlled target adapter states |
| Gate C API interposition | NOT IMPLEMENTED | HydraSeat-owned probes calling real Windows APIs through opt-in shim |
| Gate C watchdog/crash recovery | NOT IMPLEMENTED | Forced host/target/adapter failure and clean restoration |
| Gate D device cloaking | NOT IMPLEMENTED | Guarded session-cloak experiment with spare input and automatic rollback |
| Gate E two-game zero bleed | NOT IMPLEMENTED | Two distinct profile entries and objective cross-Seat metrics |

## Latest packet evidence template

When a packet changes state, append a record:

```text
### <date> — <packet ID>
State:
Branch/commit:
Windows CI:
Automated tests:
Manual evidence:
Known limitations:
Rollback result:
Next packet:
```

### 2026-08-24 — P3-API-01

State: IN_PROGRESS
Branch/commit: `docs/control-seat-runtime-ux`; uncommitted pending required Windows validation
Windows CI: not run; the current machine has no CMake, MSVC, or Windows SDK installation
Automated tests:
- strict GCC 15 snapshot build/test passed with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`
- WSL Release CMake focused build passed for the Gate C core, adapter, target, host, API probe, and tests
- WSL CTest passed 12/12 portable tests, including `GateCProbeSnapshotTests`
- Win32-only `GateCApiProbeSelfTest` and `GateCApiProbeProcessSelfTest` remain unexecuted
Manual evidence: none required by this packet; Gate A/B/C physical gates remain pending and unchanged
Known limitations: no API interposition exists; the probe records ordinary shared OS state beside direct adapter state
Rollback result: portable lifecycle code compiled; Win32 disconnect/timeout/abnormal-exit/no-orphan paths await Windows execution
Next packet: finish P3-API-01 Windows/MSVC validation; do not start P3-API-02 before that passes

## Next Codex task

Use:

```text
Implement P3-API-01 exactly as specified in
  docs/implementation/PHASE3_INPUT_ISOLATION.md

Do not implement API interposition yet. Build the controlled probe that reports
actual OS polling/cursor/focus/Raw Input observations beside the existing adapter
state, with deterministic snapshots and Windows integration tests.
```
