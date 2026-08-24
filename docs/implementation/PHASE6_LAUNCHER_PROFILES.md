# Phase 6 — Launcher, Profile Manager, and Repeatable Session Plans

## Phase objective

Turn the MVP's hand-authored configurations into repeatable, validated, provider-aware launch profiles. A user should select a Seat composition and target profile, preview the exact runtime plan and risks, start it, and later reproduce or export the result without hidden machine-specific assumptions.

## Phase exit gate

Phase 6 is complete when:

1. Seat, target, compatibility, backend, and session schemas are versioned and migratable;
2. custom executables and at least one provider launcher use the same launch-plan interface;
3. process/child/window/adapter/audio/display policies are represented as typed profile fields;
4. preflight displays exact required/optional/missing capabilities and mutations;
5. profile editor validates before saving;
6. import/export is deterministic and secrets/private local paths are handled explicitly;
7. host launches from immutable plan hashes and records the profile version;
8. provider update/path changes degrade predictably;
9. CLI and UI use the same validation/host protocol;
10. compatibility fixtures and migration regression tests pass.

## Dependency graph

```text
P5-CLOSE-01
   |
   +-> P6-SCHEMA-01 -> P6-MIG-01
   |          |
   |          +-> P6-PROFILE-01 -> P6-UI-01
   |
   +-> P6-CATALOG-01 -> P6-PROV-01 -> P6-PROV-02 / P6-PROV-03
   |
   +-> P6-PLAN-01 -> P6-PREFLIGHT-01

schemas + providers + plan -> P6-CLI-01 -> P6-IMPORT-01
all -> P6-REG-01 -> P6-CLOSE-01
```

---

## P6-SCHEMA-01 — Versioned profile schema family

**State:** BLOCKED

**Goal**

Define separate, composable schemas instead of one growing `workspace_config.json` object.

**Depends on**

- P5 launch-plan contracts
- D-012

**Create/modify**

- `schemas/seat-profile-v3.schema.json`
- `schemas/target-profile-v1.schema.json`
- `schemas/compatibility-profile-v1.schema.json`
- `schemas/session-profile-v1.schema.json`
- `include/hydra/profile_model.hpp`
- `src/profile_model.cpp`
- schema fixtures/tests.

**Schema separation**

### Seat profile

- Seat name/ID;
- display group and primary display;
- input/controller/audio assignments;
- shell preferences;
- optional/shared resources;
- no process handles or runtime-only state.

### Target profile

- executable/provider/application identity;
- arguments, environment, working directory;
- architecture and child-process expectations;
- window selection/placement policy;
- audio/controller/display target preferences;
- no machine secret.

### Compatibility profile

- required/optional capabilities;
- backend preference/deny rules;
- startup shim/hook API set;
- input/controller/window/namespace behavior;
- anti-cheat/protection declaration;
- support level and evidence metadata.

### Session profile

- references one Seat profile per Seat and one target/compatibility combination;
- stores one `managementSeatId` for whole-machine controls, defaulting deterministically to Seat 1 when valid;
- start order/dependencies;
- degraded-mode policy;
- rollback policy;
- diagnostic/trace policy.

**Invariants**

- stable IDs/references, not duplicate embedded objects where identity matters;
- unknown required field/version rejected;
- unknown optional extension preserved or rejected by declared policy;
- runtime handles/PIDs/HWNDs never persisted;
- local paths have portability metadata;
- profile hash is canonical/deterministic.

**Automated tests**

- valid/invalid/minimal/maximal fixtures;
- duplicate IDs/references/cycles;
- future/old versions;
- canonical serialization/hash;
- Unicode and path edge cases;
- no secret/runtime fields.

**Done when**

All production planning consumes typed schema models and the old file is read only through migration.

**Suggested commit**

`feat: implement P6-SCHEMA-01 profile schema family`

---

## P6-MIG-01 — Transactional profile migration and backup

**State:** BLOCKED

**Goal**

Migrate current Seat profiles and future schema versions without data loss.

**Depends on**

- P6-SCHEMA-01

**Create/modify**

- `include/hydra/profile_migration.hpp`
- `src/profile_migration.cpp`
- migration CLI/subcommand;
- fixtures for every supported source version;
- backup/restore tests.

**Implementation skeleton**

1. detect schema/version without mutating;
2. parse into version-specific source type;
3. migrate one version step at a time;
4. validate destination completely;
5. write temp file, fsync/close where applicable, atomically replace;
6. retain timestamped backup and migration report;
7. never auto-delete unknown original data;
8. support dry-run and export-only migration.

**Invariants**

- failed migration leaves original unchanged;
- repeated migration is idempotent;
- backup path and hash recorded;
- user-visible warnings for dropped/unsupported fields;
- no silent default that changes ownership/security behavior.

**Automated tests**

- every source fixture;
- malformed/partial file;
- write/replace failure;
- interrupted migration simulation;
- backup restore;
- deterministic output.

**Done when**

Existing `workspace_config.json` can be converted safely and old versions remain covered by fixtures.

**Suggested commit**

`feat: implement P6-MIG-01 profile migration`

---

## P6-CATALOG-01 — Provider-neutral application catalog

**State:** BLOCKED

**Goal**

Represent installed/known applications without coupling profiles directly to Steam/Epic/EA/GOG internals.

**Depends on**

- P6-SCHEMA-01

**Create/modify**

- `include/hydra/application_catalog.hpp`
- `src/application_catalog.cpp`
- `include/hydra/application_identity.hpp`
- catalog storage/cache and tests.

**Core types**

```cpp
struct ApplicationIdentity;
struct ApplicationInstallation;
struct ApplicationLaunchCandidate;
struct ApplicationCatalogSnapshot;
```

**Identity fields**

- provider ID and provider-specific application ID;
- executable identity/path candidates;
- display title/metadata;
- architecture where known;
- install root and manifest source;
- last verified version/hash/time;
- confidence and stale state.

**Invariants**

- title/friendly name is not identity;
- catalog discovery is read-only;
- provider metadata is untrusted input and bounded;
- missing provider/client does not break custom executable profiles;
- stale install is visible.

**Automated tests**

- duplicate titles/installs;
- moved/uninstalled app;
- malformed provider metadata;
- multiple libraries/drives;
- deterministic catalog merge.

**Done when**

Profiles reference provider-neutral application identities with resolved launch candidates.

**Suggested commit**

`feat: implement P6-CATALOG-01 application catalog`

---

## P6-PROV-01 — Launcher provider adapter contract

**State:** BLOCKED

**Goal**

Define lawful, testable provider integrations behind one interface.

**Depends on**

- P6-CATALOG-01
- P5 activation transaction

**Create/modify**

- `include/hydra/launcher_provider.hpp`
- `src/launcher_provider_registry.cpp`
- fake provider and tests;
- provider descriptor/profile fields.

**Contract**

```cpp
struct ProviderDescriptor;
struct ProviderProbeResult;
struct ProviderLaunchRequest;
struct ProviderLaunchResult;
class ILauncherProvider;
```

Operations:

- read-only availability/version probe;
- discover installations/applications;
- resolve launch command/protocol;
- identify spawned process candidates;
- cancel/cleanup only owned launch work;
- provide diagnostics and limitations.

**Invariants**

- no credential/token extraction;
- no launcher authentication bypass;
- no arbitrary command interpolation;
- process ownership validated after provider launch;
- unsupported provider version fails closed;
- provider adapter cannot mutate Seat/runtime directly.

**Automated tests**

- fake provider success/failure/timeouts;
- multiple candidate processes;
- provider client absent/update;
- malicious metadata escaping;
- cancel and no orphan helper.

**Done when**

Custom executable and future providers can produce the same `TargetLaunchPlan`.

**Suggested commit**

`feat: implement P6-PROV-01 launcher provider interface`

---

## P6-PROV-02 — Steam provider adapter

**State:** BLOCKED

**Goal**

Implement one provider integration using documented/local metadata and normal client launch behavior.

**Depends on**

- P6-PROV-01

**Create/modify**

- `src/providers/steam_provider.cpp`
- Steam metadata parser with fixtures;
- provider tests/documentation.

**Implementation skeleton**

1. detect installed Steam client/libraries read-only;
2. parse manifests with bounded input and encoding handling;
3. map app ID to installation/executable candidates;
4. launch through normal supported client/protocol or configured executable path;
5. correlate spawned process tree with app profile;
6. handle client already running/not running/update dialog;
7. never read/store credentials.

**Invariants**

- app ID/provider ID is identity;
- launch ambiguity is explicit;
- client overlays/dialogs are classified by window policy, not globally hidden;
- Steam update/path changes degrade to catalog refresh.

**Automated/manual tests**

- parser fixtures;
- multiple libraries;
- missing/corrupt manifest;
- launch controlled non-protected test app through Steam if available;
- client update/restart behavior.

**Done when**

One Steam application launches through the standard plan/runtime path.

**Suggested commit**

`feat: implement P6-PROV-02 Steam launcher adapter`

---

## P6-PROV-03 — Epic, EA, GOG, and custom provider packets

**State:** BLOCKED / SPLIT BEFORE IMPLEMENTATION

**Goal**

Add providers one at a time after Steam validates the interface. This umbrella packet must be split into:

- `P6-PROV-03A` Epic;
- `P6-PROV-03B` EA;
- `P6-PROV-03C` GOG;
- `P6-PROV-03D` custom executable/script.

**Depends on**

- P6-PROV-02 interface lessons

**Rules**

- one provider per PR;
- normal supported client behavior only;
- no credentials, session tokens, DRM bypass, or launcher impersonation;
- each provider has fixtures, version probe, timeout/process correlation, and uninstall/update behavior;
- custom scripts require explicit allowlist/quoting and are never imported as trusted by default.

**Done when**

Each subpacket independently meets P6-PROV-01 contract and compatibility evidence.

---

## P6-PROV-03A — Epic launcher adapter

**State:** BLOCKED

**Goal**

Implement the Epic provider through the normal client/manifest/URI behavior available on the tested version, without credentials or DRM bypass.

**Depends on**

- P6-PROV-02

**Create/modify**

- `src/providers/epic_provider.cpp`
- sanitized manifest/installation fixtures;
- provider version/launch/process-correlation tests.

**Implementation skeleton**

1. Probe installed client and supported metadata locations read-only.
2. Parse bounded manifest data into `ApplicationCatalogSnapshot`.
3. Resolve one normal launch candidate through P6-PROV-01.
4. Correlate the resulting target process tree.
5. Handle absent/updating client and moved installation.
6. Record exact supported client/version assumptions.

**Invariants**

- no authentication/session token access;
- provider-specific metadata never grants arbitrary command execution;
- unknown client/manifest version is unsupported;
- one provider failure cannot mutate another provider/catalog.

**Done when**

One controlled non-protected Epic application follows the same compiled plan/runtime path as custom/Steam targets.

**Suggested commit**

`feat: implement P6-PROV-03A Epic adapter`

---

## P6-PROV-03B — EA launcher adapter

**State:** BLOCKED

**Goal**

Implement the EA provider using normal supported client launch behavior and bounded local discovery.

**Depends on**

- P6-PROV-02

**Create/modify**

- `src/providers/ea_provider.cpp`
- discovery/launch fixtures and tests.

**Implementation skeleton**

1. Probe client/version and declared local metadata.
2. Resolve application/install identity without scraping credentials.
3. Generate a normal client launch request.
4. Correlate launcher/child/game processes using the process policy.
5. Handle update/login-required/user-interaction states explicitly.
6. Keep unsupported/ambiguous results out of automatic launch.

**Invariants**

- no login automation, credential/token extraction, or DRM bypass;
- launcher interaction requirements are visible preflight states;
- unknown version fails closed;
- cancellation cleans only owned launch work.

**Done when**

One controlled non-protected EA application produces a verified provider launch result or an explicit user-interaction requirement.

**Suggested commit**

`feat: implement P6-PROV-03B EA adapter`

---

## P6-PROV-03C — GOG launcher adapter

**State:** BLOCKED

**Goal**

Implement GOG/Galaxy and DRM-free executable discovery while preserving provider-neutral application identity.

**Depends on**

- P6-PROV-02

**Create/modify**

- `src/providers/gog_provider.cpp`
- manifest/catalog fixtures and tests.

**Implementation skeleton**

1. Probe Galaxy and supported local metadata read-only.
2. Resolve provider application/install identity.
3. Distinguish normal Galaxy launch from an explicitly configured DRM-free executable candidate.
4. Compile both through the same target/compatibility plan.
5. Correlate process tree and handle updates/moved installs.
6. Record which launch path produced the evidence.

**Invariants**

- no silent substitution between provider and direct executable paths;
- friendly title/path alone is not identity;
- unsupported metadata version is visible;
- no user files outside declared install metadata are scanned broadly.

**Done when**

A GOG application can be cataloged/launched through an explicitly identified path with reproducible process correlation.

**Suggested commit**

`feat: implement P6-PROV-03C GOG adapter`

---

## P6-PROV-03D — Custom executable and bounded script adapter

**State:** BLOCKED

**Goal**

Support user-selected executables and carefully bounded launch wrappers without turning profiles into arbitrary unreviewed shell execution.

**Depends on**

- P6-PROV-01
- P6-SCHEMA-01

**Create/modify**

- `src/providers/custom_provider.cpp`
- argument/environment/path validation;
- optional typed wrapper action schema;
- tests.

**Implementation skeleton**

1. Resolve an explicit executable and working directory.
2. Parse arguments as an array, not a shell command string.
3. Allow bounded environment additions/removals.
4. Represent wait/delay/file-exists/process-exists actions through an allowlisted typed schema.
5. Make PowerShell/batch/arbitrary script execution expert-only or unsupported by default.
6. Correlate root/child processes and verify executable identity.

**Invariants**

- no implicit `cmd.exe /c` or shell interpolation;
- relative paths resolve against an explicit profile root;
- path traversal/reparse/quoting cases validated;
- imported untrusted profiles cannot enable arbitrary scripts;
- cleanup affects only owned processes/actions.

**Done when**

A custom controlled executable launches with Unicode paths/arguments/environment through a deterministic plan and unsafe script forms are rejected.

**Suggested commit**

`feat: implement P6-PROV-03D custom launcher`

---

## P6-PLAN-01 — Immutable provider-aware launch-plan compiler

**State:** BLOCKED

**Goal**

Compile profile references, current topology, backend inventory, provider result, and policies into one immutable session plan.

**Depends on**

- P6-SCHEMA-01
- P6-PROV-01
- P5-LAUNCH-01

**Create/modify**

- `include/hydra/launch_plan_compiler.hpp`
- `src/launch_plan_compiler.cpp`
- deterministic plan serialization/hash;
- tests.

**Compiler stages**

1. load and validate referenced profiles;
2. resolve current stable devices/displays/audio/controllers/apps;
3. inventory providers/backends/architecture/privilege/recovery;
4. run compatibility planner;
5. generate process/window/display/audio/controller/input actions;
6. generate preconditions/rollback actions;
7. canonicalize and hash the immutable plan;
8. return warnings/missing capabilities without side effects.

**Invariants**

- compilation is pure/read-only;
- same inputs produce same plan/hash;
- no runtime handle persisted in source profile;
- every mutation has rollback metadata;
- unsupported requirement blocks plan.

**Automated tests**

- deterministic hash/order;
- missing/stale app/device/backend;
- provider ambiguity;
- architecture mismatch;
- profile reference cycle;
- optional capability warning;
- no side effects.

**Done when**

Host activation consumes only compiled plans, not scattered profile reads.

**Suggested commit**

`feat: implement P6-PLAN-01 launch plan compiler`

---

## P6-PREFLIGHT-01 — Human-readable risk and mutation preview

**State:** BLOCKED

**Goal**

Translate a compiled plan into a precise preflight report before any mutation.

**Depends on**

- P6-PLAN-01

**Create/modify**

- `include/hydra/preflight_report.hpp`
- `src/preflight_report.cpp`
- JSON/human output;
- UI model/tests.

**Report**

- resolved Seats/targets/topology;
- selected/rejected backends;
- required injection/driver/admin/restart;
- physical cloaking/suppression scope;
- display/window/audio/controller changes;
- known unsupported/experimental behavior;
- expected child processes/windows;
- watchdog/reset readiness;
- plan hash/profile versions;
- explicit confirmation requirements.

**Invariants**

- report derives from the exact immutable plan;
- no vague “may change system settings” text when exact mutation is known;
- unsupported status cannot be confirmed away;
- risky experimental override is distinct from normal support.

**Done when**

UI/CLI show identical plan details and confirmations.

**Suggested commit**

`feat: implement P6-PREFLIGHT-01 launch preflight report`

---

## P6-PROFILE-01 — Compatibility profile validator and editor model

**State:** BLOCKED

**Goal**

Provide typed editing/validation without exposing users to arbitrary JSON as the primary workflow.

**Depends on**

- P6-SCHEMA-01
- P6-PLAN-01

**Create/modify**

- `include/hydra/profile_editor_model.hpp`
- `src/profile_editor_model.cpp`
- field metadata/help/validation rules;
- tests.

**Editor sections**

- application/provider identity;
- process/architecture/startup;
- input API/hook capability requirements;
- controller API mapping;
- window/display/fullscreen policy;
- audio requirements;
- namespace/single-instance rules;
- anti-cheat/protection status;
- backend preference/deny;
- recovery/degraded policy;
- evidence/support metadata.

**Invariants**

- editor cannot create schema-invalid profile;
- dangerous raw fields live behind expert mode and still validate;
- unsupported combinations show reasons;
- defaults are conservative/fail-closed;
- profile evidence is not editable into `Supported` without matrix validation.

**Done when**

A complete profile can be created/validated without direct JSON editing.

**Suggested commit**

`feat: implement P6-PROFILE-01 profile editor model`

---

## P6-UI-01 — Profile/catalog/session management UI

**State:** BLOCKED

**Goal**

Let users manage Seats, applications, compatibility profiles, and session presets with preview/validation.

**Depends on**

- P6-CATALOG-01
- P6-PROFILE-01
- P6-PREFLIGHT-01
- P4-IPC-01
- P4-CTRL-01/P4-CTRL-02

**Screens/workflows**

- application catalog refresh;
- target profile create/clone/edit;
- compatibility profile selection/edit;
- Seat-to-target session composition;
- Management Seat selector, default Seat 1, including its primary-display control-console placement preview;
- guided inactive monitor/input/controller/audio identify-test-assign workflow reached from `Reconfigure`;
- validation and stale-reference repair;
- plan preview/risk confirmation;
- import/export;
- compatibility evidence/limitations;
- launch/history/diagnostics.

**Invariants**

- UI never edits active immutable plan; `Reconfigure` must first complete P4-CTRL-02 verified return-to-Windows and then edit inactive configuration;
- unsaved changes/version conflict handled;
- destructive delete checks references;
- provider scan is cancelable/bounded;
- UI reflects host and disk results, not optimistic assumptions.

**Automated tests**

- view-model validation/state transitions;
- stale/missing profile references;
- provider refresh cancel/error;
- import conflict;
- DPI/accessibility/localization readiness.

**Done when**

A user can create and launch the MVP profiles without hand-editing files.

**Suggested commit**

`feat: implement P6-UI-01 profile manager UI`

---

## P6-CLI-01 — Profile/catalog/plan command-line tools

**State:** BLOCKED

**Goal**

Provide scriptable operations that share production libraries with the UI.

**Depends on**

- P6 schemas/catalog/plan/preflight

**Commands**

```text
hydra_profile validate <file>
hydra_profile migrate <file> --dry-run
hydra_profile export <session-id>
hydra_catalog refresh/list/show
hydra_plan compile <session-profile> --json
hydra_plan explain <plan-hash>
hydra_hostctl start/stop/status/export-diagnostics
```

**Invariants**

- machine JSON output stable/versioned;
- no prompts in noninteractive mode;
- exit codes distinguish invalid/unsupported/runtime/error;
- CLI cannot bypass confirmation/policy without explicit expert flag and planner policy;
- no duplicate parser/business logic.

**Done when**

CI and support workflows can reproduce UI actions headlessly.

**Suggested commit**

`feat: implement P6-CLI-01 profile and launch CLI`

---

## P6-IMPORT-01 — Portable import/export and redaction

**State:** BLOCKED

**Goal**

Share profiles/evidence without leaking machine-private data or silently binding wrong devices.

**Depends on**

- P6-SCHEMA-01
- P6-MIG-01
- P6-CLI-01

**Bundle contents**

- versioned manifest;
- selected profiles;
- optional compatibility evidence;
- dependency/provider/backend requirements;
- portable matching hints;
- redaction report;
- hashes/signature placeholder.

**Import behavior**

- validate manifest/schema/hash;
- preview conflicts and machine-specific unresolved identities;
- require user mapping for displays/input/audio/controllers;
- never auto-map by friendly name alone;
- transactional write with backup;
- distinguish trusted local, signed ecosystem, and untrusted bundle.

**Automated tests**

- deterministic export;
- redaction of usernames/local paths/tokens;
- tampered/unknown version;
- ID conflicts;
- partial mapping/cancel;
- migration during import.

**Done when**

A profile can move to another PC and clearly request identity remapping without leaking private data.

**Suggested commit**

`feat: implement P6-IMPORT-01 profile bundles`

---

## P6-REG-01 — Launcher/profile regression fixture suite

**State:** BLOCKED

**Goal**

Prevent provider/profile/schema changes from silently breaking supported configurations.

**Depends on**

- all Phase 6 implementation packets

**Create/modify**

- sanitized provider metadata fixtures;
- profile schema/migration corpus;
- compiled plan snapshots;
- fake runtime/provider integration suite;
- compatibility matrix validation.

**Coverage**

- old/current/future schema;
- provider client absent/update/moved library;
- application version/path change;
- x86/x64 target;
- required backend unavailable;
- user-assisted audio route;
- multi-monitor Seat;
- recovery and import/export.

**Done when**

Every supported profile/provider path has a stable regression fixture and expected plan.

**Suggested commit**

`test: implement P6-REG-01 launcher profile regressions`

---

## P6-CLOSE-01 — Phase 6 closure

**State:** BLOCKED

**Closure checklist**

- schemas/version/migrations stable;
- custom executable and at least Steam path validated;
- plan compiler/preflight immutable and side-effect-free;
- profile editor/manager/CLI share libraries;
- import/export redaction and remapping proven;
- runtime starts only compiled plan hashes;
- provider/update/path errors degrade visibly;
- regression corpus covers supported MVP profiles;
- Phase 7 receives stable session/profile/catalog APIs.

**Done when**

Phase 6 is complete and Phase 7 becomes current.

**Suggested commit**

`docs: close Phase 6 launcher and profile manager`
