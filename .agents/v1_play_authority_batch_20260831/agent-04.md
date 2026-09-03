# Agent 04 — V1 Play-authority regression gate

You own `CHUNK-V1P-04-PLAY-AUTHORITY-QA`.

## Start
1. Work only in `C:\HydraSeat\repo`.
2. Read `AGENTS.md`, `.agents/AGENTS.md`, only the `CHUNK-V1P-04-PLAY-AUTHORITY-QA` section of `.agents/CHUNKS.md`, current `docs/qa/V1_HANDS_ON_QA.md`, the top/current-authoritative section of `docs/implementation/V1_COMPUTER_USE_ACCEPTANCE_20260831.md`, and only the relevant source/header snippets needed to make deterministic checks. Do not bulk-read history.
3. Run `python3 tools/chunk_claim.py list` immediately before claim.
4. Claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-V1P-04-PLAY-AUTHORITY-QA --owner v1p-04-play-authority-qa-20260831 --paths tools/validate_v1_play_authority.py tools/testdata/v1_play_authority docs/qa/V1_PLAY_AUTHORITY_QA.md --note "fail-closed first-use Play authority regression gate"`

## Real defect to encode
Actual Computer Use registered `hydra_window_test_app.exe` through the real Add Game picker and successfully saved Hardware Setup, but normal Play remained disabled because the production trusted requirement projection was empty.

Source/docs establish the reason:
- production reads `%LOCALAPPDATA%\HydraSeat\runtime-requirements.json`;
- the strict resolver/Host composition exists and must remain fail-closed;
- the release currently has no ordinary user/release-target writer that closes the first-use authority path;
- `PRODUCT_V1.md` promises manual/automatic local compatibility testing and a non-developer Play flow without JSON editing.

The previous automated readiness gate missed this. Your validator must make that impossible in future.

## Implement validator
Create `tools/validate_v1_play_authority.py` as a bounded source/contract validator. It is not a C++ parser and must avoid brittle full-AST assumptions. Use explicit manifests/patterns similar to existing repository validators where practical.

It must distinguish these independent requirements:
1. a release-target local compatibility evidence writer exists and is not test-only;
2. a release-target runtime requirement authority writer exists and uses the existing validated store contract;
3. normal production resolver remains `PhysicalOnly` unless an explicit future product decision changes the authoritative docs;
4. ControlledProcess/Synthetic/ImportedCommunity evidence cannot be promoted to Physical;
5. missing/corrupt/stale authority continues to block normal Play instead of defaulting permissively;
6. the local compatibility-test backend does not directly mutate normal Play authority or hard-code a controlled target;
7. user-facing Games integration is a distinct required integration point. Writer modules alone do **not** close the product P1;
8. Host still independently re-resolves trusted authority before install/activation;
9. Automated/Controlled/ComputerUse/Physical/RealGame/CleanMachine/Signing evidence classes remain distinct.

The first three agent modules may not exist yet while you work. The validator should therefore support a baseline fixture that demonstrates the current gap and fixtures for the intended correct shape without requiring you to modify product source.

## Fail fixtures
Under `tools/testdata/v1_play_authority/`, include compact deterministic fixture repositories/source snippets that prove failures for at least:
- resolver reader exists but no release writer;
- writer performs direct ad-hoc JSON instead of existing store validation;
- ControlledProcess rewritten/labeled as Physical;
- Synthetic or ImportedCommunity accepted as normal production authority;
- missing authority silently defaults to a permissive requirement;
- UI/model hard-codes `hydra_window_test_app.exe` or another test target;
- local test runner invokes shell/PowerShell/general command strings;
- Host lacks independent trusted re-resolution;
- writers exist but no declared central user-facing integration requirement;
- evidence-class matrix falsely marks Controlled/ComputerUse as Physical/RealGame.

Also include one fully passing fixture showing the intended architecture.

## QA ledger
`docs/qa/V1_PLAY_AUTHORITY_QA.md` must describe:
- the real Computer Use reproduction;
- why current 139/139 tests did not prove first-use Play;
- what Agent 01/02/03 modules can prove automatically;
- what central Games UI integration must still do;
- what Computer Use must later prove: select Game -> run local check/review -> actionable state change -> no JSON editing -> normal Play only when correct trust evidence exists;
- Controlled local test can prove process launch mechanics but cannot become Physical evidence;
- physical two-input, RealGame, CleanMachine and Signing remain separate gates.

Do not claim the P1 is fixed simply because new files exist.

## Tests
The validator should have a deterministic self-test mode or fixture runner and return non-zero on blockers. Run it against all fixtures. If current live repository is intentionally incomplete because other chunks/central integration are pending, it should report a clear BLOCKER rather than forcing a false PASS.

Do not edit `run_premerge_gate.py`; central integration will add this validator after review.

## Finish
Run fixture/self-test validation, then:

`python3 tools/chunk_claim.py done CHUNK-V1P-04-PLAY-AUTHORITY-QA --owner v1p-04-play-authority-qa-20260831 --summary "<exact result>" --verification "<fixture/self-test result>" --follow-up "central must integrate validator into premerge only after product wiring"`

If blocked, report the exact uncheckable contract. No product C++ edits, CMake/shared roadmap edits, commit/push/reset/clean/rebase, or fabricated evidence.
