# HydraSeat Implementation Status

## Current program state

- Current phase: **Phase 3 — Input Compatibility & Isolation**
- Current default packet: **P3-RAW-01 — Controlled Raw Input behavior probe**
- Current validated implementation head: `aa64fa9` (P3-API-03 branch; final merge SHA pending)
- Current validated Windows CI evidence: run `32792381573`; x64/x86 24/24 CTest and dedicated x64-host-to-x64/x86 polling + cursor/focus cross-architecture jobs passed
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
| P3-API-02 Startup-loaded polling shim for controlled probe | VALIDATED | P3-API-01, P3-ARCH-01 | Windows run `32780563364`: native x64/x86 polling-shim CTest plus x64-host-to-x64/x86 ordinary-API two-probe isolation passed |
| P3-API-03 Cursor/focus/capture shim for controlled probe | VALIDATED | P3-API-02 | Windows run `32792381573`: x64/x86 24/24 CTest plus x64-host-to-x64/x86 Seat-local cursor/clip/logical focus/capture and host-native global-state preservation passed |
| P3-RAW-01 Raw Input registration/data probe | READY | P3-API-01 | Next packet: controlled probe records registration replacement, WM_INPUT, GetRawInputData, and GetRawInputBuffer behavior before interposition |
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
| Gate C polling API interposition | VALIDATED (CONTROLLED CI) | HydraSeat-owned x64/x86 probes call the three ordinary polling APIs through the opt-in shim; physical/game acceptance is still separate |
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

State: VALIDATED
Branch/commit: `feat/p3-api-02-polling-shim`; validated implementation commit `f3bb863cc9f606aa6e83804a344ec906afcb9e21`
Windows CI: run `32780563364` passed native x64 and Win32/x86 full CTest jobs plus the dedicated x64-host-to-x64/x86 polling-shim cross-architecture job
Automated tests:
- strict GCC 15 build passed with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`
- strict-GCC portable CTest passed 15/15 overall and 9/9 Gate C tests, including polling transaction/rollback and pure-C shim ABI tests
- install/uninstall/reinstall, missing/duplicate/already-patched slots, partial-install rollback, protection failure, retryable uninstall failure, exact pointer restoration, adapter-unavailable fail-closed behavior, one-shot async edges, toggle-bit policy, and 256-byte keyboard state are covered
- Windows CI executed the native x64/x86 shim/probe tests and x64-host-to-x64/x86 polling process tests; the selector preflighted probe, adapter, and shim PE machines and both architecture legs passed
Manual evidence: none required by the packet; Gate A/B/C physical acceptance remains pending and unchanged
Known limitations: v1 does not virtualize toggle low bits, cursor/focus/capture, Raw Input, physical suppression, third-party/commercial targets, or protected processes; physical Gate A/B/C acceptance remains separate
Rollback result: the component suite proved all-or-rollback install, byte-exact reverse restoration, idempotent uninstall/reinstall, and retry after an uninstall write failure; controlled probe teardown refuses DLL unload unless restoration succeeds
Next packet: P3-API-03 is READY for a controlled cursor/clip/focus/capture shim while P3-API-02 regressions remain required

### 2026-08-25 — P3-API-03

State: VALIDATED
Branch/commit: `feat/p3-api-03-cursor-focus-shim`; validated implementation head `aa64fa9`
Windows CI: run `32792381573` passed native x64 and Win32/x86 24/24 CTest plus the dedicated x64-host-to-x64/x86 polling + cursor/focus cross-architecture matrix
Automated tests:
- strict GCC 15 syntax checks passed with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` for the changed Gate C implementation and tests
- portable adapter, protocol, probe-snapshot v2, cursor/focus policy, bounded IAT transaction, combined polling-plus-cursor rollback, malformed-plan, retry-uninstall, negative/extreme coordinate, and exact-restoration tests passed
- C11/C++ fixed-width ABI assertions cover adapter v2 clip/window structures and shim v1-to-v2 config mismatch; Windows CI will execute the actual DLL ABI smoke tests in x86 and x64
- declared Windows CTest additions cover local ordinary cursor/focus shim behavior and two controlled x64/x86 probes, including polling regression and host-native global-state preservation
Manual evidence: none claimed; physical Gate A/B/C, watchdog/crash recovery, Raw Input, device suppression/cloaking, commercial games, and protected processes remain pending/out of scope
Known limitations: v1 uses caller-declared 32-bit logical screen coordinates with no inferred DPI/client/physical transform; logical foreground/active/focus share one validated controlled target; `ShowCursor`, Raw Input virtualization, physical suppression/cloaking, third-party/commercial games, anti-cheat, and protected processes remain unimplemented/out of scope
Rollback result: portable component tests prove cursor/focus all-or-rollback, combined rollback of the polling set, reverse exact pointer restoration, retry after uninstall failure, and fail-closed legacy ABI rejection; Windows run `32792381573` also passed repeated process teardown, polling-only capability regression, exact unload restoration paths, and no-orphan controlled-process checks
Next packet: P3-RAW-01 is READY for a controlled Raw Input behavior probe; P3-RAW-02 and P3-REC-01 remain BLOCKED by their declared prerequisites

## Next Codex task

Use:

```text
Implement P3-RAW-01 exactly as specified in
  docs/implementation/PHASE3_INPUT_ISOLATION.md

Record controlled Raw Input registration, WM_INPUT, GetRawInputData, and
GetRawInputBuffer behavior before any Raw Input interposition. Preserve the
validated polling and cursor/focus shim regressions. Do not implement P3-RAW-02,
device suppression, third-party injection, game support, or protected-process work.
```
