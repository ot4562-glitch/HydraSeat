# Agent 05 — Launcher information architecture

You own `CHUNK-UXH-05-LAUNCHER-IA`.

## Start
Read `AGENTS.md`, `.agents/AGENTS.md`, your chunk section, then only the four claimed files plus `ui_localization.hpp` read-only. No broad scans.

Run `python3 tools/chunk_claim.py list`, then claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-UXH-05-LAUNCHER-IA --owner uxh-05-launcher-ia-20260830 --paths include/hydra/launcher_ui_model.hpp src/launcher_ui_model.cpp src/launcher_win32.cpp tests/test_launcher_ui_model.cpp --note "game-first launcher IA and jargon cleanup"`

## Screenshot-backed problem
The main launcher is salvageable, but the advanced page is a settings junk drawer: Player CRUD, privacy, retention, compatibility result management and status logs compete visually. Ordinary game rows expose implementation language such as compatibility evidence.

## Outcome
Keep the normal flow visually obvious: game -> Seat 1/Seat 2/Both -> Player(s) -> only actionable blocking warning -> Play. Demote or group privacy/result-management/diagnostic details so they do not compete with that flow. Replace ordinary implementation jargon with short user-action wording while preserving exact detail in advanced/diagnostic contexts. Reduce duplicated status/control density rather than adding another screen framework.

Do not change launch/preflight/readiness authority. Do not add untranslated hard-coded strings. If a needed TextId does not exist, use a temporary existing accurate string only if semantically correct; otherwise return an exact Agent 06/control-tower localization note.

## Acceptance
- normal game-first path has one obvious primary action;
- advanced settings remain reachable but are not the happy path;
- compatibility internals are not required vocabulary for normal users;
- destructive result actions remain clearly separated and accessible;
- keyboard/focus/disabled reason behavior remains correct.

Run `LauncherUiModelTests`, build `HydraSeat`, and run the focused launcher-related CTest set available in the existing x64 tree. x86 if practical. Manual screenshot approval remains pending. Finish DONE/BLOCKED with tests and localization/layout integration notes. No Git/remote actions.
