# Agent 03 — Seat hardware setup UI

You own `CHUNK-UXH-03-HARDWARE-SETUP-UI`.

## Start
Read `AGENTS.md`, `.agents/AGENTS.md`, your chunk section, then only the claimed files and directly referenced UI/model types. Do not inspect unrelated runtime/provider code.

Run `python3 tools/chunk_claim.py list`, then claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-UXH-03-HARDWARE-SETUP-UI --owner uxh-03-hardware-ui-20260830 --paths include/hydra/gui_win32.hpp src/gui_win32.cpp include/hydra/control_surface_model.hpp src/control_surface_model.cpp tests/test_control_surface_model.cpp --note "human-usable non-drag Seat hardware setup"`

## Screenshot-backed defects
The current dark hardware screen is visually disconnected from the light launcher, exposes many nearly indistinguishable keyboard/mouse tiles, and effectively teaches drag-and-drop as the assignment mechanism. PRODUCT_V1 explicitly says click/tap is primary and drag is optional.

## Outcome
Make the current native hardware setup usable without drag. A user must be able to select a tile and assign/unassign it to Seat 1 or Seat 2 through obvious controls or an equally simple click path. Keep drag as an optional shortcut. Simplify visual hierarchy, spacing, selection/assignment cues and instructions; use shape/text plus color. Use human device names/state and keep raw interface/PID/protocol details out of the normal surface.

Do not create a second hardware model. Reuse `ControlSurfaceModel` only for state it actually owns. If Agent 02's identification API is available and cleanly consumable without crossing your path envelope, you may wire it; otherwise return the exact integration call/state needed. Never duplicate identification logic inside `gui_win32.cpp` merely to finish faster.

You may refactor inside your envelope if it removes duplicated UI-state branching. Prefer fewer controls and one obvious primary path.

## Acceptance
- non-drag assignment/unassignment exists and is keyboard-reachable;
- drag remains optional;
- assigned/unassigned/selected state is unambiguous without relying only on color;
- incomplete Seat setup remains savable where product rules allow;
- no raw HID/PID/protocol details in normal state;
- existing Host/session authority is untouched.

Run `ControlSurfaceModelTests` and build the `HydraSeat` target on x64; x86 focused build if practical. Manual screenshot approval remains pending. Finish DONE/BLOCKED with exact tests and cross-chunk integration notes. No Git/remote actions.
