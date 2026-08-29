# Contributing HydraSeat Compatibility Evidence and Two-Player Setups

HydraSeat compatibility is **evidence, not certification**. A successful report means one measured configuration worked under the recorded conditions. It is not a promise that another game build, Windows update, provider state, protection state, or hardware layout will behave the same way.

The v1 community path is local-first and data-only. You can test and use HydraSeat completely offline. Sharing is optional.

## 1. What you can contribute

Two contribution types are part of the v1 data-first workflow:

1. **Compatibility result** — bounded evidence from one local test, including game/provider/version/environment, scenario, measured sub-results, and provenance.
2. **Portable TwoPlayerSetup package** — typed same-game/two-instance setup data whose machine-specific paths have been replaced with local-remap variables.

Do not submit binaries, DLLs, drivers, scripts, shell commands, credentials, provider session material, anti-cheat/DRM bypass instructions, or instructions to download/run an external helper. Broad binary provider/backend/Seat-UI extension SDKs are deferred from v1.

## 2. Run the test locally first

A compatibility contribution starts with a local result. Network submission is not part of the test itself and a service outage must never invalidate or delete a local result.

Record only what was actually measured. `Untested`/`NotMeasured` is preferable to guessing. In particular, do not infer controller/audio/input isolation or same-title support from launch success alone.

Protected titles remain **Protected / Experimental** evidence. A community result does not establish anti-cheat safety, permission to bypass provider policy, or future compatibility.

## 3. Understand the result

Community aggregation keeps materially different cohorts separate, including relevant game/provider/version, HydraSeat version, Windows class, architecture, setup revision, and compatibility path.

A display such as `87% succeeded (45 reports)` is a sample statistic. It is not a guarantee or an official `Certified` badge. Success, Failure, and Untested denominators and individual sub-results should remain visible.

## 4. Preview privacy before sharing

Local results are private by default. Before Share/Submit, HydraSeat's sharing model produces the exact redacted JSON payload. Approval is bound to those exact bytes; if the payload changes, it must be previewed again.

The public compatibility schema excludes credentials, passwords, provider authentication material, Player display names, personal paths, and raw typed text. Diagnostic/support bundles are a separate artifact and are not silently attached to a compatibility submission.

If you do not want to share, choose Decline or simply keep the local result. Declining or a failed upload does not change the technical result.

## 5. Validate a compatibility result before contributing

Use the contributor conformance CLI:

```text
hydraseat_community_validate result <compatibility-result.json>
```

On success it prints the canonical privacy-safe JSON. A validation failure gives a typed reason such as invalid schema/version, privacy/redaction, identifier, provenance, or malformed input.

Review the canonical output before attaching or submitting it. Do not edit a validated payload to add private diagnostic details.

## 6. Contribute a same-game TwoPlayerSetup

HydraSeat has two setup paths that converge on the same typed model:

- **Automatic path:** HydraSeat derives a candidate from allowed local metadata, validates it, and previews intended changes before anything is applied.
- **Guided manual path:** you edit the same typed fields and validation runs before Save; a failed Save preserves the previous valid setup.

Portable export removes machine-specific working/data paths and replaces them with typed variables. On another PC, every variable must be mapped explicitly to a local approved path and the setup is validated again.

Validate a portable package with:

```text
hydraseat_community_validate setup-package <portable-setup.package>
```

On success the tool prints the canonical representation. An imported setup still must match the local Game/provider/version selector and pass local plan/preflight. Community popularity never overrides a local safety failure or Protected / Experimental approval gate.

## 7. Provenance and attribution

Keep provenance precise and reviewable:

- use stable bounded source/package/result identifiers;
- record the declared revision/version;
- provide the applicable contribution/license metadata when the package format requires it;
- do not claim authorship or rights for third-party game artwork, binaries, provider code, or copied setup content you cannot redistribute;
- follow `docs/CLEAN_ROOM_POLICY.md` for any external research.

Local game icons/artwork should normally come from the installed executable/provider metadata. A community setup is not permission to redistribute third-party artwork.

## 8. What HydraSeat rejects

The validators intentionally fail closed on examples such as:

- oversized, malformed, unknown/future, duplicate, or conflicting schema data;
- stale/tampered package or catalog members;
- credentials, private-path leakage, or weakened mandatory redaction;
- arbitrary command/script/binary execution instructions;
- DLL injection or anti-cheat/DRM/provider-restriction bypass instructions;
- external helper/download URLs used as an execution dependency;
- selector/provenance mismatch;
- a setup that cannot be safely remapped and revalidated locally.

Do not work around a validator failure by hiding the same instruction in another text field. Fix the contribution or keep it local.

## 9. Optional submission and offline behavior

The submission boundary is optional. The expected flow is:

```text
local result
 -> inspect details
 -> generate exact redacted preview
 -> Share/Submit only after explicit approval
 -> success, duplicate acknowledgement, or failure
```

Offline/unavailable service state fails before transport invocation. Timeout/retry does not trigger a hidden automatic retry loop; an explicit retry reuses the deterministic idempotency identity for the exact payload. Local gameplay, local result history, setup validation, and cached catalog use continue without the service.

A real public submission endpoint is not required for HydraSeat core operation and is not claimed by this repository merely because the client-side transport contract exists.

## 10. Reporting security problems or withdrawing evidence

See `docs/security/COMMUNITY_ECOSYSTEM_THREAT_MODEL.md` for the current threat model, data-minimization rules, and withdrawal requirements.

The repository currently does not publish a tracked production `SECURITY.md` or community-service retention contract. Sensitive security details should use the repository owner's private security-reporting mechanism when one is available rather than a public issue. Before a public submission service is enabled, maintainers must publish the actual private reporting route, withdrawal route, and service privacy/retention terms.

A remote withdrawal identifies the public result/submission identity. It must not require provider credentials or local machine identifiers, and withdrawing remote evidence does not delete the contributor's local result.

## 11. Quick contribution checklist

Before sharing a result or setup, verify that:

- the test/setup was produced locally and still passes the current validator;
- the exact canonical/redacted payload has been reviewed;
- no credentials, Player identity, private path, raw typed text, device serial, script, binary, bypass instruction, or external helper dependency is present;
- Game/provider/version/setup provenance is accurate;
- Protected / Experimental state is retained where applicable;
- success/failure/untested claims match measured evidence rather than expectation;
- you have the right to contribute the data/metadata you are publishing.

Passing this checklist makes a contribution structurally reviewable; it does not turn the result into a compatibility guarantee.
