# Phase 3 physical input acceptance

This document is the manual acceptance procedure for `P3-HW-01`. It validates the
existing Phase 3 Gate A/B/C controlled boundaries with real local hardware. It is
**not** a claim that HydraSeat already suppresses native Windows input, supports a
commercial game, or isolates every controller API.

The canonical runner is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_phase3_hardware_acceptance.ps1 `
  -ProfilePath .\workspace_config.json `
  -BuildRoot .\build-x64 `
  -Stage All
```

The runner is resumable, preserves failed evidence, pins the source profile by
SHA-256, and cannot produce a final `PASS` from process exit codes alone. A human
must explicitly complete the required checks and enter the final manual verdict.

## 1. Safety and scope

Before running the procedure:

- use only HydraSeat-owned Gate A/B/C lab and controlled-target executables;
- do not attach HydraSeat to an anti-cheat, protected, DRM-sensitive, or unrelated
  third-party process;
- keep ordinary Windows recovery input available;
- remember that Gate A/B are observation/diagnostic routing and Gate C is a
  HydraSeat-owned controlled-process boundary;
- this packet does not install a driver, hide a physical device, replace a system
  DLL, inject into a game, or mutate persistent Windows state;
- if a test behaves unexpectedly, close the HydraSeat-owned windows or stop the
  Gate C host. The runner cleans only the process tree it started.

### Trace privacy

JSONL traces redact virtual-key identifiers by default:

```json
{"vkey":null,"key_code_redacted":true}
```

Do not enable sensitive key logging for an ordinary acceptance run. If exact key
IDs are genuinely required for diagnostics, use both explicit switches:

```powershell
-SensitiveKeyLogging -AcknowledgeSensitiveKeyLogging
```

The runner displays a warning and passes `--trace-sensitive-keys` only for that
session. Such traces can reveal typed key codes and must be handled accordingly.

## 2. Required test topology

Record the following before testing:

| Field | Value |
| --- | --- |
| Tester | |
| Test date/time | |
| Windows edition/version | |
| Windows build | |
| CPU/GPU | |
| HydraSeat branch/commit | |
| Profile path | |
| Profile SHA-256 | automatically captured |
| Seat 1 keyboard model / stable ID | |
| Seat 1 mouse/touchpad model / stable ID | |
| Seat 2 keyboard model / stable ID | |
| Seat 2 mouse/touchpad model / stable ID | |
| Composite HID used, if any | |
| USB hub/dock topology | |
| Other relevant hardware notes | |

The source profile must contain at least two active Seats. The first two active
Seats each need an exclusive keyboard and exclusive mouse/touchpad identity. The
runner refuses to start if those prerequisites are missing or if a non-shareable
physical input identity is assigned to multiple Seats.

The runner never edits the source profile. For the shared-device negative test it
creates `shared-case-profile.json` inside the session directory and records that
file's SHA-256 separately.

## 3. Build prerequisites

Use a Release build containing the current Phase 3 lab and controlled target.
For an x64 run the default paths are resolved under `build-x64`:

```text
build-x64\Release\hydra_input_lab.exe
build-x64\gate-c\x64\hydra_gate_c_host.exe
build-x64\gate-c\x64\hydra_gate_c_target.exe
```

For x86, pass the matching build root and architecture:

```powershell
-BuildRoot .\build-x86 -Architecture x86
```

CI proves that the runner/parser contracts load on Windows x64/x86 builds; CI
without the user's physical devices does not satisfy this manual packet.

## 4. Session and resume behavior

A new run creates an ignored local evidence directory similar to:

```text
out\phase3-hardware-acceptance\p3-hw-YYYYMMDD-HHMMSS\
  phase3-hardware-manifest.json
  shared-case-profile.json
  gate-a.jsonl
  gate-b-exclusive.jsonl
  gate-b-shared.jsonl
  gate-c.jsonl
  gate-c-metrics.json
  phase3-hardware-report.json
```

Run one gate at a time when convenient:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_phase3_hardware_acceptance.ps1 `
  -ProfilePath .\workspace_config.json `
  -BuildRoot .\build-x64 `
  -Stage GateA
```

Resume the same session later:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_phase3_hardware_acceptance.ps1 `
  -Resume .\out\phase3-hardware-acceptance\p3-hw-YYYYMMDD-HHMMSS `
  -BuildRoot .\build-x64 `
  -Stage All
```

For a report-only re-analysis, build binaries and even the original profile are
not required; the preserved manifest and traces are sufficient:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_phase3_hardware_acceptance.ps1 `
  -Resume .\out\phase3-hardware-acceptance\p3-hw-YYYYMMDD-HHMMSS `
  -Stage Summarize
```

If an execution stage is resumed, however, the runner reopens the original
profile path from the manifest and rejects the session if its SHA-256 changed.
Do not mix evidence from different profile revisions.

## 5. Gate A — physical observation and hot-plug

The runner launches `hydra_input_lab --no-profile` with a dedicated trace.
Exercise all of the following while the lab is open:

1. press/release keys on both keyboards independently;
2. move/click/scroll both pointing devices independently;
3. hold and release keys so down/up behavior is visible;
4. unplug and reconnect at least one keyboard and one pointing device repeatedly;
5. when a composite HID is available, remove/reconnect one child collection and
   verify the physical identity remains coherent until the final child disappears;
6. keep the observer running for at least 10 minutes by default and continue
   reconnect/input activity during the soak;
7. review the lab's dropped-event counter before closing it.

Automatic evidence requirements include:

- at least four distinct physical input identities in the trace;
- at least one removal and one arrival record;
- no malformed trace record or unexpected physical-suppression claim;
- key identifiers redacted unless the session explicitly opted in.

The tester still records the device-identity, down/up, reconnect, composite-HID,
and drop-counter observations manually.

## 6. Gate B — exclusive routing and fail-closed ambiguity

The first Gate B run uses the pinned source profile. Exercise:

- Seat 1 keyboard and pointing device;
- Seat 2 keyboard and pointing device;
- at least one connected but unassigned input device if available;
- closing one Seat target window, then generating input owned by that Seat.

Expected behavior:

- each exclusively owned device increments only its configured Seat's routing and
  target-notification counters;
- an unassigned device is `UnassignedDevice` and is not guessed into a Seat;
- an inactive/missing target is an explicit failure rather than automatic reroute;
- the JSONL `seat_id` for every `Routed` record agrees with the pinned profile.

The second Gate B run uses the derived `shared-case-profile.json`. The selected
Seat 1 device is deliberately assigned/shareable across both Seats only in that
copy. Exercise that exact device. Its records must include
`AmbiguousSharedDevice` and must contain **zero** `Routed` records for that device.
The parser checks this automatically.

Gate B remains diagnostic routing. Native Windows still receives normal input and
this result must not be described as physical input suppression.

## 7. Gate C — two controlled target processes

The runner launches the current `hydra_gate_c_host` with the pinned source
profile, a dedicated JSONL trace, a metrics report, and an explicit
HydraSeat-owned `hydra_gate_c_target.exe`.

Exercise:

1. Seat 1 keyboard/mouse against controlled Target 1;
2. Seat 2 keyboard/mouse against controlled Target 2;
3. alternate rapidly between both Seats;
4. exercise any unassigned/shared input available and confirm fail-closed behavior;
5. stop the host with Ctrl+C or close a controlled target;
6. verify the owned controlled targets are gone after cleanup.

Required observations:

- two controlled target windows are visible;
- Seat 1 physical input changes only Target 1's virtual state;
- Seat 2 physical input changes only Target 2's virtual state;
- the main trace contains routed evidence for both expected Seats;
- `cross_seat_events == 0` and `cross_process_events == 0` in the metrics report;
- writer queue and recorder drop/error counters remain zero;
- any `missing_receiver_evidence_events` warning is reviewed explicitly;
- no claim is made that native Windows input was suppressed.

A nonzero cross-Seat/cross-process counter or recorder/queue loss makes the
machine-readable report `FAIL`. A zero counter with missing receiver evidence is
only a warning and is **not** automatically zero-bleed proof; the physical target
observations remain manual evidence.

## 8. Final evidence review

The final summarizer can also be run directly:

```powershell
python tools\summarize_phase3_trace.py `
  --manifest <session>\phase3-hardware-manifest.json `
  --output <session>\phase3-hardware-report.json
```

Possible final report verdicts:

- `FAIL` — automatic evidence or an explicit manual check found a failure;
- `PENDING` — no automatic failure, but required human evidence/verdict is still
  incomplete;
- `PASS` — automatic evidence is clean **and** every required manual check is
  explicitly PASS (or the allowed composite-HID check is explicitly N/A) **and**
  the tester explicitly entered final `PASS`.

A process exit code of zero, CI result, trace existence, or zero synthetic counter
alone can never produce physical `PASS`.

After a genuine physical PASS, retain the manifest/report and record a sanitized
summary in `docs/implementation/STATUS.md` and the applicable hardware/
compatibility matrix. Do not commit traces containing sensitive key IDs, personal
paths, or unrelated hardware/private information.

## 9. Acceptance record

Fill this section when promoting `P3-HW-01` from `CODE_COMPLETE` to `VALIDATED`.

| Gate | Result | Evidence summary |
| --- | --- | --- |
| Gate A physical observation | PENDING | |
| Gate B physical Seat routing | PENDING | |
| Gate C controlled-process physical routing | PENDING | |

Final manual verdict: **PENDING**

Known limitations / failed cases:

-

Recovery/cleanup result:

-
