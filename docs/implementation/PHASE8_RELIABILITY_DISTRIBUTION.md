# Phase 8 — Reliability, Installer, Least Privilege, Offline Operation, and Updates

## Phase objective

Make HydraSeat usable by a non-developer on a normal Windows machine without requiring Visual Studio/CMake, permanent administrator mode, mandatory Internet access, or manual recovery scripts.

Selected recovery packets were implemented early because Phase 3 needed them before risky input experiments. They remain Phase 8 ownership even though their evidence already exists.

## Phase exit gate

Phase 8 closes only when:

1. watchdog, crash journal, and emergency reset remain independently validated;
2. normal UI/runtime operates without elevation whenever Windows permits;
3. any privileged broker has a narrow typed allowlist and cannot run arbitrary commands;
4. a real installer/repair/uninstaller installs the supported v1 runtime and removes only HydraSeat-owned persistent state;
5. first-run Seat setup is optional and supports `Set later`;
6. ordinary Windows is verified after uninstall/failed install/rollback;
7. program/runtime/driver updates require user approval and staged rollback/health verification;
8. compatibility/setup catalog updates are a separate optional data path and local cache works offline;
9. core Seat/Player/game/setup/runtime/recovery workflows work with network disabled;
10. diagnostics are privacy-redacted;
11. reboot/logon/fault/soak campaign passes;
12. Phase-close verification passes.

---

## P8-WATCH-01 — Independent watchdog lease and rollback protocol

**State:** VALIDATED

**Goal**

Keep an independent process able to execute only predeclared bounded rollback actions when the exact runtime owner dies/stalls.

**Depends on**

- P3-IPC-01

**Evidence**

Fork PR #21 run `32919928489` validated exact implementation head `dceaab9` on native Windows x64/x86 plus Gate C cross-architecture. P3-REC-01 later validated the integration/recovery ordering.

**Security boundary**

- inherited/capability-secured control channel;
- exact runtime process identity, not process-name kill;
- bounded versioned rollback manifest;
- no arbitrary command/path execution surface;
- clean disarm re-verifies rollback/postconditions.

**Done when**

Independent host-death/lease-expiry process tests and integrated recovery prove bounded exact-owner cleanup with no arbitrary execution capability.

---

## P8-RESET-01 — Emergency reset CLI

**State:** VALIDATED

**Goal**

Provide `hydra_reset.exe` as an independent user/recovery path when the normal UI/host cannot complete rollback.

**Depends on**

- P8-WATCH-01
- P8-JOURNAL-01

**Evidence**

Fork PR #25 run `33050902127` validates exact implementation head `3438301` across Windows x64/x86, Gate C cross-architecture, and P3-E regression. Local real scheduled-task launch also returned success with clean postconditions and zero remaining reset processes.

**Security boundary**

- exact registered runtime owner identity;
- trusted bounded rollback manifest;
- pre-acquired/revalidated exact process objects where required;
- no process-name sweep, arbitrary command/path, user-profile deletion, or broad registry/device reset.

**Done when**

The independent reset path can recover declared HydraSeat-owned state and proves exact ownership/postconditions without collateral mutation.

---

## P8-JOURNAL-01 — Crash journal and safe-mode marker

**State:** VALIDATED

**Goal**

Persist minimal durable evidence that risky activation/recovery was or was not completed so startup never silently reactivates after ambiguous failure.

**Depends on**

- P8-WATCH-01

**Evidence**

Fork PR #22 run `32947110442` validated exact implementation head `2b42d9a` on native x64/x86 and Gate C cross-architecture; P3-REC-01 validated real integration including sign-out/restart behavior.

**Privacy/security boundary**

- no raw typed input, credentials, arbitrary commands, or recovery instruction program in the journal;
- bounded fixed-width/versioned correlation/transition evidence;
- corrupt/incomplete state enters safe mode;
- safe-mode clearing requires independently verified rollback.

**Done when**

Interrupted/corrupt/non-clean startup fixtures reliably prevent unsafe automatic activation and clean recovery produces durable verified terminal state.

---

## P8-PRIV-01 — Narrow elevated privilege broker

**State:** BLOCKED

**Goal**

Keep normal `HydraSeat.exe`, Seat Launcher, game/provider discovery, and ordinary host operations unelevated; elevate only narrowly defined operations that actually require administrator rights.

**Depends on**

- P8-WATCH-01
- P8-JOURNAL-01
- P4-CLOSE-01

**Possible privileged operations**

- installer/repair/uninstaller system locations/services;
- optional driver/service install/configuration;
- explicitly documented device/system mutation that Windows restricts;
- specific recovery actions.

**Invariants**

- typed versioned allowlist only;
- same-user/request authentication and correlation;
- no shell/PowerShell/cmd/general process execution request;
- no arbitrary file/registry/device path write;
- all mutable operations include capture/verify/rollback;
- normal game-selection/Play path does not relaunch the main UI elevated.

**Done when**

Privilege-boundary tests prove normal flows stay standard-user and malformed/out-of-scope broker requests cannot be converted into general administrator execution.

---

## P8-BOOT-01 — User-approved background startup and lifecycle

**State:** BLOCKED

**Goal**

Support explicit runtime startup modes without surprising system-wide mutation.

**Depends on**

- P8-PRIV-01
- P4-CTRL-02

**Modes**

- `Manual`;
- `BackgroundIdle`;
- `AutoActivateValidatedSession` only for one explicitly selected previously validated two-Seat configuration and only after safe preflight.

**Invariants**

- auto-activation never runs after unsafe journal/safe-mode/topology/capability/recovery preflight;
- failed automatic preflight stays Idle;
- user can disable startup mode;
- background host/tray/UI roles remain separate;
- startup does not require a visible admin console.

**Done when**

Logon/reboot tests prove all three user-selected modes, safe fallback, disable/uninstall cleanup, and no duplicate host processes.

---

## P8-TRUST-01 — Optional component and data artifact trust policy

**State:** CODE_COMPLETE

**Goal**

Define how optional binaries/drivers/provider helpers and signed/versioned data catalogs are identified before use.

**Depends on**

- P6-CLOSE-01

**Requirements**

- artifact type/version/architecture;
- expected cryptographic hash/signature/trust metadata;
- source/provenance/license/redistribution metadata;
- capability scope;
- install/restart/recovery requirements;
- no silent binary execution from a community setup reference.

Data-only compatibility/setup catalogs and executable components remain different trust classes.

**Done when**

Tampered, unknown-version, wrong-architecture, untrusted, and policy-disallowed artifacts are rejected deterministically while core physical-display/offline operation remains functional with optional components absent.

**Implementation evidence — 2026-08-29**

- `hydra_artifact_trust` separates data-only catalogs/setup packages from executable/driver/provider-helper artifacts. Data uses exact hash/provenance/license/capability policy; executable artifacts additionally require exact architecture, trusted signing (or a narrow explicit development exception), and a recovery plan for install/restart paths.
- Data-only manifests cannot acquire install/restart/development flags or executable capability, and optional artifact absence is explicitly non-fatal to offline/core operation. Tampered hash, stale version, wrong architecture, unknown publisher, unapproved source/capability, missing redistribution permission, and missing recovery policy fail closed.
- Focused `ArtifactTrustTests` pass on the local Windows x64 MinGW build. Production signing-key handling and installer/reboot acceptance remain later/manual gates.

---

## P8-SIGN-01 — Code and driver signing pipeline

**State:** BLOCKED

**Goal**

Prepare production signing/release provenance for executable/installer/driver artifacts that require it.

**Depends on**

- P8-TRUST-01

**Invariants**

- signing keys never enter repository/CI logs/artifacts;
- unsigned development builds are visibly development builds;
- release verification can identify publisher/hash/version;
- no signature check is weakened merely to make an optional component load.

**Done when**

Release-candidate artifacts can be built/signed/verified through a documented secure pipeline or the release scope explicitly excludes any artifact type that cannot meet the requirement.

---

## P8-INST-01 — Reversible Windows installer, repair, and uninstaller

**State:** BLOCKED

**Goal**

Replace the developer build workflow with a real end-user installation contract.

**Depends on**

- P8-PRIV-01
- P8-TRUST-01
- P7-CLOSE-01

**Installer responsibilities**

- supported Windows/architecture/prerequisite check;
- install core HydraSeat UI/host/watchdog/reset/runtime files;
- install optional elevated driver/service components only when selected/required;
- create/startup entries according to explicit user choice;
- offer the optional first-run two-Seat setup wizard;
- allow `Skip` / `Set later` for Seat device categories;
- register repair/uninstall;
- keep logs privacy-bounded;
- handle partial failure transactionally.

**Uninstaller responsibilities**

- stop/return to ordinary Windows first;
- verify no active HydraSeat-owned risky state;
- remove only HydraSeat-owned files/services/tasks/config according to declared retention choice;
- leave unrelated games/provider data/credentials untouched;
- verify ordinary Windows input/display behavior.

**Manual acceptance**

Clean-machine install, repair, uninstall, interrupted install, missing optional component, UAC cancel, reboot, and post-uninstall checks.

**Done when**

A clean supported Windows machine can install/use/repair/uninstall HydraSeat without Visual Studio/CMake and returns to ordinary Windows with no unexpected owned residue.

---

## P8-UPD-01 — User-approved program/runtime update with staged rollback

**State:** BLOCKED

**Goal**

Update HydraSeat executables/runtime/installer/optional components only after clear user approval and trust validation.

**Depends on**

- P8-INST-01
- P8-SIGN-01

**Flow**

```text
check metadata
 -> show available version/change/restart implications
 -> user approves download/install
 -> verify version/hash/signature/trust
 -> stage
 -> ensure safe inactive/rollback state
 -> apply
 -> health check
 -> commit OR rollback previous version
```

**Invariants**

- no forced core update merely because a new version exists;
- an older working installed version remains usable where its local data/profile formats allow it;
- update cannot occur over an unsafe active mutation state;
- failed update restores previous version/config compatibility path;
- compatibility-data refresh is not implemented by silently replacing program binaries.

**Done when**

Upgrade, downgrade/rollback fixture, interrupted/tampered update, active-session refusal, UAC cancel, and post-update health tests pass.

---

## P8-DATA-01 — Optional compatibility/setup catalog refresh and offline cache

**State:** CODE_COMPLETE

**Goal**

Keep small, frequently changing compatibility/setup knowledge separate from HydraSeat program updates and make Internet use optional.

**Depends on**

- P8-TRUST-01
- P6-IMPORT-01

**Flow**

- local bundled/cache catalog always readable offline;
- optional configured update source checks versioned metadata;
- download is data-only by default;
- validate schema/hash/trust/provenance;
- stage and atomically replace cache;
- user can disable automatic checks/downloads;
- failed refresh preserves last valid cache;
- community setup entries still run full local validation/preflight.

**Invariants**

- no mandatory account/network connection;
- catalog entry cannot silently download/execute a binary or script;
- catalog staleness is visible but does not brick existing local setups;
- compatibility percentage/report data is evidence, not executable authority.

**Done when**

Network-off, stale-cache, corrupt/tampered download, update-disabled, first-download, and rollback tests prove local functionality remains available independently from the catalog service/source.

**Implementation evidence — 2026-08-29**

- `CatalogCacheModel` is a no-I/O state machine for optional data refresh. A loader/network layer may supply an observed artifact, but offline, refresh-disabled, download-disabled, source-missing, tampered, stale, or failed refresh paths never replace the last valid local cache.
- First install/update uses the P8 data-only trust class, exact expected/observed SHA-256, source/license/capability policy, and monotonic revision. Explicit rollback restores the previous trusted cache; no cache + network-off remains a valid non-fatal core state.
- Focused `CatalogCacheTests` pass. Real network transport/publication infrastructure is not required by this packet and remains separate from core offline operation.

---

## P8-DIAG-01 — Redacted diagnostic/support bundle

**State:** BLOCKED

**Goal**

Produce useful issue evidence while enforcing the privacy rules needed for later community reporting/support.

**Depends on**

- P5-MET-01
- P6-IMPORT-01
- P8-JOURNAL-01

**Default bundle excludes**

- passwords/tokens/cookies;
- raw typed text;
- Player display names unless explicitly opted in;
- personal absolute paths where avoidable;
- unrelated process data;
- unnecessary stable device serials/account IDs.

**Done when**

Deterministic redaction tests and a human preview/export flow show exactly what will be shared, with sensitive fixtures removed or blocked.

---

## P8-SOAK-01 — Reliability, reboot, offline, and fault-injection campaign

**State:** BLOCKED

**Goal**

Run the complete installed product through long/repeated failure and lifecycle cases before ecosystem/release hardening.

**Depends on**

- P8-INST-01
- P8-UPD-01
- P8-DATA-01
- P8-DIAG-01
- P8-BOOT-01

**Campaign**

- repeated two-Seat start/Seat-stop/restart/global-return cycles;
- UI/Seat UI/target/host crash cases;
- sign-out/restart/shutdown;
- device/display/audio/controller reconnect;
- network unavailable during catalog/update checks;
- installer repair/uninstall;
- program update success/failure/rollback;
- disk-full/permission-denied/corrupt state fixtures;
- watchdog/reset drills;
- long active gaming soak with resource monitoring.

**Done when**

The declared installed-product reliability campaign completes with no unexplained cross-Seat bleed, persistent risky state, orphan HydraSeat processes, or mandatory-network dependency.

---

## P8-CLOSE-01 — Phase 8 closure

**State:** BLOCKED

**Goal**

Verify that HydraSeat has become a recoverable, installable, least-privilege, offline-first Windows product rather than a developer build.

**Depends on**

- P8-SOAK-01
- P8-UPD-01
- P8-DATA-01

**Verify**

- watchdog/journal/reset remain valid;
- normal flow unelevated;
- narrow privilege broker;
- clean install/repair/uninstall;
- optional first-run Seat setup;
- user-approved program updates;
- separately refreshable/disableable data catalog;
- offline core operation;
- redacted diagnostics;
- reboot/fault/soak evidence;
- Phase-close review.

**Done when**

A clean-machine non-developer can install and use HydraSeat offline, recover from declared failures, decide when program updates occur, and uninstall back to ordinary Windows safely.