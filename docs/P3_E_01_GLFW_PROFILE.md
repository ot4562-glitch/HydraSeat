# P3-E-01 — GLFW 3.5.1 controlled external-application profile

## Scope

P3-E-01 is the first HydraSeat compatibility profile exercised against an inspectable application that was not written for HydraSeat. The selected target is the upstream GLFW `tests/cursor.c` application from GLFW 3.5.1.

This packet does **not** claim physical keyboard/mouse suppression, device cloaking, game compatibility, anti-cheat compatibility, DRM/protected-process support, or production third-party injection. Those remain separate gates.

## Clean-room and provenance

- Project: GLFW
- Version: `3.5.1`
- Commit: `d9d6f0f1f967807ffade6598ea9a631ebaf37a56`
- License: zlib/libpng
- Target: upstream `tests/cursor.c`, built as `cursor.exe`
- External GLFW source and binaries are **not vendored** in the HydraSeat repository.
- `tools/prepare_p3_e_01_glfw.ps1` does not download source. It accepts an already-provided clean source checkout, independently resolves and verifies the exact Git commit, builds the target, measures its PE imports, hashes the result, and writes ignored evidence.
- GitHub Actions performs the external clone in an explicit separate step before invoking the same preparation script.

## Measured Win32 API profile

The MSVC x64 GLFW 3.5.1 `cursor.exe` imports the following HydraSeat-controlled Win32 API subset:

- `GetKeyState`
- `GetCursorPos`
- `SetCursorPos`
- `ClipCursor`
- `GetActiveWindow`
- `SetCapture`
- `ReleaseCapture`
- `RegisterRawInputDevices`
- `GetRawInputData`

The exact `HydraGateCShimConfigV3.required_api_mask` is `0x0000b93a`.

The existing V1/V2 shim contract remains strict and still requires every API in each enabled controlled-probe group. P3-E adds the V3 profiled entry point and `installProfiled()` IAT transaction only for an explicitly declared allowlisted subset. Missing, duplicate, stale, changed, already-patched, invalid, or out-of-mask entries still fail closed, and uninstall restores only the exact slots changed by the profiled transaction.

## Controlled launch boundary

`hydra_gate_c_external_harness.exe` has no attach-to-existing-process command.

For each target it:

1. creates the configured executable itself with `CREATE_SUSPENDED`;
2. records the exact process creation identity;
3. places the new process in a private `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` Job Object before injection or resume;
4. verifies x64 process architecture;
5. creates a PID-scoped fixed-size bridge configuration containing only Seat ID, exact API mask, Gate C session token, and bounded pipe name;
6. loads the fixed HydraSeat bridge DLL into that owned suspended process;
7. validates the existing Gate C `Hello` against token, Seat, PID, architecture, and same-PID bootstrap window;
8. resumes the target only after the bridge is ready.

The packet does not expose arbitrary commands, arbitrary bridge DLL choice through an in-process protocol, or an existing-process attach surface.

## GLFW focus lifecycle discovery

The first real two-instance run exposed a compatibility requirement that the HydraSeat-owned probes did not model: Windows exposes one native foreground/focus chain, and GLFW unregisters its raw mouse request when it consumes a focus-loss lifecycle.

HydraSeat already virtualizes Seat-local foreground/active/focus/capture queries. For the controlled external profile, the harness additionally delivers process-local `WM_ACTIVATE` / `WM_SETFOCUS` lifecycle messages to each owned GLFW window. It does **not** call `SetForegroundWindow` or otherwise mutate the host desktop's global foreground state.

This keeps both GLFW instances' own raw-mouse lifecycle active while the shim continues to isolate the underlying API state per process.

## Measured acceptance

The declared deterministic trial injects four synthetic routed mouse events per Seat after both GLFW instances enter disabled-cursor/raw-mouse mode:

- Seat 1: `(dx=+11, dy=+3)` × 4
- Seat 2: `(dx=-7, dy=+5)` × 4

The acceptance result is based on the upstream GLFW application's own cursor callback stdout, not only on HydraSeat sender-side counters. A pass requires:

- Seat 1 expected callback count = 4;
- Seat 2 expected callback count = 4;
- Seat 1 cross-pattern count = 0;
- Seat 2 cross-pattern count = 0;
- receiver-verified events = 8;
- independent adapter cursor/key snapshots for both Seats;
- clean ordinary shutdown of both targets;
- a third owned target terminated by closing the Job Object handle, proving the forced recovery guard path;
- no remaining `cursor.exe` / external-harness processes;
- target SHA-256 unchanged before/after the run;
- a clean native relaunch of the same external executable after HydraSeat teardown.

Local real-process acceptance on 2026-08-27 passed all conditions with prepared target SHA-256 `84931e1874ecc5badb8d9bae713b75f701e79112923d90f7303c5d35d3f92d15`.

## Reproduction

Prepare an already-obtained clean GLFW 3.5.1 checkout:

```powershell
.\tools\prepare_p3_e_01_glfw.ps1 `
  -SourceRoot C:\path\to\glfw-3.5.1 `
  -BuildRoot C:\path\to\glfw-build `
  -EvidenceDirectory .\out\p3-e-01\prepared `
  -CMakePath cmake.exe `
  -DumpBinPath dumpbin.exe
```

Run the controlled real-application acceptance against the resulting build manifest:

```powershell
.\tools\run_p3_e_01_glfw.ps1 `
  -BuildManifest .\out\p3-e-01\prepared\glfw-build-manifest.json `
  -HarnessPath .\build\gate-c\x64\hydra_gate_c_external_harness.exe `
  -BridgePath .\build\gate-c\x64\hydra_gate_c_external_bridge.dll `
  -EvidenceDirectory .\out\p3-e-01\acceptance
```

Evidence remains ignored under `out/` and contains the exact external binary hash, API mask, process identities, receiver counts, cross counts, forced-guard result, and native-relaunch result.

## Current limitations

- Real GLFW acceptance in this packet is x64.
- Win32/x86 builds and ABI/component regressions are required in Windows CI, but no x86 GLFW compatibility claim is made by this profile.
- The deterministic Seat inputs are synthetic Gate C events; physical-device routing and physical zero-bleed remain P3-HW-01.
- No physical input suppression or device cloaking is performed or advertised.
- No anti-cheat, DRM, protected-process, security-product bypass, or existing-process injection is supported.
- This is an open-source application profile, not the P3-E-02 game profile.
