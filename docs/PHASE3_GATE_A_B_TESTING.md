# Phase 3 Gate A/B Input Lab

## Purpose

`hydra_input_lab` is the first executable Phase 3 feasibility harness.

It proves two limited properties:

- **Gate A implementation:** HydraSeat can observe Raw Input from distinct physical keyboards, mice, and touchpads, maintain per-device state, and record arrival/removal diagnostics.
- **Gate B implementation:** HydraSeat can resolve an exclusively owned device to a Seat and post a HydraSeat-specific route notification to that Seat's own target window.

It does **not** prove input isolation. Normal Windows keyboard/mouse delivery remains active, games can still read global or native state, and no physical device is hidden or suppressed.

## Safety boundary

The lab performs none of the following:

- process injection;
- API hooking;
- driver installation;
- HidHide control calls;
- physical input suppression;
- cursor or foreground virtualization;
- anti-cheat or protected-process interaction.

Every trace input record includes:

```text
isolation_guarantee = diagnostic_route_only_native_os_input_not_suppressed
```

This prevents Gate B evidence from being confused with Gate C/D/E isolation evidence.

Virtual-key identifiers are redacted from JSONL traces by default (`vkey=null`,
`key_code_redacted=true`). Exact key IDs require the explicit
`--trace-sensitive-keys` diagnostic switch, which may reveal typed key codes and
must not be used silently.

## Build targets

```text
hydra_input_lab
input_observation_tests
```

Automated CTest entries:

```text
InputObservationTests
InputLabSelfTest
```

For the real P3-HW-01 hardware gate, prefer the guided runner instead of assembling
Gate A/B evidence by hand:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_phase3_hardware_acceptance.ps1 `
  -ProfilePath .\workspace_config.json `
  -BuildRoot .\build-x64 `
  -Stage All
```

It pins the profile SHA-256, preserves a resumable manifest, creates the shared/
ambiguous negative-test profile only inside the ignored session directory, and
requires explicit human checks before any physical PASS. See
[`hardware/PHASE3_ACCEPTANCE_TEMPLATE.md`](hardware/PHASE3_ACCEPTANCE_TEMPLATE.md).

## Usage

### Use an existing Seat profile

First assign devices in the main HydraSeat UI and save `workspace_config.json`. Then run:

```powershell
.\build\Release\hydra_input_lab.exe --profile workspace_config.json
```

Optional trace destination:

```powershell
.\build\Release\hydra_input_lab.exe `
  --profile workspace_config.json `
  --trace phase3-input-lab.jsonl
```

The first two active Seats are shown as separate top-level windows. Each window displays:

- assigned keyboard/mouse/controller stable IDs;
- whether assigned devices have been observed online;
- routed keyboard/mouse event counters;
- the last physical device routed to that Seat;
- dispatch failures;
- global unassigned/ambiguous/hot-plug diagnostics;
- a short list of every observed physical Raw Input identity.

### Observer-only mode

```powershell
.\build\Release\hydra_input_lab.exe --no-profile
```

Two empty Seats are created. Raw Input identities and hot-plug state are still displayed and written to JSONL, but events remain `UnassignedDevice` because no Seat owns them.

### Non-interactive self-test

```powershell
.\build\Release\hydra_input_lab.exe --self-test
```

This checks deterministic two-Seat routing without opening windows or touching devices.

## Gate A manual acceptance procedure

Required equipment:

- two physical keyboards;
- two physical mice, or one mouse and one touchpad;
- an optional composite keyboard/mouse for collection-deduplication testing.

Procedure:

1. Start `hydra_input_lab` in observer-only mode.
2. Press keys independently on each keyboard.
3. Move/click each mouse independently.
4. Confirm each physical device appears with a stable device ID and independent event counter.
5. Hold and release a key; confirm the JSONL trace has matching down/up transitions.
6. Unplug one device and confirm one or more collection-removal records appear.
7. For a composite device, confirm removing one child collection does not mark the physical identity offline while sibling collections remain.
8. Reconnect the device and confirm it returns online under the expected stable identity.
9. Run for at least ten minutes while repeatedly reconnecting one test device; confirm no crash and inspect the router dropped-event counter.

Gate A acceptance criteria:

- every tested event has exactly one stable physical identity;
- distinct identical-model units remain distinguishable;
- composite child collections aggregate to one physical identity;
- device removal clears pressed-button/key state only after the final collection is gone;
- hot-plug does not crash or corrupt the trace;
- any dropped decode/callback count is visible rather than silently ignored.

## Gate B manual acceptance procedure

1. In the main HydraSeat UI, assign keyboard/mouse A to Seat 1 and keyboard/mouse B to Seat 2.
2. Save `workspace_config.json`.
3. Start `hydra_input_lab --profile workspace_config.json`.
4. Confirm the Seat 1 window lists only Seat 1's assigned stable IDs and Seat 2 lists only Seat 2's IDs.
5. Generate input on keyboard/mouse A and verify only Seat 1's **routed-event counter** and **target notifications processed** counter change.
6. Generate input on keyboard/mouse B and verify only Seat 2's routed-event counter and target-notification counter change.
7. Generate input from an unassigned device and verify neither Seat routed counter changes while the global unassigned counter increases.
8. Assign one input device as shareable to both Seats and reload. Verify the lab reports it as ambiguous and dispatches it to neither target.
9. Close one target window and verify its later events become a visible missing-target/dispatch failure rather than being guessed into the other Seat.
10. Inspect JSONL and verify every `Routed` record has the expected `seat_id` and `target_hwnd`.

Gate B acceptance criteria:

- HydraSeat's own routed path delivers each exclusive stable ID to exactly one target window;
- unassigned and shared/ambiguous devices fail closed;
- inactive/missing targets are explicit failures;
- no route record claims that native Windows input was suppressed.

## What Gate B does not prove

The foreground Seat window may still receive ordinary Windows input, and another game may still read:

- normal keyboard messages;
- `GetAsyncKeyState` / `GetKeyState` / `GetKeyboardState`;
- global cursor/capture/focus state;
- its own Raw Input registration;
- DirectInput, XInput, SDL, or direct HID state.

Those are Gate C/D concerns. Do not use Gate B results to claim that two commercial games are isolated.

## Trace format

The trace is JSON Lines. Representative records:

```json
{"record":"device_change","sequence":1,"change":"Arrival","device_id":"Keyboard:...","online":true}
{"record":"input","sequence":2,"device_id":"Keyboard:...","route":"Routed","seat_id":1,"isolation_guarantee":"diagnostic_route_only_native_os_input_not_suppressed"}
```

The monotonic timestamp is suitable for event ordering and relative latency measurements. It is not a wall-clock timestamp.

## Next step after acceptance

After physical Gate A/B acceptance, Phase 3 moves to controlled HydraSeat-owned target processes that independently exercise:

- Raw Input registration and `GetRawInputData`;
- `GetAsyncKeyState`, `GetKeyState`, and `GetKeyboardState`;
- cursor position/clip/capture;
- foreground/focus queries.

That is Gate C. No commercial game or device-cloaking backend should be used until the controlled target-process protocol and rollback path are tested.
