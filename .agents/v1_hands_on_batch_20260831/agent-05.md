# Agent 05 — V1 user-journey mock and hands-on readiness gate

You own `CHUNK-V1H-05-UX-ACCEPTANCE`.

## Start
1. Work only in `C:\HydraSeat\repo`.
2. Read `AGENTS.md`, `.agents/AGENTS.md`, the `CHUNK-V1H-05-UX-ACCEPTANCE` section of `.agents/CHUNKS.md`, and only directly relevant launcher/hardware/installer source as read-only evidence. Do not load the whole roadmap/history.
3. Run `python3 tools/chunk_claim.py list`.
4. Claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-V1H-05-UX-ACCEPTANCE --owner v1h-05-ux-acceptance-20260831 --paths tests/test_v1_user_journey.cpp tools/validate_v1_hands_on_readiness.py tools/testdata/v1_hands_on_readiness docs/qa/V1_HANDS_ON_QA.md --note "mock-first V1 user journey and hands-on readiness gate"`

## Mission
Existing `135/135` green tests are not sufficient evidence that a normal person can use HydraSeat. Build a new narrow gate around the actual journey and known regressions. Do not change product source to make your test pass.

## Evidence classes
Keep these separate in both code and QA documentation:
- `Automated/Mock`: deterministic model/source/process checks you can run now;
- `ComputerUse`: real OpenAI Codex manipulation of the Windows app; only PASS when actual Computer Use evidence exists;
- `Physical`: real keyboard/mouse/display hardware;
- `RealGame`: lawful actual game launch/session;
- `CleanMachine`: installer/UAC/reboot/uninstall on a clean supported Windows machine;
- `Signing`: protected production-signing environment.

Never infer one class from another.

## User journey contract
A future V1 acceptance should be able to execute:
1. launch HydraSeat;
2. create/select Player 1;
3. optionally select Player 2;
4. select an actual playable game, with Steam runtimes/tools absent;
5. enter Display/Input Setup;
6. distinguish two active monitors by human-readable metadata;
7. intentionally identify keyboard and mouse;
8. assign devices to Display 1/2;
9. return to Games without a leftover hardware window;
10. understand Play readiness;
11. exit and restart and recover last Player selection;
12. install/repair/uninstall through a user-facing installer path without developer tools.

## Known defects/regressions to encode
At minimum the validator/fixtures should fail when:
- launcher exposes normal-path `Use Seat 1`, `Use Seat 2`, `Use Both`, Settings/Diagnostics or Runtime Host jargon;
- Player 1 is not required/Player 2 is not clearly optional;
- launcher persistence code exists only as an ad-hoc transient combo state with no durable storage authority/wiring;
- critical localized labels can be clipped by fixed too-small line heights/widths;
- Hardware Setup falls back to indistinguishable `Generic PnP Monitor` for two active displays despite available friendly topology metadata;
- intentional keyboard/mouse identification controls/semantics disappear;
- Steam appid 228980/runtime/tool/server filtering regresses;
- Back to Games leaves hardware UI reachable/visible according to the source/process contract;
- no real double-click installer/bootstrapper artifact is present in the reviewed release contract once Agent 02 lands.

Avoid brittle checks that merely grep one exact implementation line if a semantic/model test can express the requirement.

## `test_v1_user_journey.cpp`
Use existing public/testable models only. Build a deterministic mock journey where practical:
- empty profile -> create/select Player 1;
- optional Player 2 None vs selected;
- game selection/readiness;
- incomplete state blocks Play with actionable reason;
- two-display presentation identity requirements if an existing projection supports it;
- stale selection IDs fail closed;
- no mock result may claim real window/physical/installer success.

Do not edit CMake. Return the exact target/library links required for control tower integration.

## QA ledger
`docs/qa/V1_HANDS_ON_QA.md` should be short and operational: current automated facts, exact Computer Use scenarios to run after integration, and remaining physical/real-game/clean-machine/signing gates. Do not repeat the entire roadmap.

## Verification
Run your Python validator self-tests/fixtures and any build-independent checks possible. If the C++ test needs CMake integration, report it accurately rather than compiling through an unreviewed ad-hoc graph.

## Finish
Use:

`python3 tools/chunk_claim.py done CHUNK-V1H-05-UX-ACCEPTANCE --owner v1h-05-ux-acceptance-20260831 --note "<mock coverage + validator results + required central CMake integration>"`

Use `blocked` for a real blocker. No Git/remote/product/CMake/shared status/README edits and no changes outside claimed paths.
