# HydraSeat Implementation Status

## Current program state

- Current phase: **Phase 3 — Input Compatibility & Isolation**
- Current default packet: **P3-REC-01 — Gate C watchdog and crash recovery acceptance** (`CODE_COMPLETE`; real logoff exposed and then repaired a session-end ordering defect; repair head `bb8fd28` is green on Windows x64/x86 and Gate C cross-architecture CI, while human re-test of actual logoff plus shutdown/reboot acceptance remains pending)
- Parent fork-main baseline for P8-WATCH-01 validation: `494426e882832b0ae6d94a75d038473ba96f104a` (P3-D-01 integrated; physical acceptance remains pending)
- Current automated Windows CI evidence: fork PR #23 run `32973197727` validates session-end repair head `bb8fd28`: Windows x64 and Win32/x86 build/test jobs pass, Gate C x64-host-to-x64/x86 cross-architecture passes, and roadmap/current-packet validators remain green. This automated evidence does not replace the remaining human actual-logoff and shutdown/reboot acceptance.
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
| P3-RAW-01 Raw Input registration/data probe | VALIDATED | P3-API-01 | Windows run `32800513365`: native x64/x86 28/28 CTest, retained observed registration traces, replacement/remove/destroyed-HWND evidence, and repeated process teardown passed |
| P3-RAW-02 Raw Input virtualization shim | VALIDATED | P3-RAW-01, P3-API-02 | Windows run `32806163164`: native x64/x86 and x64-host-to-x64/x86 ordinary Raw Input API/two-process acceptance passed with zero cross-Seat/API/stale-token/queue-overflow counters |
| P3-ARCH-01 x86 Gate C build and cross-architecture launcher selection | VALIDATED | P3-IPC-01, P3-STATE-01 | Windows run `32727711605`: x64/x86 full CTest and x64-host-to-x86/x64 controlled target/probe matrix passed |
| P3-REC-01 Host/target/adapter crash and watchdog recovery | CODE_COMPLETE | P8-WATCH-01, P8-JOURNAL-01, P3-API-03 | Actual logoff against `8f0f4fd` removed all guarded processes but left the crash journal `active`, exposing that `WM_QUERYENDSESSION` returned before durable rollback. Repair head `bb8fd28` moves the session-end window to a dedicated message thread, blocks the query on bounded rollback completion, fails closed on timeout/unproven cleanup, and is green in PR #23 run `32973197727` on Windows x64/x86 plus Gate C cross-architecture. Human logoff re-test and shutdown/reboot remain PENDING |
| P3-HW-01 Gate A/B/C physical acceptance runner | CODE_COMPLETE | P3-QUEUE-01 | Fork PR #19 run `32911603828` validates the tooling on x64/x86 plus Gate C cross-architecture; real two-keyboard/two-pointing-device Gate A/B/C evidence remains manual and PENDING |
| P3-CTRL-01 XInput controlled adapter state | VALIDATED | P3-STATE-01 | Remediation head `b351afdd` validated by fork PR #15 run `32832036967`: native x64/x86 36/36 plus x64-host-to-x64/x86 zero-cross controller acceptance |
| P3-CTRL-02 DirectInput enumeration/visibility controlled adapter | VALIDATED | P3-CTRL-01, P3-ARCH-01 | Fork PR #16 run `32840474306`: native x64 and Win32/x86 full CTest passed with controlled DirectInput policy/probes plus read-only `DirectInputNativeObservationSelfTest`; Gate C cross-architecture regressions stayed green |
| P3-D-01 HidHide read-only availability/capability probe | VALIDATED | P3-PLAN-01 | Fork PR #20 run `32915683414` validates head `146b3e6`: native x64/x86 build/CTest plus Gate C cross-architecture pass; exact-version tri-state probe remains read-only and P3-D-02 stays blocked |
| P3-D-02 Guarded HidHide session-cloak lab | BLOCKED | P3-REC-01, P3-D-01 | Spare input/watchdog/timeout/crash rollback and physical evidence |
| P3-E-01 Open-source non-protected application profile | BLOCKED | P3-RAW-02, P3-API-03 | Reproducible profile and measured no-cross-state result |
| P3-E-02 First non-anti-cheat game profile | BLOCKED | P3-E-01, P3-D-02 or proven non-cloak suppression path | Explicit experimental compatibility entry |
| P3-E-03 Two different game zero-bleed proof | BLOCKED | P3-E-02, P3-CTRL-01 | Measured keyboard/mouse/controller bleed and latency report |
| P3-MET-01 Input latency and bleed measurement harness | VALIDATED | P3-QUEUE-01 | Fork PR #18 run `32857666855`: native x64 and Win32/x86 43/43 CTest pass metrics library/CLI/report plus instrumented Gate C host; receiver evidence remains explicit and physical zero-bleed/latency stays manual |

## Cross-phase prerequisites allowed to start early

| Packet | State | Reason |
| --- | --- | --- |
| P8-WATCH-01 Watchdog protocol and lease model | VALIDATED | Fork PR #21 run `32919928489` validates head `dceaab9`: native x64/x86 full CTest including `WatchdogProcessFaultTests` plus Gate C cross-architecture pass; actual Gate C recovery integration remains P3-REC-01 |
| P8-RESET-01 Emergency reset CLI contract | BLOCKED | Depends on watchdog rollback registry/contracts |
| P8-JOURNAL-01 Crash journal and safe-mode marker | VALIDATED | Fork PR #22 run `32947110442` validates exact implementation head `2b42d9a`: native x64/x86 full-project CTest plus Gate C cross-architecture pass; bounded durable journal/startup safe-mode contract validated, while Gate C recovery wiring remains P3-REC-01 |
| P4-RUN-01 Production host service/process skeleton | BLOCKED | Start after P3 controlled shim contract stabilizes |

## Manual gates

| Gate | State | Required evidence |
| --- | --- | --- |
| Gate A physical device observation | PENDING | Run P3-HW-01: two keyboards, two pointing devices, composite HID when available, repeated hot-plug + >=10-minute trace; no real manifest/report recorded yet |
| Gate B physical Seat routing | PENDING | Run P3-HW-01 source-profile + derived shared-case sessions; per-Seat route/target, unassigned, ambiguous, and missing-target evidence; no real manifest/report recorded yet |
| Gate C physical controlled-process routing | PENDING | Run P3-HW-01 with two physical input sets driving two HydraSeat-owned controlled targets plus P3-MET report/manual target-state review; no real manifest/report recorded yet |
| Gate C polling API interposition | VALIDATED (CONTROLLED CI) | HydraSeat-owned x64/x86 probes call the three ordinary polling APIs through the opt-in shim; physical/game acceptance is still separate |
| Gate C Raw Input API behavior baseline | VALIDATED (CONTROLLED CI) | Run `32800513365` retained native x64/x86 registration traces and validated replacement/remove/destroyed-HWND/process teardown; physical `WM_INPUT` and hot-plug remain P3-HW-01 |
| Gate C Raw Input API virtualization | VALIDATED (CONTROLLED CI) | Run `32806163164` passed native x64/x86 plus x64-host-to-x64/x86 ordinary registration/data/buffer APIs, two-process zero-cross counters, rollback, and existing polling/cursor regressions; physical evidence remains P3-HW-01 |
| Gate C watchdog/crash recovery | CODE_COMPLETE / REPAIR CI PASS | Existing crash/process acceptance remains green. First actual logoff on `8f0f4fd` proved zero guarded-process orphans but failed durable clean-stop (`phase=active`, 8 activation records), exposing premature `WM_QUERYENDSESSION` approval. Repair head `bb8fd28` makes the query wait boundedly for control-loop rollback and fails closed otherwise; PR #23 run `32973197727` passes Windows x64/x86 and Gate C cross-architecture. Human actual-logoff re-test and shutdown/reboot remain PENDING |
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

### 2026-08-25 — P3-RAW-01

State: VALIDATED
Branch/commit: `test/p3-raw-01-behavior-probe`; validated implementation head `ec704d4`
Windows CI: run `32800513365` passed native x64 and Win32/x86 28/28 CTest, retained both observed registration trace artifacts, and kept the existing Gate C cross-architecture job green
Automated tests:
- strict WSL GCC build passed with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` for the new trace/parser library, trace tests, and portable probe self-test
- trace tests cover round trip, old/future schema rejection, truncation, fixture/event/registration/packet bounds, invalid UTF-8, deterministic registration ordering, fixed-width runtime handles, registration contracts, Raw Input data size/query/read failures, and bounded raw-buffer traversal
- existing portable Gate C protocol/state, architecture selector, probe snapshot, adapter C++/C11 ABI, polling shim, cursor/focus shim, and host self-tests passed
- Windows x64/x86 traces agree that registration replacement is last-wins per usage, removal deletes only that usage, `RIDEV_INPUTSINK` is echoed by `GetRegisteredRawInputDevices`, and accepted `RIDEV_DEVNOTIFY` is not echoed in returned flags
- both traces retain a destroyed target HWND runtime value until a fresh valid registration replaces it; process teardown passed twice without prior-process registration dependence
- x64 observed `RAWINPUTHEADER`/`RAWINPUT` sizes 24/48 bytes; Win32/x86 observed 16/40 bytes and records the WOW64-compatible 8-byte raw-buffer alignment contract
Manual evidence: pending in P3-HW-01 for physical keyboard/mouse `WM_INPUT`, actual hot-plug ordering, and composite HID behavior; the committed fixture is explicitly synthetic
Known limitations: no Raw Input hook, synthetic HRAWINPUT/WM_INPUT, virtual queue/token table, physical suppression, third-party process, or game support exists; CI observed neither physical `WM_INPUT` nor device change, so those remain P3-HW-01
Rollback result: the standalone probe tracks only its own keyboard/mouse registrations, records bounded best-effort removal, makes cleanup idempotent, destroys owned windows, and contains teardown children in a kill-on-close Job Object
Next packet: P3-RAW-02 is READY; implement controlled Raw Input virtualization only against the validated P3-RAW-01 behavior contract and keep P3-HW-01 physical evidence separate

### 2026-08-25 — P3-RAW-02

State: VALIDATED
Branch/commit: `feat/p3-raw-02-virtualization-shim`; implementation `4b643d4968a8c899551cefeeaac181e929e2f8f4`, warning cleanup `56241cb`
Windows CI: run `32806163164` passed native x64, Win32/x86, and the dedicated x64-host cross-architecture matrix after the P3-RAW-02 probe warning cleanup
Automated tests:
- strict GCC 15 syntax and component builds pass with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`
- portable CMake/CTest passes 20/20 targets, including virtual registration/token/queue/data/buffer, C++ and C11 adapter ABI, four-function Raw Input IAT transaction, combined rollback, polling, cursor/focus, protocol, snapshot-schema v3, target, and host regressions
- Windows CTest executes local ordinary Raw Input API use and the dedicated artifact-matrix job executes x64-host-to-x64/x86 two-process acceptance
- the process snapshot schema emits bounded machine-readable expected/cross counters and failure counters; Windows acceptance requires nonzero expected keyboard/mouse counts, zero cross counters, zero API failures, zero destroyed-target/stale-token/queue-overflow failures, and zero final system error
Manual evidence: none claimed; physical keyboard/mouse `WM_INPUT`, hot-plug, composite HID, and physical zero-bleed remain P3-HW-01
Known limitations: only HydraSeat-owned startup-loaded controlled probes and the four allowlisted Raw Input APIs are supported; no physical suppression, device cloaking, third-party/commercial process, game, anti-cheat, DRM, protected-process, `GetRawInputDeviceInfo`, or `GetRawInputDeviceList` support is claimed
Rollback result: portable transaction tests pass all-or-rollback, combined polling prerequisite rollback, reverse exact restoration, idempotent install/uninstall, and retry after uninstall failure; Windows run `32806163164` also passed native x64/x86 process teardown/uninstall and the cross-architecture regression matrix
Next packet: P3-CTRL-01 is READY; implement controlled XInput state/slot remapping while P3-HW-01 remains PENDING and P3-E-01 remains blocked by metrics/recovery prerequisites

### 2026-08-25 — P3-CTRL-01

State: VALIDATED
Branch/commit: historical `feat/p3-ctrl-01-virtual-xinput`; implementation `2128ad89e5e31dd5e594c59abc887e6cb1098a57`, source-identity review fix `33dfb2b`, routing-hint hardening `c34364c`; remediation branch `fix/p3-ctrl-01-generation-snapshot-hardening`, validated remediation head `b351afdd60236b953d913b8488a5e350f705faec`; fork PR #15
Windows CI: fork PR #15 run `32832036967` reports head SHA `b351afdd60236b953d913b8488a5e350f705faec`; the pull-request checkout used merge ref `490af0f68e2a0c9b42e14efbb764b83f117f2910` (remediation head into fork-main `96e0f892bd4d32e0ce06fb8ddfc29a5255ea3559`). Native x64 and Win32/x86 each passed 36/36 CTest; the dedicated x64-host-to-x64/x86 Gate C matrix passed both controller legs.
Automated tests:
- strict GCC 15 CMake builds pass with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` for the bounded virtual XInput component, adapter ABI v4, controller protocol, planner, and controlled host/probe portable surfaces
- 14/14 portable Gate C CTest regressions pass, including virtual XInput state, C++/C11 adapter ABI, protocol, architecture, virtual Raw Input, polling, cursor/focus, target, and host tests
- deterministic component tests cover logical slots 0..3, invalid slot 4, independent contexts, duplicate-source rejection including runtime-hint changes on the same opaque source, packet change/wrap, capabilities/battery consistency, vibration routing, disconnect clearing, reconnect generation, stale state/vibration rejection, and reset
- fresh Windows cross-architecture acceptance in run `32832036967` runs two complete cycles for both x64 and x86 targets; each architecture reports Seat 1/Seat 2 state/capability/battery/vibration expected=2, every cross counter=0, `api_failures=0`, and `stale_accepted=0`; `runXInputSelfTest` requires cleanup success and both child processes not running after each cycle
Manual evidence: none claimed; physical XInput discovery/routing, physical vibration, physical suppression, and commercial-game behavior remain pending and separate from the synthetic controlled acceptance
Known limitations of the P3-CTRL-01 slice: no ordinary XInput API interposition, XInput DLL proxy, Raw HID/SDL, physical polling worker, physical controller mutation/hiding, third-party injection, or game support is implemented; DirectInput visibility/order is tracked separately by P3-CTRL-02
Rollback result: adapter reset deterministically clears all four controller slots; disconnect clears state/metadata/vibration and requires a newer source generation; controlled process acceptance runs two complete start/stop cycles and requires no surviving child
Remediation scope: reject stale/same-generation resurrection through explicit routing-hint remaps, preserve independent generation namespaces for genuinely different stable sources, and require canonical empty metadata for failed controller snapshot fields. Historical run `32816241577` predates this fix and cannot validate the repaired semantics.
Remediation evidence: strict GCC 15 CMake build with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror` passed all selected portable targets; 20/20 portable CTest cases passed across planner/observation, protocol, architecture, probe snapshot, Raw Input, virtual XInput, C++/C11 adapter ABI, polling/cursor/Raw Input shims, and target/host self-tests. Focused tests reproduced both defects before the production fix and then passed after it.
Post-remediation Windows evidence: run `32832036967` satisfies the required native x64, native Win32/x86, and x64-host-to-x64/x86 controlled matrix on Windows Server 2025 / MSVC. Historical run `32816241577` remains pre-remediation evidence only and is not used to validate the repaired semantics.
Next packet: P3-CTRL-02 is READY for a separate DirectInput enumeration/visibility packet. This validation task does not implement it.

### 2026-08-25 — P3-CTRL-02

State: VALIDATED
Branch/commit: `feat/p3-ctrl-02-directinput-visibility`; implementation commit `084043b` (`feat: implement P3-CTRL-02 DirectInput visibility policy`), pre-validation evidence head `f52535bdb160aa58006c694e86d536cca3d88529`; fork PR #16
Windows CI: fork PR #16 run `32840474306` validated head `f52535bdb160aa58006c694e86d536cca3d88529` against fork `main` via merge ref `98903728bf680e56cdc0c3f36f16ffdea7e2f991`. Native x64 and Win32/x86 full CTest jobs both passed, including `DirectInputPolicyTests`, `DirectInputControlledSeatA`, `DirectInputControlledSeatB`, and the read-only `DirectInputNativeObservationSelfTest`; the existing Gate C cross-architecture regression job also passed.
Automated tests:
- new portable `hydra_directinput_policy` and `hydra_directinput_probe` build under GCC 15 with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`;
- focused DirectInput CTest passes 3/3: policy, controlled Seat A (`C,A`), controlled Seat B (`B`), with calculated `cross_visible=0`;
- selected Phase 3 portable regression set passes 21/21 including planner/plan CLI observation/observation routing, Gate C protocol/architecture/snapshot, Raw Input, XInput, adapter C++/C11 ABI, polling/cursor/Raw Input shims, and target self-test;
- policy tests cover native-order reversal, duplicate friendly/product metadata, stable instance identity, two independent views, zero/duplicate/missing IDs, no partial output on failure, empty inventory, and 64-native/32-policy bounds;
- planner test proves enabling the controlled DirectInput visibility/order backend still leaves `PhysicalInputSuppression` missing and the production game plan unsupported.
Manual evidence: none claimed. Real physical DirectInput controller behavior, physical hiding/suppression, third-party/commercial games, SDL/Raw HID/vendor APIs, and production process deployment remain separate future evidence.
Known limitations: the Windows probe is read-only and calls only `DirectInput8Create` plus attached-game-controller `EnumDevices`; it does not implement a `dinput8.dll` proxy, COM API interposition, `CreateDevice`, device acquire/state, cooperative level, force feedback, or physical mutation.
Clean-room evidence: implementation used the packet/design specification and official Microsoft DirectInput 8 documentation for `IDirectInput8::EnumDevices`, `DIDEVICEINSTANCE`, and `DIEnumDevicesCallback`; no unlicensed devreorder/Duo implementation source was copied, translated, or consulted while writing the component.
Rollback result: this slice owns only process-local vectors/policy state and a read-only COM enumeration object; no persistent/device state is changed, so teardown is ordinary object/process destruction.
Next packet: P3-MET-01 is READY and becomes the default packet. Keep production DirectInput interposition, physical hiding/suppression, SDL/Raw HID, and game support outside P3-MET-01.

### 2026-08-25 — P3-MET-01

State: VALIDATED
Branch/commit: `feat/p3-met-01-input-metrics`; implementation commit `97c6ceb` (`feat: implement P3-MET-01 input metrics harness`)
Windows CI: fork PR #18 run `32857666855` validates head `55953b2205d0bb1f9f929c542fcb837a543e0824` via PR merge ref `13dc288b47ea271aec7cd50620fee0fc8fc71116`. Native x64 and Win32/x86 each pass 43/43 CTest; `InputMetricsTests`, `InputMetricsCliSelfTest`, and `InputMetricsFixtureReport` pass explicitly on both architectures, both builds produce `hydra_gate_c_host.exe`, and the existing Gate C cross-architecture job passes.
Automated evidence:
- strict GCC 15 `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror` build and focused metrics tests pass;
- metrics CTest passes 3/3 and selected Phase 3 portable regression passes 24/24 across planner, observation, Raw Input, XInput, DirectInput, Gate C protocol/architecture/snapshot, C++/C11 adapter ABI, shims, and target self-test;
- both schema-v1 JSON files parse, and the deterministic `hydra_input_metrics --fixture-report` output validates against `input_metrics_report_v1.schema.json`;
- tests cover bounded rotation, privacy redaction/diagnostic detail, event classes, deterministic nearest-rank p50/p95/p99, missing stages, receiver-evidence accounting, verified cross-Seat/process detection, timestamp order/wrap failure, explicit route-drop semantics, per-Seat/target-process queue high-water/drop aggregation, rollback duration, and CPU/memory sample hooks.
Live integration: `hydra_gate_c_host` records physical/enqueue/dequeue/write timestamps plus queue state into a fixed-capacity in-memory recorder; report sorting/JSON/file I/O occurs only after router/writer shutdown and rollback. Failed enqueue/write is `route_dropped`, never a fabricated enqueue/write latency stage. Host queue/write samples leave receiver identity unknown; only target apply/query evidence may populate actual receiving Seat/process fields.
Known limits: target apply/query timestamps are hooks but are not transported or fabricated by this packet, so live host reports keep those stages/end-to-end latency missing and report `receiver_verified_events=0` until P3-HW/E supplies validated receiver evidence. Therefore `cross_seat_events=0`/`cross_process_events=0` alone is not a zero-bleed claim. Future cross-process timestamps must be normalized into the recorder clock domain. Physical latency/zero-bleed, game performance, physical suppression/cloaking, and CPU/memory overhead remain manual/later acceptance under D-027.
Portable whole-project note: `cmake --build build-p3met --parallel` still reaches the pre-existing Linux-incompatible Windows-only `reset_input.cpp` target; every packet-scoped and selected Phase 3 target builds cleanly.
Rollback/privacy: metrics owns no persistent OS/device state; default redacted mode zeros key/button detail IDs before storage, `--metrics-diagnostic` is explicit, and generated Gate C metrics reports are ignored by Git.
Next packet: P3-HW-01 is READY and becomes the default packet. Implement only the guided/resumable physical acceptance runner and report tooling; real hardware pass/fail remains manual and must not be inferred from CI.

### 2026-08-26 — P3-HW-01

State: CODE_COMPLETE
Branch/commit: `test/p3-hw-01-hardware-acceptance`; implementation commit `fadea30` (`test: implement P3-HW-01 hardware acceptance runner`); PowerShell 5 portability fix `3253f61` (`fix: make P3-HW-01 hashing PowerShell 5 compatible`)
Windows CI: fork PR #19 run `32911603828` validates head `3253f617b15da31aa81155875c836eb99e6dcb3c`: native x64 and Win32/x86 full 45-test CTest jobs pass, including `Phase3HardwareAcceptanceParserTests` and `Phase3HardwareAcceptanceRunnerSelfTest`, and the dedicated Gate C cross-architecture job passes. The earlier run `32911051198` failed only the new runner self-test because `Get-FileHash` was unavailable in the runner's Windows PowerShell context; `3253f61` replaced that module dependency with bounded .NET SHA-256 hashing. CI validates tooling only and does not supply physical Gate A/B/C evidence.
Automated/local evidence:
- PowerShell 5 runner self-test passes and exercises schema parsing, a two-Seat schema-v2 fixture, four exclusive keyboard/mouse identities, source-profile SHA immutability, and derived shared/ambiguous profile creation;
- Python evidence parser self-test passes PENDING-without-human-evidence, explicit clean manual PASS, sensitive-key privacy failure, exact shared-device `AmbiguousSharedDevice`/no-route behavior, and verified cross-Seat metrics failure;
- strict GCC 15 `-Werror` input-observation build passes after default JSONL key-ID redaction; `InputObservationTests` verifies redacted-by-default and explicit diagnostic-key behavior;
- affected CMake targets (`hydra_input_lab`, `hydra_gate_c_host`, `hydra_gate_c_target`, input observation, and P3-MET) build successfully; focused acceptance/observation/metrics CTest passes 7/7 and the broader selected Phase 3 regression set passes 26/26 across planner, Raw Input, XInput, DirectInput, Gate C protocol/ABI/shims, metrics, and target self-test;
- the runner captures source profile SHA-256 and expected Seat ownership before execution, never edits the source profile, preserves failed traces, supports stage-by-stage resume, and permits report-only re-analysis without build binaries or the original profile;
- Gate A machine evidence requires at least four distinct physical input identities plus removal/arrival records; Gate B verifies both expected Seats in the exclusive trace and the exact derived shared-case device is ambiguous and never routed; Gate C rejects nonzero verified cross-Seat/process counters and queue/recorder loss while preserving missing-receiver-evidence warnings;
- `InputTraceWriter`, `hydra_input_lab`, and `hydra_gate_c_host` now redact virtual-key IDs by default; exact key IDs require the visibly warned `--trace-sensitive-keys` opt-in. P3-MET diagnostic detail remains a separate opt-in.
Manual evidence: PENDING. No claim is made yet for the user's two keyboards, two pointing devices, composite HID behavior, repeated hot-plug, 10-minute soak, physical controlled-target routing, physical latency, or zero bleed.
Rollback/cleanup: the runner mutates no persistent/device state and derives shared-case data only in ignored session output; it tracks only processes it starts and attempts tree cleanup in `finally`, while failed evidence remains on disk for review/resume.
Next packet after tooling CI/merge: P3-D-01 may proceed independently as the read-only HidHide availability probe, but P3-HW-01 remains `CODE_COMPLETE` until real physical evidence is recorded and must not be treated as a completed Phase 3 gate.

### 2026-08-26 — P3-D-01

State: VALIDATED
Branch/commit: `feat/p3-d-01-hidhide-readonly-probe`; implementation commit `6b354c216f30cfdec755f993553d270b536ddb50` (`feat: implement P3-D-01 read-only HidHide probe`); Windows SDK include fix `146b3e6399dd2c9bc59052cdbc4a392bd4d7697e` (`fix: include Windows IOCTL definitions for P3-D-01`)
Windows CI: fork PR #20 final run `32915683414` validates exact head `146b3e6399dd2c9bc59052cdbc4a392bd4d7697e`: native x64 and Win32/x86 configure/build/CTest jobs pass and the existing Gate C cross-architecture job passes. Initial run `32914259441` failed both native builds because `CTL_CODE` / `METHOD_BUFFERED` were used without the Windows SDK `<winioctl.h>` declaration; `146b3e6` fixes that declaration-only defect and the fresh matrix is green.
Automated/local evidence:
- strict GCC 15 builds pass with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror` for the probe library/CLI, fake-platform tests, planner tests, and coupled Phase 3 targets;
- focused `HidHideProbeTests`, `HidHideProbeCliSelfTest`, `IsolationPlannerTests`, and `IsolationPlanObservation` pass 4/4;
- the full locally available selected Phase 3 CTest set passes 31/31 across engine/hardware identity, planner, observation, Raw Input, XInput, DirectInput, P3-MET, P3-HW parser, Gate C protocol/architecture/snapshot, C++/C adapter ABI, polling/cursor/Raw Input shims, and target/host self-tests;
- fake-platform coverage includes no evidence, installed-without-interface, OS query failure, access denial without elevation, exact supported/unknown versions, canonical active/inverse values, malformed/truncated/oversized responses, contract-only session capability inference, zero mutation calls, bounded privacy-preserving output, deterministic repeat, and non-Windows unsupported behavior;
- roadmap validation and current-packet lookup pass; Windows Git `git diff --check` passes.
Read-only evidence: exact `HidHide` SCM service identity/configuration, documented interface GUID `{0C320FF7-BD9B-42B6-BDAF-49FEB9C91649}`, driver file version, and only function 2052 (`GET_ACTIVE`) plus function 2054 (`GET_WLINVERSE`) with canonical one-byte Boolean responses. Exact known versions are `1.7.339.0`, `1.7.344.0`, and `1.7.346.0`; every other version is installed-unverified.
Mutation/privacy: no installer, service/device mutation, administrator-elevation request, set/list/session IOCTL, allow/deny/session-list read, device-path output, or third-party binary exists in this packet.
Planner behavior: unavailable and installed-unverified HidHide remain unavailable; verified-supported may advertise only `PhysicalDeviceCloaking` while retaining administrator, kernel-driver, recovery-guard, session-scope, and high-risk requirements. `PhysicalInputSuppression` remains missing, so production zero-bleed profiles remain unsupported.
Manual evidence: none claimed. P3-HW-01 remains `CODE_COMPLETE`, and physical Gate A/B/C remain PENDING.
Known limitation: authoritative compatibility is deliberately an exact three-version allowlist; newer or otherwise unknown builds require a separate public-contract review before recognition.
Next packet: P8-WATCH-01 became the current cross-phase prerequisite because P3-REC-01 depends on it; P3-D-02 remains blocked. Do not start P3-D-02 directly.

### 2026-08-26 — P8-WATCH-01

State: VALIDATED
Branch/commit: `feat/p8-watch-01-watchdog-rollback`; implementation `238dadb` (`feat: implement P8-WATCH-01 watchdog lease and rollback`); Windows macro-safety fix `dceaab9a6626eaac6848f0083fba586f716bf812` (`fix: make P8-WATCH-01 Windows headers macro-safe`)
Windows CI: fork PR #21 run `32919928489` validates exact head `dceaab9a6626eaac6848f0083fba586f716bf812`: native x64 and Win32/x86 full-project builds and unfiltered CTest pass, including the new watchdog protocol/process fault tests, and the existing Gate C cross-architecture regression job passes. Initial run `32919575735` exposed only the Windows `min`/`max` macro collision fixed by `dceaab9`.
Automated/local evidence:
- strict GCC 15 builds pass with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror` for the watchdog executable plus protocol/rollback registry; `WatchdogProtocolTests` pass;
- the complete locally available selected portable suite passes 32/32, preserving engine/hardware, planner, HidHide, observation, Raw Input, XInput, DirectInput, metrics, P3-HW parser, Gate C protocol/ABI/shim, target, and host regressions;
- protocol tests cover fixed-width round trip, future/bad magic/reserved/truncated/oversized rejection, zero/duplicate/unknown manifest identities, maximum action count, lease/disarm/status framing, reverse activation order, idempotent replay, partial failure/retry, armed-plan replacement rejection, and reconstructed-manifest already-satisfied behavior;
- Windows process/fault coverage launches `hydra_watchdog.exe` through an explicit inherited-handle allowlist and passes on both x64 and Win32/x86 for clean disarm after host rollback, disarm safety-backstop rollback, lease expiry, stale-sequence protocol failure, PID creation-time mismatch without killing the unrelated process, and host death with independent target cleanup.
Security/recovery boundary: the control plane is an anonymous inherited-pipe capability with session/generation/sequence correlation; the manifest carries no arbitrary command/path field. Only exact `PID + creation time` process termination mutates OS state in this packet. Other typed rollback actions fail closed as `RecoveryRequired` until their owning packet supplies a narrow executor.
Manual evidence: none claimed. P3-HW-01 remains `CODE_COMPLETE`; physical Gate A/B/C, production Gate C watchdog integration, durable crash journal/safe mode, emergency reset, device cloaking, and game compatibility remain pending/later packets.
Known limitation: P8-WATCH-01 does not persist a crash journal and does not implement cryptographic signing for its non-nameable inherited local transport. Durable journal/restart recovery belongs to P8-JOURNAL-01; any future named/cross-trust transport requires explicit authenticated framing.
Next packet: P8-JOURNAL-01 is now the current critical prerequisite; P3-REC-01 remains blocked until the durable journal/safe-mode contract is also validated.

### 2026-08-26 — P8-JOURNAL-01

State: VALIDATED
Branch/commit: `feat/p8-journal-01-crash-journal`; implementation commit `2b42d9a7e585a2629f27911ed7a8683298ec9ab8` (`feat: implement P8-JOURNAL-01 crash journal`).
Windows CI: fork PR #22 run `32947110442` validates exact implementation head `2b42d9a7e585a2629f27911ed7a8683298ec9ab8`: native x64 and Win32/x86 full-project configure/build/unfiltered CTest pass, and the existing Gate C cross-architecture regression job passes.
Automated/local evidence:
- `cmake --build build-p8journal --target crash_journal_tests --parallel` passes and focused `CrashJournalTests` passes;
- the complete locally available portable CTest suite passes 33/33, including all existing watchdog, planner, observation, Raw Input, XInput, DirectInput, metrics, hardware-acceptance parser, Gate C protocol/ABI/shim, target, and host regressions;
- a fresh GCC 15 strict build of `crash_journal_tests` passes with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`, and its focused CTest passes;
- roadmap validation reports 115 packet definitions and 0 warnings; Windows Git `git diff --check` passes. The WSL Git executable incorrectly reports repository-wide CRLF lines as trailing whitespace, so Windows Git is the authoritative whitespace check for this Windows worktree;
- tests cover fixed-width bounded codec round trip, checksum corruption, truncation/future/oversized rejection, deterministic canonical rollback-plan binding, invalid/stale action identity, reverse rollback order, maximum 32-action lifecycle fit, every valid nonterminal startup boundary, explicit failure state, critical write and atomic-replace failures, immutable-history/truncation rejection, one-record durable-boundary enforcement, bounded four-slot rotation, active safe-mode activation blocking, verified-reset-only clearing, journal-not-instruction-channel behavior, and native temporary-directory persistence.
Persistent-state/recovery boundary: schema v1 is capped at 8 KiB with at most 160 records, 16 snapshot references, and four history slots. Production Windows storage resolves to current-user LocalAppData and uses write-through/flush plus temp-file atomic replacement. Storage is synchronous/single-writer and forbidden on latency-sensitive input callbacks. Corrupt/unreadable/incomplete state fails closed into safe mode; a journal write failure blocks risky activation.
Security/privacy: persisted bytes contain only fixed-width session/lease/generation/action/snapshot identities and digests. The deterministic plan/journal digest is correlation evidence, not authentication. Journal bytes never manufacture recovery actions; action IDs and generations must validate against the trusted watchdog manifest. No raw input, credentials, command strings, executable paths, arbitrary process data, or third-party code are persisted.
Manual evidence: none claimed or required by this foundation packet. P3-HW-01 remains `CODE_COMPLETE`; physical Gate A/B/C remain PENDING.
Known limitation: P8-JOURNAL-01 supplies the durable evidence/safe-mode/reset-verification contract only. Actual Gate C watchdog/process integration and forced-failure acceptance remain `P3-REC-01`; the standalone emergency reset executable remains `P8-RESET-01`.
Next packet: P3-REC-01 is READY and becomes the current default packet. Implement only Gate C watchdog/process recovery wiring and its declared forced-failure acceptance matrix; do not start P3-D-02 or P8-RESET-01 in the same task.

### 2026-08-26 — P3-REC-01

State: CODE_COMPLETE
Branch/commit: `feat/p3-rec-01-gate-c-recovery`; session-end repair implementation head `bb8fd28ae02ee245f7243b1743fd3882576548d5` follows the earlier `8f0f4fd` first-pass monitor implementation and the previously green P3-REC evidence heads.
Windows CI: fork PR #23 run `32973197727` passed exact repair head `bb8fd28ae02ee245f7243b1743fd3882576548d5` on Windows/MSVC x64, Win32/x86, and the x64-host-to-x64/x86 Gate C cross-architecture matrix; roadmap/current-packet validators and Raw Input trace checks also pass.
Automated/local evidence:
- `GateCRecoveryCoreTests`, `GateCTargetSelfTest`, and `GateCHostPortableSelfTest` pass after the recovery integration; the complete locally available portable CTest suite passes 34/34;
- focused GCC 15 recovery core builds/tests pass with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`;
- the pre-existing Linux whole-project build still stops only at Windows-only `reset_input.cpp` including `windows.h`; `cmake --build build-p3rec --parallel -- -k` builds the remaining available targets and the full portable CTest suite remains green;
- recovery core tests reject duplicate action/ordinal/exact process identities, bind only trusted exact-process termination actions, preserve volatile journal state on durable-write failure, complete a full reverse clean rollback, and persist `RecoveryRequired` on unresolved cleanup;
- the Windows process matrix is implemented for clean/repeated cycles, lease stall, exact target kill, watchdog kill/restart, pipe disconnect, controlled adapter loss, abrupt exit of a polling-shim process, independent UI-surrogate death, session-end notification handling, stale-journal startup blocking, and a real host-kill case that waits on exact watchdog/target process handles for orphan detection;
- existing `GateCPollingShimTests` provide the coupled partial-init proof: partial IAT patch failure rolls back earlier slots, failed rollback remains explicit/retryable, complete uninstall restores every original pointer, and repeated uninstall is idempotent.
Ownership/recovery boundary: the host remains authoritative; UI death does not own or stop the session. All watchdog/journal I/O is on the control path, not Raw Input callbacks. Targets are created suspended, exact process identities are registered with the independent watchdog, and journal prepare boundaries are durable before target threads resume. The watchdog manifest contains no arbitrary command/path action and cannot target the host, watchdog, unrelated process, or device/global state.
Automated acceptance: clean and repeated start/stop, forced real host death, controlled target death, watchdog death/restart verification, pipe disconnect, adapter failure, shim-owning process abnormal exit, independent UI-surrogate death, stale-journal startup refusal, and restart blocking after forced failure all execute with actual built Windows processes in `GateCWatchdogRecoveryTests`. The session-end cases now run the hidden top-level window on a dedicated message thread: `WM_QUERYENDSESSION` sets only the atomic stop request, must remain blocked while the ordinary host control loop performs exact-process rollback and durable `CleanStop`, returns TRUE only after a bounded cleanup-complete signal, and returns FALSE on timeout/unproven cleanup. The self-test asserts the query cannot complete before rollback is signaled.
Local real-process acceptance: on committed docs-head `0c24fb04df145c94946dfd4bb4cc250d1fb7723b`, a newly provisioned native MSVC x64 Release build reruns the complete 53/53 CTest suite successfully. The ignored local acceptance evidence under `out/p3-rec-acceptance/20260826-local` separately runs 12 safe x64 real-process cases: two clean cycles with generation `1 -> 2`, lease stall, target kill, watchdog kill/restart, pipe disconnect, adapter failure, shim-owner abnormal exit, UI-surrogate loss, controlled notification paths, and stale-journal startup refusal. Every case preserves the same unrelated sentinel identity and leaves zero HydraSeat target/watchdog orphans; cleanable faults end with durable `RollbackVerified` + `CleanStop`, while the stale journal remains `preparing` with `safe_mode_reason=incomplete-session`.
External host-death acceptance: a dedicated local harness captures exact `PID + creation time` identities for the real x64 host, watchdog, two controlled targets, and an unrelated sentinel, revalidates the host identity immediately before terminating only that host PID, and does not manually terminate its guarded children. The independent watchdog removes both targets and itself with no PID-reuse mismatch, the unrelated sentinel remains the same live identity, a subsequent real Gate C start returns `127` with `previous runtime session is incomplete`, and no target/watchdog process is created. The durable journal remains `active` with `safe_mode_reason=incomplete-session`, which is the intentional fail-closed evidence for abrupt host death rather than a forged `CleanStop`.
First actual-logoff result: human-initiated Windows Sign out was executed against first-pass session-end head `8f0f4fd`. Pre-action evidence recorded host PID `12848`, target PIDs `2512`/`19124`, watchdog PID `10528`, exact creation times, Session 3, and an `active` generation-1 journal with 8 records through `ActivationCommitted`. After sign-in, every exact identity was gone with zero HydraSeat orphans and no PID reuse, but the journal was still `phase=active` with the same 8 records, `safe_mode=absent`, and no metrics file. Verdict: **FAIL** for durable session-end cleanup even though watchdog/process cleanup succeeded; this proved Windows could continue session termination after the window returned TRUE before the ordinary control loop reached `RollbackStarted`/`CleanStop`.
Acceptance-discovered repair: repair head `bb8fd28ae02ee245f7243b1743fd3882576548d5` moves the hidden session-end window onto a dedicated message thread. `WM_QUERYENDSESSION` requests stop and waits up to a strict bound for a cleanup-complete event while the normal control loop performs fast exact-process rollback for session-end requests; TRUE is returned only after verified durable cleanup, while timeout or failed/unproven rollback returns FALSE and vetoes session end. Early interactive failure paths also signal their rollback result. Local MSVC x64 Release passes 53/53, and exact-head PR #23 run `32973197727` passes Windows x64, Win32/x86, and Gate C cross-architecture.
Known limits/manual gate: the first actual OS logoff was a genuine FAIL on superseded head `8f0f4fd`; actual logoff must be re-run against repair head `bb8fd28`, and actual shutdown/reboot plus post-sign-in/post-reboot review remain required before `VALIDATED`. P3-D-02 and P8-RESET-01 remain outside this task and blocked/not started.
Next action: re-arm the human-controlled actual-logoff acceptance against repair head `bb8fd28`, verify post-sign-in exact identities/orphan state plus durable `RollbackVerified` + `CleanStop`, then separately arm actual shutdown/reboot and verify the same postconditions after reboot. Keep `CODE_COMPLETE` until both disruptive cases pass; do not start P3-D-02 or P8-RESET-01.

## Next Codex task

Use:

```text
Finish only the remaining human/disruptive P3-REC-01 Windows acceptance gate.

Repair head `bb8fd28ae02ee245f7243b1743fd3882576548d5` is CODE_COMPLETE and
PR #23 run `32973197727` is green on Windows x64/x86 plus Gate C cross-architecture.
The first actual logoff on superseded head `8f0f4fd` was a genuine FAIL: all exact
guarded identities disappeared but the journal remained Active because the session
query was approved before durable rollback. The repair now blocks WM_QUERYENDSESSION
on bounded cleanup completion and vetoes the session end if cleanup is not proven.
Re-run actual Windows logoff under explicit human control, inspect exact identities,
orphan state, and durable RollbackVerified/CleanStop after sign-in, then separately
arm actual shutdown/reboot and inspect the same postconditions after reboot. Keep
P3-REC-01 CODE_COMPLETE until both disruptive cases pass. Do not start P3-D-02,
P8-RESET-01, physical cloaking, or game profiles; keep P3-HW-01 CODE_COMPLETE and
physical Gate A/B/C rows PENDING.

```
