# V1 Play Authority QA

Date: 2026-09-01  
Scope: `CHUNK-V1P-04-PLAY-AUTHORITY-QA` — fail-closed first-use Play authority regression contract.

## Why this gate exists

Actual Computer Use on 2026-08-31 used the real Add Game picker to register `hydra_window_test_app.exe`, completed the Hardware Setup save path, returned to Games, and still found normal Play disabled. That behavior was not a launcher-button bug. The production trusted requirement projection was empty because `%LOCALAPPDATA%\HydraSeat\runtime-requirements.json` had no ordinary release-target/user flow that could publish the exact trusted local requirement record.

The existing resolver and Host were correct to fail closed. Production resolves with `LocalEvidenceTrust::PhysicalOnly`, rejects missing/corrupt/stale/untrusted evidence, and the Host independently re-resolves trusted authority before provider-plan installation and again before Seat activation. This QA contract protects those security properties while requiring the missing first-use writer and Games integration path.

The earlier x64 and x86 **139/139 PASS** results did not prove first-use Play. Those tests proved the tested resolver, runtime, Host, launcher, and subsystem contracts; they did not prove that an ordinary user could execute the complete product flow `select Game -> run/review local compatibility evidence -> publish trusted requirement authority -> refresh readiness -> Play` without editing JSON.

## Automated contract

`tools/validate_v1_play_authority.py` treats the following as independent requirements. All must pass before its live repository result is green:

1. Local compatibility evidence has a non-test release-target writer and persists through the existing validated `CompatibilityLocalStore` contract.
2. Runtime requirement authority has a non-test release-target writer and publishes through `GameRuntimeRequirementStore`, never ad-hoc JSON/file I/O.
3. Normal production resolution remains `PhysicalOnly`.
4. `ControlledProcess`, `Synthetic`, and `ImportedCommunity` evidence cannot be relabeled or promoted into Physical production authority.
5. Missing, malformed, invalid, or stale authority continues to block trusted requirement projection rather than producing a permissive default.
6. The local compatibility runner is bounded to the exact `ProcessLaunchSpec`/owned-process path. It cannot write Play authority, invoke a general shell, or hard-code a controlled test executable.
7. Games is a distinct required integration point. Merely compiling writer/runner modules does not close the P1; product Games source must actually use the modules and refresh the trusted requirement projection.
8. The Host keeps an independently owned production trusted source and re-resolves before plan install and before Seat activation.
9. Automated, Controlled, ComputerUse, Physical, RealGame, CleanMachine, and Signing evidence classes remain separate.

The fixture suite includes one intended passing architecture plus deterministic failures for the original no-writer gap, direct JSON publication, origin promotion, permissive missing authority, hard-coded test targets, shell execution, missing Host re-resolution, missing Games integration, and false evidence-class promotion.

## What the closure modules can prove automatically

The local evidence writer can prove that a completed local session report is converted through the canonical compatibility-result path and persisted using the existing bounded local history store. It must preserve the measured origin; it cannot invent Physical evidence.

The requirement-authority writer can prove that a reviewed current Game/provider/evidence/report tuple is validated, rebound to an exact `LocalRequirementEvidenceRecord`, and saved transactionally through `GameRuntimeRequirementStore`. It can reject ineligible evidence and derive capabilities conservatively. The writer existing by itself does not make Games usable.

The local compatibility runner can prove bounded exact-process launch/ownership/window-observation/cleanup mechanics for a user-selected `ProcessLaunchSpec`, including timeout and cancellation. Its controlled local process result is technical evidence only. It is intentionally not allowed to mutate the normal Play authority store.

## Central Games integration result — 2026-09-03

The control tower has now connected all three module boundaries to the ordinary Games journey without weakening the resolver or Host. Before any local process is launched, the user must explicitly review the selected title's protection risk: Protected / Experimental and Unknown both fail closed, while Standard is accepted only from that explicit review and is no longer hard-coded by the launcher. The selected Game can then start the bounded exact-executable compatibility check, persist its measured Controlled result through the canonical local store, present the reviewed requirement summary, attempt publication through `GameRuntimeRequirementStore`, and refresh the production trusted projection. When the evidence is below production trust, Play remains disabled and the UI reports that Physical validation is still required.

The integrated path does not special-case `hydra_window_test_app.exe`, invent a permissive default requirement, accept community/imported evidence as local runtime authority, or write `runtime-requirements.json` directly. Normal production resolution remains `LocalEvidenceTrust::PhysicalOnly`, and the Host still re-resolves independently before provider-plan installation and Seat activation.

The same integration pass also completed the previously missing production execution composition around the Host: the production activation bridge is linked into the Host, recovery owns the watchdog dependency, and the exact-process Gate C receiver is built as the architecture-matched `hydra_production_gate_c_bridge.dll`. The receiver has its own production source/owner and shares only the bounded mapping protocol with the Host-side bridge. Missing exact game input profile/approval still fails closed; the control tower did not invent a profile for an unvalidated game.

A further 2026-09-03 control-tower pass added the production Physical-input prerequisite source without manufacturing a Physical verdict. Games may select an already completed `phase3-hardware-manifest.json` through the product UI; the product persists only its bounded canonical path. Save and every fresh Host load re-run the typed P3-HW evidence loader, so Pending, stale, tampered, missing, or malformed evidence cannot yield `ProductionInputEvidenceClass::Physical`. Exact Gate C game profiles remain release-owned only, and the current profile table is intentionally empty while P3-E-02 is blocked. Selecting valid P3-HW evidence therefore does **not** enable Play by itself; it only closes the no-JSON prerequisite handoff from completed hardware acceptance into Host composition.

Current automated evidence after integration:

- `python3 tools/validate_v1_play_authority.py`: **10/10 PASS**;
- validator fixture self-test: **17/17 PASS**;
- `python3 tools/validate_production_reachability.py`: **9 components PASS, 0 inactive**; reachability self-test: **12/12 PASS**;
- x64 Release full build: **PASS**;
- x64 full CTest: **143/143 PASS**;
- Win32/x86 Release full build: **PASS**;
- Win32/x86 full CTest: **143/143 PASS**;
- `tools/run_premerge_gate.py`: **7/7 automated validators PASS**.

`tools/run_premerge_gate.py` now includes `v1-play-authority` as a normal automated gate. This is still Automated/Controlled evidence only; it does not promote ComputerUse, Physical, RealGame, CleanMachine, or Signing evidence.

## Computer Use acceptance after integration

A later Computer Use run must use the rebuilt release candidate and demonstrate the real normal flow, not fixture/source inspection:

1. Select a real registered Game in Games.
2. Start the product's local compatibility check/review action for that selected Game.
3. Observe an understandable waiting/result/review state and an actionable state change after completion.
4. Complete any required requirement review/publication through product UI only; do not edit or inject JSON.
5. Verify readiness refreshes from the trusted projection.
6. Verify normal Play becomes enabled only when the exact trust evidence required by production exists.
7. Corrupt/remove/stale the relevant authority only through a controlled test setup and confirm normal Play returns to a blocked/actionable state rather than defaulting permissively.
8. Confirm no controlled test executable name is baked into the user flow.

Computer Use can prove that the UI flow is reachable and understandable. It does not by itself prove two distinct physical input devices, Physical compatibility evidence, a lawful real game, clean-machine installation, or production signing.

## Evidence classes

<!-- V1_PLAY_AUTHORITY_EVIDENCE_CLASSES
Automated=Automated
Controlled=Controlled
ComputerUse=ComputerUse
Physical=Physical
RealGame=RealGame
CleanMachine=CleanMachine
Signing=Signing
V1_PLAY_AUTHORITY_EVIDENCE_CLASSES -->

| Evidence class | What this work may claim | What it cannot replace |
| --- | --- | --- |
| Automated | Validator/self-test and deterministic source-contract results | Any manual/runtime evidence |
| Controlled | Bounded owned-process mechanics from a controlled local check | Physical evidence or RealGame |
| ComputerUse | Actual product UI flow and visible state transitions | Physical, RealGame, CleanMachine, Signing |
| Physical | Human-performed physical compatibility/input evidence | RealGame, CleanMachine, Signing |
| RealGame | Lawful actual game launch/session behavior | CleanMachine or Signing |
| CleanMachine | Supported clean Windows install/repair/reboot/uninstall exercise | Signing |
| Signing | Production-signing environment and signature verification | Other evidence classes |

A Controlled local check may validate process launch, authoritative-window observation, cleanup, and result plumbing. Its `ControlledProcess` evidence must remain `ControlledProcess`; it cannot be renamed, rewritten, or counted as Physical authority for normal production Play.

## Release decision rule

A green validator is necessary Automated evidence, not release acceptance. The original no-writer/no-Games-wiring product gap is now closed in code and protected by premerge, but the release remains blocked until the post-integration Computer Use journey is re-run and the required Physical authority is obtained through real hardware evidence rather than Controlled evidence or JSON editing. P3-HW physical input acceptance and the first real non-protected game profile are still pending, and RealGame, CleanMachine, and Signing remain independent release gates regardless of this validator result.
