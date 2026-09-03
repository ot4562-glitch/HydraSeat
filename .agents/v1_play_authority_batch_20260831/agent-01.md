# Agent 01 — Local compatibility evidence release writer

You own `CHUNK-V1P-01-LOCAL-EVIDENCE-WRITER`.

## Start
1. Work only in `C:\HydraSeat\repo`.
2. Read `AGENTS.md`, `.agents/AGENTS.md`, only the `CHUNK-V1P-01-LOCAL-EVIDENCE-WRITER` section of `.agents/CHUNKS.md`, then the directly relevant public APIs in `compatibility_result`, `compatibility_share_model`, `compatibility_local_store`, and `session_metrics`. Do not load roadmap/history wholesale.
3. Run `python3 tools/chunk_claim.py list` immediately before claim.
4. Claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-V1P-01-LOCAL-EVIDENCE-WRITER --owner v1p-01-local-evidence-20260831 --paths include/hydra/local_compatibility_evidence.hpp src/local_compatibility_evidence.cpp tests/test_local_compatibility_evidence.cpp --note "release-target local compatibility evidence persistence"`

## Real product gap
Computer Use reached the normal Add Game + Hardware Setup flow and registered a controlled executable. Normal Play then remained disabled because trusted runtime requirement authority is absent. Repository audit also establishes that the release has readers for local compatibility/requirement evidence but no ordinary release-target writer for the requirement authority path.

Your narrow part is the first missing writer: turn already-measured session evidence into the existing canonical local compatibility history without inventing truth.

## Implement
Create a small UI-independent module whose responsibilities are only:
- accept an exact `compat::LocalEvidenceContext` and a completed `metrics::SessionMetricsReport`;
- build the `compat::CompatibilityResult` only through `buildCompatibilityResultFromSessionMetrics`;
- never let a caller override the result origin or derived launch/input/controller/audio/exit/rollback statuses after conversion;
- reject Synthetic as a release-produced local result; ControlledProcess and Physical are valid distinct evidence classes and must remain distinct;
- load the existing `CompatibilityShareModel` history through `CompatibilityLocalStore`;
- record through `CompatibilityShareModel::recordLocalResult` so supersession/retention rules remain authoritative;
- save through `CompatibilityLocalStore` so existing staged/atomic persistence remains authoritative;
- preserve an existing good history if conversion, record, or save fails;
- support an explicit injectable store path in tests;
- optionally expose a production convenience helper that resolves the existing fixed LocalAppData compatibility-history path. Do not introduce another state root.

Prefer a typed result such as diagnostic code + exact produced result on success. Keep the API bounded; no HWND, launcher, Host, registry, arbitrary filesystem or network dependency.

## Security/evidence invariants
- `metrics::EvidenceOrigin::ControlledProcess` => `compat::ResultOrigin::ControlledProcess`, never Physical.
- `metrics::EvidenceOrigin::Physical` may remain Physical only if the existing metrics/result validation accepts it; do not synthesize `physicalValidationEligible` or input samples.
- Synthetic must not become a release-produced trusted local result.
- No ImportedCommunity result is created by this writer.
- No personal paths, Player names, typed text, credentials, tokens or device serials may be added to the compatibility payload.
- Do not modify the existing schema to make a test easier.

## Tests
Use disposable temp roots only. Cover at least:
- ControlledProcess metrics -> canonical ControlledProcess result -> persisted history;
- genuine Physical metrics stays Physical without field rewriting;
- Synthetic is rejected and previous history is unchanged;
- invalid LocalEvidenceContext leaves disk/model unchanged;
- invalid/empty SessionMetricsReport leaves disk/model unchanged;
- existing history is preserved when adding a new result;
- superseding a prior result follows existing `CompatibilityShareModel` semantics;
- retention bound is honored by the existing model rather than reimplemented;
- corrupt existing store fails closed and is not overwritten;
- repeated identical/duplicate result behavior follows the existing model deterministically;
- no caller-supplied path escapes the explicit test root through this module's own path construction.

## Integration note
Do not edit CMake. If the new module/test is not buildable until central CMake integration, compile whatever can be compiled safely and report the exact target/link dependencies needed. Likely dependencies are the existing compatibility-result/share/local-store and session-metrics libraries; do not duplicate their source.

Agent 02 will independently build the requirement-authority writer and may consume your public header after central integration. Do not edit Agent 02 files.

## Finish
Run the focused verification available inside your envelope, `git diff --check` only if permitted by the local worker protocol, then:

`python3 tools/chunk_claim.py done CHUNK-V1P-01-LOCAL-EVIDENCE-WRITER --owner v1p-01-local-evidence-20260831 --summary "<exact result>" --verification "<tests/builds>" --follow-up "<central CMake/API integration note or none>"`

If blocked, use `blocked` with exact evidence. No commit/push/reset/clean/rebase and no edits outside claimed paths.
