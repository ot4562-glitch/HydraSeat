# Phase 9 — Community Compatibility and Two-Player Setup Ecosystem

## Phase objective

Scale compatibility knowledge beyond one maintainer without requiring an official `HydraSeat Certified` badge or a mandatory cloud backend.

The first ecosystem is **data-first**, not a large binary plugin SDK:

- run compatibility tests locally;
- keep results local by default;
- let the user preview a redacted result;
- optionally submit/share it;
- aggregate success/failure counts and percentages by materially relevant environment;
- distribute versioned compatibility/two-player setup catalogs as static/signed/hash-checked data artifacts where practical;
- keep protected-title results clearly experimental and never interpret them as anti-cheat safety.

A broader binary extension SDK remains deferred unless concrete post-v1 needs justify its security and maintenance cost.

## Phase exit gate

Phase 9 closes only when:

1. local compatibility results use a versioned bounded schema;
2. sharing is explicit opt-in with exact redacted JSON preview;
3. reports exclude credentials/raw typed text/Player names/personal paths by default;
4. aggregation records sample size, success/failure, and useful sub-results;
5. materially different game/HydraSeat/provider/Windows/compatibility environments are segmented rather than blindly averaged;
6. `Untested` is distinct from failure;
7. protected/experimental technical success never becomes an anti-cheat safety claim;
8. community TwoPlayerSetup data has validation/provenance/trust boundaries;
9. initial catalog distribution works without requiring a custom always-on HydraSeat backend;
10. core remains fully functional offline with all community features disabled;
11. contribution/security/privacy docs/tests pass;
12. Phase-close verification passes.

---

## P9-SDK-01 — Community compatibility result schema and version policy

**State:** CODE_COMPLETE

**Goal**

Freeze the first public **data** boundary for local/community compatibility evidence, not a broad in-process C++ plugin ABI.

**Depends on**

- P8-CLOSE-01
- P5-COMPAT-01

**Schema should cover**

- schema version/result ID/timestamp class;
- Game/provider/version identity;
- HydraSeat version/build;
- Windows version/build class;
- scenario (`different-games`, `same-game-two-instance`, protected experiment, etc.);
- relevant compatibility/backend versions/capabilities;
- launch/instance result;
- receiver-verified input/bleed result;
- controller/audio result where applicable;
- clean exit/rollback result;
- bounded resource/latency summary;
- protection/experimental flag;
- redaction/provenance metadata.

**Invariants**

- no universal `certified=true` field;
- unknown future required semantics fail closed;
- missing measurement is `unknown/not measured`, never zero;
- result data is evidence, not executable instructions;
- schema can be reprocessed under newer aggregation rules.

**Done when**

Valid/malformed/future/max-size/privacy fixtures and deterministic canonicalization tests pass, with compatibility to the local Phase 5/8 evidence sources.

**Implementation evidence — 2026-08-29**

- `CompatibilityResult` v1 is a bounded data-only public result containing privacy-bucketed timestamp, exact Game/provider/version, HydraSeat/Windows/architecture, scenario/protection/setup revision, backend versions, typed outcomes, optional measurements, origin, mandatory redaction metadata, and provenance. It has no certification flag, credentials, Player-name, raw typed-input, path, script, or executable fields.
- Strict JSON decode is capped at 256 KiB/depth 32, rejects duplicate/unknown/future required semantics transactionally, and canonicalizes backend ordering. `null` means not measured while numeric zero remains a measured zero.
- `buildCompatibilityResultFromSessionMetrics` converts the existing privacy-safe Phase 5 evidence into this boundary without copying raw input data. Focused `CompatibilityResultTests` pass. P5/P8 real-game/install/soak dependencies remain deferred, so this is automated `CODE_COMPLETE`, not validated community evidence.

---

## P9-CAP-01 — Evidence dimensions, grouping, and confidence policy

**State:** CODE_COMPLETE

**Goal**

Define which environment differences require separate compatibility statistics so a simple percentage does not become misleading.

**Depends on**

- P9-SDK-01

**Dimensions may include**

- exact/compatible game version range;
- HydraSeat version range;
- provider/launch path;
- Windows build family;
- x86/x64 target path where relevant;
- same-game versus different-game scenario;
- input/controller/audio/display compatibility path;
- protection status;
- material setup revision.

**Aggregation rules**

- show sample size;
- show success/failure count;
- allow sub-results (`launch`, `two instances`, `input`, `audio`, `clean shutdown`);
- do not merge materially incompatible cohorts;
- stale evidence can remain visible but is labeled/weighted/segmented by policy;
- one maintainer run and one community run are both evidence, not official support guarantees.

**Done when**

Deterministic fixtures prove grouping/version/staleness rules and the same raw dataset always produces the same displayed cohort statistics.

**Implementation evidence — 2026-08-29**

- Cohorts segment exact Game/provider/app/version, HydraSeat version, Windows class, architecture, scenario/protection, material setup revision, backend identities/versions, and explicit current/stale freshness. Materially different environments never merge.
- Session `Success`, `Failure`, and `Untested` are separate counts, as are each sub-result's `NotMeasured`/`Pass`/`Fail`/`Unsupported` denominators. Duplicate result IDs and future timestamps fail closed under a deterministic reference-month policy.
- Protected technical success remains in a Protected/Experimental cohort. Focused `CompatibilityAggregationTests` pass and raw input order does not change statistics.

---

## P9-PKG-01 — Community setup/catalog package manifest and lifecycle

**State:** CODE_COMPLETE

**Goal**

Package compatibility metadata and TwoPlayerSetup entries as bounded data artifacts with provenance and trust information.

**Depends on**

- P9-SDK-01
- P6-IMPORT-01
- P8-TRUST-01

**Manifest**

- package/schema version;
- setup/result/catalog entries;
- source/provenance/license metadata;
- hashes;
- supported game/provider/version selectors;
- minimum/maximum HydraSeat schema compatibility;
- no executable/script payload by default.

**Invariants**

- data package cannot grant executable privileges;
- path traversal/absolute output paths rejected;
- duplicate/conflicting setup IDs deterministic;
- unknown/tampered package disabled without corrupting local cache;
- local user edits remain separate from downloaded package state.

**Done when**

Create/import/update/rollback/tamper fixtures pass and a package can be mirrored as a static release/catalog artifact.

**Implementation evidence — 2026-08-29**

- `CommunityPackageManifest` v1 is data-only and can declare only compatibility results, portable TwoPlayerSetup data, or catalog snapshots with exact entry hashes/selectors/schema versions, source/license metadata, profile-schema range, and monotonic package revision. There is no executable/script entry kind or output-path field.
- `CommunityPackageStore` requires P8 data-artifact trust, exact package/entry hash observations, deterministic declared-entry matching, and transactional install/update/rollback history. Tamper, missing/extra member, incompatible schema, untrusted provenance, and stale revision leave the last valid package untouched.
- Focused `CommunityPackageTests` pass. Static hosting/mirroring is intentionally deployment-independent and not required for offline core operation.

---

## P9-RPC-01 — Optional community submission transport boundary

**State:** BLOCKED

**Goal**

Define a narrow optional transport for submitting redacted compatibility evidence while allowing v1 to operate without it.

**Depends on**

- P9-SDK-01
- P8-DIAG-01

**v1 deployment options**

The implementation may start with a GitHub/community contribution workflow, static upload endpoint, or other simple bounded transport. A custom always-on backend is not a prerequisite for the product.

**Required client behavior**

- result is generated locally first;
- user chooses Share/Submit;
- exact redacted JSON preview is available before submission;
- no automatic background telemetry submission by default;
- timeout/offline/failure leaves local result intact;
- duplicate/idempotent submission identity defined where transport supports it.

**Done when**

A reference submission path proves opt-in, redaction preview, offline failure, retry/idempotence, and no impact on local gameplay when the service is unavailable.

---

## P9-ADAPT-01 — Backend adapter binary SDK

**State:** DEFERRED

**Goal**

Historical public backend plugin boundary.

**Depends on**

- P9-SDK-01

**v1 decision**

A broad third-party binary input/display/audio backend SDK is not required to launch the data-first community compatibility ecosystem. It creates a much larger code-execution/trust/ABI/security burden and is deferred.

**Done when**

Deferred. Reactivate only for a concrete capability that cannot reasonably ship as core or data-only configuration.

---

## P9-PROV-01 — Launcher provider extension SDK

**State:** DEFERRED

**Goal**

Historical third-party launcher provider plugin boundary.

**Depends on**

- P9-SDK-01

**v1 decision**

Initial providers are core typed adapters. A public binary provider SDK is deferred until actual provider coverage needs justify the security/support cost.

**Done when**

Deferred beyond the initial v1 ecosystem.

---

## P9-PROFILE-01 — Community TwoPlayerSetup format and validator

**State:** CODE_COMPLETE

**Goal**

Make lawful same-game setup knowledge easy to share and review without turning community profiles into arbitrary scripts.

**Depends on**

- P6-PROFILE-01
- P9-PKG-01

**Community entry includes**

- exact Game/provider/version selectors;
- setup schema version;
- instance/data/config/argument/provider-account-reference fields allowed by P6;
- compatibility requirements and known limitations;
- protection/experimental status;
- evidence/sample references;
- source/provenance/license/author attribution metadata where applicable.

**Invariants**

- validator applies all local P6 safety bounds;
- no credentials/tokens;
- no arbitrary script/binary auto-execution;
- protection bypass instructions are rejected/out of scope;
- imported setup still requires local preflight and user-visible mutation review;
- community popularity does not override a local safety failure.

**Done when**

Community setup fixtures can be validated/imported/remapped and compiled locally while malicious/unsafe/tampered entries are rejected.

**Implementation evidence — 2026-08-29**

- `CommunitySetupEntry` wraps the existing P6 portable SetupPackage with exact Game/provider/app/version selector, package/source/license/author provenance, protection marker, bounded limitations, and evidence references. The portable payload provenance must match the enclosing package revision.
- Import still requires explicit local path remapping and reuses P6 `portable::importSetup`, setup validation, and immutable plan/preflight. Community popularity/evidence cannot supply or override local runtime requirements or Protected/Experimental approval.
- Passive community text rejects executable/protection-bypass/external-resource instruction markers such as shell commands, DLL injection, DRM/anti-cheat bypass, and URLs. Focused `CommunitySetupTests` pass.

---

## P9-SHELL-01 — Seat shell extension SDK

**State:** DEFERRED

**Goal**

Historical per-Seat UI extension interface.

**Depends on**

- P7-SHELL-01

**v1 decision**

The minimal Seat Launcher does not need arbitrary third-party UI extensions.

**Done when**

Deferred beyond v1.

---

## P9-DIAG-01 — Local compatibility test/export/submission model

**State:** BLOCKED

**Goal**

Unify local test results, human-readable details, redacted JSON preview, and optional community submission state.

**Depends on**

- P9-SDK-01
- P8-DIAG-01
- P9-RPC-01

**States**

- test not run;
- local result available;
- preview ready;
- user declined sharing;
- submit pending/succeeded/failed;
- result superseded by newer local test;
- protected/experimental marker retained throughout.

**Invariants**

- local result is never deleted because upload failed;
- upload state never changes technical result truth;
- user can inspect/copy/export the exact redacted payload;
- no background submission without explicit product setting/consent decision;
- diagnostic detail not intended for community sharing remains separate.

**Done when**

A user can run a local compatibility check, inspect results, preview redaction, decline or submit, and continue offline regardless of submission outcome.

---

## P9-REG-01 — Local catalog and optional remote/static catalog contract

**State:** CODE_COMPLETE

**Goal**

Define how compatibility/setup data is cached, updated, and rendered without a mandatory central service.

**Depends on**

- P9-PKG-01
- P9-CAP-01
- P8-DATA-01

**Initial model**

- bundled seed catalog optional;
- local cache authoritative for offline display;
- versioned static JSON/package releases may be hosted/mirrored through ordinary release infrastructure;
- remote check is optional/disableable;
- aggregation can be performed during catalog publication rather than requiring a live query service;
- last valid cache survives refresh failure.

**Done when**

Catalog install/update/offline/stale/tamper/rollback tests pass and the UI can show evidence/sample statistics with networking completely disabled.

**Implementation evidence — 2026-08-29**

- `LocalCatalogSnapshot` accepts only decoded compatibility/setup payloads declared by an already trusted `CommunityPackageManifest`; payload kind, ID, selector, package identity/revision, and underlying schemas are revalidated before replacing local catalog state.
- `queryGameEvidence` runs entirely from the local snapshot and deterministic aggregation policy, returning current/stale sample counts, cohort sub-results, and matching setup candidates with no network dependency. Unknown games return an explicit empty/untested view rather than a compatibility failure.
- P8 `CatalogCacheModel` separately preserves the last trusted local package across offline/disabled/tampered refreshes. Focused `CompatibilityCatalogTests` and `CatalogCacheTests` pass.

---

## P9-TEST-01 — Community contribution conformance kit

**State:** CODE_COMPLETE

**Goal**

Let contributors validate compatibility results and TwoPlayerSetup packages before submitting them.

**Depends on**

- P9-PROFILE-01
- P9-REG-01

**Kit**

- schema validator;
- privacy/redaction validator;
- provenance/license field checks;
- forbidden executable/script payload checks;
- deterministic canonicalization/hash checks;
- version/cohort sanity checks;
- example valid/invalid fixtures;
- local dry-run of setup preflight against fake/local inventory.

**Done when**

CI/community contributors can run one documented validation workflow and malformed/private/unsafe contribution fixtures fail with precise reasons.

**Implementation evidence — 2026-08-29**

- `hydraseat_community_validate` provides one bounded offline contributor workflow: `result <file>` strict-decodes a public compatibility result and prints its canonical privacy-safe JSON; `setup-package <file>` strict-decodes/revalidates a portable setup package and prints its canonical representation.
- The CLI inherits exact schema/version/size/privacy/provenance/path-placeholder checks from P9-SDK/P6-IMPORT. P9 package/setup/catalog tests additionally cover tamper, unsafe instructions/external resources, selector mismatch, stale revisions, cohort sanity, and local preflight reuse with precise typed diagnostics.
- The conformance executable builds under strict Windows x64 MinGW warnings. Public contribution documentation and security review remain later packets; no network submission is implied.

---

## P9-SEC-01 — Community ecosystem threat model and privacy review

**State:** BLOCKED

**Goal**

Review the data-first ecosystem as an untrusted-content boundary.

**Depends on**

- P9-TEST-01
- P9-RPC-01

**Threats**

- malicious JSON/package size/depth/path values;
- setup attempting command/script/binary execution;
- credential/private-path leakage;
- forged/stale misleading compatibility evidence;
- spam/duplicate reports;
- protected-game result misrepresented as safety;
- malicious artwork/URI/external resource references;
- catalog rollback/tamper;
- transport outage/compromise.

**Done when**

Threat model, mitigations, negative tests, retention/privacy rules, and reporting/withdrawal process are documented and reviewed before public ecosystem launch.

---

## P9-DOC-01 — Community testing and setup contribution documentation

**State:** BLOCKED

**Goal**

Explain how users can help expand compatibility without needing the maintainer to own every game.

**Depends on**

- P9-TEST-01
- P9-SEC-01

**Document**

- how to run a local test;
- what each measured result means;
- why success percentage is evidence rather than guarantee;
- how same-game setup contributions work;
- automatic versus manual setup paths;
- privacy/redaction preview;
- protected/experimental warnings;
- no anti-cheat/DRM/provider restriction bypass;
- offline use and optional sharing;
- contribution validation/provenance.

**Done when**

A new contributor can produce a valid privacy-safe result/setup contribution using only public documentation and the conformance tools.

---

## P9-CLOSE-01 — Phase 9 closure

**State:** BLOCKED

**Goal**

Verify that community compatibility can scale without official-certification theater, mandatory telemetry, or a broad binary plugin attack surface.

**Depends on**

- P9-DOC-01
- P9-SEC-01
- P9-REG-01
- P9-DIAG-01

**Verify**

- local-first result flow;
- opt-in redacted sharing;
- success/failure/sample/sub-result aggregation;
- cohort/version segmentation;
- `Untested` semantics;
- protected/experimental semantics;
- community TwoPlayerSetup validation/provenance;
- static/offline catalog operation;
- all broad binary SDK packets remain deferred unless explicitly reactivated;
- Phase-close review.

**Done when**

HydraSeat can publish/update a privacy-safe compatibility/setup catalog from community evidence while every user can keep using the complete core product offline and without sharing data.