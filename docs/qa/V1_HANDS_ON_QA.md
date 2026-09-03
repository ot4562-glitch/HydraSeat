# V1 Hands-On QA

Date: 2026-08-31  
Scope: `CHUNK-V1H-05-UX-ACCEPTANCE` — non-developer V1 journey gate.

## Current automated facts

`python3 tools/validate_v1_hands_on_readiness.py --self-test` passes the deterministic fixture suite. The suite proves every reviewed regression signal is independently rejected and that `ComputerUse`, `Physical`, `RealGame`, `CleanMachine`, and `Signing` cannot be promoted from mock/source evidence.

The live source check currently reports **9/12 PASS**. V1H-01's durable user-state boundary has landed, the current V1H-02 source/release contract now satisfies the reviewed `HydraSeatSetup.exe` bootstrapper signal, and V1H-03 plus central CMake integration remain unfinished. V1H-04 also remains claimed; its current source passes the static display/identification/lifecycle signals, but that is not ComputerUse or Physical evidence. Current FAIL/PENDING integration is limited to `V1UserJourneyTests` CMake registration, an explicit Player 1-required UI guard, and launcher wiring to `launcher_user_state` instead of the ad-hoc persistence path.

The new `tests/test_v1_user_journey.cpp` covers only `Automated/Mock`: empty state, Player 1 creation/selection, optional Player 2, playable-game readiness, actionable blocking evidence, durable last-Player projection, stale ID fail-closed behavior, and evidence-class separation. It deliberately does not claim a real window, physical device, game, installer mutation, or signature result.

## Required central CMake integration

After V1H-01 is stable, the control tower should add one `hydra_launcher_user_state` static library from `src/launcher_user_state.cpp`, with public include path `include`, strict warnings, and `hydra_profile_schema` as its public dependency. Then add `v1_user_journey_tests` from `tests/test_v1_user_journey.cpp`, link `hydra_launcher_ui_model` and `hydra_launcher_user_state`, enable strict warnings, and register `add_test(NAME V1UserJourneyTests COMMAND v1_user_journey_tests)`.

No ad-hoc compile/link graph was used in this worker because CMake is outside the claimed envelope.

## Computer Use scenarios after integration

These remain **PENDING** until OpenAI Codex actually manipulates the built Windows application and records the result.

1. **First-run Games flow:** launch HydraSeat; create Player 1; verify Player 2 is visibly optional; select a real playable library entry; confirm Steam runtime/tool entries are absent; verify the only primary completion action is Play and any disabled state has an understandable reason.
2. **Display/Input round trip:** open Display/Input Setup; verify two active displays are distinguishable by friendly name, DISPLAY identity, resolution and primary state; intentionally identify one keyboard and one mouse by press/click; assign them to Display 1/2; choose Back to Games; confirm the hardware window is not left visible/reachable underneath.
3. **Restart persistence:** exit HydraSeat, relaunch it, and verify the last valid Player 1 selection is restored; optional Player 2 is restored only when still valid; stale/deleted Player IDs are not invented or silently rebound.
4. **Bootstrapper presentation:** use `HydraSeatSetup.exe` from the reviewed release-package layout (`<signed-package-root>\\x64\\HydraSeatSetup.exe`), never the raw CMake `Release\\HydraSeatSetup.exe` build output. Verify Install / Repair / Uninstall are understandable without developer tools. An unsigned staging package may be used only to verify the explicit signing-blocked presentation; it must not enable mutation. Actual signed install/UAC/reboot/uninstall acceptance remains `CleanMachine` + `Signing` evidence.

## Evidence classes

| Class | Current state | What is still required |
| --- | --- | --- |
| Automated/Mock | FAIL until live 12/12 + CMake test integration | Complete V1H-03 Player-role/persistence wiring, register/link `V1UserJourneyTests` with the landed V1H-01 state boundary, rerun the Python gate |
| ComputerUse | PENDING | Execute the four Windows UI scenarios above with the integrated build |
| Physical | PENDING | Real two-display + keyboard/mouse assignment/identification and no-cross-device validation |
| RealGame | PENDING | Lawful actual game launch/session and stop/change behavior |
| CleanMachine | PENDING | Supported clean Windows install/repair/UAC/reboot/uninstall exercise |
| Signing | PENDING | Protected production-signing environment and signature verification |

A green mock/source gate is necessary but is not V1 hands-on acceptance by itself.
