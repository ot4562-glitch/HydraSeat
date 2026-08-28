# Phase 6 — Game Library, Player Profiles, and Two-Player Setup

## Phase objective

Turn the proven two-Seat MVP into the repeatable user model defined by `docs/PRODUCT_V1.md`:

```text
Game -> Seat 1 / Seat 2 / Both -> Player(s) -> Play
```

Phase 6 separates persisted hardware, people, installed games, and same-game setup knowledge. It makes local installed-game discovery the normal path, manual EXE entry the fallback, and same-game two-instance setup automatic where safely possible with a guided manual path when automation cannot finish.

No packet in this phase may create a credential vault or bypass provider/game restrictions.

## Phase exit gate

Phase 6 closes only when:

1. Seat, Player, Game, TwoPlayerSetup, and RuntimeSession schemas are separate/versioned;
2. existing hardware profiles migrate transactionally;
3. locally installed games can be discovered through at least the declared initial provider set plus manual EXE fallback;
4. a Player can move between Seat 1 and Seat 2 while keeping its game/account-reference preferences;
5. provider credentials remain provider-owned;
6. selecting the same game for both Seats resolves a validated TwoPlayerSetup;
7. HydraSeat can generate a bounded candidate setup automatically where feasible;
8. the guided manual setup path can finish an otherwise unknown lawful title;
9. at least one real same-title/two-instance scenario works where game/provider rules permit it;
10. import/export is typed/redacted and cannot silently gain arbitrary code execution;
11. a Phase-close verification passes.

---

## P6-SCHEMA-01 — Versioned Seat, Player, Game, setup, and session schema family

**State:** CODE_COMPLETE

**D-051 automated-development rule**

P5-MVP-01 CODE_COMPLETE is sufficient to start the isolated schema/model implementation. P5-CLOSE-01 remains a required real-product validation gate before Phase 6 can be declared validated/closed; it no longer blocks unrelated versioned schema coding.

**Goal**

Define separate persisted/runtime schemas instead of growing one monolithic profile.

**Depends on**

- P5-MVP-01

**Phase-entry validation gate**

- P5-CLOSE-01 before Phase 6 validation/closure claims

**Required schemas/models**

- `SeatConfig` — physical station hardware only;
- `PlayerProfile` — local display identity/preferences/provider account references;
- `GameRecord` — provider/install identity and local metadata;
- `TwoPlayerSetup` — optional same-game/two-instance recipe;
- runtime session selection/binding — temporary Seat + Player + Game assignment;
- compatibility-result reference/provenance fields where needed.

**Invariants**

- v1 active Seat count <= 2;
- runtime PIDs/HWNDs/handles never persist as stable identity;
- Player is independent from Seat;
- Game is independent from Seat/Player;
- provider passwords/tokens/cookies are not schema fields;
- every schema has explicit version/count/string/path bounds;
- unknown future versions fail closed or use an explicitly safe read-only path.

**Done when**

Round-trip, malformed, migration-boundary, Unicode/path, maximum-size, unknown-version, and cross-concept isolation tests pass for the schema family.

**Automated implementation evidence — 2026-08-28**

- `hydra_profile_schema` defines five separate version-1 model/document families: hardware-only persisted Seat configuration, Player profiles, Game records, optional two-player setup recipes, and temporary runtime Seat+Player+Game bindings. The v1 Seat/binding count is bounded to two without forcing the internal runtime to become an N-Seat product.
- Stable persisted Seat data has no PID/HWND/handle field. Converting the legacy runtime `SeatConfig` fails closed if `targetHwnd` is nonzero, and restoring a persisted Seat always sets the transient HWND to zero; P6-MIG-01 owns migration of the legacy workspace file rather than silently preserving runtime identity.
- Provider accounts expose only bounded provider/account references; passwords, OAuth tokens, cookies, refresh tokens, arbitrary scripts, and shell commands are not representable schema fields. Unknown fields fail closed.
- Game and TwoPlayerSetup records may carry only a bounded logical compatibility `record_id`, provenance identifier, and nonzero evidence revision. Compatibility result bodies/evidence remain in their own store rather than being copied into profile persistence.
- The parser rejects duplicate object keys, malformed/overlong UTF-8, invalid surrogate/code-point sequences, unknown future schema versions, wrong types, unsupported fields, unbounded arrays/strings/paths/documents, duplicate IDs, and invalid cross-references transactionally without replacing the previous valid destination object.
- Runtime selection validates active Seat, Player, Game, setup ownership, unique Seat/Player assignment, and distinct setup instance indices. Two different Players may intentionally reference the same Game through the two typed instance recipes.
- MSVC x64 and Win32/x86 exact-head full suites pass **84/84**. Strict MinGW P6-SCHEMA-01 passes **1/1** with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` and no packet-source warnings. One pre-existing x64 cursor/focus process test produced a single transient host-global-state failure on the first full run, then passed both isolated and subsequent full rerun.

`CODE_COMPLETE` establishes bounded typed schema contracts only. Legacy on-disk migration, provider discovery, compatibility execution, real-game evidence, and UI persistence are separate packets.

---

## P6-MIG-01 — Transactional profile migration and backup

**State:** CODE_COMPLETE

**Goal**

Migrate legacy Seat/workspace data into the separated v1 model without destroying the last valid user configuration.

**Depends on**

- P6-SCHEMA-01

**Requirements**

- parse/validate old state read-only first;
- produce a deterministic migration report;
- create transactional backup/temp output;
- commit only after complete validation;
- preserve unmapped data in a bounded diagnostic report rather than guessing;
- rollback to previous valid state on any error.

**Done when**

Legacy fixture profiles migrate deterministically and injected write/validation failures leave the original profile usable byte-for-byte where declared.

**Automated implementation evidence — 2026-08-28**

- `hydra_profile_migration` first parses the bounded legacy schema-version-2 workspace bytes into a temporary model and does not open the source for writing, rename it, truncate it, or modify it during planning/commit.
- Migration emits a deterministic separated bundle containing `seat_config.v1.json`, empty versioned Player/Game/TwoPlayerSetup stores, `migration_report.v1.json`, and an exact byte-preserving `workspace_config.legacy-v2.backup.json`.
- Legacy `target_hwnd` values are explicitly reported and excluded from stable Seat output. Unknown root/Seat values and legacy shareable-resource declarations are retained as bounded canonical JSON diagnostics rather than silently interpreted; the complete source remains in the exact backup.
- Cross-Seat duplicate legacy resources fail closed unless the corresponding legacy shareable declaration exists. A third Seat, malformed/unknown schema, invalid references, oversized source, and oversized diagnostic values fail without replacing a previously valid migration plan.
- All files are durably written under a sibling staging directory, compared against deterministic planned bytes, decoded through the v1 schema validators, and checked against the exact backup before directory commit. Existing v1 output is replaced only with explicit authorization and is moved to a rollback directory until post-commit validation succeeds.
- Focused tests inject write failure, staged-document corruption, commit failure after moving the old destination, and post-commit validation failure; every case keeps the legacy source byte-for-byte unchanged, restores the complete previous v1 bundle, and removes migration-owned staging/rollback paths.
- Strict Windows x64 MinGW build completed without packet warnings and `profile_migration_tests.exe` passed. Broader physical/game validation remains unrelated and deferred under D-051.

`CODE_COMPLETE` covers the legacy-to-v1 migration engine and deterministic failure recovery. Wiring migration into installer/first-run UI and production profile-root selection belongs to later packets.

---

## P6-CATALOG-01 — Provider-neutral local game catalog

**State:** CODE_COMPLETE

**Goal**

Create the normal HydraSeat game library from locally installed/discovered titles rather than forcing users to browse for executables first.

**Depends on**

- P6-SCHEMA-01

**Catalog fields**

- provider + provider app identity where available;
- install root/executable candidates;
- architecture/version/hash/staleness metadata where safely available;
- title and local icon source;
- protection/compatibility metadata reference;
- manual/custom origin.

**Invariants**

- discovery is read-only;
- friendly title/icon is presentation, not stable identity;
- provider metadata is untrusted bounded input;
- duplicate provider/executable records reconcile deterministically;
- missing cover art never blocks a game entry.

**Done when**

A provider-neutral catalog merges deterministic fixtures and local provider discoveries into stable GameRecords with no filesystem mutation.

**Automated implementation evidence — 2026-08-28**

- `hydra_game_catalog` is a pure candidate-to-catalog core: it performs no filesystem, registry, network, launcher, process, or provider I/O and therefore cannot mutate provider/game state while reconciling supplied local metadata.
- Provider IDs are canonicalized independently from friendly title/icon presentation. Strong provider+app identity generates a bounded stable `game_id`; candidates without a provider app ID fall back to normalized Windows executable identity. Title changes never merge unrelated executables, and provider/app IDs keep a stable game identity across title/install/icon presentation changes.
- Windows executable identity canonicalizes ASCII path case, `/` versus `\\`, repeated separators, and `.`/`..` segments without opening the path. Duplicate provider records and manual/discovered records that resolve to the same executable merge deterministically, with current strong provider metadata preferred and candidate-order-invariant output.
- Two different strong provider/app identities that claim the same normalized executable fail closed rather than guessing. Malformed identifiers, invalid enum values, empty executable sets, overlong icon metadata, schema-invalid hashes/compatibility metadata, and candidate-count overflow leave the previous catalog unchanged.
- Missing local artwork never blocks a valid game entry. Architecture and staleness remain catalog-layer metadata rather than silently changing the P6-SCHEMA-01 persistence contract; conflicting equally current architecture observations conservatively degrade to `Unknown`.
- Final reconciled records are revalidated through `GameRecordDocument`, sorted by stable `game_id`, and the output object is replaced only after the complete build validates.
- Strict Windows x64 MinGW compilation completed with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`, and focused `GameCatalogTests` passed **1/1**. Provider-specific discovery/launch/account behavior is intentionally deferred to P6-PROV-01 and later provider packets.

`CODE_COMPLETE` establishes the provider-neutral read-only catalog/reconciliation contract only; it does not claim Steam/Epic/EA/GOG discovery has been implemented or physically/real-game validated.

---

## P6-PROV-01 — Launcher provider adapter contract

**State:** CODE_COMPLETE

**Goal**

Define lawful provider-specific discovery/launch/account-reference operations behind a small typed interface.

**Depends on**

- P6-CATALOG-01

**Contract**

Providers may expose only their supported bounded operations such as:

- read-only installed-game discovery;
- executable/launch URI/argument resolution;
- local icon metadata;
- detection of already authenticated account identities where the provider exposes a safe supported selector/reference;
- launch request construction;
- post-launch child/process identification evidence.

**Invariants**

- adapter never bypasses account/license/single-instance/provider policy;
- HydraSeat does not collect provider passwords/tokens;
- undocumented mutation is not silently treated as a normal provider feature;
- provider absence/offline state is explicit;
- provider launch remains part of exact game compatibility evidence.

**Done when**

Fake providers prove deterministic discovery/launch-plan/account-reference behavior with malformed/stale metadata rejection.

**Automated implementation evidence — 2026-08-28**

- `hydra_provider_adapter` defines one bounded interface for provider descriptors, read-only installed-game discovery, existing authenticated-account references, typed launch-request construction, and post-launch process evidence. The interface has no credential, provider mutation, shell command, arbitrary script, policy-bypass, or restriction-bypass operation.
- Provider state distinguishes `Available`, `Offline`, and `Absent`, and every operation advertises an explicit capability. Absence, unsupported operations, and online-only launch while offline fail before invoking the adapter; offline local discovery/account-reference lookup remains possible when supported.
- Every installed provider snapshot has a nonzero metadata revision. Launch selections, discovery/account responses, launch requests, and process evidence must match that revision, so stale provider metadata cannot silently produce a plan.
- Provider discovery is revalidated through the P6-CATALOG-01 pure catalog core before replacing caller output. Cross-provider candidates, missing provider app identities, malformed paths/Unicode/IDs, ambiguous catalog identity, and candidate overflow fail transactionally.
- Account references contain only provider ID plus bounded opaque reference, are deduplicated and sorted deterministically, and cannot represent passwords, tokens, cookies, or refresh tokens.
- Launches are typed as an absolute executable or bounded provider URI plus a vector of bounded arguments and optional absolute working directory. No command-line/shell string field exists. Returned provider/game/app/account identity must exactly match the request.
- Post-launch evidence records bounded PID + creation time + absolute executable path and explicit provider-relationship verification state. Malformed, duplicate, or stale evidence is rejected without replacing prior output.
- Fake-provider tests cover deterministic discovery, canonical account ordering, typed launch plans, process evidence, absent/offline/unsupported states, stale revisions, cross-provider/candidate/schema failure, duplicate accounts, shell-like invalid targets, malformed process identity, bounds, and transactional output preservation.
- Strict MinGW compilation passes with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`; focused P6-SCHEMA/CATALOG/PROV tests pass **3/3** under MinGW, MSVC x64, and MSVC Win32/x86. Full MSVC builds exposed and repaired a pre-existing P6-MIG-01 `Windows.h` `max` macro collision. Broader x64/x86 suites cannot be run concurrently on one interactive desktop because existing Host IPC/cursor tests share host-global state; isolated reruns cleared the IPC/lifecycle collisions, while the pre-existing x64 cursor/focus host-native-state tests still require an idle/virtualized desktop to avoid real global cursor drift.

`CODE_COMPLETE` proves the bounded provider-neutral contract with fake evidence only. Steam/Epic/EA/GOG/custom adapters, live provider discovery, supported provider launch behavior, account/license/single-instance policy, and real-game evidence remain owned by later packets.

---

## P6-PROV-02 — Steam provider adapter

**State:** CODE_COMPLETE

**Goal**

Implement the first concrete provider using local Steam installation metadata and normal Steam-supported launch behavior.

**Depends on**

- P6-PROV-01

**Requirements**

- read local library/app metadata without mutation;
- resolve installed app identity/path/executable hints;
- reuse local icon/art where safe/licensed through installed metadata rather than bundling it;
- keep Steam authentication owned by Steam;
- document what account switching/second-instance behavior Steam does or does not expose;
- fail closed when same-title/account/license rules cannot support the requested two-player plan.

**Done when**

Installed Steam fixture/live discovery works read-only and supported launches are reproducible without HydraSeat storing Steam credentials.

**Automated implementation evidence — 2026-08-28**

- `hydra_steam_provider` implements an independent bounded KeyValues reader for local `libraryfolders.vdf` and `appmanifest_*.acf` observations. It rejects duplicate keys, malformed/overlong documents, excessive depth/node/library/manifest/scan counts, invalid UTF-8/AppID/build ID, manifest filename/AppID mismatch, undeclared library roots, duplicate AppIDs, and install-directory traversal.
- The native source reads supported Steam registry locations, library/app manifests, bounded local executable/icon hints, and matching process paths/PID/creation times only. Its interface has no write, launch, authentication, account-switch, download, provider-mutation, or executable-start operation. Fake sources virtualize all file/process observations for deterministic tests.
- Provider metadata revision is a deterministic hash of normalized Steam root, library metadata, sorted manifest paths/bytes, and sorted executable hints. Material changes invalidate older launch/process selections before adapter invocation.
- Discovery emits provider-neutral `GameCatalogCandidate` values containing Steam AppID, title, install root, bounded executable hints, local build ID, and a local executable icon source. Every candidate is revalidated by the P6-CATALOG-01 core before exposure; manifests with no executable hint are not claimed as launchable games.
- The adapter constructs only the normal typed `steam://run/<appid>` request documented by Steam. It does not execute the URI during automated/local smoke tests. Account selection and arbitrary/per-instance Steam arguments are explicitly unsupported, and the adapter makes no same-title, license, offline-launch, or single-instance claim.
- Process observation matches exact discovered executable paths and returns PID + creation time evidence, but forcibly leaves `providerRelationshipVerified=false`: path matching alone is not proof that Steam caused the process. Runtime ownership must establish stronger evidence after an actual launch.
- Virtual-source tests cover two libraries, deterministic discovery/revision, normal URI construction, unsupported account/arguments, stale refresh, duplicate/malformed/traversal metadata, source failure/absence, and unverified process candidates. The native read-only smoke discovered **2** launchable local apps without starting Steam/a game or mutating provider/game state.
- Strict MinGW builds without packet warnings; focused schema/catalog/provider/Steam tests pass **5/5** under MinGW, MSVC x64, and MSVC Win32/x86. Sequential full MSVC Release suites pass **89/89 x64** and **89/89 x86**.

**Clean-room/provenance record**

- Product/source: Valve Steam client and official Steam documentation; proprietary software/documentation-only classification under `docs/CLEAN_ROOM_POLICY.md`.
- Consulted public documentation: Steam Support `Installed games are appearing as uninstalled` (`https://help.steampowered.com/en/faqs/view/4578-18A7-C819-8620`) for default/additional local library behavior, and Steamworks `ISteamApps` documentation (`https://partner.steamgames.com/doc/api/ISteamApps`) for the normal `steam://run/<appid>` path.
- Local behavior studied: ordinary read-only observation of this machine's registry installation path, `steamapps/libraryfolders.vdf`, and `steamapps/appmanifest_*.acf` field shapes. No credential/session data, binary inspection, decompilation, memory extraction, or protection behavior was consulted.
- Code reuse: none. No Valve or third-party source/binary/artwork was copied, adapted, linked, redistributed, or made a build input. No attribution or third-party notice is introduced by this independent metadata reader.

`CODE_COMPLETE` proves bounded fixture behavior, live read-only discovery, and deterministic construction of the documented launch request. Actual URI/game launch, provider authentication/license behavior, process causality, same-title/two-instance behavior, and real-game evidence remain unperformed and must run in an isolated VM/manual acceptance environment before validation or compatibility claims.

---

## P6-PROV-03 — Epic, EA, GOG, and custom provider packets

**State:** BLOCKED

**Goal**

Track secondary provider integrations as separate bounded work rather than one broad unverified launcher abstraction.

**Depends on**

- P6-PROV-01

**Rule**

P6-PROV-03 is complete only when the required v1 subset of its child provider packets is explicitly selected and validated. Non-required providers may remain deferred without blocking release if the published v1 scope says so.

**Done when**

The release-target provider subset is explicitly chosen and each selected child packet has truthful evidence or a documented deferred state.

---

## P6-PROV-03A — Epic launcher adapter

**State:** BLOCKED

**Depends on**

- P6-PROV-01

**Goal**

Read Epic local install metadata and build only supported provider launch requests/account references without handling credentials.

**Done when**

The declared Epic v1 discovery/launch subset passes fixtures/live smoke evidence, or the packet is explicitly deferred from v1 scope.

---

## P6-PROV-03B — EA launcher adapter

**State:** BLOCKED

**Depends on**

- P6-PROV-01

**Goal**

Read EA local install metadata and build only supported provider launch requests/account references without bypassing provider policy.

**Done when**

The declared EA v1 discovery/launch subset passes fixtures/live smoke evidence, or the packet is explicitly deferred from v1 scope.

---

## P6-PROV-03C — GOG launcher adapter

**State:** BLOCKED

**Depends on**

- P6-PROV-01

**Goal**

Read GOG/local installation metadata and construct supported launch paths while preserving offline/core use when practical.

**Done when**

The declared GOG v1 discovery/launch subset passes fixtures/live smoke evidence, or the packet is explicitly deferred from v1 scope.

---

## P6-PROV-03D — Custom executable fallback

**State:** CODE_COMPLETE

**Depends on**

- P6-PROV-01

**Goal**

Preserve the power-user `Add game / EXE` path for unsupported providers and unusual installations.

**Requirements**

- explicit executable/path/args/working-directory fields;
- local icon extraction/reference where available;
- executable identity validation;
- no arbitrary shell-string interpolation;
- any future bounded helper/script feature requires a separate typed/trust decision and is not implied by manual EXE support.

**Done when**

A manually added executable becomes a normal GameRecord/launch plan with safe path/argument handling and no implicit arbitrary command runner.

**Implementation evidence**

- `CustomExecutableDefinition` carries only an explicit title, absolute executable path, bounded argument vector, optional working directory, and optional local icon source. It has no shell-command or script field.
- A virtualizable read-only source canonicalizes and validates the selected file, working directory, local icon, PE identity/architecture, size, and write time before emitting a provider-neutral catalog candidate. Missing, malformed, relative, shell-like, or changed selections fail closed without partial output.
- The adapter pins discovery and launch selection to a deterministic revision derived from the complete definition plus observed executable identity. It emits an exact executable target, argument vector, and working directory; it never starts a process or performs shell interpolation.
- Native process observation is read-only and retains exact PID/creation/path evidence while leaving the provider relationship unverified. Account selection is explicitly unsupported for the custom provider.
- Deterministic fake-source tests cover success, stale selection, unsupported account fields, path/argument bounds, source failures, and transactional output. A native smoke validates only the current test executable as a local PE and does not launch it.
- Strict MinGW focused tests pass 7/7 across schema/catalog/provider/Steam/custom boundaries. MSVC Release x64 and Win32/x86 focused tests pass 7/7 and each complete suite passes 91/91 sequentially.

---

## P6-PLAN-01 — Immutable provider-aware game/Seat launch-plan compiler

**State:** CODE_COMPLETE

**Goal**

Compile current Game + Seat + Player + provider/setup information into an immutable runtime plan consumed by Phase 5/4 activation contracts.

**Depends on**

- P6-PROV-02
- P6-PROV-03D
- P5-LAUNCH-01

**Plan behavior**

- resolve exactly one or two active Seats;
- resolve selected Game per Seat;
- resolve Player account-reference/instance preference;
- if same Game on both Seats, require a valid TwoPlayerSetup;
- compute selected provider launches and compatibility capabilities;
- include exact requirement-aware hardware preflight;
- hash/correlate the immutable plan.

**Done when**

Equivalent input produces an identical plan/hash and any material stale/missing requirement prevents activation rather than silently changing behavior.

**Implementation evidence — 2026-08-28**

- `compileProviderAwareLaunchPlan` is a pure one/two-Seat compiler over validated Seat/Player/Game/TwoPlayerSetup documents, exact provider snapshots, and revisioned runtime requirements. It performs no launch or persistent mutation.
- Equivalent binding order is canonicalized by Seat ID and the plan fingerprint includes Player/Game/setup instance, compatibility/requirement revision, normalized Seat hardware fingerprint, provider revision, target/argument vector, and capability requirements.
- Same-game two-Seat plans require one valid setup with distinct instance indices; missing/stale requirements, provider absence/offline state, ambiguous account references, high-risk approval gaps, missing hardware/capabilities, and cross-Seat exclusive hardware collisions fail closed before a plan exists.
- The strict MinGW Windows x64 target `provider_launch_plan_tests` builds successfully; focused runtime verification is recorded with the Phase 6 batch below and does not imply a real-game launch.

---

## P6-PREFLIGHT-01 — Human-readable requirements, risk, and mutation preview

**State:** CODE_COMPLETE

**Goal**

Translate the immutable plan into a normal-user summary first and an Expert technical detail view second.

**Depends on**

- P6-PLAN-01

**Normal UX examples**

- `Seat 2 needs a controller for this game`;
- `This same-game setup needs a separate data directory`;
- `This title is Protected / Experimental`;
- `Audio cannot be separated with the current setup`;
- `Two-player setup needs review`.

Technical backend/device path/plan hash details remain expandable diagnostics.

**Done when**

Every plan-blocking requirement and user-approved mutation/risk has a clear user message and deterministic expert detail without exposing secrets.

**Implementation evidence — 2026-08-28**

- `buildSummary` converts every plan issue into a normal-user blocking message plus bounded deterministic Expert detail, and emits visible requirement/setup/protection messages for successful plans.
- Mutation previews are typed (`CreateDirectory`, config/device/controller/audio/display routing, or another explicitly approved kind), bounded, deterministically ordered, and carry only an opaque mutation ID/Seat/kind/approval state rather than arbitrary payload text.
- Missing approval, duplicate/invalid mutation identity, or an over-bound mutation set prevents activation. Protected/Experimental paths remain visibly warned after approval.
- `plan_preflight_tests` builds under strict warnings and its focused runtime test passed in the local Windows x64 build.

---

## P6-PROFILE-01 — Two-player setup validator and editor model

**State:** CODE_COMPLETE

**Goal**

Make same-game/two-instance configuration a typed reusable `TwoPlayerSetup` with both automatic-generation and guided-manual edit paths.

**Depends on**

- P6-SCHEMA-01
- P6-PLAN-01

**Setup fields may include**

- exact Game/provider/version match/provenance;
- per-instance data/config directories;
- args/environment/working directories;
- provider account references where supported;
- bounded start order/waits;
- process/window matching;
- input/controller/audio/display requirements;
- known limitations/protection state;
- evidence references.

**Automatic path**

- inspect allowed local metadata read-only;
- generate a candidate setup;
- validate the candidate;
- show intended mutations before applying;
- never silently edit game/provider files outside declared approved paths.

**Manual path**

- expose typed fields and tests;
- validate continuously;
- preserve previous valid setup until Save commits transactionally;
- do not grant arbitrary script execution by default.

**Done when**

Both an automatically generated fixture setup and a manually edited fixture setup compile into the same validated runtime contract, with all invalid/unsafe combinations rejected.

**Implementation evidence — 2026-08-28**

- `generateCandidate` produces a validated two-instance setup from already-discovered Game metadata without touching the filesystem; intended data-root creation is emitted only as typed mutation intent.
- `SetupEditor` keeps committed/draft state separate and replaces the committed setup only after complete validation. Relative Windows paths, stale Game/compatibility references, shared explicit data roots, invalid instance indices, and malformed schema fail closed.
- Automatic and guided-manual fixture paths converge on the same `TwoPlayerSetup` and the same two-Seat `RuntimeSessionSelection`; the result is cross-validated through the stable profile schema.
- `two_player_setup_editor_tests` builds under strict Windows x64 MinGW warnings; focused execution is included in the Phase 6 batch check.

---

## P6-UI-01 — Game library, Player, Seat, and two-player setup UI

**State:** READY

**Goal**

Implement the normal product flow without exposing internal schema jargon.

**Depends on**

- P6-CATALOG-01
- P6-PROFILE-01
- P6-PREFLIGHT-01

**Primary flow**

```text
Games
 -> choose title
 -> Seat 1 / Seat 2 / Both
 -> choose Player(s)
 -> if same game: resolve/create Two-player setup
 -> preflight only necessary requirements/warnings
 -> Play
```

**Player UI**

- create/rename/remove lightweight local Player;
- optional avatar;
- recent games/Seat preference;
- provider account reference selection where supported;
- never ask HydraSeat to store a provider password.

**Seat settings**

Hardware configuration remains separately accessible and can contain unset items. `Set later` is valid until a selected game actually requires the device.

**Done when**

A non-developer can discover/add a game, create two Players, select both Seats, resolve same/different-game flows, and produce a validated Play plan without editing JSON.

---

## P6-CLI-01 — Expert catalog/setup/plan command-line tools

**State:** CODE_COMPLETE

**Goal**

Provide deterministic diagnostic/admin tooling without making CLI the normal product workflow.

**Depends on**

- P6-PLAN-01
- P6-PROFILE-01

**Commands**

Read/list/validate/export catalog, Player metadata, TwoPlayerSetup, and compiled plan in human/JSON forms with redaction.

**Done when**

CLI output round-trips through stable schemas, never exposes credentials, and is sufficient for issue diagnostics/CI fixtures.

**Implementation evidence — 2026-08-28**

- `hydraseat_profilectl` implements bounded `list`, `validate`, and `export` commands for Game, Player, TwoPlayerSetup, and compiled-plan diagnostic snapshot files in human or JSON form.
- Game/Setup JSON reuses the stable profile schema directly. Player JSON is copied to a schema-valid redacted document before encoding, so provider account-reference values never appear in human or JSON output.
- Compiled plans convert to a versioned stable diagnostic snapshot that records only whether an account reference was selected, never its opaque value. The snapshot has strict bounded encode/decode, Unicode validation, malformed/trailing-data rejection, and human/JSON renderers.
- `profile_cli_tests` and the `hydraseat_profilectl` executable build under strict Windows x64 MinGW warnings; the focused CLI test passes.

---

## P6-IMPORT-01 — Portable import/export, provenance, and redaction

**State:** CODE_COMPLETE

**Goal**

Allow users/community to share setup knowledge without sharing local secrets or machine-specific identity blindly.

**Depends on**

- P6-SCHEMA-01
- P6-PROFILE-01

**Requirements**

- versioned package/schema;
- source/provenance metadata;
- no credentials/tokens/cookies;
- no Player display names by default;
- personal absolute paths redacted or represented with typed variables;
- device identities remapped through explicit local selection;
- imported setup is validated before use;
- imported data cannot silently execute arbitrary code/download binaries.

**Done when**

A setup can be exported, privacy-reviewed, imported on another fixture machine, remapped, validated, and compiled without exposing the source machine's private data.

**Implementation evidence — 2026-08-28**

- `SetupPackage` is a bounded version-1 portable envelope carrying provenance plus exactly one redacted `TwoPlayerSetup`; Player identity, device identity, authentication material, scripts, and binaries are not representable.
- Every source working directory/data root is replaced with a typed `${...}` variable before export. Import requires an explicit unique local binding for every declared variable, rejects unexpected bindings, and re-runs the normal setup validator before committing output.
- Strict length-prefixed encode/decode rejects unsupported versions, malformed lengths, duplicate variable identity/fields, unredacted local paths, trailing bytes, missing remaps, and relative remaps transactionally.
- `setup_package_tests` builds under strict Windows x64 MinGW warnings; roundtrip/privacy/malformed/remap cases are included in the focused Phase 6 check.

---

## P6-REG-01 — Game/provider/setup regression fixture suite

**State:** CODE_COMPLETE

**Goal**

Prevent provider metadata and setup-schema changes from silently breaking known game-library/two-player behavior.

**Depends on**

- P6-IMPORT-01

**Corpus**

- multiple provider fixtures;
- duplicate/moved/uninstalled games;
- Player moves between Seats;
- same-game auto/manual setup fixtures;
- provider offline/missing cases;
- protection metadata;
- malformed/imported package cases;
- Unicode/path edge cases.

**Done when**

The fixture corpus runs deterministically on CI and any intentional behavior/schema change requires an explicit migration/update.

**Implementation evidence — 2026-08-28**

- `phase6_regression_tests` spans provider-neutral catalog reconciliation, multiple provider identities, duplicate/path-normalized/moved/stale candidates, Unicode titles, Player/Game Seat moves, provider missing/offline state, same-game automatic/manual setup convergence, portable import/remap/malformed input, and Protected/Experimental approval behavior.
- The new corpus exposed a real P6-CATALOG-01 tie-break defect: semantically equivalent path spellings could leave representative display metadata input-order-dependent. `canonicalCandidateKey` now includes deterministic display-spelling tie-break fields while retaining normalized path identity.
- After that correction the focused regression executable passes deterministically on the local Windows x64 MinGW build.

---

## P6-CLOSE-01 — Phase 6 closure

**State:** BLOCKED

**Goal**

Verify the repeatable game-first data/model layer, including one lawful same-title real demonstration.

**Depends on**

- P6-UI-01
- P6-REG-01

**Required acceptance**

- separate Seat/Player/Game/TwoPlayerSetup persistence;
- Player swaps Seats cleanly;
- automatic installed-game discovery plus manual EXE fallback;
- no HydraSeat credential storage;
- same-game setup auto path and guided manual path;
- one real same-title/two-instance run where game/provider rules permit it;
- one Seat instance can exit/change while the other continues under D-042;
- import/export privacy review;
- x64/x86/provider regression tests;
- dedicated Phase-close review.

**Done when**

A user can repeatably launch different or lawfully same games through the Game/Player/Seat model without developer JSON editing and without widening HydraSeat into a provider/DRM bypass tool.
