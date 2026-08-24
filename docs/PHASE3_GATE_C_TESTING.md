# Phase 3 Gate C Controlled-Process Lab

## Status

Gate C now has an implemented **controlled-process foundation**:

- a versioned host/target wire protocol;
- local named-pipe transport with timeouts and session-token validation;
- a process-local shared-library adapter with a stable C ABI;
- independent keyboard, mouse, cursor, clip, virtual-foreground and virtual-capture state;
- two separate HydraSeat-owned target processes;
- an automated Windows self-test that proves their state does not bleed across processes.

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

### Target self-test

```powershell
.\build\Release\hydra_gate_c_target.exe --self-test
```

Verifies that the executable loads and uses the adapter DLL and can apply/query controlled state.

### Two-process integration self-test

```powershell
.\build\Release\hydra_gate_c_host.exe `
  --self-test `
  --target .\build\Release\hydra_gate_c_target.exe
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

## Manual controlled-process run

Prerequisites:

1. Use the HydraSeat assignment UI to assign exclusive keyboard/mouse identities to two Seats.
2. Save `workspace_config.json`.
3. Build `hydra_gate_c_host`, `hydra_gate_c_target` and `hydra_gate_c_adapter` in the same output directory.

Run:

```powershell
.\build\Release\hydra_gate_c_host.exe `
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

### Pending

- [ ] Physical Gate C run using the user's two keyboard/two pointing-device profile
- [ ] Controlled Raw Input consumer that calls `RegisterRawInputDevices` / `GetRawInputData`
- [ ] Controlled probe using actual Win32 polling/cursor/focus APIs through an opt-in compatibility shim
- [ ] Clean-room API interposition for HydraSeat-owned test binaries
- [ ] Adapter crash/watchdog recovery acceptance
- [ ] Commercial non-anti-cheat game profile experiment
- [ ] Physical device cloaking/suppression

## Next implementation step

The next Gate C sub-stage is **controlled API interposition**, still limited to HydraSeat-owned probe executables:

1. Add a probe process that directly calls the ordinary Windows input/focus APIs.
2. Add an opt-in compatibility shim loaded at process startup, not injected into an arbitrary running process.
3. Interpose only the selected APIs for the controlled probe:
   - `RegisterRawInputDevices`;
   - `GetRawInputData` / `GetRawInputBuffer`;
   - `GetAsyncKeyState` / `GetKeyState` / `GetKeyboardState`;
   - `GetCursorPos` / `SetCursorPos` / `ClipCursor`;
   - `GetForegroundWindow` / `GetFocus` / `GetCapture`.
4. Make every hook call the adapter C ABI rather than owning duplicate state.
5. Prove rollback and unhook behavior before testing any third-party executable.

Gate C is not complete until controlled probes observe Seat-local values through the API surface a real game would call.
