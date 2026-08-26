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

**State:** VALIDATED

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
- `include/hydra/gate_c_adapter.h`
- `include/hydra/win32_iat_patch.hpp`
- `src/gate_c_shim.cpp`
- controlled probe/host, CMake, CI, and Gate C documentation

**Implementation skeleton**

1. Define policy for pure query, virtual mutation, and pass-through APIs.
2. Define the controlled v1 coordinate domain explicitly; defer unproven
   client/physical/per-monitor-DPI transforms rather than guessing them.
3. Store virtual clip/capture/focus in the adapter, not static DLL globals.
4. make setters affect only virtual state when profile policy requires virtualization;
5. preserve original behavior when the shim is inactive;
6. unhook and clear virtual state on shutdown/error.

**Invariants**

- no global `ClipCursor` call for two-Seat mode;
- one Seat cannot alter another Seat's adapter state;
- v1 performs no implicit DPI conversion and exposes only its declared
  logical screen-coordinate contract;
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

**Code-complete implementation note (2026-08-25)**

- the existing startup-loaded `hydra_gate_c_shim.dll` now owns one combined
  lifecycle for the validated three polling imports and a separate bounded
  ten-entry cursor/focus/capture patch set;
- adapter ABI v2 adds fixed-width clip and transient process-local logical
  HWND state while retaining the existing cursor/clip storage as the single
  source of truth;
- active setters update only adapter state. They never call native
  `SetCursorPos`, `ClipCursor`, `SetCapture`, `ReleaseCapture`, or a focus
  mutation API;
- controlled v1 coordinates are signed 32-bit logical screen coordinates,
  clip right/bottom are exclusive, negative values are valid, and cursor
  setters clamp to an enabled valid clip rectangle;
- logical foreground, active, and focus share the validated controlled target
  in v1. Capture may point at another validated window owned by the same
  controlled process;
- `ShowCursor` is explicitly deferred because its counter/thread contract is
  not defined by this packet;
- strict portable component evidence is green. Windows CI run `32792381573`
  validates native x64/x86 full CTest plus x64-host-to-x64/x86 ordinary-API,
  no-cross-Seat, rollback, and host-native global-state-preservation execution.

---

## P3-RAW-01 — Controlled Raw Input behavior probe

**State:** VALIDATED

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

**Code-complete implementation note (2026-08-25)**

- `hydra_gate_c_raw_input_probe.exe` is a separate HydraSeat-owned process; it
  never shares Raw Input registration state with the production host or
  `InputRouter`;
- schema v1 is deterministic UTF-8 JSON with fixed-width runtime diagnostic
  values, strict version/size/count checks, a synthetic parser fixture, and
  separately generated observed-Windows traces;
- native registration experiments cover keyboard/mouse foreground targets,
  Window A-to-B replacement, independent usage mutation/removal,
  `RIDEV_INPUTSINK`, `RIDEV_DEVNOTIFY`, their legal combination, destroyed
  target observation, cleanup, and repeated process teardown;
- `WM_INPUT` callbacks use pre-reserved bounded storage and one aligned fixed
  scratch buffer. File serialization and optional stable device identity
  resolution happen after callback processing;
- `GetRawInputData` and `GetRawInputBuffer` record query/read sizes, errors,
  headers, block offsets, pointer-width alignment, and explicit malformed or
  overflow results without manufacturing a successful `HRAWINPUT`;
- strict portable trace/parser and existing Gate C regression tests pass.
- Windows CI run `32800513365` validates native x64 and Win32/x86 28/28 CTest,
  repeated process teardown, and retained observed registration traces while the
  existing Gate C cross-architecture job remains green;
- both architectures observed the same registration contract: target replacement
  is last-registration-wins per usage, removal deletes only that usage,
  `RIDEV_INPUTSINK` is echoed by `GetRegisteredRawInputDevices`, while an accepted
  `RIDEV_DEVNOTIFY` request is not echoed in the returned flags;
- destroying the registered target HWND leaves its runtime value in the process's
  registration snapshot until a fresh valid registration replaces it. x64 records
  a 24-byte `RAWINPUTHEADER`/48-byte `RAWINPUT`; Win32/x86 records 16/40 bytes and
  both paths use the documented 8-byte Raw Input buffer alignment policy;
- CI observed no physical `WM_INPUT` or device change. Those remain P3-HW-01
  evidence and are not implied by this packet's `VALIDATED` state.

---

## P3-RAW-02 — Controlled Raw Input virtualization shim

**State:** VALIDATED

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
- `tests/test_gate_c_raw_input_shim.cpp`
- `src/gate_c_api_probe.cpp`
- `src/gate_c_host.cpp`

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

**Implementation status (2026-08-25)**

The controlled implementation is validated on
`feat/p3-raw-02-virtualization-shim`. The adapter owns a bounded per-context
registration table, immutable packet queue, and generation-checked opaque
handle table. The startup-loaded shim adds an explicit Raw Input capability
for the four allowlisted APIs, posts only to a revalidated same-process HWND,
and restores Raw Input, cursor/focus, then polling IAT entries on uninstall.
Portable strict builds and 20/20 CTest targets pass. Windows run `32806163164`
passes native x64, Win32/x86, and the dedicated x64-host cross-architecture
matrix. The x64 and x86 controlled probes use ordinary Raw Input APIs while
their expected keyboard/mouse counters are nonzero, every cross-Seat counter
is zero, and API/stale-token/queue-overflow failure counters remain zero.
P3-HW-01 physical evidence remains pending.

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

**State:** VALIDATED

**Goal**

Add process-local XInput state to the controlled adapter and prove two targets can map logical user index 0 to different selected physical/test controllers.

**Create/modify**

- `include/hydra/virtual_xinput_state.hpp`
- `src/virtual_xinput_state.cpp`
- adapter C ABI v4 extension functions;
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
- stable source identity is source kind plus opaque source key; runtime XInput
  slot hint changes are explicit mapping metadata changes, not new identities;
- explicit same-source remaps never decrease source generation or bypass the
  post-disconnect newer-generation barrier, while different stable sources use
  independent generation namespaces;
- controller snapshot failures are canonical empty values and successful
  capability/battery/vibration metadata requires the connected state mapping;
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

**Implementation evidence (2026-08-25)**

- a bounded four-slot `VirtualXInputContext` owns normalized state, metadata,
  packet numbers, mappings, source/mapping generations, and vibration routes;
- adapter ABI v4 and separate fixed-width little-endian controller messages
  preserve the x86/x64 contract without serializing Windows XInput structs;
- synthetic two-context/component tests and 14/14 portable Gate C regressions
  pass under strict GCC warnings;
- review fixed source ownership so the session-scoped opaque source key remains
  the identity even if its optional runtime XInput slot hint changes; changing
  that routing hint requires an explicit remap and mapping-generation advance;
- Windows run `32816241577` historically passed the pre-remediation native x64,
  Win32/x86, and x64-host-to-x64/x86 controlled process acceptance. Both
  architecture legs report state/capability/
  battery/vibration expected counters of 2, every cross counter at 0,
  `api_failures=0`, and `stale_accepted=0`;
- ordinary XInput API interposition and physical controller polling/mutation stay
  out of scope.
- correctness remediation enforces generation monotonicity across explicit
  routing-hint remaps and canonical failure metadata in controller snapshots;
- strict GCC 15 `-Werror` builds and 20/20 selected portable CTest regressions
  pass after first reproducing both defects with failing tests;
- fork PR #15 run `32832036967` validates remediation head
  `b351afdd60236b953d913b8488a5e350f705faec` against fork `main`: native x64
  and Win32/x86 each pass 36/36 CTest, including `GateCProtocolTests`,
  `VirtualXInputStateTests`, `GateCXInputAdapterTests`, the C ABI smoke test,
  and controlled XInput probe/process tests;
- the x64-host-to-x64 and x64-host-to-x86 XInput legs each report Seat 1/Seat 2
  state/capability/battery/vibration expected=2, every cross counter=0,
  `api_failures=0`, and `stale_accepted=0`; repeated session cleanup leaves no
  controlled child running. Historical run `32816241577` remains pre-fix only.

**Suggested commit**

`fix: harden P3-CTRL-01 controller generation invariants`

---

## P3-CTRL-02 — DirectInput enumeration and visibility adapter

**State:** VALIDATED

**Goal**

Provide a clean-room, process-local controlled DirectInput enumeration/order/visibility adapter.

**Depends on**

- P3-CTRL-01
- P3-ARCH-01

P3-CTRL-01 remediation is freshly Windows-validated by run `32832036967`, and
P3-ARCH-01 is already validated. DirectInput work may now start only as this separate packet.

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

**Implementation evidence (2026-08-25)**

- `DirectInputInstanceId` normalizes the 128-bit DirectInput instance GUID into fixed-width fields without serializing the Windows `GUID` memory layout;
- `DirectInputVisibilityPolicy` is a bounded ordered allowlist (32 entries) over a bounded native inventory (64 entries); native enumeration order, product GUID, and friendly names never select identity or order;
- invalid/zero IDs, duplicate policy/native instance IDs, an oversized inventory, and a missing required instance fail closed with an empty visible result;
- two HydraSeat-owned controlled probe invocations expose `C,A` versus `B` from the same synthetic `A,B,C` inventory and calculate `cross_visible=0`;
- the Windows-only observation mode uses `DirectInput8Create` and `IDirectInput8W::EnumDevices(DI8DEVCLASS_GAMECTRL, ..., DIEDFL_ATTACHEDONLY)` read-only; it does not create/acquire devices, change cooperative level, send force feedback, hide devices, replace `dinput8.dll`, or touch SDL/Raw HID;
- `GameCompatibilityProfile` now carries ordered DirectInput instance IDs, while planner tests prove controlled visibility/order still does not satisfy `PhysicalInputSuppression` or production zero-bleed requirements;
- strict GCC 15 `-Werror` component/probe/planner builds pass, focused DirectInput CTest passes 3/3, and the selected Phase 3 portable regression set passes 21/21 including plan CLI observation, XInput, Raw Input, Gate C protocol/ABI/shims, and target tests;
- fork PR #16 run `32840474306` validated head `f52535bdb160aa58006c694e86d536cca3d88529` on Windows Server 2025 / MSVC: native x64 and Win32/x86 full CTest jobs both passed, including the two controlled views and read-only `DirectInputNativeObservationSelfTest`; the existing Gate C cross-architecture job also remained green.

**Clean-room evidence**

Implemented independently from the packet specification and official Microsoft DirectInput 8 documentation for `IDirectInput8::EnumDevices`, `DIDEVICEINSTANCE`, and `DIEnumDevicesCallback`. No unlicensed devreorder/Duo implementation source was copied, translated, or consulted while writing this component.

**Suggested commit**

`feat: implement P3-CTRL-02 DirectInput visibility policy`

---

## P3-MET-01 — Input latency, queue, and bleed metrics

**State:** VALIDATED

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

**Implementation evidence (2026-08-25)**

- `InputMetricsRecorder` preallocates a bounded process-local ring (default 4096, maximum 16384 samples). `tryRecord()` performs no allocation, file/pipe I/O, sleep, or blocking lock; recorder contention is rejected and counted, while a full ring rotates the oldest sample and counts the loss;
- schema-v1 stages cover physical observation, route enqueue/dequeue/write, explicit route drop, target apply/query hooks, rollback, and host resource hooks. `RawInputEvent.sequence` is the live host correlation ID and all latency timestamps use monotonic microseconds;
- `hydra_gate_c_host` records physical/enqueue/dequeue/write and queue depth/high-water/drop counters on the existing bounded writer path, then builds and writes the JSON report only after the router/writers have stopped and rollback has completed. Cumulative queue drops are de-duplicated per `(Seat, target process)` writer queue, not per Seat alone;
- host queue/write samples carry expected Seat/target identity but leave `receivingSeatId`/`receivingProcessId` unknown. Only target apply/query samples may verify actual receiver identity, so `cross_* = 0` is not zero-bleed evidence when `receiver_verified_events = 0`;
- target apply/query timestamps are intentionally not fabricated or added to the Gate C protocol in this packet. Missing target stages remain explicit and produce no end-to-end percentile until P3-HW/E supplies validated receiver-stage evidence. Any future cross-process timestamps must be normalized into the recorder's monotonic clock domain before report generation;
- redacted mode is the default and removes key/button identity while preserving Key/Button/Movement/Wheel class; `--metrics-diagnostic` explicitly enables detail IDs for a diagnostic run;
- `schemas/input_metrics_trace_v1.schema.json` and `schemas/input_metrics_report_v1.schema.json` define the machine-readable contracts, and the `hydra_input_metrics` CLI emits a deterministic receiver-verified fixture report;
- the report calculates nearest-rank p50/p95/p99/max for each available stage pair, end-to-end and rollback, plus missing-stage, receiver-evidence, verified cross-Seat/process, queue loss/high-water, recorder loss, event-class, and CPU/memory hook summaries;
- strict GCC 15 `-Werror` tests pass; metrics CTest passes 3/3; selected Phase 3 portable regressions pass 24/24; both JSON schemas parse and the fixture report validates against the report schema;
- fork PR #18 run `32857666855` validates branch head `55953b2205d0bb1f9f929c542fcb837a543e0824`: native x64 and Win32/x86 each pass 43/43 CTest, explicitly including `InputMetricsTests`, `InputMetricsCliSelfTest`, and `InputMetricsFixtureReport`; both architectures build the instrumented `hydra_gate_c_host`, and the existing Gate C cross-architecture job remains green;
- a Linux whole-project build still hits the pre-existing Windows-only `reset_input.cpp` `<windows.h>` target; the packet-scoped and selected Phase 3 targets build cleanly. Physical receiver-stage/latency/zero-bleed evidence remains manual/later work and is not implied by the automated Windows pass.

**Manual/physical limits**

No physical latency, zero-bleed, CPU/memory overhead, game, device-cloaking, or controller performance claim is made by this packet. D-027 keeps those acceptance measurements manual and they remain work for P3-HW/E and later compatibility gates.

**Suggested commit**

`feat: implement P3-MET-01 input metrics harness`

---

## P3-HW-01 — Gate A/B/C physical acceptance runner

**State:** CODE_COMPLETE

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

**Implementation evidence (2026-08-26)**

- `run_phase3_hardware_acceptance.ps1` creates a resumable ignored session, pins the source schema-v2 Seat profile by SHA-256, records Windows/build/hardware context, and refuses to mix execution evidence after the source profile changes;
- the runner never edits the source profile. Its Gate B ambiguity check derives a session-local shared-case profile, records that copy's own SHA-256, and runs the exact selected input identity against both Seats;
- Gate A records observer-only hot-plug evidence and enforces the configured soak duration; Gate B preserves exclusive and shared/ambiguous traces; Gate C launches only the HydraSeat-owned host/target pair and preserves the P3-MET-01 report;
- `phase3_hardware_acceptance_manifest_v1.schema.json` bounds ownership, stage, auxiliary-trace, privacy, and explicit manual-verdict fields;
- `summarize_phase3_trace.py` bounds JSONL input, verifies profile-to-Seat routed evidence, requires Gate A arrival/removal and at least four physical identities, proves the selected Gate B shared-case device is ambiguous and never routed, rejects verified Gate C cross-Seat/process events and queue/recorder loss, and keeps missing receiver evidence visible rather than converting it to zero-bleed;
- final `PASS` is impossible without clean machine-readable evidence, every required manual check, and an explicit human `manual_verdict=PASS`;
- JSONL virtual-key identifiers are redacted by default in `InputTraceWriter`. `hydra_input_lab` and `hydra_gate_c_host` require the separate visibly warned `--trace-sensitive-keys` opt-in before exact key IDs are retained;
- PowerShell 5 runner self-test covers source-profile immutability and derived shared-case creation; the Python parser self-test covers PENDING/PASS, privacy failure, ambiguous routing, and verified cross-Seat failure. Focused acceptance tests pass and the broader selected Phase 3 portable regression set passes 26/26; strict input-observation compilation also passes locally.

Physical Gate A/B/C results remain pending. CI/self-test evidence validates only the acceptance tooling and must not change this packet to `VALIDATED`.

**Suggested commit**

`test: implement P3-HW-01 hardware acceptance runner`

---

## P3-D-01 — HidHide read-only availability probe

**State:** VALIDATED

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

**Implementation note (2026-08-26)**

- `HidHideAvailability` is explicitly tri-state: `Unavailable`,
  `InstalledUnverified`, and `VerifiedSupported`. There is no boolean
  conversion and installed-unverified never makes the backend available.
- The Windows observer checks the exact `HidHide` service, enumerates the
  documented control-interface GUID
  `{0C320FF7-BD9B-42B6-BDAF-49FEB9C91649}`, reads the driver file version from
  the service's configured binary, opens that interface with `GENERIC_READ`,
  and issues only `GET_ACTIVE` (function 2052) and `GET_WLINVERSE` (function
  2054). Responses must be exactly one byte with value zero or one.
- The exact known contract allowlist is `1.7.339.0`, `1.7.344.0`, and
  `1.7.346.0`. Their tag commits (`98ccf172`, `0aa3c946`, and `22a1ff5f`)
  contain session-blacklist functions 2056/2057. No version range is inferred;
  every other version remains installed-unverified.
- Session-blacklist support is inferred only after an allowlisted version and
  both read-only queries succeed. The probe never calls functions 2056/2057,
  never queries allow/deny/session-list contents, never requests elevation,
  and never installs, starts, stops, or reconfigures a service or device.
- Access denial, unknown versions, missing interface/version evidence,
  malformed/truncated values, and oversized results all fail closed with a
  bounded diagnostic and optional system error. Non-Windows builds report
  `Unavailable` / `unsupported-platform`.
- `external.hidhide-session` retains administrator, kernel-driver,
  recovery-guard, session-scope, and high-risk production requirements. Even a
  verified probe supplies only `PhysicalDeviceCloaking`; it does not satisfy
  `PhysicalInputSuppression` or make a zero-bleed profile runnable.

**Clean-room/source record**

- Repository: `https://github.com/nefarius/HidHide`, MIT-licensed upstream;
  studied revision `2b950fd9393e1644b4199f6eb4999e1720f0c6e9`.
- Public contract consulted: `DEVELOPER.md`, `HidHide/HidHide.inf`,
  `HidHide/Version.rc`, and the minimal IOCTL/GUID declarations in
  `Shared/HidHideIoctlContract.h` at that revision, plus the same contract in
  the three exact tag commits above.
- No HidHide implementation code, binary, installer, dependency, or build
  input is copied or vendored. HydraSeat independently represents only the
  documented identity, numeric query contract, exact tagged versions, and
  bounded one-byte Boolean response needed by this read-only probe.

**Validation evidence (2026-08-26)**

- fork PR #20 Windows run `32915683414` validates final implementation head
  `146b3e6399dd2c9bc59052cdbc4a392bd4d7697e`: native x64 and Win32/x86
  configure/build/CTest jobs pass and the existing Gate C cross-architecture
  job remains green;
- the preceding run `32914259441` correctly exposed a Windows-only build defect:
  the independently defined documented IOCTL values used `CTL_CODE` /
  `METHOD_BUFFERED` without including `<winioctl.h>`. Commit `146b3e6` fixes only
  that SDK declaration dependency; the read-only probe contract is unchanged;
- portable strict-GCC tests and the complete locally available selected Phase 3
  CTest set pass 31/31 after the fix. No physical HidHide cloaking, mutation,
  suppression, game, or P3-D-02 evidence is inferred from this validation.

**Suggested commit**

`feat: implement P3-D-01 read-only HidHide probe`

---

## P3-REC-01 — Gate C watchdog and crash recovery acceptance

**State:** IN_PROGRESS

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

**P3-REC-01 implementation note (2026-08-26)**

- Gate C now stages controlled targets with `CREATE_SUSPENDED`, validates their architecture and exact `PID + creation time`, then arms the already-validated P8 watchdog and persists `ActionPrepared` journal boundaries before any target thread resumes. Production recovery leases are longer than the synchronous handshake budget; fault tests use a deliberately short lease.
- The watchdog rollback manifest is intentionally narrow: one `TerminateOwnedProcess` action per exact controlled target, with duplicate action IDs, ordinals, or exact process identities rejected. Journal bytes remain evidence only and cannot introduce rollback commands. Process-local adapter/IAT/shim state is therefore removed by exact owned-process exit rather than by a new remote-unpatch command.
- The host renews the watchdog lease only from its control loop, never from Raw Input callbacks. Clean stop persists `RollbackStarted`, tears targets down in reverse order, verifies they are gone, re-arms the same trusted manifest if the watchdog itself died, uses watchdog `Disarm` as an idempotent postcondition backstop, then persists `RollbackVerified` and `CleanStop`. Any unresolved process/watchdog/journal cleanup is recorded as `RecoveryRequired` when possible.
- Recovery self-test modes cover clean/repeated cycles, lease stall, target kill, watchdog kill/restart, pipe disconnect, adapter loss, abrupt shim-owning process exit, UI-surrogate death, logoff/shutdown notification handling, stale-journal startup blocking, and an external host-kill path. The Windows `GateCWatchdogRecoveryTests` process test additionally kills the real Gate C host and waits on exact watchdog/target handles to prove no guarded child/helper remains.
- Existing `GateCPollingShimTests` remain the coupled shim-initialization evidence: partial IAT install failure restores all already-patched pointers, protection/rollback failure is surfaced, incomplete rollback remains retryable, and repeated uninstall is idempotent. Gate C still owns no global cursor/clip/device mutation in this packet.
- Local portable validation currently passes 34/34 CTest, including the new recovery core plus existing watchdog/journal/Gate C regressions. Focused GCC 15 strict recovery tests pass with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`. The pre-existing Linux-only whole-build blocker remains `reset_input.cpp` including `windows.h`; `make -k` builds the remaining portable targets successfully.
- Native Windows/MSVC x64 and Win32/x86 `GateCWatchdogRecoveryTests` plus the existing Gate C cross-architecture matrix are still pending. The packet cannot advance beyond `CODE_COMPLETE` until those automated checks pass, and it cannot become `VALIDATED` until the declared manual Windows crash/logoff/shutdown acceptance is performed by a human tester.

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
