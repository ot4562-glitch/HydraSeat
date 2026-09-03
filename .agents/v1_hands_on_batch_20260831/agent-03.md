# Agent 03 — Games screen V1 hands-on UX

You own `CHUNK-V1H-03-GAMES-UX`.

## Start
1. Work only in `C:\HydraSeat\repo`.
2. Read `AGENTS.md`, `.agents/AGENTS.md`, and only the `CHUNK-V1H-03-GAMES-UX` section of `.agents/CHUNKS.md` plus directly relevant launcher files. Do not load all product/roadmap history.
3. Run `python3 tools/chunk_claim.py list`.
4. Claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-V1H-03-GAMES-UX --owner v1h-03-games-ux-20260831 --paths include/hydra/launcher_layout.hpp src/launcher_layout.cpp src/launcher_win32.cpp include/hydra/launcher_win32.hpp include/hydra/launcher_ui_model.hpp src/launcher_ui_model.cpp tests/test_launcher_ui_model.cpp tests/test_ui_accessibility.cpp --note "V1 game-first launcher hands-on UX"`

## User-facing target
A non-developer should open HydraSeat and understand the normal journey without reading documentation:
1. create or select Player 1;
2. optionally select Player 2;
3. choose a game;
4. open Display/Input Setup only when needed;
5. see why Play is unavailable, or press Play.

The current launcher has improved substantially but still contains historical hidden/advanced Seat-use, Settings/Diagnostics and duplicated state. Remove genuine UI slop rather than adding more wrappers.

## Reference research
Use targeted filename/symbol searches only in:
- `C:\HydraSeat\references\splitscreenme-nucleus-master` for interaction/flow ideas only. It is GPL: no code, algorithms, resources or derived implementation copying into HydraSeat.
- `C:\HydraSeat\references\WinUI-Gallery-main` and `C:\HydraSeat\references\WindowsAppSDK-Samples-main` for MIT/official control/layout/accessibility patterns.

Do not migrate the product to WinUI in this chunk. The current deliverable remains native Win32.

## Inspect and improve
Focus on actual reachable UI, not hidden implementation trivia:
- first-launch empty Player state;
- quick Player creation feedback;
- Player 1 visibly required;
- Player 2 clearly optional and default None;
- game selection and selected state obvious;
- Display/Input Setup button wording and placement obvious;
- Play readiness/error copy concise and actionable;
- no ordinary-path `Use Seat 1`, `Use Seat 2`, `Use Both`, Settings/Diagnostics, compatibility evidence, Runtime Host or management-Seat terminology;
- no stale hidden controls left participating in layout, keyboard tab order or state synchronization;
- Korean/Chinese clipping fix must remain intact;
- disabled Play has a visible user-action reason;
- repeated refresh/game/player changes do not reset unrelated choices.

Delete obsolete hidden control plumbing where safe instead of merely `ShowWindow(..., SW_HIDE)` forever. Preserve runtime/Host readiness authority and fail-closed launch semantics.

## Player persistence integration
Agent 01 owns the new storage module. If `include/hydra/launcher_user_state.hpp` and implementation already exist and are clearly DONE, consume that module and remove duplicated ad-hoc profile/selection persistence from `launcher_win32.cpp`. If Agent 01 is still working or blocked, do not clone its storage implementation; finish launcher UX and leave one exact integration note.

The user's current confirmed defect is: Player profiles are present in `players.json`, but the previous Player 1 selection is not restored after process restart. Do not report this fixed unless the actual launcher path is wired to durable state.

## Verification
Run focused x64 tests for `LauncherUiModelTests` and `UiAccessibilityTests`. If possible build `HydraSeat` x64 Release and use the existing real first-window/startup probe without Computer Use. Do not burn time on all 135 tests; control tower owns broad regression.

## Finish
Use:

`python3 tools/chunk_claim.py done CHUNK-V1H-03-GAMES-UX --owner v1h-03-games-ux-20260831 --note "<UX changes + focused tests + persistence integration status>"`

If blocked, use `blocked`. No Git/remote actions, CMake/shared-doc edits, or edits outside claimed paths.
