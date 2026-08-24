# HydraSeat Implementation Status

## Current program state

- Current phase: **Phase 3 — Input Compatibility & Isolation**
- Current default packet: **P3-API-02 — Startup-loaded polling shim for controlled probes**
- Current validated implementation baseline: `7c4c114a179cdfb3f5cb9495cf26b0a988f71aff` (merged P3-ARCH-01 baseline)
- Current validated Windows CI evidence: run `32727711605`; x64/x86 full CTest, x64 host to x64/x86 targets, stale protocol, API-probe failure/cleanup, and roadmap/prompt validation passed
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
| P3-API-01 | VALIDATED | P3-STATE-01 | Controlled Win32 API probe baseline — Windows/MSVC run `32722277035`; 15/15 tests including both API probe self-tests passed |
| P3-API-02 Startup-loaded polling shim for controlled probe | CODE_COMPLETE | P3-API-01, P3-ARCH-01 | Portable transaction/ABI tests pass; Windows x64/x86 ordinary-API probe execution is pending CI |
| P3-API-03 Cursor/focus/capture shim for controlled probe | BLOCKED | P3-API-02 | Probe sees Seat-local cursor/clip/focus/capture while global state remains unchanged |
| P3-RAW-01 Raw Input registration/data probe | BLOCKED | P3-API-01 | Controlled probe records real registration/data behavior before interposition |
| P3-RAW-02 Raw Input virtualization shim | BLOCKED | P3-RAW-01, P3-API-02 | Two probes receive only their Seat synthetic Raw Input stream |
| P3-ARCH-01 x86 Gate C build and cross-architecture launcher selection | VALIDATED | P3-IPC-01, P3-STATE-01 | Windows run `32727711605`: x64/x86 full CTest and x64-host-to-x86/x64 controlled target/probe matrix passed |
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
| Gate C polling API interposition | CODE COMPLETE / CI PENDING | HydraSeat-owned x64/x86 probes calling the three ordinary polling APIs through the opt-in shim |
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

State: VALIDATED
Branch/commit: validated baseline `cc88e7934461a9ad0cbae516312a254be173558c`
Windows CI: run `32722277035` passed on Windows Server 2025 / MSVC x64; 15/15 CTest targets passed
Automated tests:
- strict GCC 15 snapshot build/test passed with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`
- Windows CMake/MSVC Release configure/build passed in run `32722277035`
- Windows CTest passed 15/15 targets, including `GateCProbeSnapshotTests`
- `GateCApiProbeSelfTest` and `GateCApiProbeProcessSelfTest` both passed on the Windows runner
Manual evidence: none required by this packet; Gate A/B/C physical gates remain pending and unchanged
Known limitations: no API interposition exists; the probe records ordinary shared OS state beside direct adapter state
Rollback result: Windows baseline self-test exercised missing-window, handshake-timeout, abnormal-exit, host-disconnect, repeated startup/shutdown, and no-orphan paths successfully
Next packet: P3-API-02 startup-loaded polling shim for controlled x64/x86 probes

### 2026-08-24 — P3-ARCH-01

State: VALIDATED
Branch/commit: `build/p3-arch-01`; validated implementation commit `ea766bd36a87645eb27f34358848ad4f994f5a08`
Windows CI: run `32727711605` passed independent x64 and Win32/x86 full CTest jobs plus the dedicated x64-host cross-architecture job
Automated tests:
- GCC 15 built and passed 7/7 Gate C portable tests, including manifest/PE architecture, HPS1, protocol, C11/C++ ABI, target, and host regression tests
- modern `IsWow64Process2` mapping, bounded legacy fallback, unsupported/failed detection, schema/version/entry/path bounds, duplicate/missing/unknown/path-traversal, malformed PE, and wrong-architecture artifacts are covered
- Windows x64 CTest passed 17/17 and Win32/x86 CTest passed 17/17, including runtime process/PE architecture detection and the C11 adapter ABI smoke test in both architectures
- the dedicated cross-architecture job passed x64-host-to-native-x64 and x64-host-to-x86 controlled target handshakes, the x86 stale-protocol failure path, and the x86 API-probe baseline/failure/cleanup matrix
- Windows validation exposed and fixed a process-handle bug: `GetCurrentProcess()` is the valid `-1` pseudo-handle and must not be rejected as `INVALID_HANDLE_VALUE`
Manual evidence: none claimed; physical Gate C input, API interposition, commercial games, protected processes, and physical suppression remain pending/out of scope
Known limitations: architecture selection and the controlled x86/x64 Gate C boundary are validated; API interposition, Raw Input virtualization, physical suppression, commercial games, and protected-process work remain unimplemented/out of scope
Rollback result: selector/preflight failures occur before child launch; launched test children retain the existing shutdown, timeout, forced termination, repeat-cycle, and no-orphan cleanup paths
Next packet: P3-API-02 startup-loaded polling API shim, limited to HydraSeat-owned x64/x86 controlled probes

### 2026-08-25 — P3-API-02

State: CODE_COMPLETE
Branch/commit: `feat/p3-api-02-polling-shim`; commit recorded after final checks
Windows CI: not run for this packet in the local environment; x64/x86 MSVC and cross-architecture polling-probe evidence remains required before `VALIDATED`
Automated tests:
- strict GCC 15 build passed with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`
- strict-GCC portable CTest passed 15/15 overall and 9/9 Gate C tests, including polling transaction/rollback and pure-C shim ABI tests
- install/uninstall/reinstall, missing/duplicate/already-patched slots, partial-install rollback, protection failure, retryable uninstall failure, exact pointer restoration, adapter-unavailable fail-closed behavior, one-shot async edges, toggle-bit policy, and 256-byte keyboard state are covered
- CMake/CI declares native x64/x86 shim/probe tests and x64-host-to-x64/x86 polling process tests; the selector preflights probe, adapter, and shim PE machines
Manual evidence: none required by the packet; Gate A/B/C physical acceptance remains pending and unchanged
Known limitations: Windows/MSVC execution is pending; v1 does not virtualize toggle low bits, cursor/focus/capture, Raw Input, physical suppression, third-party/commercial targets, or protected processes
Rollback result: the component suite proved all-or-rollback install, byte-exact reverse restoration, idempotent uninstall/reinstall, and retry after an uninstall write failure; controlled probe teardown refuses DLL unload unless restoration succeeds
Next packet: P3-API-03 remains BLOCKED until P3-API-02 receives Windows x64/x86 integration evidence and becomes `VALIDATED`

## Next Codex task

Use:

```text
Implement P3-API-02 exactly as specified in
  docs/implementation/PHASE3_INPUT_ISOLATION.md

Validate P3-API-02 on Windows/MSVC x64 and Win32/x86, including the dedicated
x64-host-to-x64/x86 polling-probe matrix. Do not start P3-API-03 until this
packet is `VALIDATED`. Cursor/focus, Raw Input virtualization, device
suppression, third-party injection, and game support remain out of scope.
```
