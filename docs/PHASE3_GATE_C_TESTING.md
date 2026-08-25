# Phase 3 Gate C Controlled-Process Lab

## Status

Gate C now has an implemented **controlled-process foundation**:

- a versioned host/target wire protocol;
- local named-pipe transport with timeouts and session-token validation;
- a process-local shared-library adapter with a stable C ABI;
- independent keyboard, mouse, cursor, clip, virtual-foreground and virtual-capture state;
- two separate HydraSeat-owned target processes;
- an automated Windows self-test that proves their state does not bleed across processes.

P3-API-01 adds a read-only controlled Win32 API probe and a fixed-width
comparison snapshot. Its Windows/MSVC baseline is validated by CI run
`32722277035`, where all 15 tests, including both API probe self-tests, passed.

P3-ARCH-01 is Windows-validated by CI run `32727711605`. It adds explicit
Win32 process-architecture detection, PE-machine preflight, a bounded schema-v1
artifact manifest/selector, deterministic `gate-c/x86` and `gate-c/x64`
layouts, and x86/x64 CI coverage. Both native matrix legs and the real
x64-host-to-x86/x64 controlled target/probe job passed.

P3-API-02 is now `VALIDATED` by Windows CI run `32780563364`. It adds a
separate startup-loaded polling shim for HydraSeat-owned probes, transactional
process-internal IAT patching, fixed-width C diagnostics, exact unpatch, and
x86/x64 artifact selection. Native x64/x86 CTest and the real x64-host-to-x64/x86
ordinary-polling two-probe matrix all passed.

P3-API-03 is `VALIDATED` on `feat/p3-api-03-cursor-focus-shim` by Windows
CI run `32792381573`. It extends the same startup-loaded shim with a bounded
cursor/clip/logical-focus/capture allowlist and adapter ABI v2. Native x64 and
Win32/x86 each passed 24/24 CTest, while the dedicated x64-host cross-architecture
job passed both x64 and x86 ordinary-API, no-cross-Seat, teardown, polling-regression,
and host-native global-state-preservation paths.

P3-RAW-01 is `VALIDATED` on `test/p3-raw-01-behavior-probe` by Windows CI
run `32800513365`. Native x64 and Win32/x86 each passed 28/28 CTest, both
observed registration traces were retained and reviewed, repeated process
teardown passed, and the existing Gate C cross-architecture job stayed green.
The traces agree on last-registration-wins replacement, usage-local removal,
`RIDEV_INPUTSINK` echo, accepted-but-not-echoed `RIDEV_DEVNOTIFY`, and a
registered destroyed HWND remaining as a runtime value until valid replacement.
This is observation only and does not add Raw Input interposition.

This is not a general game hook or a completed Gate C implementation. The
controlled targets still call HydraSeat's adapter API directly; only the
explicitly launched API probe may opt into the polling/cursor-focus shim. Commercial games
remain unsupported and are never modified by this packet.

## Safety boundary

The Gate C lab performs none of the following:

- injection into a third-party or commercial process;
- Import Address Table patching outside the explicitly launched HydraSeat API
  probe, code detours, or system-wide hooks;
- anti-cheat, DRM or protected-process bypass;
- HidHide control or driver installation;
- physical keyboard/mouse suppression;
- global cursor movement or `ClipCursor` modification;
- claims of zero input bleed between games.

Normal Windows input remains active. Gate C currently proves that HydraSeat can maintain and deliver independent **virtual process state**, not that an unmodified game is forced to use that state.

## Components

### `hydra_gate_c_host.exe`

The host:

- loads the first two active Seats from `workspace_config.json`;
- starts one controlled target per Seat;
- creates a unique local named pipe and session token per target;
- validates the target's token, Seat ID, process ID and architecture;
- selects a target/adapter pair from the bounded architecture manifest and
  validates both PE machine values before launch;
- validates the launched process with `IsWow64Process2`, using
  `IsWow64Process` plus `GetNativeSystemInfo` only when the modern API is absent;
- routes each exclusively owned Raw Input keyboard/mouse observation to a bounded per-target writer queue;
- sends initial virtual cursor, clip, foreground and capture state;
- records Gate A/B JSONL traces;
- shuts down or force-terminates controlled children when the session fails;
- never injects into an existing process.

### `hydra_gate_c_target.exe`

The target is a HydraSeat-owned test process. It:

- connects to the assigned host pipe;
- sends a versioned `Hello` containing the full random session token;
- loads the process-local adapter DLL normally through the Windows loader;
- applies input/control frames to its own adapter context;
- answers snapshot queries;
- displays virtual state beside the actual OS foreground state;
- exits when the host sends `Shutdown` or the pipe/session fails.

### `hydra_gate_c_api_probe.exe`

The API probe is a separate HydraSeat-owned process. It:

- uses the existing Seat/pipe/token launch and handshake path;
- owns a real Win32 window even in hidden process-test mode;
- calls `GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState`,
  `GetCursorPos`, `GetClipCursor`, `GetForegroundWindow`, `GetFocus`, and
  `GetCapture` on its window/UI thread;
- reads the matching process-local state through the existing adapter C ABI;
- returns both observations in one versioned `ProbeSnapshot` frame;
- treats HWND values as transient runtime diagnostics, never persisted identity;
- in cursor/focus test mode calls ordinary Win32 setters only after the
  process-local shim is active; those calls mutate adapter state, not native
  global state;
- optionally loads the explicitly selected shim before the controlled
  session and refuses DLL unload until exact unpatch succeeds.

Visible probe mode shows the last OS and adapter observations side by side. A
foreground mismatch is expected before interposition: two adapters may both
report virtual foreground while Windows still has only one real foreground
window.

### `hydra_gate_c_raw_input_probe.exe`

The P3-RAW-01 probe is an independent HydraSeat-owned process. It never runs
beside `InputRouter` or the production Gate C host in the same process, because
Raw Input registrations are process state.

Its registration self-test creates controlled Window A and Window B and
records:

- keyboard and mouse foreground registrations;
- per-usage Window A-to-B target replacement;
- independent keyboard/mouse targets and usage-local removal;
- `RIDEV_INPUTSINK`, `RIDEV_DEVNOTIFY`, and their legal combination;
- registration state after destroying a registered target window, followed by
  replacement and removal through a valid surviving window;
- bounded best-effort cleanup and idempotent teardown.

The observe mode has a hard 30-second maximum. Its window procedure records
`WM_INPUT` and `WM_INPUT_DEVICE_CHANGE` into pre-reserved bounded memory using
one aligned 64-KiB scratch buffer. It performs no pipe or file I/O and no
unbounded allocation in the callback. UTF-8 serialization and optional
hDevice-to-path/stable-ID resolution happen afterward.

`HRAWINPUT`, `RAWINPUTHEADER::hDevice`, HWND, and HANDLE values are serialized
only as fixed-width `runtime_value` diagnostics. They are not physical identity.
The committed fixture is marked `synthetic_parser_fixture`; native CI traces
are marked `observed_windows_api` and are uploaded per architecture.
The buffer scratch space is always 8-byte aligned, and the trace records whether
block traversal used native alignment or the documented WOW64 8-byte rule; see
Microsoft's [`GetRawInputBuffer` contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputbuffer).
The executable uses the normal `gate-c/x86` or `gate-c/x64` output directory,
but it is not launched by the Gate C host selector. Therefore the validated
eight-entry target/adapter/API-probe/shim manifest remains unchanged.

### `hydra_gate_c_shim.dll`

The P3-API-02 shim is a separate architecture-matching DLL loaded through the
normal Windows loader only by `hydra_gate_c_api_probe.exe`. It parses only the
current executable image and patches only its named `GetAsyncKeyState`,
`GetKeyState`, and `GetKeyboardState` imports. Direct `USER32.dll` and
explicit NTUSER API-set module-name families are allowed; ordinal imports
cannot identify an allowlisted function and are not matched.
Forwarded-export resolution does not require a second patch path because the
loader has already placed the final callable address in the probe's IAT slot.

All three imports must exist exactly once. Install is all-or-rollback, exact
original pointers are retained, pointer-sized IAT ranges receive temporary
write protection, and old protection is immediately restored. The shim calls
the probe-owned adapter context and owns no keyboard state. Inactive and
out-of-domain calls use the saved original directly. Adapter loss/read failure
enters visible fail-closed mode for supported keys. Lifecycle transitions wait
for active adapter reads, restore the IAT, and only then permit unload.

P3-API-03 keeps that lifecycle and adds a separate closed ten-entry patch set
for `GetCursorPos`, `SetCursorPos`, `ClipCursor`, `GetClipCursor`,
`GetForegroundWindow`, `GetActiveWindow`, `GetFocus`, `GetCapture`,
`SetCapture`, and `ReleaseCapture`. Both sets form one capability transaction:
cursor/focus discovery or installation failure restores the already-applied
polling set, and uninstall restores cursor/focus then polling in reverse.

Active cursor/clip/capture setters call only adapter ABI v2. They never call
their saved native mutator. Logical foreground/active/focus queries share the
validated controlled target in v1; capture may reference another validated
window owned by the same process. Stale/destroyed/foreign HWNDs return a
visible failure or null and never silently expose native global state.

The coordinate contract is signed 32-bit caller-declared logical screen
coordinates with no inferred DPI/client/physical conversion. Negative values
are valid, clip right/bottom are exclusive, invalid rectangles do not mutate
state, and cursor setters clamp to an enabled clip. Disabled `GetClipCursor`
returns the documented full signed logical-domain sentinel. `ShowCursor`
remains deferred because its counter/thread semantics are not defined.

Shim ABI v1 is `__cdecl` and uses packed fixed-width config/status structures
with `struct_size`, generation, API masks, result, adapter-result, system-error,
and rollback diagnostics. Toggle low bits are explicitly unsupported in v1:
`GetKeyState` returns only the adapter high bit and every `GetKeyboardState`
low bit is zero.

Provenance: this slice was independently implemented from the packet/design
requirements and Windows SDK declarations. No third-party implementation code
or reference-repository source was copied, adapted, or used as a build input.

### `hydra_gate_c_adapter.dll`

The adapter is a process-local state component with a C ABI. ABI v2 currently provides semantic equivalents for controlled tests:

- key down/up state;
- `GetAsyncKeyState`-style high bit plus one-shot press edge;
- `GetKeyState`-style key-down high bit;
- `GetKeyboardState`-style 256-byte high-bit array;
- mouse button and wheel state;
- virtual cursor position;
- virtual clip rectangle;
- virtual foreground state;
- virtual capture state;
- fixed-width transient controlled target, logical foreground/active/focus,
  and virtual capture HWND runtime values;
- full state snapshot and reset.

The adapter is linked normally into the controlled target. It is not injected into another program.

### `hydra_gate_c_core`

The core contains:

- explicit little-endian frame encoding/decoding;
- protocol magic/version/message/size/sequence validation;
- bounded payload sizes;
- typed messages;
- process-local virtual input state;
- local named-pipe transport.

## Protocol overview

A frame contains:

```text
magic            uint32  "HGC1"
protocol_version uint16
message_type     uint16
payload_size     uint32
sequence         uint64
payload          bounded bytes
```

Messages:

```text
Hello
HelloAck
InputEvent
ControlState
QuerySnapshot
StateSnapshot
Shutdown
Error
ProbeSnapshot
```

Important properties:

- explicit little-endian encoding; no raw C++ object serialization;
- fixed-width integers only;
- maximum payload/frame size;
- monotonic input/control sequence enforcement;
- malformed enum, boolean, reserved-field, clip rectangle and virtual-key rejection;
- full 128-bit token validation during the handshake;
- server validates the connecting target's claimed PID against the child process it created;
- `PIPE_REJECT_REMOTE_CLIENTS` prevents remote named-pipe clients;
- Windows session-token bytes use the system-preferred `BCryptGenRandom` provider.

The command line still carries the token to the controlled child. Consequently, this is a robust local test-session boundary, not a security boundary against another process already running as the same Windows user and able to inspect that user's processes.

## C adapter ABI

The adapter header is:

```text
include/hydra/gate_c_adapter.h
```

The ABI is versioned and uses packed fixed-width C structures with `struct_size` validation. A caller must check:

```c
hydra_gate_c_adapter_api_version() == HYDRA_GATE_C_ADAPTER_API_VERSION
```

All exports explicitly use `HYDRA_GATE_C_ADAPTER_CALL` (`__cdecl` on Windows).
The public header declares the v1 structure sizes as 36, 32, and 120 bytes;
both C11 and C++ tests assert those constants in x86 and x64 builds. Consumers
link through the architecture-matching import library, so x86 decoration is
resolved by the toolchain rather than guessed at runtime.

Major functions:

```text
hydra_gate_c_adapter_create
hydra_gate_c_adapter_destroy
hydra_gate_c_adapter_reset
hydra_gate_c_adapter_apply_input
hydra_gate_c_adapter_apply_control
hydra_gate_c_adapter_get_async_key_state
hydra_gate_c_adapter_get_key_state
hydra_gate_c_adapter_get_keyboard_state
hydra_gate_c_adapter_get_control_state
hydra_gate_c_adapter_get_mouse_state
hydra_gate_c_adapter_get_snapshot
hydra_gate_c_adapter_set_virtual_cursor
hydra_gate_c_adapter_set_virtual_clip
hydra_gate_c_adapter_configure_window_state
hydra_gate_c_adapter_get_window_state
hydra_gate_c_adapter_set_virtual_capture
hydra_gate_c_adapter_release_virtual_capture
hydra_gate_c_adapter_invalidate_window
```

This API is the boundary a future clean-room Windows compatibility layer can call after interposing a target API. It does not itself install that interposition.

## Probe comparison snapshot

The P3-API-01 comparison payload has its own `HPS1` magic, schema version,
declared byte size, reserved fields, and fixed schema-v1 wire size. It is
little-endian, bounded to 1024 bytes, and rejects truncated, oversized,
malformed, or future-version data.

`ProbeComparison` contains:

- monotonic sequence/timestamp plus Seat, process, UI-thread, key, and transient
  target-window context;
- actual OS polling arrays/results, cursor/clip rectangles, and transient
  foreground/focus/capture HWND observations;
- direct adapter key/edge arrays, mouse/wheel, cursor/clip, virtual foreground,
  and virtual capture state;
- deterministic comparison flags that preserve differences rather than forcing
  `OS == adapter`.

## Automated tests

### Protocol/state tests

```powershell
ctest --test-dir build --build-config Release -R GateCProtocolTests --output-on-failure
```

Covers:

- every protocol message round trip;
- malformed magic/version/type/size/reserved fields;
- invalid virtual keys and clip rectangles;
- random token generation/encoding;
- stale sequence rejection;
- key state and one-shot async edges;
- mouse buttons/wheel;
- virtual cursor clipping;
- virtual foreground/capture;
- state reset.

### Adapter ABI tests

```powershell
ctest --test-dir build --build-config Release -R GateCAdapterTests --output-on-failure
```

Covers:

- API version;
- two independent adapter contexts;
- key/keyboard/async state;
- cursor/focus/capture query functions;
- mouse state;
- struct-size validation;
- stale sequence rejection;
- undersized buffer rejection;
- reset.

### Pure-C ABI smoke test

```powershell
ctest --test-dir build --build-config Release -R GateCAdapterCSmoke --output-on-failure
```

Compiles and calls the exported adapter API from a C11 translation unit, confirming that the public header is not accidentally dependent on C++ language features or C++ name mangling.

### Architecture manifest and runtime tests

The version-1 internal manifest is bounded to 8 entries and 128 bytes per
relative path. It has canonical `(architecture, artifact kind)` ordering and
contains target, adapter, API-probe, and polling-shim descriptors under:

```text
<artifact-root>/x86/hydra_gate_c_target.exe
<artifact-root>/x86/hydra_gate_c_adapter.dll
<artifact-root>/x86/hydra_gate_c_api_probe.exe
<artifact-root>/x86/hydra_gate_c_shim.dll
<artifact-root>/x64/hydra_gate_c_target.exe
<artifact-root>/x64/hydra_gate_c_adapter.dll
<artifact-root>/x64/hydra_gate_c_api_probe.exe
<artifact-root>/x64/hydra_gate_c_shim.dll
```

Configure and run each native matrix leg with:

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release --parallel
ctest --test-dir build-x64 --build-config Release --output-on-failure

cmake -S . -B build-x86 -A Win32
cmake --build build-x86 --config Release --parallel
ctest --test-dir build-x86 --build-config Release --output-on-failure
```

After staging both architecture directories under one artifact root, the
critical cross-architecture test is:

```powershell
.\build-x64\gate-c\x64\hydra_gate_c_host.exe `
  --architecture-self-test `
  --artifact-root .\matrix-artifacts\gate-c `
  --target-architecture x86
```

It must select the x86 target and adapter, reject machine mismatches before
launch, detect the real child architecture, complete the existing versioned
handshake/state-separation test, and leave no child after shutdown. The same
x64 host then runs the stale-protocol failure self-test against the x86 target
and the repeated normal, missing-window, handshake-timeout, abnormal-exit, and
host-disconnect matrix against the x86 API probe. Unit tests
also reject future schema versions, over-limit/duplicate/unknown/missing
entries, non-canonical order, absolute/traversal paths, malformed PE files, and
wrong target/adapter/shim architectures.

### Polling shim component and process tests

```powershell
ctest --test-dir build --build-config Release -R GateCPollingShim --output-on-failure
ctest --test-dir build --build-config Release -R GateCShimCSmoke --output-on-failure
```

The component tests cover fixed C/C++ ABI sizes, install/uninstall/reinstall,
missing/duplicate/already-patched imports, malformed slot metadata, partial
install rollback, protection failure, retryable uninstall failure, exact
pointer restoration, adapter-unavailable fail-closed behavior, async high/edge
semantics, zero toggle bits, null/no-partial keyboard-state behavior, and
out-of-domain pass-through.

On Windows the controlled probe and two-process checks are:

```powershell
.\build-x64\gate-c\x64\hydra_gate_c_api_probe.exe `
  --polling-shim-self-test `
  --shim .\build-x64\gate-c\x64\hydra_gate_c_shim.dll

.\build-x64\gate-c\x64\hydra_gate_c_host.exe `
  --polling-shim-self-test `
  --target .\build-x64\gate-c\x64\hydra_gate_c_api_probe.exe `
  --shim .\build-x64\gate-c\x64\hydra_gate_c_shim.dll
```

The host launches two probes, applies different A/B adapter state, verifies
the ordinary imported polling APIs see only their own Seat, then requires
successful unpatch before clean exit. Artifact-root mode preflights probe,
adapter, and shim PE machines before launch. Forced probe termination changes
no other process because the patch is confined to the terminated process.

### Cursor/focus shim component and process tests

```powershell
ctest --test-dir build --build-config Release -R GateCCursorFocusShim --output-on-failure
```

Portable component coverage includes the closed allowlist, missing/duplicate
and malformed plans, partial install rollback, combined rollback of an
already-installed polling set, exact pointer restoration, retryable uninstall,
negative/extreme coordinates, right/bottom-exclusive clamping, and fixed-width
C/C++ ABI assertions.

On Windows the local ordinary-API and two-process checks are:

```powershell
.\build-x64\gate-c\x64\hydra_gate_c_api_probe.exe `
  --cursor-focus-shim-self-test `
  --shim .\build-x64\gate-c\x64\hydra_gate_c_shim.dll

.\build-x64\gate-c\x64\hydra_gate_c_host.exe `
  --cursor-focus-shim-self-test `
  --target .\build-x64\gate-c\x64\hydra_gate_c_api_probe.exe `
  --shim .\build-x64\gate-c\x64\hydra_gate_c_shim.dll
```

The local test covers ordinary getters/setters, null/invalid rectangles,
foreign and destroyed HWNDs, virtual capture return/release, adapter-owned
state, no partial output, fail-closed error reporting, native-state
preservation, exact unpatch, and `ShowCursor` deferral. The host test launches
two probes with distinct cursor/clip/logical-focus/capture state and requires
the host-native cursor, clip, foreground, and capture observations to remain
unchanged. Native x64/x86 and x64-host-to-x64/x86 execution passed in Windows
CI run `32792381573`.

### Raw Input behavior probe tests

Portable trace and malformed-contract coverage:

```powershell
ctest --test-dir build --build-config Release -R RawInputProbe --output-on-failure
```

Native registration and teardown coverage:

```powershell
.\build-x64\gate-c\x64\hydra_gate_c_raw_input_probe.exe `
  --registration-self-test --output raw-input-observed-x64.json

.\build-x64\gate-c\x64\hydra_gate_c_raw_input_probe.exe `
  --process-teardown-self-test
```

The native matrix runs the equivalent commands in Win32/x86. The registration
test requires successful documented replacement/removal behavior but treats the
post-destroyed-HWND snapshot as an observation rather than an assumed result.
The process test starts two job-contained children that each prove a new process
has no inherited keyboard/mouse registration, register both usages, and exit
without stack cleanup. A timeout terminates and waits for the contained child.

Manual bounded observation is available as:

```powershell
.\hydra_gate_c_raw_input_probe.exe --observe `
  --duration 10 --output raw-input-observed-manual.json
```

No physical event is required for automated CI. `physical_input_observed=false`
and `device_change_observed=false` mean that no qualifying event was seen; they
do not fail registration lifecycle validation.

### Target self-test

```powershell
.\build-x64\gate-c\x64\hydra_gate_c_target.exe --self-test
```

Verifies that the executable loads and uses the adapter DLL and can apply/query controlled state.

### Two-process integration self-test

```powershell
.\build-x64\gate-c\x64\hydra_gate_c_host.exe `
  --self-test `
  --target .\build-x64\gate-c\x64\hydra_gate_c_target.exe
```

The host starts two separate target processes and verifies:

```text
Target 1
- A down
- B not down
- async A = 0x8001, then 0x8000
- cursor = (15, 27)
- left mouse down
- wheel = +120
- virtual foreground/capture = true

Target 2
- A not down
- B down
- async A = 0
- async B = 0x8001
- cursor = (62, 71)
- right mouse down
- wheel = -120
- virtual foreground/capture = true
```

The test passes only when both child exit codes are zero and cleanup completes.

### Protocol-error fail-closed self-test

```powershell
.\build-x64\gate-c\x64\hydra_gate_c_host.exe `
  --protocol-error-self-test `
  --target .\build-x64\gate-c\x64\hydra_gate_c_target.exe
```

The host first applies sequence 2, then deliberately sends a second state-changing frame with the stale sequence 2. The target must return an `Error` frame, terminate its controlled session with the expected nonzero code, and leave no running child. Continuing after the protocol/state disagreement is a test failure.

### API probe snapshot tests

```powershell
ctest --test-dir build --build-config Release -R GateCProbeSnapshotTests --output-on-failure
```

Covers exact round-trip serialization, schema/magic rejection, truncated and
oversized payloads, malformed booleans, inconsistent comparison flags, and a
missing target-window identity.

### Local API probe self-test

```powershell
.\build-x64\gate-c\x64\hydra_gate_c_api_probe.exe --baseline-self-test
```

Creates a hidden HydraSeat-owned window, reads all baseline APIs on that UI
thread, reads direct adapter state, and proves an intentional foreground-view
difference without modifying global state.

### Two-process API baseline and failure self-test

```powershell
.\build-x64\gate-c\x64\hydra_gate_c_host.exe `
  --baseline-self-test `
  --target .\build-x64\gate-c\x64\hydra_gate_c_api_probe.exe
```

The host launches two probes with different Seat adapter state and verifies:

- A/B keyboard, async-edge, mouse, wheel, cursor, clip, foreground, and capture
  adapter state never crosses processes;
- each OS API observation was captured by the probe window's owning UI thread;
- both adapters may report virtual foreground while neither hidden probe is the
  real OS foreground window;
- missing target HWND, handshake timeout, abnormal child exit, and host
  disconnect fail closed;
- clean shutdown and two repeated start/stop cycles leave no child process.

## Manual controlled-process run

Prerequisites:

1. Use the HydraSeat assignment UI to assign exclusive keyboard/mouse identities to two Seats.
2. Save `workspace_config.json`.
3. Build `hydra_gate_c_host`, `hydra_gate_c_target` and `hydra_gate_c_adapter` in the same output directory.

Run:

```powershell
.\build-x64\gate-c\x64\hydra_gate_c_host.exe `
  --profile workspace_config.json `
  --trace hydra_gate_c_host.jsonl
```

Or click **Gate C Process Lab** in the main UI.

Expected behavior:

- two controlled target windows appear;
- both display `virtual foreground: true`, even though only one can be the actual Windows foreground window;
- Seat 1's assigned keyboard/mouse changes only Target 1's virtual state;
- Seat 2's devices change only Target 2's virtual state;
- unassigned/shared devices fail closed through the existing routing policy;
- normal Windows input still affects ordinary applications because no physical suppression is enabled;
- closing either target or pressing Ctrl+C causes host cleanup of the controlled session.

## Bounded writer queues

Interactive Raw Input delivery uses a dedicated bounded queue and writer thread per target. This prevents a slow or dead target from blocking the Raw Input message loop for the full pipe timeout.

Policy:

- maximum queued frames are bounded;
- queue-full events return dispatch failure and are visible in route diagnostics;
- target write failure is fatal for the controlled session;
- the host stops routing, drains/stops writers and cleans up children;
- no event is silently rerouted to another Seat.

A later implementation may coalesce relative mouse movement while preserving key/button transitions. The current version prioritizes correctness and visible failure over silent state loss.

## Gate C acceptance status

### Completed

- [x] Versioned host/target protocol
- [x] Local-only named-pipe transport with timeouts
- [x] Session token, Seat, PID and architecture handshake
- [x] Separate target processes
- [x] Process-local adapter DLL and versioned C ABI
- [x] Keyboard down/high-bit state
- [x] One-shot async press edges
- [x] Mouse button/wheel state
- [x] Virtual cursor/clip state
- [x] Virtual foreground/capture state
- [x] Two-process synthetic no-cross-state test on Windows CI
- [x] Child shutdown/forced cleanup
- [x] Bounded interactive writer queues
- [x] Versioned, bounded OS/adapter probe snapshot serialization
- [x] Controlled API probe source and CMake/test integration
- [x] Explicit process/PE architecture detection and bounded manifest selector source
- [x] Deterministic architecture-neutral names under `gate-c/x86` and `gate-c/x64`
- [x] Declared x86/x64 CTest and x64-host-to-x86-target CI jobs
- [x] Windows/MSVC execution of the x86/x64 architecture matrix and x64-host-to-x86-target/probe self-tests (`32727711605`)
- [x] P3-API-02 polling shim source, fixed C ABI, transactional IAT engine, architecture manifest, native x64/x86 tests, and cross-architecture ordinary-polling proof (`32780563364`)
- [x] P3-API-03 cursor/clip/logical-focus/capture shim, adapter ABI v2, native x64/x86 24/24 CTest, cross-architecture two-probe isolation, and host-native global-state preservation (`32792381573`)
- [x] P3-RAW-01 standalone probe, bounded trace/parser contract, explicit synthetic fixture, native x64/x86 28/28 CTest, retained/reviewed observed registration traces, and process teardown evidence (`32800513365`)

### Pending

- [ ] Physical Gate C run using the user's two keyboard/two pointing-device profile
- [ ] Physical keyboard/mouse `WM_INPUT` and actual device-change trace (P3-HW-01)
- [ ] Controlled Raw Input virtualization consumer (P3-RAW-02; ready)
- [ ] Adapter crash/watchdog recovery acceptance
- [ ] Commercial non-anti-cheat game profile experiment
- [ ] Physical device cloaking/suppression

## Next implementation step

P3-RAW-01 is `VALIDATED` by Windows run `32800513365`. P3-RAW-02 is now
READY and must use the retained x64/x86 observed behavior as its contract:
last-registration-wins per usage, usage-local removal, accepted-but-not-echoed
`RIDEV_DEVNOTIFY`, retained destroyed-HWND runtime values until replacement,
and the architecture-specific structure sizes with 8-byte raw-buffer alignment.
Physical `WM_INPUT` and device-change evidence remains P3-HW-01.

The two-probe process test releases Probe B's virtual capture while asserting
that Probe A retains its own capture, then shuts down Probe A and re-queries
Probe B after A's HWND has been destroyed. Capture release and destroyed-window
isolation are therefore explicit cross-process regressions.

Gate C is not complete until controlled probes observe Seat-local values through the API surface a real game would call.
