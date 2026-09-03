# Agent 01 — Durable launcher user state

You own `CHUNK-V1H-01-PLAYER-STATE`.

## Start
1. Work only in `C:\HydraSeat\repo`.
2. Read only `AGENTS.md`, `.agents/AGENTS.md`, and the `CHUNK-V1H-01-PLAYER-STATE` section of `.agents/CHUNKS.md` plus directly relevant profile-schema code. Do not load roadmap/history documents wholesale.
3. Run `python3 tools/chunk_claim.py list`.
4. Claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-V1H-01-PLAYER-STATE --owner v1h-01-player-state-20260831 --paths include/hydra/launcher_user_state.hpp src/launcher_user_state.cpp tests/test_launcher_user_state.cpp --note "durable Player profile and last-selection persistence boundary"`

## Real defect
The user created Players successfully, but after closing and reopening HydraSeat the previous Player 1 selection was not restored. `%LOCALAPPDATA%\HydraSeat\players.json` exists and contains persisted profiles, so the visible failure is selection-state persistence/wiring, not loss of the profiles themselves.

The control tower has temporary persistence code inside `src/launcher_win32.cpp`. Do not edit that file. Build the reusable storage boundary that will replace the ad-hoc GUI-local persistence during integration.

## Implement
Create a small UI-independent module that owns launcher user-state storage semantics:
- transactional load/save for `profile::PlayerProfileDocument`;
- transactional load/save for last Player 1 plus optional Player 2 IDs;
- injectable root path so tests never touch the user's real `%LOCALAPPDATA%`;
- production helper may resolve `%LOCALAPPDATA%\HydraSeat`, but storage operations themselves must accept an explicit root/path;
- bounded files and IDs;
- staged write + flush + atomic replace on Windows;
- malformed, empty-when-invalid, oversize, embedded NUL/newline and stale IDs fail closed;
- stale selection IDs must never invent a profile; provide a deterministic validation/filter operation against a supplied PlayerProfileDocument;
- no database, registry, global singleton or HWND dependency.

If extracting the existing `players.json` format would duplicate schema logic, call existing `encodePlayerProfileDocument` / `decodePlayerProfileDocument` and existing validators instead.

## Tests
`tests/test_launcher_user_state.cpp` must use disposable temp roots and cover at least:
- missing files => valid empty/default state;
- profile round-trip;
- selection round-trip with Player 1 only;
- Player 1 + Player 2 round-trip;
- stale Player ID ignored/fails closed according to the API contract;
- malformed/oversize selection payload;
- malformed player document;
- atomic replacement preserves the previous good file when staging/write/replace precondition fails where deterministic simulation is possible;
- repeated save/load behaves deterministically.

Do not edit CMake. If the new test target cannot be built without control-tower CMake integration, compile/test any existing reusable pieces you can and state the exact CMake integration required.

## Finish
Run focused verification available within the claimed envelope, then:

`python3 tools/chunk_claim.py done CHUNK-V1H-01-PLAYER-STATE --owner v1h-01-player-state-20260831 --note "<exact result + tests + control-tower wiring note>"`

If blocked, use `blocked` instead and explain the exact blocker. No commit/push/reset/clean/rebase and no edits outside claimed paths.
