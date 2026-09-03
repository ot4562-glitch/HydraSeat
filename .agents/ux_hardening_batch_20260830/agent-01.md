# Agent 01 — Physical input identity

You own `CHUNK-UXH-01-INPUT-IDENTITY`.

## Start
1. Work only in `C:\HydraSeat\repo`.
2. Read `AGENTS.md`, `.agents/AGENTS.md`, and the `CHUNK-UXH-01-INPUT-IDENTITY` section of `.agents/CHUNKS.md`. Do not reread the whole repository.
3. Run `python3 tools/chunk_claim.py list`.
4. Claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-UXH-01-INPUT-IDENTITY --owner uxh-01-input-identity-20260830 --paths include/hydra/hardware_identity.hpp include/hydra/raw_input_utils.hpp src/raw_input_utils.cpp include/hydra/hardware_detector.hpp src/hardware_detector.cpp tests/test_hardware_identity.cpp tests/test_hydra.cpp --note "physical Raw Input/HID identity collapse"`

## Problem to solve
The real screenshot machine has one physical keyboard/mouse pair but HydraSeat reports many keyboard/mouse identities. Treat this as a product bug. The current Raw Input path already resolves device instance/parent IDs, but one physical USB/HID device can expose several collections/interfaces. Make the user-visible inventory represent physical devices, not each collection.

Use Windows SetupAPI/ConfigMgr authority and deterministic stable physical/container/ancestor identity. Friendly names, enumeration order, VID/PID alone, and current HANDLE values are not stable authority. Distinct devices with identical VID/PID must remain distinct. Pick one deterministic representative handle/path per physical device for current-process use while stable persisted identity stays independent of that handle.

You have autonomy to refactor inside the claimed files if it removes duplication or makes identity semantics clearer. Do not create a second detector/identity subsystem.

## Acceptance
- same physical device exposed through multiple HID interfaces collapses to one keyboard/mouse inventory item;
- distinct physical devices never collapse merely because names/VID/PID match;
- remote/synthetic filtering remains fail-closed;
- missing Windows properties have deterministic conservative fallback;
- ordering is deterministic;
- no claim that physical hardware is validated until the user recaptures it.

Run focused x64 tests for `HardwareIdentityTests` and `HydraTests` (or the exact equivalent existing targets). If practical, run the same focused tests on Win32/x86. Do not fight another worker for a locked shared build tree; report a verification blocker instead.

Finish with `chunk_claim.py done` or `blocked`, including exact tests and any control-tower integration note. No Git/remote actions.
