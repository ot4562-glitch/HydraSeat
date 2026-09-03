# Agent 08 — Game catalog filtering

You own `CHUNK-UXH-08-GAME-CATALOG`.

## Start
Read `AGENTS.md`, `.agents/AGENTS.md`, your chunk section, then only the six claimed files and the existing provider/catalog contracts they directly include. Do not inspect reference repositories or browse the web.

Run `python3 tools/chunk_claim.py list`, then claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-UXH-08-GAME-CATALOG --owner uxh-08-game-catalog-20260830 --paths include/hydra/steam_provider.hpp src/steam_provider.cpp include/hydra/game_catalog.hpp src/game_catalog.cpp tests/test_steam_provider.cpp tests/test_game_catalog.cpp --note "filter Steam runtime tool non-game entries"`

## Screenshot-backed problem
The normal game library displayed `Steamworks Common Redistributables` as a playable game. Fix the class of bug, not just that title.

## Outcome
Use bounded local Steam/provider metadata already present in manifests/catalog inputs to classify clearly non-playable runtimes, tools, redistributables, dedicated servers or support packages out of the normal game list. Prefer explicit provider metadata/type semantics over title-name blacklists. Be conservative: unknown legitimate titles stay visible. Manual Add EXE remains available and unaffected. Parsing stays read-only, bounded and deterministic.

You may refactor provider classification inside the envelope if it reduces duplicated filtering. Do not add network scraping or a remote database.

## Acceptance
- representative runtime/tool/server manifests are filtered;
- ordinary game manifests remain;
- unknown type/metadata fails conservative-visible rather than overfiltering;
- duplicate AppID/path rules remain unchanged;
- manual custom executables remain unaffected;
- no compatibility/support claim is manufactured.

Run `SteamProviderTests` and `GameCatalogTests` on x64 and, if practical, x86. Finish DONE/BLOCKED with exact tests and any UI integration note. No Git/remote actions.
