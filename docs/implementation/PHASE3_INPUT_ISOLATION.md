# Phase 3 — Input Compatibility and Isolation

## Phase objective

Complete the input path from physical Seat-owned devices to documented target-process API behavior, with measurable zero cross-Seat bleed and deterministic rollback for a limited, explicit set of non-protected profiles.

Phase 3 does not mean universal game support. It ends when HydraSeat can truthfully support a small compatibility matrix and explain exactly which APIs/backends are required.

## Existing validated baseline

Already implemented and retained as regression coverage:

- capability vocabulary, profiles, backend descriptors, and `hydra_plan`;
- stable Raw Input observation and composite-HID-aware hot-plug ledger;
- fail-closed keyboard/mouse Seat routing;
- Gate A/B two-window lab and JSONL trace;
- versioned Gate C host/target protocol;
- local named-pipe transport and session handshake;
- process-local adapter C ABI;
- synthetic two-process keyboard/mouse/cursor/focus state separation;
- bounded per-target writer queues;
- fail-closed stale-sequence process test.

The current controlled target calls the adapter directly. An unmodified application still calls the real Windows APIs. Closing that gap is the immediate critical path.

## Phase exit gate

Phase 3 is complete only when all are true:

1. HydraSeat-owned probes obtain Seat-local values through the actual API surfaces declared by a profile.
2. x64 and x86 adapter paths are tested where support is claimed.
3. keyboard/mouse physical acceptance passes with two devices per class.
4. controller API behavior is profile-specific and tested for at least XInput.
5. replacement input works before any physical device cloaking/suppression is enabled.
6. watchdog/reset/crash rollback passes.
7. one open-source application and one explicit non-anti-cheat game profile pass.
8. two different target applications run concurrently with measured zero cross-Seat key/button/movement events for the declared test duration.
9. latency, drops, queue depth, process exits, and rollback results are recorded.
10. unsupported/protected profiles fail closed.

## Dependency graph

```text
P3-API-01 -> P3-API-02 -> P3-API-03
      |            |
      +-> P3-RAW-01 -> P3-RAW-02

P3-ARCH-01 --------------------------+
P3-MET-01 ---------------------------+-> P3-E-01 -> P3-E-02 -> P3-E-03
P3-CTRL-01 -> P3-CTRL-02 ------------+
P8-WATCH-01 -> P3-REC-01 -> P3-D-02 -+
P3-D-01 --------------------^         |
P3-HW-01 -----------------------------+

all required packets -> P3-CLOSE-01
```

---

## P3-API-01 — Controlled Win32 API probe baseline

**State:** VALIDATED

**Goal**

Create a HydraSeat-owned probe that calls ordinary Windows input/focus/cursor APIs and reports their real OS values beside the existing direct adapter values. This provides the before-interposition baseline.

**Depends on**

- P3-STATE-01
- P3-PROC-01
- D-006, D-012

**Create/modify**

- `src/gate_c_api_probe.cpp`
- `include/hydra/gate_c_probe_snapshot.hpp`
- `src/gate_c_probe_snapshot.cpp`
- `tests/test_gate_c_probe_snapshot.cpp`
- `CMakeLists.txt`
- `docs/PHASE3_GATE_C_TESTING.md`

**Public/internal contracts**

```cpp
struct OsInputSnapshot;
struct AdapterInputSnapshot;
struct ProbeComparison;
```

The snapshot records:

- `GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState` for configured probe keys;
- `GetCursorPos`, `GetClipCursor`;
- `GetForegroundWindow`, `GetFocus`, `GetCapture`;
- process/thread/window IDs and monotonic sequence;
- adapter C ABI results for the same conceptual state.

**Implementation skeleton**

1. Add a controlled probe executable derived from the existing target process.
2. Accept Seat/pipe/token arguments through the existing Gate C launch path.
3. Query OS APIs on the probe UI thread.
4. Query adapter state through the C ABI.
5. publish a deterministic comparison snapshot to the host and visible UI;
6. add a `--baseline-self-test` that proves OS and adapter views can intentionally differ;
7. never patch/import-hook any function in this packet.

**Invariants**

- probe code changes no global cursor/clip/focus state;
- results name the API and calling thread;
- no API result is treated as Seat-local before interposition;
- snapshot serialization is versioned and bounded.

**Automated tests**

- snapshot encode/decode and invalid-size/version tests;
- real Windows probe process launches, handshakes, reports a snapshot, and exits;
- adapter may report `virtualForeground=true` while OS foreground is false;
- timeout and missing-window failure tests;
- no child remains after failure.

**Manual acceptance**

None. This is a controlled baseline packet.

**Done when**

Windows CI can launch two probes and show that each has independent adapter state while ordinary OS APIs still expose shared/global state.

**Suggested commit**

`feat: implement P3-API-01 controlled Win32 API probe`

---

## P3-API-02 — Startup-loaded polling API shim for controlled probes

**State:** VALIDATED

**Goal**

Make a HydraSeat-owned probe receive Seat-local `GetAsyncKeyState`, `GetKeyState`, and `GetKeyboardState` results through the ordinary imported API calls.

**Depends on**

- P3-API-01
- P3-ARCH-01 for any architecture declared supported

**Create/modify**

- `include/hydra/gate_c_shim_api.h`
- `src/gate_c_shim.cpp`
- `src/gate_c_shim_iat.cpp`
- `include/hydra/win32_iat_patch.hpp`
- `tests/test_gate_c_polling_shim.cpp`
- controlled probe launch manifest/config

**Implementation skeleton**

1. Build a separate architecture-neutral `hydra_gate_c_shim.dll` under each
   deterministic `gate-c/x86` and `gate-c/x64` directory.
2. Load it explicitly at controlled probe startup; do not inject an already running process.
3. Patch only the controlled probe's intended imports.
4. Keep original function pointers and an idempotent unpatch table.
5. Resolve Seat-local state through `hydra_gate_c_adapter` C ABI.
6. Pass through to the original API when the shim is inactive/uninitialized or the queried key is outside the supported domain.
7. expose hook install/uninstall diagnostics and generation ID;
8. restore all imports before unload.

**Invariants**

- no system-wide hook;
- no patch outside the controlled probe;
- patch targets are allowlisted by module/function;
- install is all-or-rollback, not partially active;
- recursion is impossible;
- `GetAsyncKeyState` edge consumption matches the adapter contract;
- toggle-key behavior is explicitly unsupported or implemented/tested, never guessed.

**Automated tests**

- install/uninstall/reinstall;
- missing import and duplicate import entries;
- original pointer restoration byte-for-byte;
- two probes see different A/B state through ordinary API calls;
- stale adapter connection fails closed or passes through according to declared mode;
- probe crash leaves no other process modified.

**Manual acceptance**

None beyond Windows integration CI.

**Done when**

Two controlled probes call the ordinary polling APIs and receive their own Seat states, and unpatch restores the original Windows results.

**Suggested commit**

`feat: implement P3-API-02 controlled polling shim`

---

## P3-API-03 — Cursor, clip, focus, and capture shim

**State:** READY

**Goal**

Provide Seat-local cursor/focus/capture views to HydraSeat-owned probes without moving or clipping the global Windows cursor.

**Depends on**

- P3-API-02

**Target API set**

Initial controlled allowlist:

- `GetCursorPos`, `SetCursorPos`;
- `ClipCursor`, `GetClipCursor`;
- `GetForegroundWindow`, `GetActiveWindow`, `GetFocus`;
- `GetCapture`, `SetCapture`, `ReleaseCapture`;
- optionally `ShowCursor` after its process/thread semantics are specified and tested.

**Create/modify**

- `src/gate_c_cursor_focus_shim.cpp`
- `include/hydra/gate_c_cursor_focus_policy.hpp`
- `tests/test_gate_c_cursor_focus_shim.cpp`
- adapter ABI version/migration tests if new fields are required

**Implementation skeleton**

1. Define policy for pure query, virtual mutation, and pass-through APIs.
2. Convert between screen, client, and Seat-local coordinates explicitly.
3. Store virtual clip/capture/focus in the adapter, not static DLL globals.
4. make setters affect only virtual state when profile policy requires virtualization;
5. preserve original behavior when the shim is inactive;
6. unhook and clear virtual state on shutdown/error.

**Invariants**

- no global `ClipCursor` call for two-Seat mode;
- one Seat cannot alter another Seat's adapter state;
- coordinate conversions are DPI-aware;
- invalid/stale HWNDs produce explicit failure;
- actual and virtual foreground are both diagnostic fields.

**Automated tests**

- two probes simultaneously report virtual foreground/capture true;
- actual OS foreground remains singular;
- virtual cursor stays inside each Seat clip rectangle;
- global cursor position/clip are unchanged before/after test;
- install/uninstall and target-window destruction.

**Done when**

Controlled probes use the ordinary API calls and observe independent Seat-local values with proven global-state restoration.

**Suggested commit**

`feat: implement P3-API-03 cursor and focus shim`

---

## P3-RAW-01 — Controlled Raw Input behavior probe

**State:** BLOCKED

**Goal**

Record the exact behavior of `RegisterRawInputDevices`, registration replacement, `WM_INPUT`, `GetRawInputData`, and `GetRawInputBuffer` in HydraSeat-owned probes before virtualization.

**Depends on**

- P3-API-01

**Create/modify**

- `src/gate_c_raw_input_probe.cpp`
- `include/hydra/raw_input_probe_trace.hpp`
- `tests/test_raw_input_probe_trace.cpp`

**Implementation skeleton**

1. Probe keyboard and mouse usage registrations with controlled windows.
2. test registration replacement within one process;
3. test foreground/background flags and device notifications;
4. record structure sizes, handles, message ordering, and buffer semantics;
5. never interfere with the production host registration during the test;
6. emit a versioned trace fixture used by P3-RAW-02 tests.

**Automated tests**

- parser/fixture tests;
- Windows registration lifecycle integration;
- malformed sizes and destroyed target window;
- probe cleanup leaves no registration owned by the dead process.

**Manual acceptance**

Physical keyboard/mouse trace on the target PC is part of P3-HW-01.

**Done when**

P3-RAW-02 has a stable behavior specification and fixtures rather than assumptions.

**Suggested commit**

`test: implement P3-RAW-01 Raw Input behavior probe`

---

## P3-RAW-02 — Controlled Raw Input virtualization shim

**State:** BLOCKED

**Goal**

Make two HydraSeat-owned probes receive only their Seat's synthetic Raw Input through ordinary registration/data APIs.

**Depends on**

- P3-RAW-01
- P3-API-02
- P3-ARCH-01 for declared architectures

**Create/modify**

- `include/hydra/virtual_raw_input.hpp`
- `src/virtual_raw_input.cpp`
- `src/gate_c_raw_input_shim.cpp`
- `tests/test_virtual_raw_input.cpp`
- `tests/test_gate_c_raw_input_process.cpp`

**Core types**

```cpp
struct VirtualRawRegistration;
struct VirtualRawPacket;
class VirtualRawInputQueue;
class SyntheticRawHandleTable;
```

**Implementation skeleton**

1. Interpose controlled probe registration/query/data APIs.
2. record requested usage/page/flags and target window;
3. maintain a bounded per-process raw packet queue;
4. create non-pointer synthetic tokens with generation checks;
5. post synthetic `WM_INPUT` only to the registered controlled target;
6. resolve token in `GetRawInputData`/buffer calls with exact size/query semantics;
7. expire tokens after consumption/timeout/window destruction;
8. pass through unsupported usages only when profile policy explicitly allows it.

**Invariants**

- Seat A packet never enters Seat B queue;
- token collision/stale reuse is rejected;
- query-then-read sizes match Windows-compatible expectations;
- queue overflow is visible and session-fatal for zero-bleed profiles;
- removal/unpatch restores normal registration behavior.

**Automated tests**

- keyboard make/break and mouse buttons/wheel/relative movement;
- size query, insufficient buffer, stale token, duplicate read;
- two processes, two windows, no cross-Seat packet;
- destroyed window and process crash;
- unpatch returns probe to native Raw Input behavior.

**Done when**

Two controlled probes consume separate Raw Input streams through the ordinary API surface and rollback restores the baseline.

**Suggested commit**

`feat: implement P3-RAW-02 controlled Raw Input virtualization`

---

## P3-ARCH-01 — x86/x64 adapter and controlled-target matrix

**State:** VALIDATED

**Goal**

Support and test both target architectures without pointer-size assumptions.

**Create/modify**

- CMake architecture-specific output naming;
- CI matrix for x64 and x86;
- `hydra_gate_c_target32/64.exe` or architecture-neutral installed naming;
- `hydra_gate_c_shim.dll` under each architecture directory when P3-API-02 exists;
- host architecture selector and manifest;
- protocol/ABI tests across both builds.

**Invariants**

- one host selects target-matching binaries deterministically;
- IPC carries fixed-width fields only;
- an x64 module is never loaded into x86 or vice versa;
- missing architecture produces `Unsupported`, not a launch attempt;
- artifact layout is deterministic for installer/GUI discovery.

**Automated tests**

- x86 build and CTest;
- x64 host launches x86 controlled target through named pipe;
- ABI struct sizes match declared constants in C and C++;
- wrong-architecture manifest rejection.

**Done when**

The compatibility planner may truthfully advertise x86 controlled adapter availability.

**Suggested commit**

`build: implement P3-ARCH-01 x86 Gate C matrix`

---

## P3-CTRL-01 — XInput controlled state and slot remapping

**State:** READY

**Goal**

Add process-local XInput state to the controlled adapter and prove two targets can map logical user index 0 to different selected physical/test controllers.

**Create/modify**

- `include/hydra/virtual_xinput_state.hpp`
- `src/virtual_xinput_state.cpp`
- adapter C ABI v2 or compatible extension functions;
- Gate C protocol controller messages;
- controlled XInput probe;
- tests.

**Implementation skeleton**

1. Define normalized controller state/capabilities/vibration contracts.
2. map profile-selected physical/controller source to process-local logical slots;
3. expose controlled equivalents for state, capabilities, battery, and vibration;
4. make disconnect/reconnect explicit;
5. keep physical polling outside the latency-sensitive Raw Input callback;
6. reserve real API interposition for a later controlled shim packet if needed.

**Invariants**

- target index mapping is profile-defined;
- vibration never reaches the wrong physical controller;
- disconnect clears stale buttons;
- no controller is claimed supported solely from detection.

**Automated tests**

- two contexts map slot 0 to different synthetic controllers;
- capabilities and vibration routing;
- reconnect/generation change;
- missing controller and stale packet;
- adapter ABI migration.

**Done when**

Controlled targets have independent, queryable XInput-style state and the planner has a tested capability boundary.

**Suggested commit**

`feat: implement P3-CTRL-01 virtual XInput state`

---

## P3-CTRL-02 — DirectInput enumeration and visibility adapter

**State:** BLOCKED

**Goal**

Provide a clean-room, process-local controlled DirectInput enumeration/order/visibility adapter.

**Depends on**

- P3-CTRL-01
- P3-ARCH-01

**Create/modify**

- `include/hydra/directinput_policy.hpp`
- `src/directinput_policy.cpp`
- controlled DirectInput probe/wrapper target;
- profile fields for stable instance IDs and order;
- tests.

**Implementation skeleton**

1. Record controlled DirectInput 8 enumeration behavior.
2. represent allowlist/order by stable instance identity;
3. filter and order enumerated devices for the controlled probe;
4. preserve unsupported interfaces as pass-through or explicit unsupported;
5. do not copy unlicensed wrapper code;
6. document that XInput/Raw HID/SDL remain separate.

**Done when**

Two controlled probes enumerate only their declared DirectInput devices in deterministic order.

**Suggested commit**

`feat: implement P3-CTRL-02 DirectInput visibility policy`

---

## P3-MET-01 — Input latency, queue, and bleed metrics

**State:** READY

**Goal**

Provide objective metrics used by all later physical/application/game gates.

**Create/modify**

- `include/hydra/input_metrics.hpp`
- `src/input_metrics.cpp`
- `hydra_input_metrics.exe` or subcommand;
- trace schema and report generator;
- tests.

**Metrics**

- physical observation timestamp;
- route enqueue/dequeue/write/apply/query timestamps;
- queue depth/high-water/drop count;
- target/Seat/process IDs;
- cross-Seat event count;
- key/button/movement/wheel class;
- rollback duration;
- host CPU/memory sample hooks.

**Invariants**

- monotonic clocks used for latency;
- metrics collection cannot block the input path;
- traces are bounded/rotated;
- privacy mode can omit key identity while preserving event class;
- cross-Seat metric is computed from expected owner versus receiving process.

**Automated tests**

- deterministic fixture report;
- clock wrap/ordering and missing stage;
- overflow/drop reporting;
- no negative latency silently accepted.

**Done when**

P3-HW/E packets can produce a reproducible report with p50/p95/p99 and zero-bleed counters.

**Suggested commit**

`feat: implement P3-MET-01 input metrics harness`

---

## P3-HW-01 — Gate A/B/C physical acceptance runner

**State:** READY

**Goal**

Turn existing manual checklists into one guided, resumable physical acceptance session and report.

**Create/modify**

- `tools/run_phase3_hardware_acceptance.ps1`
- `tools/summarize_phase3_trace.py` or C++ CLI if Python is not a supported user dependency;
- `docs/hardware/PHASE3_ACCEPTANCE_TEMPLATE.md`;
- output manifest schema;
- tests for report parsing.

**Procedure coverage**

- two keyboards;
- two mice or mouse/touchpad;
- composite HID child removal;
- repeated unplug/replug;
- exclusive and shared/unassigned profile cases;
- two controlled Gate C target processes;
- soak duration and drop/queue metrics.

**Invariants**

- the runner never marks success from process exit alone;
- device IDs and expected Seat ownership are captured before the test;
- sensitive raw keystroke logging is opt-in and visibly indicated;
- failures preserve traces and cleanup the labs.

**Manual acceptance**

Required. The user records hardware model, Windows build, topology, duration, traces, and pass/fail.

**Done when**

Gate A/B/C physical rows in `STATUS.md` have real evidence and compatibility/hardware matrix entries.

**Suggested commit**

`test: implement P3-HW-01 hardware acceptance runner`

---

## P3-D-01 — HidHide read-only availability probe

**State:** READY

**Goal**

Detect whether a supported HidHide control interface/version is present without changing any driver state.

**Create/modify**

- `include/hydra/hidhide_probe.hpp`
- `src/hidhide_probe.cpp`
- `hydra_hidhide_probe.exe` or `hydra_diag` subcommand;
- backend descriptor availability integration;
- tests with a fake device-control interface.

**Report**

- installed/not installed;
- control interface availability;
- driver/product version where safely queryable;
- current active/inverse policy state where documented;
- whether session blacklist operations appear supported;
- no allow/deny list contents unless explicitly needed and privacy-reviewed.

**Invariants**

- no state-changing IOCTL;
- no installation prompt;
- no administrator escalation merely to report unavailable;
- unknown version is unsupported, not assumed compatible.

**Done when**

The planner can distinguish unavailable, installed-unverified, and verified-supported HidHide environments.

**Suggested commit**

`feat: implement P3-D-01 read-only HidHide probe`

---

## P3-REC-01 — Gate C watchdog and crash recovery acceptance

**State:** BLOCKED

**Goal**

Prove that host, target, shim, and adapter failure cannot leave a controlled session or global state behind.

**Depends on**

- P8-WATCH-01
- P8-JOURNAL-01
- P3-API-03

**Create/modify**

- watchdog integration for Gate C lease;
- rollback registry entries for shim/import patches and child processes;
- crash injection test modes;
- `GateCWatchdogRecoveryTests`.

**Failure matrix**

- host killed;
- watchdog killed/restarted;
- target killed;
- shim initialization partial failure;
- pipe stall/disconnect;
- UI killed;
- user logoff/shutdown notification;
- stale crash journal on next startup.

**Invariants**

- no orphan controlled target/helper;
- no IAT patch survives module unload/process exit;
- no global cursor/clip/device state changed by Gate C;
- repeated reset is safe;
- unresolved cleanup becomes `RecoveryRequired`.

**Done when**

Automated process tests and manual Windows crash acceptance both pass.

**Suggested commit**

`test: implement P3-REC-01 Gate C recovery matrix`

---

## P3-D-02 — Guarded HidHide session-cloak lab

**State:** BLOCKED

**Goal**

Test session-scoped physical-device cloaking only after the replacement controlled path and independent recovery are proven.

**Depends on**

- P3-D-01
- P3-REC-01
- P3-HW-01
- P8-RESET-01

**Create/modify**

- `include/hydra/hidhide_session_backend.hpp`
- `src/hidhide_session_backend.cpp`
- guarded lab executable;
- watchdog rollback action;
- physical acceptance document/tests with fake backend.

**Activation transaction**

1. require explicit device IDs and a spare recovery input or short expiry timeout;
2. verify host replacement path is live;
3. snapshot current HidHide state;
4. add session-scoped cloak entries;
5. verify non-owning controlled process loses visibility;
6. verify owning feeder/host retains required access;
7. run timed self-test;
8. clear entries and verify restoration;
9. repeat with forced host exit.

**Invariants**

- no persistent global list mutation unless separately approved;
- activation expires automatically;
- keyboard/mouse applicability is measured, not assumed;
- composite device behavior is recorded;
- failure triggers watchdog/reset cleanup.

**Manual acceptance**

Required with spare recovery input and visible countdown.

**Done when**

`PhysicalDeviceCloaking` may be advertised for the tested matrix. `PhysicalInputSuppression` remains separate unless the complete input-path test proves it.

**Suggested commit**

`feat: implement P3-D-02 guarded HidHide lab`

---

## P3-E-01 — Open-source non-protected application profile

**State:** BLOCKED

**Goal**

Validate the full controlled compatibility stack against an inspectable application not written specifically for HydraSeat.

**Depends on**

- P3-API-03
- P3-RAW-02
- P3-MET-01
- P3-REC-01

**Selection criteria**

- open source and redistributable/testable;
- no anti-cheat/protected process;
- uses a documented combination of Raw Input/polling/cursor/focus;
- deterministic input response and visible telemetry.

**Outputs**

- first real `GameCompatibilityProfile` fixture;
- exact backend requirements;
- launch and rollback script;
- compatibility matrix entry;
- metrics/bleed report.

**Done when**

Two instances/processes use different Seat devices with zero measured cross-state for the declared duration and rollback returns the application to native behavior.

**Suggested commit**

`test: validate P3-E-01 open-source compatibility profile`

---

## P3-E-02 — First explicit non-anti-cheat game profile

**State:** BLOCKED

**Goal**

Produce one experimental game profile through the exact same planner/transaction/recovery path.

**Depends on**

- P3-E-01
- P3-D-02 or another proven physical-suppression path when required
- P3-CTRL-01 if controller support is required

**Rules**

- record game/version/launcher/Windows/GPU/input API;
- do not bypass anti-cheat or DRM;
- profile is `Experimental` until repeated physical acceptance;
- no undocumented global machine mutation;
- game-specific workaround becomes a typed profile field or backend capability, not a hard-coded title check in the engine.

**Done when**

One Seat runs the game using only its assigned devices for the tested profile, with metrics and clean rollback.

**Suggested commit**

`test: add P3-E-02 experimental game profile`

---

## P3-E-03 — Two different applications/games zero-bleed proof

**State:** BLOCKED

**Goal**

Demonstrate the product-defining input requirement with two different targets at the same time.

**Depends on**

- P3-E-02
- P3-HW-01
- P3-MET-01
- required controller/audio packets for the selected targets

**Acceptance topology**

```text
Seat 1: keyboard A + mouse A + optional controller A -> target A
Seat 2: keyboard B + mouse B + optional controller B -> target B
```

**Measured pass criteria**

- zero cross-Seat key down/up;
- zero cross-Seat mouse button/wheel/movement packets;
- zero controller cross-routing where used;
- both targets retain their required virtual focus/cursor behavior;
- no unbounded queue or unexplained drops;
- latency within declared budget;
- target restart and one device reconnect recover;
- stop/reset restores normal Windows behavior.

**Duration**

Initial minimum: 30 minutes active interaction plus 30 start/stop cycles. The release phase will add longer soak requirements.

**Done when**

The compatibility matrix contains two explicit targets and the evidence bundle is reproducible.

**Suggested commit**

`test: complete P3-E-03 two-target zero-bleed proof`

---

## P3-CLOSE-01 — Phase 3 closure

**State:** BLOCKED

**Goal**

Close Phase 3 only after the implementation and evidence are coherent.

**Depends on**

- all required Phase 3 packets for the supported scope;
- P3-E-03;
- P3-REC-01;
- P3-HW-01.

**Closure checklist**

- planner capabilities match implemented/tested backends;
- x86/x64 claims match CI;
- controlled probes cover API surfaces required by supported profiles;
- physical and application evidence recorded;
- watchdog/reset rollback passes;
- unsupported/protected profiles fail closed;
- no README wording implies universal game support;
- Phase 4 receives stable host/adapter/profile contracts;
- `docs/ROADMAP.md`, `STATUS.md`, architecture, and compatibility matrix agree.

**Done when**

Phase 3 is marked complete and Phase 4 is current. A closure PR contains no new feature implementation beyond documentation/evidence corrections.

**Suggested commit**

`docs: close Phase 3 input compatibility and isolation`
