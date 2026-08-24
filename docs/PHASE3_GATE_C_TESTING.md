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

P3-ARCH-01 is code-complete but not yet Windows-validated. It adds explicit
Win32 process-architecture detection, PE-machine preflight, a bounded schema-v1
artifact manifest/selector, deterministic `gate-c/x86` and `gate-c/x64`
layouts, and declared x86/x64 CI jobs. The x86 availability claim remains
disabled until the new Windows matrix and real x64-host-to-x86-target job pass.

This is not yet a general game hook or a completed Gate C implementation. The controlled targets call HydraSeat's adapter API directly. Commercial games still call the ordinary Windows APIs unless a later, explicitly approved compatibility adapter interposes those calls.

## Safety boundary

The Gate C lab performs none of the following:

- injection into a third-party or commercial process;
- Import Address Table patching, detours or system-wide hooks;
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
- never calls a Win32 cursor/focus/capture setter and installs no hook or shim.

Visible probe mode shows the last OS and adapter observations side by side. A
foreground mismatch is expected before interposition: two adapters may both
report virtual foreground while Windows still has only one real foreground
window.

### `hydra_gate_c_adapter.dll`

The adapter is a process-local state component with a C ABI. It currently provides semantic equivalents for controlled tests:

- key down/up state;
- `GetAsyncKeyState`-style high bit plus one-shot press edge;
- `GetKeyState`-style key-down high bit;
- `GetKeyboardState`-style 256-byte high-bit array;
- mouse button and wheel state;
- virtual cursor position;
- virtual clip rectangle;
- virtual foreground state;
- virtual capture state;
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
contains target, adapter, and API-probe descriptors under:

```text
<artifact-root>/x86/hydra_gate_c_target.exe
<artifact-root>/x86/hydra_gate_c_adapter.dll
<artifact-root>/x86/hydra_gate_c_api_probe.exe
<artifact-root>/x64/hydra_gate_c_target.exe
<artifact-root>/x64/hydra_gate_c_adapter.dll
<artifact-root>/x64/hydra_gate_c_api_probe.exe
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
wrong target/adapter architectures.

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

### Pending

- [ ] Physical Gate C run using the user's two keyboard/two pointing-device profile
- [ ] Controlled Raw Input consumer that calls `RegisterRawInputDevices` / `GetRawInputData`
- [ ] Windows/MSVC execution of the new x86/x64 architecture matrix and x64-host-to-x86-target self-test
- [ ] Controlled probe using actual Win32 polling/cursor/focus APIs through opt-in compatibility shims (P3-API-02/P3-API-03)
- [ ] Clean-room API interposition for HydraSeat-owned test binaries
- [ ] Adapter crash/watchdog recovery acceptance
- [ ] Commercial non-anti-cheat game profile experiment
- [ ] Physical device cloaking/suppression

## Next implementation step

First run and pass the P3-ARCH-01 Windows/MSVC x86/x64 matrix and the real
x64-host-to-x86-target self-test documented above. Only after that evidence is
recorded may P3-API-02 begin controlled polling API interposition, still limited
to HydraSeat-owned probe executables:

1. Add an opt-in compatibility shim loaded at process startup, not injected into an arbitrary running process.
2. Interpose `GetAsyncKeyState`, `GetKeyState`, and `GetKeyboardState` only for the controlled probe.
3. Make every interposed call use the adapter C ABI rather than owning duplicate state.
4. Prove rollback and unhook behavior before proceeding to cursor/focus or Raw Input packets.

Gate C is not complete until controlled probes observe Seat-local values through the API surface a real game would call.
