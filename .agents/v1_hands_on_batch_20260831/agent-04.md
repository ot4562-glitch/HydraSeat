# Agent 04 — Hardware Setup V1 hands-on UX

You own `CHUNK-V1H-04-HARDWARE-UX`.

## Start
1. Work only in `C:\HydraSeat\repo`.
2. Read `AGENTS.md`, `.agents/AGENTS.md`, only `CHUNK-V1H-04-HARDWARE-UX` from `.agents/CHUNKS.md`, and directly relevant hardware/input/UI files. Do not load the full roadmap/history.
3. Run `python3 tools/chunk_claim.py list`.
4. Claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-V1H-04-HARDWARE-UX --owner v1h-04-hardware-ux-20260831 --paths include/hydra/gui_win32.hpp src/gui_win32.cpp include/hydra/hardware_detector.hpp src/hardware_detector.cpp include/hydra/input_observation.hpp src/input_observation.cpp tests/test_hardware_identity.cpp tests/test_input_observation.cpp tests/test_control_surface_model.cpp --note "V1 human-readable Display and input assignment UX"`

## Current real-machine evidence
The latest real machine now exposes distinguishable display topology authority:
- `DISPLAY1` = `LG ULTRAGEAR`, 1920x1080, primary;
- `DISPLAY2` = `LF24T35`, 1920x1080.

Press/click identification works in the current user test, Display assignment works once the monitor can be identified, and Back to Games lifecycle passed. Preserve those wins.

## Goal
Turn Hardware Setup into a screen a non-developer can use without reading device IDs or knowing HydraSeat internals.

A user should be able to:
1. recognize the two active displays;
2. recognize or intentionally identify the keyboard/mouse they are holding;
3. assign each device to Display 1 or Display 2;
4. understand success/failure/ambiguous/timeout state;
5. return to Games without stale windows or stale selected tiles.

## Required behavior
- active+attached monitor outputs are preferred over stale/disabled topology records;
- display card/column labels include real friendly name plus a deterministic discriminator such as DISPLAY number, resolution and primary state;
- identical physical monitor models remain distinguishable;
- do not display raw long device-instance IDs in the ordinary path;
- keyboard identification accepts intentional key-down, not key-up/noise;
- mouse identification accepts intentional button-down, not movement/wheel/release;
- waiting state is visually explicit;
- success immediately selects/highlights the identified tile and makes assignment obvious;
- timeout, stale device, removal and ambiguity tell the user to retry rather than silently failing;
- hot-plug refresh clears stale selection and never assigns a guessed device;
- two physically distinct identical VID/PID devices remain distinct;
- multiple HID collections from one physical device collapse only through stable Windows identity authority, never through name/VID/PID count hacks;
- repeated Games -> Setup -> Games must not leak/duplicate a window.

Use existing localized TextIds where possible. Do not hard-code new Korean/Chinese strings; report any missing TextId to control tower.

## Tests
Expand only claimed focused tests. Cover at least:
- same VID/PID distinct physical devices;
- multiple collections one physical device;
- key-down vs key-up/noise;
- mouse click vs movement/wheel/release;
- stale sequence/removal/ambiguity;
- display presentation chooses active+attached output;
- two identical display names remain distinguishable by presentation metadata if testable in the claimed boundary;
- stale selected tile is cleared on refresh.

Run focused x64 tests for HardwareIdentity/InputObservation/ControlSurfaceModel equivalents. Do not run the whole suite; control tower owns broad regression.

## Finish
Use:

`python3 tools/chunk_claim.py done CHUNK-V1H-04-HARDWARE-UX --owner v1h-04-hardware-ux-20260831 --note "<hardware UX changes + focused tests + physical evidence still pending>"`

Use `blocked` for a real blocker. No Git/remote/CMake/shared-doc edits and no changes outside claimed paths.
