# HydraSeat Community Compatibility Ecosystem Threat Model

Status: implementation/security review baseline for the v1 data-first community path.

This document covers untrusted compatibility results, portable TwoPlayerSetup data, community package/catalog data, local redaction previews, and the optional submission transport. It does not authorize a public service launch, broad binary plugin SDK, launcher/DRM bypass, or automatic telemetry.

## 1. Trust boundaries

HydraSeat treats every community-provided byte as untrusted until it has passed the relevant local boundary.

| Boundary | Authority | Security rule |
| --- | --- | --- |
| Local compatibility result | Local test/runtime evidence | Technical truth is created locally before any sharing state exists. |
| Public compatibility JSON | `compatibility_result` schema | Bounded, versioned, redacted data only; unknown/future/private fields fail closed. |
| Portable TwoPlayerSetup | P6 portable/setup validators | Typed data only; machine paths are variables requiring explicit local remap. |
| Community setup entry | P9 setup validator | Passive metadata only; executable/bypass/external-resource instructions are rejected. |
| Community package/catalog | P8 trust + P9 package/catalog validators | Data-only manifest, exact hashes/selectors/revisions, transactional install/update/rollback. |
| Support bundle | P8 support bundle | Aggregate/public evidence only; exact export bytes require preview and explicit approval. |
| Optional submission | P9 submission boundary | Exact redacted JSON requires explicit approval; no automatic retry or background telemetry. |
| Local game launch | P6 plan/preflight | Community popularity or remote acceptance never overrides local compatibility/protection gates. |

Binary backend/provider/Seat-UI extension SDKs remain deferred for v1. A community data package cannot introduce executable code, output paths, scripts, helpers, drivers, shell commands, or download authority.

## 2. Threats and required mitigations

### 2.1 Malformed or resource-exhausting data

Threats include oversized JSON/packages, excessive arrays, invalid enum/version values, malformed Unicode, path abuse, duplicate/conflicting identities, and deep/unknown structures intended to consume memory or confuse parsers.

Mitigations:

- every public schema has explicit byte/count/string bounds;
- strict decoders reject unknown/future schema fields unless a versioned packet explicitly allows them;
- package members are declared before use and exact member count/hash/type/selectors are checked;
- invalid input is transactional: the previous trusted cache/setup/result remains unchanged;
- no archive member is mapped directly to an arbitrary local output path.

Evidence: `CompatibilityResultTests`, `CommunityPackageTests`, `CommunitySetupTests`, `SetupPackageTests`, `CompatibilityCatalogTests`, and `CatalogCacheTests`.

### 2.2 Script, command, binary, or protection-bypass payloads

Threats include limitations/metadata telling HydraSeat to run PowerShell/cmd, load/inject a DLL, disable anti-cheat/DRM, fetch a helper executable, or otherwise turn community data into code execution.

Mitigations:

- community package entry kinds are data-only;
- P6 setup fields are typed arguments/paths, not shell command strings;
- passive community text rejects executable/protection-bypass/external-resource instruction markers;
- URLs and external helper references are not implicit acquisition authority;
- protected/experimental setups still require local explicit approval and never imply anti-cheat safety.

Evidence: `CommunitySetupTests`, `CommunityPackageTests`, P6 custom-executable/provider tests, and local plan/preflight tests.

### 2.3 Credential, identity, private-path, or typed-text leakage

Threats include provider credentials/session material, account IDs, Player display names, personal absolute paths, device serials, raw typed text, unrelated process details, or raw crash-journal identities entering public/support payloads.

Mitigations:

- public compatibility results have mandatory redaction metadata;
- portable setup export converts machine-specific paths to typed variables;
- support bundles accept only bounded environment tokens, aggregate metrics, reduced recovery counts/state, public compatibility data, and stable event codes;
- support export is bound to the exact JSON preview bytes;
- provider account-reference values are not part of public compatibility/support payloads;
- privacy-invalid local results cannot enter the submission state machine.

Evidence: `CompatibilityResultTests`, `SetupPackageTests`, `SupportBundleTests`, `ProfileCliTests`, `CommunitySubmissionTests`.

### 2.4 Forged, stale, conflicting, or misleading evidence

Threats include fabricated provenance, stale package revisions, conflicting duplicate IDs, evidence for a different game/provider/setup, or a success percentage presented as a guarantee.

Mitigations:

- results include provenance, environment/game/provider/scenario selectors, evidence origin, and explicit `NotMeasured`/unsupported semantics;
- package/catalog installation requires exact trust metadata, hashes, selectors, schema versions, and monotonic revision policy;
- aggregation is cohort-segmented and keeps Success/Failure/Untested denominators explicit;
- imported setup/evidence never supplies the local runtime requirement or bypasses local preflight;
- UI/documentation must call percentages evidence/sample statistics, not certification or guarantee.

Evidence: `CompatibilityAggregationTests`, `CommunityPackageTests`, `CompatibilityCatalogTests`, `CommunitySetupTests`.

### 2.5 Duplicate/spam/retry ambiguity

Threats include accidental repeated submissions, retry after timeout producing duplicate records, or a remote response falsely claiming acceptance.

Mitigations:

- submission identity and idempotency key are deterministic for the exact canonical payload;
- retry is caller-explicit; there is no hidden automatic retry loop;
- a duplicate may be acknowledged only as the typed `DuplicateAccepted` result;
- accepted/duplicate-accepted responses require a bounded receipt and explicit remote acceptance;
- malformed responses are not converted into success.

Evidence: `CommunitySubmissionTests`.

Rate limiting, abuse detection, moderation, and service-side authentication are deployment responsibilities of any future public transport. The local client does not assume they exist and continues to work offline.

### 2.6 Protected-game evidence misrepresented as safety

Threat: a community success report for a protected/experimental title is interpreted as permission, anti-cheat safety, or a bypass recipe.

Mitigations:

- `protectedExperimental` is retained in result, aggregation, preview, setup, and sharing state;
- local high-risk approval is authoritative;
- protection-bypass instructions are rejected;
- HydraSeat does not publish an official `Certified` badge and does not infer future safety from a sample result.

Evidence: `CommunitySetupTests`, `CompatibilityAggregationTests`, `CompatibilityShareModelTests`, Phase 6 preflight/regression tests.

### 2.7 Malicious artwork, URI, or external resources

Threats include remote image/URI fields used for tracking, unexpected downloads, helper acquisition, or executable handoff.

Mitigations:

- community setup/package schemas do not grant executable/download authority;
- passive metadata containing active external-resource instructions is rejected;
- game artwork/icons remain local/provider metadata by default and are not redistributed merely because a launcher displays them;
- a future external resource feature requires its own trust/schema/review packet.

### 2.8 Catalog rollback/tamper

Threats include replacing a trusted cache with modified bytes, replaying an older revision, partial updates, or corrupting the only offline copy.

Mitigations:

- exact hash/trust/provenance policy is checked before first install/update;
- revision policy rejects stale updates;
- update is transactional and the last trusted local cache survives failed/offline/tampered refresh;
- explicit rollback restores a previously trusted cache rather than an arbitrary file.

Evidence: `ArtifactTrustTests`, `CatalogCacheTests`, `CommunityPackageTests`, `CompatibilityCatalogTests`.

### 2.9 Transport outage or compromise

Threats include offline/timeout/unavailable service, malicious response metadata, or a compromised service returning misleading acceptance.

Mitigations:

- local result is generated and retained before transport exists;
- sharing requires exact-preview approval;
- offline/unavailable fails before a transport call;
- timeout/failure affects submission state only, never technical result truth or gameplay;
- receipt IDs are bounded opaque evidence, not execution authority;
- catalog/package data downloaded from any service still passes independent P8/P9 trust and schema checks.

Evidence: `CommunitySubmissionTests`, `CompatibilityShareModelTests`, `CatalogCacheTests`.

## 3. Data minimization and retention rules

1. Local testing is the default. Network submission is optional and disabled unless the user explicitly chooses Share/Submit for a prepared result.
2. The user can inspect/copy/export the exact redacted payload before sharing.
3. HydraSeat does not require provider credentials or a HydraSeat community account for core local play.
4. A submission failure never deletes the local result. New local tests may supersede older results but history remains distinguishable from transport state.
5. Public service operators should retain only the public schema payload, bounded transport receipt/abuse metadata necessary to operate the service, and documented moderation state. Raw support bundles should not be silently attached to compatibility submissions.
6. Retention periods, server logs, IP-address handling, and jurisdiction-specific privacy terms must be declared by the actual deployment before public collection begins. This repository does not invent a server retention period for a service that does not yet exist.
7. Withdrawal of a remote result must not delete or alter the user's local result. Catalog publication should remove/mark withdrawn evidence on a later versioned publication while preserving aggregate integrity rules.

## 4. Reporting and withdrawal process

The repository currently has no tracked `SECURITY.md` or deployed community service contract. Therefore public community collection must not be advertised as production-ready until a private vulnerability-reporting route and operator privacy/retention terms are configured.

For the codebase today:

- security defects should be reported through the repository owner's private security-reporting mechanism when available; sensitive exploit details should not be posted in a public issue;
- ordinary malformed fixture/compatibility-data bugs may use the normal issue workflow if they contain no sensitive data;
- a withdrawal request identifies the public `result_id` or submission receipt, not provider credentials or local machine identifiers;
- maintainers must publish the actual private reporting/withdrawal route before enabling a public submission endpoint.

This deployment prerequisite does not block offline/local compatibility testing, static local catalogs, package validation, or the conformance CLI.

## 5. Review conclusions

The v1 community path remains data-first and optional. Existing core boundaries cover the declared parser/privacy/tamper/command-execution/staleness/offline failure cases, and focused negative tests exercise them. Remaining public-launch work is operational: configure a real reporting/withdrawal channel, publish deployment privacy/retention terms, and review the chosen transport/service configuration. Those items are not evidence that network sharing or a public backend exists today.
