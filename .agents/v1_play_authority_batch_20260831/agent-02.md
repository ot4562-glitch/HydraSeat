# Agent 02 — Runtime requirement authority writer

You own `CHUNK-V1P-02-REQUIREMENT-AUTHORITY-WRITER`.

## Start
1. Work only in `C:\HydraSeat\repo`.
2. Read `AGENTS.md`, `.agents/AGENTS.md`, only the `CHUNK-V1P-02-REQUIREMENT-AUTHORITY-WRITER` section of `.agents/CHUNKS.md`, then only directly relevant public APIs in `game_runtime_requirement_resolver.hpp`, `game_catalog.hpp`, `provider_adapter.hpp`, `compatibility_result.hpp`, `session_metrics.hpp`, and `two_seat_launch.hpp`.
3. Run `python3 tools/chunk_claim.py list` immediately before claim.
4. Claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-V1P-02-REQUIREMENT-AUTHORITY-WRITER --owner v1p-02-requirement-authority-20260831 --paths include/hydra/runtime_requirement_authority.hpp src/runtime_requirement_authority.cpp tests/test_runtime_requirement_authority.cpp --note "exact local requirement review and authority publication"`

## Real product gap
The production resolver already correctly fails closed on missing/corrupt/stale/untrusted runtime requirement authority and the Host independently re-resolves it. The missing piece is a release-target writer. Today `%LOCALAPPDATA%\HydraSeat\runtime-requirements.json` can be consumed but an ordinary user cannot create a valid exact record through the product.

Your module publishes one reviewed authority record without weakening the resolver.

## Inputs and authority
Design a bounded request around exact current values, not arbitrary strings. It should contain enough to bind:
- one current `catalog::LocalGameCatalogEntry` or equivalent exact `profile::GameRecord` plus current catalog state;
- one current `provider::ProviderDescriptor` and exact provider AppID identity from the Game;
- one canonical local `compat::CompatibilityResult`;
- the exact `metrics::SessionMetricsReport` that produced that result;
- explicit user-reviewed `launch::Requirements` only.

Do **not** accept `launch::Capabilities` as caller authority. Derive capability booleans from actual report/result facts and the current exact game/provider identity. At minimum, process/launch, window ownership, display placement, input isolation, controller/audio outcomes and cleanup/recovery must never become true when their source observation is absent/failing.

If an existing type lacks enough evidence to truthfully derive one capability, fail closed or leave that capability false. Do not invent a new meaning for an existing evidence bit merely to make the resolver pass.

## Record publication
Implement a transactional writer around `RequirementEvidenceDocument` / `GameRuntimeRequirementStore`:
- explicit injectable store path for tests;
- production convenience path may use `defaultGameRuntimeRequirementStorePath`;
- load the existing document; Missing means an empty valid document;
- validate exact Game/provider/AppID/version/hash identity against evidence and report before mutation;
- reject stale/unknown catalog state, unavailable/mismatched provider descriptor, invalid metadata revision, mismatched evidence provenance, and inconsistent metrics/result origins;
- Synthetic and ImportedCommunity can never publish trusted local requirement authority;
- ControlledProcess may be stored only as exactly ControlledProcess-backed authority; production `PhysicalOnly` resolution must continue to reject it. Never rewrite origin to Physical;
- Physical-backed publication requires the report/result to both genuinely encode Physical evidence and satisfy existing physical-eligibility invariants. Never set eligibility in this writer;
- bind `recordId`, record `revision`, `providerMetadataRevision`, evidence result/provenance IDs/revision, exact optional game version/hash and compatibility reference deterministically;
- preserve unrelated Game records;
- replace only the exact same Game authority;
- replacement revision must monotonically advance with overflow/future/corruption fail-closed behavior;
- validate the complete new document before save;
- save only through `GameRuntimeRequirementStore` transactional semantics.

Use an explicit deterministic record ID strategy that cannot collide across different exact Game identities. Do not use absolute executable paths as persistent identity if the existing schema already has stronger IDs/hash fields.

## High-risk rule
This writer must not create or persist a protected-runtime approval. If `requirements.highRisk` requires separate exact approval under the current resolver, leave that separate gate intact. Never turn an approval checkbox into authority inside this module.

## Tests
Use temp paths. Cover at least:
- first valid Physical-backed record publication;
- exact ControlledProcess record remains Controlled and is rejected by a production `PhysicalOnly` resolver check;
- Synthetic/ImportedCommunity publication rejected;
- caller cannot supply capabilities directly;
- process/window/display/input/controller/audio/recovery capabilities are derived only when observations actually support them;
- Game ID/provider/AppID/version/hash mismatch rejected without changing previous store;
- provider metadata revision mismatch/zero/unavailable rejected;
- evidence result/provenance mismatch rejected;
- stale catalog entry rejected;
- preserve unrelated records;
- same Game update increments revision and replaces exactly one record;
- revision overflow/corrupt existing store fails closed;
- high-risk requirements do not manufacture protected approval;
- saved document round-trips through existing resolver/store validation.

Where useful, prove the newly written Physical record actually resolves through `resolveTrustedGameRuntimeRequirements`, while a ControlledProcess-backed equivalent does not under `LocalEvidenceTrust::PhysicalOnly`.

## Integration note
Do not edit CMake or Agent 01/03 files. Central integration will link this module and later wire the normal UI. Report exact target dependencies; likely resolver/store + catalog/provider + compatibility/session-metrics/two-seat-launch contracts.

## Finish
Run focused verification available in your envelope, then:

`python3 tools/chunk_claim.py done CHUNK-V1P-02-REQUIREMENT-AUTHORITY-WRITER --owner v1p-02-requirement-authority-20260831 --summary "<exact result>" --verification "<tests/builds>" --follow-up "<central integration note or none>"`

If blocked, use `blocked` and identify the exact evidence field/API that prevents truthful publication. No commit/push/reset/clean/rebase and no edits outside claimed paths.
