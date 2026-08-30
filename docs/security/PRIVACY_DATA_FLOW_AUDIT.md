# HydraSeat v1 Privacy Data-Flow Audit

Status: **P10-PRIV-01 automated code audit, 2026-08-30**. This is a source/contract review of the current v1 code line. It is not a deployed-service privacy assessment and it does not replace clean-machine/manual, physical-hardware, real-game, or certificate validation.

## 1. Audit boundary

The audit follows data that can cross one of these boundaries:

1. persisted local user/profile/recovery state;
2. local diagnostics/support export;
3. community compatibility submission;
4. provider launch/account-reference integration;
5. installer/update state;
6. runtime-only identifiers that must not become persisted identity.

The review checks the v1 privacy rules in `docs/PRIVACY.md`, D-022, D-040, D-046 through D-048, and the P8/P9/P10 packet contracts. Test/lab-only traces remain separate from shipping support bundles; explicitly enabled input diagnostic traces are not treated as ordinary telemetry.

## 2. Data-flow inventory

| Boundary | Code/contract | Data allowed | Data prohibited / redacted | Current result |
| --- | --- | --- | --- | --- |
| Seat hardware persistence | `WorkspaceManager`, P6 Seat schema | Seat ID/name, stable assigned hardware IDs, Management Seat, active flag | runtime PID/HWND/handles as stable identity | **Repaired**: legacy schema-v2 `target_hwnd` is now compatibility-only, written as `0` and discarded on load |
| Player persistence | `PlayerProfileDocument` | Player ID/display name/locale, bounded provider account references | provider password, OAuth/refresh token, cookie, general credential | Local-only contract; account references validate as bounded identifiers |
| Game/setup persistence | P6 schema/setup packages | game/provider identity, local path/config fields required for local use, typed setup data | credential fields, arbitrary executable script surface in imported community data | Local machine paths remain local; portable/community package path mapping is separately redacted/typed |
| Crash journal / safe mode | `CrashJournalState`, `SafeModeMarker` | session/plan hashes, generations, action IDs, phase/result, bounded snapshot references | Player names, typed input, arbitrary paths, credential payloads | Fixed-width bounded recovery state only |
| HidHide recovery | `HidHideSessionRecoveryRecord` | guarded local rollback snapshot required to restore prior device policy | community/network export, arbitrary caller-selected persisted path | Local recovery-only, numeric resource-ID derived file naming |
| Compatibility result | `CompatibilityResult` | game/provider/version/environment, typed outcomes, bounded measurements, provenance | credentials, Player names, personal paths, raw typed text | Mandatory redaction metadata validated before result can enter sharing history |
| Local compatibility history | `CompatibilityShareModel` + `CompatibilityLocalStore` | canonical `CompatibilityResult` records only | preview consent, receipt, remote acceptance, retry/pending state | Versioned/bounded/transactional; restored results require a fresh preview/consent, and stale/orphan staging files are removed with cleanup verification |
| Privacy settings | `%LOCALAPPDATA%\\HydraSeat\\privacy-settings.json` | sharing enabled flag, retained-result count, schema version | compatibility data, Player/provider/account/path/token payloads | 1 KiB strict versioned contract, staged replace; sharing defaults off |
| Community submission | `SubmissionEnvelope` | canonical exact redacted result JSON, result/submission/idempotency IDs, protected marker | account field, credential field, arbitrary file, background retry state | Explicit opt-in + exact bytes + deterministic payload identity + one-use preview generation; stale/superseded approval is rejected before transport |
| Compatibility-share UI history | `CompatibilityShareModel` | local technical truth plus local share-state presentation | transport/export state changing technical result truth | Decline/offline/failure/upload never changes compatibility truth. Local-file export now fails closed unless its own exact frozen preview/version/identity/generation was approved; the existing Win32 Save handler has not yet been wired to present that preview, so it currently receives `PreviewRequired` rather than bypassing consent |
| Profile CLI export | `profile_cli` | redacted plan/profile diagnostics | provider account-reference value | Account selection is exported as boolean / `<redacted-selected>`; Player document export replaces each stored opaque `accountRef` with `redacted` |
| Support bundle | `SupportBundle`, `SupportExportSession` | bounded public environment classes, metrics, recovery summary, compatibility result, stable event codes | credentials, Player names, personal paths, raw typed text, unrelated process data, device serials, machine names | Mandatory redaction stays fail-closed; export binds canonicalization version, exact frozen bytes, deterministic identity, and a fresh preview generation, then returns those frozen bytes even if the source bundle changes after approval |
| Seat UI self-test report | `seat_ui_main.cpp` report path | Seat ID, phase, connection/authority/transition state | Player ID/name, game ID, path, credential | Controlled/test report contains state only |
| Provider boundary | `LauncherProviderAdapter` | bounded account reference, launch target/args, process evidence | credential acquisition, arbitrary script/shell operation, bypass operation | Account reference remains a local opaque selector; public compatibility/support formats do not expose it |
| Installer state | `installer_transaction` | release/revision, architecture, owned component version/hash, startup mode, retention choice | Player/provider secrets, arbitrary user paths/commands | Fixed owned-component enums; executor owns real paths outside the transaction data contract |
| Program update | `update_transaction` | release notes ID, target/current revision/version, restart flag, exact approval identity | forced approval, credential/account fields, compatibility-data payload replacement | Explicit target-bound approval and installer trust/rollback boundary |

## 2A. Installed-product retained-data audit

This table is intentionally limited to data for which a current **release-target writer** was found, plus installer-owned machine state. A parser/store type by itself is not counted as retained product data. In particular, the production runtime-requirement resolver currently reads `%LOCALAPPDATA%\\HydraSeat\\runtime-requirements.json`, but no release-target writer for that file was found; test-only writes do not turn it into a claimed installed-product retention flow.

| Retained authority | Storage location category | Fields / data class | Default retention | Maximum retention | User-visible delete / clear path | Exported? | Shared? | Redaction boundary | Corruption behavior |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Legacy Seat/workspace configuration (`WorkspaceManager`, Setup UI) | `workspace_config.json` in the process working directory | two Seat records, Seat names/active state, stable display/keyboard/mouse/controller/audio endpoint IDs, Management Seat; legacy `target_hwnd` key is always persisted as zero | one current file until reconfigured or manually removed | 1 MiB committed file; one sibling `.tmp` may exist only during an interrupted atomic write | Reconfigure overwrites it. No dedicated product-wide clear action was found. The installer `-RemoveHydraSeatUserData` switch targets `%LOCALAPPDATA%\\HydraSeat` and therefore does **not** guarantee removal of this working-directory file | No automatic privacy export | No | Stable hardware IDs stay local; runtime HWND/PID/handle identity is discarded and cannot become share identity | bounded/strict load fails closed; malformed state is not adopted into live configuration |
| Player profiles (Management Players) | `%LOCALAPPDATA%\\HydraSeat\\players.json` | up to 64 Player IDs/display names/locales and up to 16 bounded opaque provider account references per Player | until edited/removed or optional user-data uninstall | one strict document, max 4 MiB | individual Player `Remove`; full `%LOCALAPPDATA%\\HydraSeat` removal only when uninstall is explicitly invoked with `-RemoveHydraSeatUserData` | `hydraseat_profilectl` can render a user-selected Player document, but replaces every `accountRef` with `redacted` | Not through community/support flows | provider credentials/tokens/cookies are not representable; opaque account references are redacted on CLI export and excluded from compatibility/support payloads | decode/read failure leaves the disk file untouched and disables writes instead of partially adopting or overwriting corrupt data |
| Manual custom-game records (Management Add Executable) | `%LOCALAPPDATA%\\HydraSeat\\manual-games.json` | custom game/provider IDs, title, install root, executable candidates, local version/hash/compatibility reference | until updated or user-data uninstall | one strict document, max 4 MiB and schema max 4096 game records; current UI applies a tighter provider bound | no dedicated manual-game delete/clear control was found in the current launcher; full user-data uninstall removes the LocalAppData root | user-directed profile CLI/game-document rendering is possible; it is not an automatic upload | No | personal/local paths remain local; community compatibility and support-bundle schemas do not accept this document or raw local paths | malformed/oversized/partial data disables the writable manual-game store and is not partially imported |
| Compatibility privacy preferences | `%LOCALAPPDATA%\\HydraSeat\\privacy-settings.json` | schema version, `communitySharingEnabled`, `retainedLocalResults` only | one current settings record until changed or user-data uninstall | 1 KiB, exactly one current record plus same-directory staging during replace | change settings in Management; full user-data uninstall removes the LocalAppData root | No payload export | No | contains no compatibility result, Player, account reference, user path, token, or diagnostics | strict transactional restore rejects malformed/future/duplicate/unknown/wrong-type/oversized input without changing prior live settings |
| Local compatibility technical-result history | `%LOCALAPPDATA%\\HydraSeat\\compatibility-results.jsonl` | canonical `CompatibilityResult` only; no consent generation, pending/retry state, receipt, or remote acceptance | 32 results by default | 64 results hard maximum; each result max 256 KiB; aggregate store uses the fixed 64-record bound | visible delete-one and clear-all actions; user-data uninstall also removes it | **Yes, only after exact local-export preview approval**; current Win32 Save UI is fail-closed until it is wired to the new preview/approve calls | Optional community submission only when sharing is enabled **and** the exact separate community preview is approved | compatibility redaction contract must exclude credentials, Player names, personal absolute paths, and raw typed text before a result can enter this store/share flow | malformed/future/duplicate/empty/oversized history is rejected transactionally; stale/orphan sibling staging is removed and cleanup verified |
| Explicit Setup/Diagnostics log | `hydraseat_debug.log` in the process working directory; created only when `HYDRASEAT_DIAGNOSTICS=1` | redacted device-change/input-category/mapping/tile-display diagnostics; no key text is written by this logger | opt-in file is truncated on each diagnostics-enabled application start; otherwise an old file remains until manually deleted | 1 MiB byte budget per opened log; logging closes at the cap; no time-based TTL | no dedicated UI clear was found; manual file deletion is required, and LocalAppData uninstall does not guarantee removal of a working-directory log | Not included by the support-bundle contract | No | this file is outside community/support schemas and must not be treated as approved telemetry; it records categories/mapping labels, not raw typed text or device paths | not consumed as authority; a damaged old log is overwritten/truncated when explicit diagnostics next opens it |
| Crash journal + safe-mode recovery state | current-user `%LOCALAPPDATA%\\HydraSeat\\Recovery` | session/plan hashes, runtime generation, fixed action IDs, rollback records, bounded snapshot references; safe-mode reason/diagnostic/hash | retained while needed for startup/recovery; clean/reset paths rotate or clear verified state | current journal up to 8 KiB + four 8 KiB history slots; safe-mode marker exactly 88 bytes; max 160 journal records and 16 snapshot references | `hydra_reset` verified recovery / safe-mode clear; explicit user-data uninstall removes the LocalAppData root after machine uninstall succeeds | support bundles may contain only a bounded **derived recovery summary**, never the raw journal | No | no Player/account/typed-input/arbitrary-path payload; raw recovery files are not community data | corrupt/inconsistent recovery state is not promoted to clean; startup assessment enters safe/recovery-required handling and corrupt safe-mode identity cannot be silently cleared |
| Installer committed state + uninstall registration | `%ProgramData%\\HydraSeat\\install-state.json` and fixed HKLM uninstall key | release version/revision/commit, architecture, fixed install root/startup mode, exact owned-file names/hashes; uninstall metadata | installed lifetime | install-state file max 64 KiB; one fixed product registration | normal `Uninstall`; user-data removal remains a separate explicit switch | No | No | fixed product/release metadata only; no Player/provider credential fields or arbitrary commands | strict state validation fails closed; invalid ownership/state prevents repair/uninstall from guessing ownership |
| Installer mutation/recovery transaction | `%ProgramData%\\HydraSeat\\installer-transactions` | transaction ID/phase/operation, previous-state presence, previous known-good owned files/state/registration required for rollback | deleted after committed success or verified rollback; retained on interruption/recovery-required | at most 8 bounded transaction directories; transaction marker max 4 KiB; snapshot file set is the fixed installer-owned release set | next elevated install/repair/uninstall performs bounded recovery; normal uninstall removes committed machine state only after recovery completes | No | No | release binaries/state only; no per-user Player/account/compatibility payload | pending/partial/foreign/reparse/malformed state fails closed; failed rollback retains recovery material and reports recovery-required |

User-chosen output files are not counted as automatic retention. The compatibility Save handler has a file-dialog writer, but after this change the core returns no bytes until exact local-export approval, so the existing unwired Win32 handler cannot silently produce a file. `SupportExportSession` similarly has no shipping automatic persistence path; a future UI writer must write exactly `exportApproved()` bytes. The `hydra_seat_ui` self-test report is an explicit controlled/test output path, not ordinary retained user state.

The installer `-RemoveHydraSeatUserData` option removes `%LOCALAPPDATA%\\HydraSeat` only **after** machine uninstall succeeds. It intentionally does not claim to remove working-directory `workspace_config.json` or `hydraseat_debug.log`; that remaining location mismatch is documented rather than hidden.

## 3. Finding P10-PRIV-F01 — persisted HWND identity

**Severity:** correctness/recovery safety; privacy-adjacent local-state minimization.

The legacy `WorkspaceManager::saveToFile()` schema-v2 path was still serializing `SeatConfig::targetHwnd`, and `loadFromFile()` restored that numeric HWND into live state. The Seat Manager UI still calls this save path for `workspace_config.json`.

This violated the current invariant that runtime HWND/PID/handle identity is never stable profile identity. An HWND can be reused after the original window is destroyed, so restoring it after restart could create stale ownership state.

Repair:

- `SeatConfig::targetHwnd` is now explicitly documented runtime-only;
- schema-v2 retains the `target_hwnd` key only for backward compatibility;
- new saves always write `target_hwnd: 0`;
- loads still validate that the legacy field is an unsigned integer, then discard it and set runtime `targetHwnd` to zero;
- regression tests assign a nonzero runtime HWND before save and verify it is not restored;
- a historical schema-v2 fixture with a nonzero `target_hwnd` still parses, but the live value becomes zero.

This is intentionally a compatibility repair rather than a schema-version rewrite. P6 migration remains responsible for converting historical profile data into the separated stable v1 schema.

## 4. Diagnostic input traces

Phase 3 observation/lab tooling can explicitly record key codes when its visible diagnostic mode disables key-code redaction. That behavior is allowed only under the existing diagnostic-mode contract: bounded retention, visible opt-in, and no promotion into default support/community telemetry.

Shipping support/community formats do not contain a raw typed-text field and require the relevant redaction flags. Build/test fixture JSONL files are generated developer artifacts and are not release package inputs.

## 5. Local identifiers and paths

Stable Seat hardware IDs and local game/setup paths are needed for local configuration and recovery and therefore may exist on the user's machine. They must not be copied blindly into community evidence or support bundles.

Current public compatibility/support contracts enforce this separation. Portable setup import/export uses typed path variables/remapping rather than treating one machine's absolute path as portable identity.

Provider `accountRef` is intentionally an opaque local selector. The profile schema permits only a bounded identifier form and does not define password/token/cookie fields. Diagnostic plan export exposes only whether an account reference was selected, not the reference value itself.

## 6. Network boundaries

HydraSeat core operation remains offline-first. The reviewed v1 data contracts distinguish:

- optional compatibility/setup catalog refresh;
- explicit community compatibility submission;
- user-approved program/runtime update.

No reviewed release-target contract turns local compatibility sharing on by default. `CompatibilityShareModel` requires both the persisted sharing setting and exact-preview approval for the current result. Submission transport has no automatic retry/background worker contract.

The P8 installer/update transaction structures contain version/trust/owned-component state and exact approval identity but no Player, account, credential, or arbitrary command fields.

## 7. Local RC evidence integrity

The product-v1 acceptance campaign uses campaign/evidence schema version `2`. Campaign identity binds `campaign_id == session_run_id` to the authoritative session directory plus the exact release artifact name/hash, release revision, architecture, profile SHA-256, installed-state SHA-256, RC commit, Windows build class, topology/input fingerprint, Seat pair, and scenario identity. Every child evidence record repeats that exact campaign/session and build/profile/input binding in addition to a unique evidence ID, artifact name, test name, content SHA-256, origin, and evidence class. Duplicate IDs/artifact names/test names fail closed.

Local validation does not trust the campaign record or filename alone. Every referenced evidence artifact must exist as a bounded non-reparse regular file under that exact session's `artifacts` directory, and its SHA-256 is recomputed and compared with the recorded content hash. The offline validator then parses the artifact itself: generated Preflight probes must match commit/revision/architecture/install-state; general imported artifacts must expose a strict schema-v2 `release_binding` matching campaign/session, class/origin, RC artifact name/hash/revision, architecture, profile/install-state, Windows build, topology fingerprint, scenario, test name, result and timestamp. Missing, altered, partial, oversized, stale/future, foreign-build/profile/session evidence and stale campaign staging files are rejected.

Origin and evidence class are independent checks. Current stage classes are `Controlled`, `Physical`, `Manual`, `RealGame`, and `CleanMachineInstall`; `Synthetic` and `SigningDeployment` remain separate representable classes and cannot be substituted into another stage. Controlled stages require `ControlledProcess` origin; every physical/manual/real-game/clean-machine stage requires `Physical` origin as well as the exact stage class. Merely labelling Controlled evidence as `Physical` is therefore insufficient. A child artifact is attached with `Pending` human verdict only; manual verdict is recorded later and a manual stage cannot validate as `Passed` while evidence remains `Pending`. Runner-generated probe evidence is limited to controlled-process Preflight use. Phase 3 hardware evidence is checked for current manual pass plus exact Windows build, architecture, profile hash, and campaign/session identity before import.

A limitation remains: the current Phase 3 hardware manifest does not itself carry the RC commit, release revision, release-artifact hash, or installed-state hash. The campaign binds the imported manifest bytes to those RC identities only after verifying the manifest's build/profile/session fields. Therefore Phase 3 source evidence is still physical/manual evidence, not cryptographic proof by itself of the exact executable RC artifact.

## 8. Remaining release privacy work

This automated code/document closure does not claim deployed/manual P10 privacy validation. Remaining work is intentionally explicit:

- wire the Win32 local-result Save handler to the already-enforced core sequence `prepareLocalExport()` -> show exact frozen bytes/identity/generation -> `approveLocalExport()` -> `exportLocalResult()`. Until then the existing handler fails closed with `PreviewRequired`; this audit did not edit hard-excluded launcher/window/controller code;
- connect support-bundle export/delete affordances to the visible Management diagnostics UX and prove that displayed bytes are exactly the approved bytes;
- decide whether the working-directory `workspace_config.json` and opt-in `hydraseat_debug.log` should move under the per-user data root or gain explicit product clear actions; the uninstall user-data switch does not currently guarantee their removal;
- add a dedicated manual-game remove/clear affordance if v1 product requirements require one; current UI writes/updates `manual-games.json` but the audit found no dedicated delete control;
- if a public community service is deployed, publish and test server-side retention, abuse handling, reporting, withdrawal/deletion semantics, and transport policy. No deployed service policy is inferred from the local transport interface;
- run end-to-end UI/accessibility tests proving the user sees and approves exactly the redacted payload submitted/exported and that stale/superseded consent is rejected;
- collect real physical-hardware, real-game, clean-machine/install, and certificate/signing/deployment evidence separately. Synthetic/Controlled/local validators cannot satisfy those gates;
- rerun this audit after release packaging and installed data locations are frozen.

## 9. Automated evidence

The current source line includes focused checks for:

- privacy sharing default-off and disable-before-send behavior;
- strict versioned privacy-setting persistence;
- bounded local compatibility history with no restored network consent and verified stale/orphan staging cleanup;
- malformed/future/duplicate/oversized history rejection;
- deterministic canonical community-preview identity, canonicalization version, fresh consent generation, retention/version/stale/mutated approval rejection, and exact approved bytes/idempotency identity at transport;
- local compatibility export with an independent exact preview/version/identity/generation, byte-identical approved export, one-byte/stale/decline/retention invalidation, delete/clear scoping, and no network requirement;
- mandatory support-bundle redaction, public build-class validation, canonicalization version, exact preview identity/generation, and frozen approved export bytes after source mutation;
- RC campaign binding to exact campaign/session, artifact name/hash/revision, architecture, profile/install-state, Windows build, topology/input fingerprint and scenario; independent evidence class/origin checks; actual evidence-file SHA-256 plus internal release-binding validation; stale/missing/partial/altered/foreign-session/mixed-class rejection; and manual `Pending` fail-closed behavior;
- acceptance probe release revision/architecture/commit/install-state identity in encoded evidence;
- redacted profile CLI account-reference export;
- runtime HWND non-persistence across WorkspaceManager save/load;
- localized privacy actions in `en-US`, `ko-KR`, and `zh-CN`.

Manual/deployed-service/clean-machine evidence remains separate and must not be inferred from this source audit.
