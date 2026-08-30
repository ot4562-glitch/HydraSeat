# HydraSeat v1 Privacy and Local Data Contract

Status: implementation contract for **P10-PRIV-01**. HydraSeat is local-first. This document describes the privacy behavior implemented by the current code and distinguishes it from deployment/UI work that is still pending.

## 1. Default posture

HydraSeat v1 does not require a HydraSeat cloud account or continuous Internet access for core operation.

Community compatibility sharing is **disabled by default**. A locally generated compatibility result remains local unless all of the following occur:

1. the user explicitly enables community sharing;
2. HydraSeat generates the exact redacted canonical JSON preview locally and assigns its deterministic payload identity plus a one-use preview generation;
3. the user approves that exact preview identity and those exact bytes for that result;
4. an explicit submission action consumes the already-approved bytes at the transport boundary. Recomputed or superseded bytes are not substituted.

There is no background retry, timer, implicit upload, account field, or credential surface in the community submission session.

Turning community sharing off before the transport call cancels the pending transition and the transport is not invoked. Local-file export is a separate consent boundary: it does not require network sharing to be enabled, but it does require preparation and approval of its own exact frozen preview before the core export API will return bytes.

## 2. Compatibility-result privacy controls

`CompatibilityShareModel` is the UI-independent privacy/control state machine used by the compatibility-sharing flow.

### Sharing setting

`CompatibilityPrivacySettings.communitySharingEnabled` defaults to `false`.

The exact-preview approval remains a second, independent gate. Enabling the setting alone does not submit anything, and approving a preview while the setting is disabled cannot open the transport boundary.

### Local retention

The model keeps a bounded local-result history contract:

- default retained results: 32;
- maximum retained results: 64;
- zero is rejected instead of being interpreted as silent automatic deletion;
- lowering the bound immediately removes only the oldest history;
- the current result is preserved while it is inside the configured bound.

This retention contract is exposed by the current Win32 Management Games surface. The user can keep community sharing disabled while changing the local retention count.

### Per-user privacy-setting persistence

The Management UI stores only the two privacy preferences in `%LOCALAPPDATA%\\HydraSeat\\privacy-settings.json`:

- schema version;
- `communitySharingEnabled`;
- `retainedLocalResults`.

The file is bounded to 1 KiB and contains no compatibility result, Player, provider account reference, path chosen by the user, token, or diagnostic payload. Restore is transactional and rejects missing/duplicate/unknown/future/wrong-type/oversized input without changing the previous in-memory settings. The Win32 writer stages bytes in the same directory and replaces the settings file with `MOVEFILE_WRITE_THROUGH`; a failed write does not apply the candidate settings to the live model.

### Local technical-result history format

The model also defines a versioned bounded JSONL format for local technical-result history. The first line is a fixed store header and every later line is one canonical `CompatibilityResult` JSON object. The store accepts at most 64 results and applies the user's current retention bound on restore.

The history deliberately does **not** persist preview approval, submission-pending state, receipt IDs, remote acceptance, or retry state. After restart the newest restored technical result starts again at `LocalResultAvailable`; a new exact preview and explicit sharing approval are required before any future upload. Malformed/future/duplicate/empty/oversized history is rejected transactionally rather than partially replacing existing evidence. Store publication uses a same-directory staging file; stale/orphan staging is removed before reuse and cleanup is verified after failure, publication, and explicit local-history removal so sensitive intermediate bytes are not silently retained.

### Export

Local compatibility export uses the same canonical, bounded `CompatibilityResult` JSON contract as community submission, but has its own consent generation. `prepareLocalExport()` canonicalizes and freezes the exact bytes once, records canonicalization version `1`, the selected `resultId`, a deterministic `compat-v1-*` payload identity, and a fresh one-use generation. `approveLocalExport()` accepts only that exact tuple. `exportLocalResult()` never reserializes the result; it returns only the already-approved frozen bytes and otherwise fails with `PreviewRequired`/stale-approval diagnostics.

The `compat-v1-*` payload identity is a deterministic byte identity built from two independently seeded FNV-1a-64 values over the canonical JSON. It is used for deterministic equality/idempotency, not presented as a cryptographic signature. Byte equality is checked independently at approval/export; SHA-256 remains the integrity primitive for release evidence files.

Changing the selected result, result bytes, redaction/provenance data, retention metadata, canonicalization version, or preview generation invalidates prior local-export consent. Deleting or clearing local history also clears local-export approval. Network sharing may remain disabled throughout this flow.

Compatibility-result redaction metadata requires exclusion of:

- credentials;
- Player display names;
- personal absolute paths;
- raw typed text.

An invalid redaction contract cannot enter shareable local history.

### Delete and clear

The privacy model exposes explicit operations to:

- delete one local result by stable result ID;
- clear all local compatibility-share history.

Deleting an unknown result fails without changing local evidence. Deleting the active result does not silently promote an older result into the active sharing state.

Deleting a local result after a successful remote submission removes the local copy only. A deployed community service needs its own documented retention/withdrawal policy; HydraSeat must not claim that local deletion retracts a server copy unless a verified server operation exists.

## 3. Community submission boundary

The submission layer accepts only the canonical redacted result JSON plus bounded submission/idempotency identifiers and the protected/experimental marker.

It deliberately does not define fields for:

- provider passwords or session tokens;
- cookies;
- Windows account credentials;
- raw keyboard text;
- arbitrary file uploads;
- arbitrary process memory or process lists;
- scripts/commands to execute remotely.

Offline/unavailable/timeout/failure results never erase the local technical result. A retry is an explicit later action rather than a background worker.

Protected-title evidence retains its `Protected / Experimental` meaning through preview and submission state. A successful upload never becomes an anti-cheat safety claim.

## 4. Support bundles

`SupportBundle` and `SupportExportSession` are a separate diagnostics boundary. The bundle records bounded environment/evidence/recovery summaries and requires all default redaction flags to remain true.

The support-bundle contract excludes by default:

- credentials;
- Player names;
- personal paths;
- raw typed text;
- unrelated process data;
- stable device serials.

Export requires canonicalization version `1`, an exact canonical JSON preview, its deterministic `support-v1-*` payload identity, a fresh one-use preview generation, and explicit approval of that exact preview. Re-previewing the same bytes produces a new consent generation, so an older approval cannot be replayed. After approval, export returns the frozen preview bytes even if the caller later mutates its source `SupportBundle`; no second serialization path can reintroduce a private path/account/raw-device field. Support environment fields accept only public Windows build classes and bounded architecture values; a machine name such as `DESKTOP-*` is not a valid build class. Diagnostics must not be treated as permission to collect arbitrary local data.

## 5. Player and provider data

Player profiles are local lightweight identities/preferences. Provider integration may keep only the minimum opaque reference needed to select an already authenticated provider identity where that provider supports it.

HydraSeat must not become a password/token/cookie vault. Provider authentication secrets remain owned by the provider whenever practical. Any future provider adapter that requires broader identity data must update the privacy contract and undergo security/privacy review before release-target use.

## 6. Catalog/update networking

Three trust domains stay separate:

1. local core operation;
2. optional compatibility/setup catalog refresh;
3. program/runtime/driver update.

Disabling optional community sharing must not disable local game discovery, cached compatibility/setup data, launching already available games, diagnostics, or recovery.

Compatibility/setup catalog refresh is not permission to replace executable/runtime binaries. Program/runtime/driver updates remain user-approved and independently trust-checked.

## 7. What still needs release integration

The core privacy contracts now fail closed for both community submission and local compatibility export. P10-PRIV-01 must still not be treated as manually/deployment validated. Remaining integration work is deliberately narrower:

- the existing Win32 local-result `Save` handler still calls `exportLocalResult()` directly. The core now rejects that call with `PreviewRequired` until the UI/controller owner adds a visible `prepareLocalExport()` -> display exact bytes -> `approveLocalExport()` sequence. This worker did not edit the hard-excluded launcher/Win32 UI;
- connect support-bundle preview/export/delete affordances to the visible Management diagnostics UX and prove the displayed bytes are the frozen bytes passed to `SupportExportSession::approve()`;
- provide deployed community-service privacy/retention/withdrawal terms only if such a service is actually shipped. No reviewed deployed service exists in this source line, so no server retention period is invented here;
- run end-to-end UI/accessibility fixtures for exact local-export/community/support approval, including superseded consent;
- separately collect real physical-hardware, real-game, clean-machine/install, and protected signing/deployment evidence. Automated validators cannot promote Controlled/Synthetic evidence into those classes.

The automated code/document closure is therefore fail-closed and testable, while deployed-service and manual acceptance remain explicit release gates rather than implied validation.

## 8. Verification

Focused automated coverage in `tests/test_compatibility_share_model.cpp` verifies at least:

- sharing is disabled by default;
- exact preview does not bypass the disabled sharing setting;
- preview canonicalization is deterministic while each re-preview gets a fresh consent generation;
- stale preview approval, changed canonicalization version, changed retention metadata, and mutated payload identity/bytes are rejected before transport;
- the transport receives the exact approved canonical bytes and deterministic payload/idempotency identity;
- disabling sharing before transport causes zero transport calls;
- retention bounds reject zero and values above the maximum;
- old history rotates deterministically while the current result remains;
- privacy settings encode canonically and malformed/future/duplicate/unknown/wrong-type/oversized settings restore transactionally;
- local technical history round-trips in order, applies the current retention policy, strips network-consent/receipt state, rejects duplicates/future/malformed/empty stores transactionally, preserves Protected/Experimental truth, and verifies removal of stale/orphan staging payloads;
- support export rejects stale generations, changed canonicalization version, and changed payload bytes/identity while preserving exact approved export bytes even after the source bundle changes;
- local compatibility export requires its own exact preview/approval while network sharing remains disabled, rejects one-byte/version/generation/retention drift, and returns only frozen approved bytes;
- unknown delete is non-mutating;
- deleting the active result does not revive stale evidence;
- clear removes local share history;
- existing offline/failure/retry and Protected/Experimental semantics remain intact;
- all new privacy labels remain non-empty in `en-US`, `ko-KR`, and `zh-CN`, and the Windows `HydraSeat.exe` target links the privacy model successfully.

This privacy contract should be updated in the same change whenever a new release-target data flow can persist data or send data off the machine.
