# Agent 02 — Press/click-to-identify input model

You own `CHUNK-UXH-02-INPUT-IDENTIFY`.

## Start
Read only `AGENTS.md`, `.agents/AGENTS.md`, your chunk section, and the three claimed files plus directly referenced types as read-only context. Do not broad-scan the repository.

Run `python3 tools/chunk_claim.py list`, then claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-UXH-02-INPUT-IDENTIFY --owner uxh-02-input-identify-20260830 --paths include/hydra/input_observation.hpp src/input_observation.cpp tests/test_input_observation.cpp --note "intentional press/click device identification"`

## Problem to solve
Seat setup needs a human flow such as "press any key on the keyboard for Seat 1" or "click a button on the mouse for Seat 2" instead of asking users to distinguish HID path noise. Implement the smallest reusable identification state in the existing input-observation area.

The model must consume existing stable device IDs/events and return an exact candidate/result. It must not save Seat configuration or claim physical suppression. Keyboard identification requires an intentional key transition. Mouse identification ignores motion-only noise; a button transition is the safe default. Controller identification requires an intentional button event if the existing event model can express it; if not, keep the API honest and return an integration note instead of inventing a fake controller path. Cancellation, timeout, removal, stale sequence and ambiguous/shared-device cases fail closed.

Prefer a small state object/value types over callbacks scattered through the UI. Do not log typed text/key sequences beyond what existing privacy rules already permit.

## Acceptance
- deterministic begin/cancel/timeout/result state;
- intentional input only, no mouse-motion accidental capture;
- exact stable device ID returned;
- stale/out-of-order/device-removal/ambiguous cases rejected;
- no Seat mutation and no global hook.

Run focused `InputObservationTests` on x64 and, if practical, x86. Finish DONE/BLOCKED with exact verification and any narrow GUI integration contract. No Git/remote actions.
