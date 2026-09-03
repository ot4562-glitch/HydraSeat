# Agent 04 — Launcher layout / clipping

You own `CHUNK-UXH-04-LAUNCHER-LAYOUT`.

## Start
Read `AGENTS.md`, `.agents/AGENTS.md`, your chunk section, then only your three claimed files plus localization declarations as read-only context. Do not reread the repository.

Run `python3 tools/chunk_claim.py list`, then claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-UXH-04-LAUNCHER-LAYOUT --owner uxh-04-launcher-layout-20260830 --paths include/hydra/launcher_layout.hpp src/launcher_layout.cpp tests/test_ui_accessibility.cpp --note "locale DPI clipping-proof launcher geometry"`

## Problem to solve
Real EN/KO/ZH screenshots show fixed-geometry artifacts, including Korean action text wrapping/clipping awkwardly. Existing tests already cover several DPI widths; make the layout policy itself derive critical geometry from measured/wrapped content and stable minimum metrics instead of English-sized rectangles.

You may improve the layout data model inside this envelope, but do not move UI state/readiness authority into layout code and do not create a generic web-style layout framework.

## Acceptance
Cover 96/120/144/192 DPI and the supported normal/narrow widths. Critical Seat actions, advanced action buttons, selected-game title/status, library rows and bottom launch reason must receive enough measurable/wrappable space. No solution based on unreadable font shrinking, forced ellipsis for blocking text, or absurdly increasing the minimum window size. Geometry must remain deterministic and one-source-of-truth.

Run focused `UIAccessibilityTests` on x64 and, if practical, x86. Report any `launcher_win32.cpp` consumer change as a control-tower/Agent 05 integration note rather than crossing your envelope. Finish DONE/BLOCKED with tests. No Git/remote actions.
