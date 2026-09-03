# V1 Play-authority closure dispatch — 2026-08-31

## Why this batch exists
Actual GPT-5.6 Sol Computer Use registered a controlled executable through the real Add Game picker, saved Hardware Setup successfully after fixes, and then found normal Play permanently disabled because the production trusted runtime-requirement store has no ordinary release-target creation flow. Full x64/x86 automated suites still pass 139/139, so this is precisely the class of product P1 that mock/unit green status did not detect.

Production Play trust stays fail-closed and `PhysicalOnly`. This batch must not weaken that policy.

## Parallel workers
- Agent 01 / `CHUNK-V1P-01-LOCAL-EVIDENCE-WRITER`: canonical SessionMetrics -> CompatibilityResult -> local compatibility history release writer.
- Agent 02 / `CHUNK-V1P-02-REQUIREMENT-AUTHORITY-WRITER`: exact reviewed requirement record + derived capability authority -> transactional runtime-requirements store.
- Agent 03 / `CHUNK-V1P-03-LOCAL-CHECK-RUNNER`: bounded user-initiated native executable local check producing truthful ControlledProcess SessionMetrics; no normal Play bypass.
- Agent 04 / `CHUNK-V1P-04-PLAY-AUTHORITY-QA`: deterministic first-use Play-authority regression gate and QA ledger.

Agents 01-04 may run in parallel because their write envelopes do not overlap. CMake, Games UI integration, shared STATUS/README/product docs, acceptance report cleanup, broad regression, and final evidence classification are control-tower work.

## Central integration after workers finish
1. Review all new APIs for evidence-class escalation, path safety, exact identity and transactional semantics.
2. Add new libraries/tests to CMake without source duplication.
3. Connect selected Game -> explicit local compatibility check/review UX in the normal Games surface.
4. Never enable normal Play from ControlledProcess alone when production resolver requires Physical evidence. Instead show actionable state explaining what evidence remains.
5. Provide a real supported path for a user with appropriate physical hardware to complete required local validation without JSON editing.
6. Add `validate_v1_play_authority.py` to premerge only after the actual user-facing wiring exists.
7. Re-run x64/x86 full tests, readiness/premerge/diff-check.
8. When Codex/Work quota is available again, GPT-5.6 Sol High Computer Use re-tests the exact user journey and completes the 21-item design score.

No worker commits/pushes/resets/cleans. Physical/RealGame/CleanMachine/Signing evidence is never synthesized.
