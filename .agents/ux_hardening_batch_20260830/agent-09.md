# Agent 09 — Screenshot-backed UX regression gate

You own `CHUNK-UXH-09-UX-REGRESSION-GATE`.

## Start
Read `AGENTS.md`, `.agents/AGENTS.md`, your chunk section, `README.md`, and only the claimed QA/tool files plus the specific source/test paths the validator already inspects read-only. No whole-repo scan.

Run `python3 tools/chunk_claim.py list`, then claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-UXH-09-UX-REGRESSION-GATE --owner uxh-09-ux-gate-20260830 --paths docs/qa/LAUNCHER_RELEASE_QA.md tools/validate_launcher_release_readiness.py tools/testdata/launcher_release_readiness --note "screenshot-backed UX regression contract"`

## Problem to solve
The project previously reached green automated test counts while the actual UI still had obvious usability defects. Make that harder to repeat without pretending software can objectively score aesthetics.

## Outcome
Extend the existing launcher/readiness validator and QA ledger with objective contracts derived from the real screenshots and README/PRODUCT_V1: EN/KO/ZH coverage; supported DPI/narrow geometry policy; critical actions cannot rely on clipping/ellipsis; normal game-first action order; hardware setup must have a non-drag assignment path once product code exposes it; Seat UI remains minimal rather than a general shell/telemetry form; compatibility/diagnostic detail stays outside the ordinary happy path; real first-window responsiveness remains checked. Human screenshot approval, visual taste, physical hardware and real-game evidence stay explicitly manual/PENDING.

Prefer source-contract and existing-test evidence; do not parse generated build projects as authority. Add deterministic positive/negative fixtures for every new automated rule.

## Acceptance
- validator fails on representative regressions and passes valid fixtures;
- subjective color/style taste is not encoded as brittle static rules;
- manual visual/physical gates remain PENDING;
- no product source is edited.

Run the validator's self-test/fixture suite and the existing premerge invocation path if safe. Finish DONE/BLOCKED with exact commands. No Git/remote actions.
