# Agent 06 — Shipped localization catalog

You own `CHUNK-UXH-06-LOCALIZATION`.

## Start
Read `AGENTS.md`, `.agents/AGENTS.md`, `docs/LOCALIZATION.md`, your chunk section, and only the three claimed files. You may search the normal launcher/Seat/setup sources read-only for current TextId usage, but do not broad-scan unrelated runtime code.

Run `python3 tools/chunk_claim.py list`, then claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-UXH-06-LOCALIZATION --owner uxh-06-localization-20260830 --paths include/hydra/ui_localization.hpp src/ui_localization.cpp tests/test_ui_localization.cpp --note "EN KO ZH production UI catalog cleanup"`

## Problem to solve
Real screenshots show EN/KO/ZH are present but some wording is implementation-oriented or too long for critical actions. Hardware/setup surfaces also contain hard-coded English elsewhere; you do not own those consumers, but you can provide the correct stable message IDs/catalog entries for integration.

## Outcome
Ensure every shipping normal-UI TextId has en-US fallback plus ko-KR and zh-CN, placeholders match, and newly necessary screenshot-backed UX phrases have stable IDs. Prefer concise natural button/status wording over literal technical translations. Machine-readable identifiers, protocol/backend names, CLI switches and developer diagnostics remain English.

Do not invent marketing claims or hide a technical failure by mistranslating it. Do not encode one pixel width into translations; tests should catch missing catalogs, placeholder mismatch and clearly unreasonable critical-action strings at the semantic level.

## Acceptance
- complete EN/KO/ZH catalog for shipping normal UI IDs;
- placeholder parity and deterministic fallback;
- concise action/status translations;
- no localization of protocol/CLI/diagnostic codes;
- exact integration notes for hard-coded consumer strings outside the envelope.

Run `UiLocalizationTests` on x64 and, if practical, x86. Finish DONE/BLOCKED with tests and the list of consumer strings that control tower/other chunks must wire. No Git/remote actions.
