# Phase 8 — Reliability, Watchdog, Privilege, Installer, and Updates

## Phase objective

Turn the development runtime into a crash-safe Windows product that starts predictably, recovers without a visible console/UAC prompt during ordinary use, can be reset independently, and installs/updates/uninstalls without leaving drivers, hooks, routes, windows, processes, or profiles in an unknown state.

Some Phase 8 packets are prerequisites for risky Phase 3–5 experiments and may be implemented early.

## Phase exit gate

Phase 8 is complete when:

1. host, watchdog, UI, shell, adapters, and optional elevated broker have explicit leases/lifetimes;
2. host or helper failure triggers bounded rollback;
3. `hydra_reset.exe` can restore safe state without the UI/host;
4. crash journal and safe mode prevent automatic reactivation after incomplete rollback;
5. startup/autostart is user-approved and silent in normal operation;
6. elevated operations use a narrow broker and explicit confirmation;
7. installer/uninstaller are reversible and preserve/export user data as chosen;
8. optional components are hash/version/license/trust verified;
9. update has signed manifest, staging, health check, and rollback;
10. clean-machine install/update/uninstall/reboot/manual tests pass;
11. long soak and fault-injection matrix meet reliability budgets.

## Dependency graph

```text
P8-WATCH-01 -> P8-RESET-01
      |
      +-> P8-JOURNAL-01 -> P8-BOOT-01
      +-> P8-PRIV-01 -> P8-INST-01 -> P8-UPD-01
      +-> P8-DIAG-01

P8-TRUST-01 -> optional driver/adapter packages and P8-UPD-01
P8-SIGN-01 -> P8-INST-01/P8-UPD-01 production release path
all -> P8-SOAK-01 -> P8-CLOSE-01
```

---

## P8-WATCH-01 — Independent watchdog lease and rollback protocol

**State:** CODE_COMPLETE

**Goal**

Create `hydra_watchdog.exe` that can detect runtime/helper failure and execute a pre-registered, bounded rollback plan independently of the UI and host.

**Depends on**

- existing Gate C protocol/lifecycle lessons
- D-020

**Create/modify**

- `include/hydra/watchdog_protocol.hpp`
- `src/watchdog_protocol.cpp`
- `include/hydra/rollback_registry.hpp`
- `src/rollback_registry.cpp`
- `src/watchdog_main.cpp`
- `hydra_watchdog.exe`;
- process/fault tests.

**Core contracts**

```cpp
struct WatchdogLease;
struct RollbackActionDescriptor;
struct RollbackPlanManifest;
struct WatchdogStatus;
```

**Implementation skeleton**

1. host starts watchdog before risky activation and establishes a versioned lease;
2. host sends a bounded authenticated/correlated rollback manifest containing only allowlisted action types;
3. host renews lease on a dedicated path not blocked by UI/input queues;
4. watchdog monitors host/process handles and expiry;
5. on clean stop, host requests disarm after verified rollback;
6. on expiry/death, watchdog executes idempotent rollback actions and writes result;
7. watchdog never interprets arbitrary shell commands or file paths as actions;
8. unresolved action yields `RecoveryRequired`.

**Allowlisted initial actions**

- terminate owned helper/target process by validated identity;
- close host-owned named-pipe/session handles;
- clear session-scoped optional backend state through a narrow broker;
- release documented cursor/clip/window overlays;
- restore captured display/audio/window/profile state via versioned snapshots;
- write safe-mode/crash result.

**Invariants**

- watchdog is outside host Job Object;
- rollback manifest is bounded/versioned and cannot run arbitrary code;
- action identity includes generation/session/process creation data;
- repeated rollback safe;
- lease timeout balances false positives and recovery time;
- watchdog cannot activate a Seat, only observe/rollback.

**Automated tests**

- host clean disarm;
- host killed/hung/lease stops;
- malformed/unauthorized action;
- partial rollback failure;
- watchdog restart/reload journal;
- no orphan controlled process;
- bounded completion timeout.

**Done when**

Gate C can register a harmless rollback plan, kill the host, and prove watchdog cleanup without UI participation.

**P8-WATCH-01 implementation note (2026-08-26)**

- protocol v1 uses a fixed 24-byte little-endian frame header, a 4096-byte payload ceiling, nonzero monotonic control sequences, a 128-bit session identity, explicit lease generation, bounded lease/action/total timeouts, and at most 32 rollback actions;
- the manifest contains only fixed-width action enums and identities. It has no command string, executable path, registry path, file path, or arbitrary payload field, so the watchdog cannot become a shell/command runner;
- the local launch contract authenticates the control plane with Windows handle capabilities: the host creates anonymous control/status pipes and must pass only the intended pipe handles through `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. Session/generation/sequence checks provide correlation. P8-WATCH-01 deliberately does not invent a cryptographic signature/key scheme for this non-nameable inherited transport; any later named or cross-trust-boundary transport must add an explicit authenticated framing design before use;
- rollback actions execute in reverse activation order. Successful/already-satisfied action IDs are remembered so retry is idempotent, and a different plan cannot replace an armed registry implicitly;
- `TerminateOwnedProcess` is the only OS-mutating executor implemented in this packet. It requires exact PID plus process-creation time and refuses watchdog/host self-targets. The other allowlisted action kinds remain typed but return `Unsupported`, causing `RecoveryRequired`, until the packet that owns that mutable subsystem supplies a narrow implementation;
- `Disarm` is not trusted as proof by itself: the watchdog re-runs the registered idempotent rollback plan before releasing the lease. Any unresolved action keeps the watchdog in `RecoveryRequired`;
- durable crash-journal persistence, safe-mode markers, emergency reset, Gate C host integration, HidHide state mutation, and production Seat activation remain outside this packet (`P8-JOURNAL-01`, `P8-RESET-01`, `P3-REC-01`, and later runtime packets). The restart/reload check here covers deterministic manifest reconstruction/idempotence only, not a persistent crash journal.

**Suggested commit**

`feat: implement P8-WATCH-01 watchdog lease and rollback`

---

## P8-RESET-01 — Emergency reset CLI

**State:** BLOCKED

**Goal**

Provide a small, independently launchable reset tool that restores known safe HydraSeat state even when UI/host/shell are broken.

**Depends on**

- P8-WATCH-01
- P8-JOURNAL-01 contract may be developed together if tightly coupled

**Create/modify**

- `src/reset_main.cpp` replacing/expanding current `reset_input.cpp`;
- `include/hydra/reset_actions.hpp`
- `src/reset_actions.cpp`
- `hydra_reset.exe`;
- tests and recovery documentation.

**Commands**

```text
hydra_reset status
hydra_reset dry-run
hydra_reset session <id>
hydra_reset all --confirm
hydra_reset safe-mode on|off
hydra_reset export-diagnostics
```

**Implementation skeleton**

1. read watchdog/crash journal and known runtime registrations;
2. display exact actions in dry run;
3. attempt normal host stop when available;
4. ask watchdog/elevated broker for allowlisted cleanup;
5. terminate only validated HydraSeat-owned processes;
6. clear overlays/routes/session state;
7. verify postconditions;
8. set `RecoveryRequired` and nonzero exit if verification fails.

**Invariants**

- no broad process-name kill;
- no arbitrary registry/file deletion;
- safe to run repeatedly;
- user profiles preserved by default;
- JSON output available for support;
- tool does not depend on Qt/main UI.

**Automated/manual tests**

- no active session;
- clean active session;
- dead host/stale journal;
- partial elevated failure;
- fake unrelated same-name process;
- repeated reset;
- emergency shortcut/task execution.

**Done when**

A user can recover from a forced host/target failure through one documented command and verification report.

**Suggested commit**

`feat: implement P8-RESET-01 emergency reset tool`

---

## P8-JOURNAL-01 — Crash journal and safe-mode marker

**State:** READY

**Goal**

Persist only the minimum state needed to detect incomplete activation/rollback and prevent unsafe automatic restart.

**Create/modify**

- `include/hydra/crash_journal.hpp`
- `src/crash_journal.cpp`
- versioned journal schema;
- host/watchdog/reset integration;
- tests.

**Journal records**

- runtime session/plan hash and generation;
- transition start/commit/rollback markers;
- prepared/applied/verified/rolled-back action IDs;
- watchdog lease identity;
- previous-state snapshot references and hashes;
- final clean/failed/recovery-required result;
- no raw input, credentials, or arbitrary process data.

**Implementation skeleton**

1. append or transactionally replace bounded journal entries;
2. flush critical transition markers before risky mutation;
3. mark clean only after postcondition verification;
4. on startup, inspect last incomplete session;
5. enter safe mode and invoke recovery rather than auto-start;
6. rotate/archive bounded history for diagnostics.

**Invariants**

- corrupted journal fails safe;
- journal alone cannot trigger arbitrary actions;
- path/permissions restrict same user/admin model;
- write failure blocks risky activation;
- safe-mode marker cleared only after verified reset.

**Automated tests**

- crash at every transition boundary;
- truncated/corrupt/future version;
- disk full/write failure;
- replay/recovery;
- rotation/retention;
- no secrets fixture.

**Done when**

Host startup can distinguish clean stop, recoverable incomplete session, and recovery-required state.

**Suggested commit**

`feat: implement P8-JOURNAL-01 crash journal`

---

## P8-PRIV-01 — Narrow elevated privilege broker

**State:** BLOCKED

**Goal**

Move administrator-only operations into a minimal broker instead of running the UI/host permanently elevated.

**Depends on**

- concrete elevated needs from HidHide/display/installer packets
- P8-WATCH-01/P8-JOURNAL-01

**Create/modify**

- `include/hydra/admin_protocol.hpp`
- `src/admin_protocol.cpp`
- `src/admin_broker_main.cpp`
- `hydra_admin_broker.exe` or service only if justified;
- policy manifest and tests.

**Allowed operation model**

Each operation has:

- fixed operation ID/version;
- bounded validated arguments;
- caller/session/plan identity;
- explicit user approval or installer context;
- captured before state;
- verify/rollback action;
- audit result.

No arbitrary command, path write, registry write, service operation, driver operation, or IOCTL passthrough.

**Invariants**

- normal UI/host remain standard user;
- broker authenticates caller/session;
- operation allowlist compiled/manifest-validated;
- no long-lived admin token exposed to clients;
- logs contain no secret;
- UAC appears only for declared setup/risky operation, not every normal launch.

**Automated/security tests**

- malformed/oversized/unknown operation;
- caller spoof/replay;
- path traversal/symlink/reparse concerns;
- partial operation rollback;
- broker crash;
- least-privilege review.

**Done when**

All elevated features use explicit broker operations and the host runs unelevated.

**Suggested commit**

`feat: implement P8-PRIV-01 elevated broker`

---

## P8-BOOT-01 — Silent user-approved startup and tray lifecycle

**State:** BLOCKED

**Goal**

Start the background runtime/watchdog predictably at user logon without console windows or repeated UAC prompts, while supporting three explicit user modes: manual launch, background-idle launch, and validated automatic Seat activation. The Management Seat control console remains available on demand in every non-safe-mode case.

**Depends on**

- P4-RUN-01/P4-IPC-01
- P4-CTRL-01/P4-CTRL-02
- P8-WATCH-01/P8-JOURNAL-01

**Create/modify**

- startup registration abstraction;
- `include/hydra/startup_mode.hpp` and `src/startup_mode.cpp` with `Manual`, `BackgroundIdle`, and `AutoActivateValidatedSession`;
- persisted startup settings for the selected validated session, `openControlConsoleAtLogon`, and safe fallback behavior;
- tray lifecycle/startup settings UI;
- startup diagnostics/CLI;
- tests/manual checklist.

**Policy options**

- `Manual`: no automatic host/session startup; launching `HydraSeat.exe` starts/connects the host/watchdog on demand and the user presses Start;
- `openControlConsoleAtLogon`: optional presentation flag that opens the Management Seat console after logon but does not change the selected runtime mode;
- `BackgroundIdle`: start host/watchdog at logon with no automatic Seat activation; keep the controller hidden unless requested and allow Start from the Management Seat;
- `AutoActivateValidatedSession`: start host/watchdog at logon and activate exactly one explicitly selected, previously validated session only after safe-mode, crash-journal, topology, capability, privilege, watchdog, and rollback preflight all pass; otherwise stay safely idle and surface the reason.

**Invariants**

- startup mode is explicit, opt-in, visible in the Management Seat console, stored transactionally, and reversible;
- no console window;
- no permanent administrator requirement;
- safe mode, incomplete crash journal, missing required hardware, changed plan hash, or failed capability/recovery preflight prevents automatic activation and leaves the host safely idle;
- duplicate host instance rejected/reconnected;
- closing `HydraSeat.exe` never stops an active session by itself;
- requesting the control console while a split session is active places it on the Management Seat primary display or the documented visible fallback;
- `Stop / Return to Windows` ends the Seat session but may leave host/watchdog idle when the selected startup mode requires background availability;
- uninstall removes startup registration.

**Automated tests**

- startup-mode schema/serialization and invalid/future values;
- Manual launch starts/connects one host on demand and never auto-activates a Seat;
- BackgroundIdle starts one hidden host/watchdog pair and remains Idle until Start;
- AutoActivateValidatedSession activates only the matching validated plan after all preflight gates pass;
- changed topology/plan hash, safe mode, stale crash journal, missing watchdog, or unavailable required backend prevents auto-activation and leaves Idle with a diagnostic reason;
- duplicate logon/startup invocations reconnect rather than creating duplicate hosts;
- closing/reopening the controller while Active does not change runtime state;
- Stop / Return to Windows leaves host/watchdog idle or exits them according to mode/user request;
- Management Seat console placement/fallback is preserved across logon and monitor changes.

**Manual acceptance**

Logon/reboot acceptance for all three runtime modes: Manual performs no automatic Seat activation; BackgroundIdle starts host/watchdog hidden and allows the Management Seat console to Start later; AutoActivateValidatedSession restores only the selected validated session; changed/missing hardware or crash-safe mode leaves the host idle. Also verify standard-user operation, no console window, no UAC in ordinary use, closing/reopening the controller, and Stop / Return to Windows without reboot.

**Done when**

A user can choose Manual, BackgroundIdle, or AutoActivateValidatedSession; boot/logon behavior matches that choice; the Management Seat can reopen the controller at any time; and Stop / Return to Windows restores ordinary use without requiring a reboot or permanently exiting the background host unless requested.

**Suggested commit**

`feat: implement P8-BOOT-01 startup lifecycle`

---

## P8-TRUST-01 — Optional component manifest, hash, and trust policy

**State:** BLOCKED

**Goal**

Validate optional adapters/drivers/tools before the host loads or executes them.

**Create/modify**

- `include/hydra/component_manifest.hpp`
- `src/component_manifest.cpp`
- manifest schema;
- signature/hash verifier;
- inventory/CLI/UI;
- tests.

**Manifest fields**

- component ID/version/type;
- architectures;
- executable/module paths relative to package root;
- SHA-256 hashes;
- publisher/signature metadata;
- license/source/origin URL metadata;
- capabilities and required privileges;
- compatible host/protocol/ABI versions;
- install/uninstall/recovery notes.

**Invariants**

- absolute/path traversal entries rejected;
- hash checked before load/execute;
- unknown/modified component disabled;
- user-supplied unsigned experimental component is explicit and cannot become `Supported` evidence;
- manifest does not grant capabilities beyond planner/adapter probe.

**Automated tests**

- valid/tampered/missing/duplicate/path traversal/future version;
- architecture mismatch;
- signature present/absent/invalid;
- component update hash change;
- deterministic inventory.

**Done when**

External display/input/audio/controller components cannot be silently replaced or loaded by path alone.

**Suggested commit**

`feat: implement P8-TRUST-01 component manifests`

---

## P8-SIGN-01 — Code and driver signing pipeline

**State:** BLOCKED

**Goal**

Define and automate signing/verification for production application artifacts and any adopted driver.

**Depends on**

- release ownership/license decision;
- P8-TRUST-01;
- driver adoption decision if applicable.

**Create/modify**

- CI release signing stage using protected secrets;
- local unsigned developer path;
- verification scripts/tests;
- certificate rotation/revocation/runbook;
- artifact provenance/SBOM hooks.

**Invariants**

- secrets never enter repository/logs/artifacts;
- PR builds are unsigned or test-signed, clearly labeled;
- release manifest hashes signed artifacts;
- runtime verifies optional production component signature/hash according to policy;
- certificate change does not silently trust arbitrary publisher.

**Manual acceptance**

SmartScreen/signature properties, clean-machine install, driver signature where applicable.

**Done when**

Release pipeline can produce verifiable artifacts without exposing signing material.

**Suggested commit**

`build: implement P8-SIGN-01 release signing pipeline`

---

## P8-INST-01 — Reversible installer and uninstaller

**State:** BLOCKED

**Goal**

Install the correct architecture/runtime artifacts, startup options, optional components, and recovery tools with predictable rollback.

**Depends on**

- stable executable layout from Phases 4–7;
- P8-PRIV-01;
- P8-TRUST-01;
- P8-SIGN-01 for production.

**Installer responsibilities**

- preflight supported Windows/architecture/prerequisites;
- install UI/host/watchdog/reset/shell/adapters/CLI/docs;
- set correct ACLs/data directories;
- optional startup registration;
- optional component selection with risk/license display;
- migrate/import profiles only with backup;
- health check before commit;
- uninstall/repair/export/reset modes.

**Invariants**

- failure rolls back installed files/registrations;
- uninstall stops/resets runtime first;
- user profile/evidence preservation choice explicit;
- optional drivers/components uninstall separately and verify cleanup;
- no PATH/system-wide change without reason;
- installer log redacted.

**Automated/manual tests**

- fresh install/repair/upgrade/uninstall;
- standard user + UAC;
- interrupted install;
- files locked/running session;
- profile preservation/delete/export;
- reboot where driver requires;
- clean Windows VM/hardware machine.

**Done when**

HydraSeat can be installed and completely removed/repaired without manual file/registry cleanup.

**Suggested commit**

`build: implement P8-INST-01 installer lifecycle`

---

## P8-UPD-01 — Signed staged update and rollback

**State:** BLOCKED

**Goal**

Update application/components/profiles safely with automatic rollback on failed health checks.

**Depends on**

- P8-JOURNAL-01
- P8-TRUST-01
- P8-SIGN-01
- P8-INST-01

**Create/modify**

- signed update manifest schema;
- updater/stager executable or installer mode;
- host quiesce/stop protocol;
- versioned backup/rollback;
- health check and tests.

**Update flow**

1. check manifest/channel manually or opt-in background check;
2. verify signature/hash/version/compatibility;
3. download/stage outside active install;
4. compile migration/update plan and show changes;
5. stop/reset session and quiesce processes;
6. swap files transactionally where possible;
7. migrate profiles/components;
8. start health check in safe mode;
9. commit or roll back files/profiles/startup registrations;
10. retain report and bounded previous version.

**Invariants**

- no update during active risky session without explicit stop;
- downgrade/migration compatibility checked;
- component/driver update independent and capability-gated;
- failed health check restores prior working version;
- update channel and telemetry are opt-in/configurable.

**Automated/manual tests**

- valid/tampered manifest;
- interrupted download/stage/swap;
- locked file;
- migration failure;
- health check failure;
- rollback after reboot-required component;
- channel switching/downgrade policy.

**Done when**

A broken test update automatically returns to the prior healthy version with profiles intact.

**Suggested commit**

`feat: implement P8-UPD-01 staged updates`

---

## P8-DIAG-01 — Diagnostic bundle and support command

**State:** BLOCKED

**Goal**

Export enough structured evidence to debug hardware/profile/runtime failures without exposing secrets/private content.

**Depends on**

- host/watchdog/journal/profiles/metrics contracts

**Create/modify**

- `include/hydra/diagnostic_bundle.hpp`
- `src/diagnostic_bundle.cpp`
- `hydra_diag.exe`;
- redaction rules/tests;
- bundle manifest schema.

**Bundle sections**

- build/Windows/architecture;
- sanitized hardware/display/audio/controller topology;
- profile/plan hashes and redacted selected fields;
- backend/component inventory/version/hash/trust;
- runtime/watchdog/journal snapshot;
- bounded event/metric/error logs;
- compatibility matrix entry reference;
- reset/rollback results;
- user-provided description.

**Invariants**

- tokens/credentials/private file content/raw typed text excluded;
- explicit preview before export;
- deterministic manifest/hash;
- size/retention bounded;
- bundle collection read-only except temporary output;
- corrupt components do not block partial bundle with errors.

**Done when**

Support can reproduce the environment/state from a redacted bundle and users can inspect contents.

**Suggested commit**

`feat: implement P8-DIAG-01 support bundles`

---

## P8-SOAK-01 — Reliability, reboot, and fault-injection campaign

**State:** BLOCKED

**Goal**

Validate product lifecycle over time rather than one successful session.

**Depends on**

- Phase 8 implementation packets;
- representative Phase 5/7 session.

**Campaign**

- 500 start/stop cycles on controlled targets;
- 100 MVP start/stop cycles;
- 24-hour idle host/watchdog/shell;
- 8-hour active two-Seat session;
- repeated UI/shell/target/host kills;
- display/input/controller/audio reconnect loops;
- logoff/reboot/startup/safe mode;
- install/update/rollback/uninstall;
- disk full/permission/log corruption simulation;
- optional component unavailable/modified.

**Metrics/pass criteria**

- zero orphan process/overlay/route after stop/reset;
- no unbounded memory/handle/thread/log growth;
- rollback within budget or explicit recovery-required;
- no silent auto-activation after crash;
- no unowned window/device mutation;
- failure and recovery evidence retained.

**Done when**

Campaign report meets defined budgets on the release reference topology.

**Suggested commit**

`test: complete P8-SOAK-01 reliability campaign`

---

## P8-CLOSE-01 — Phase 8 closure

**State:** BLOCKED

**Closure checklist**

- independent watchdog/reset/safe mode proven;
- host/adapter/shell failure cleanup proven;
- standard-user normal operation and narrow elevation proven;
- startup quiet/reversible;
- optional components trusted/versioned;
- installer/update/uninstall/rollback pass clean-machine tests;
- diagnostic bundle redaction passes;
- soak/fault campaign meets budgets;
- production artifacts signing path established;
- Phase 9 receives stable component/plugin/trust contracts.

**Done when**

Phase 8 is complete and Phase 9 becomes current.

**Suggested commit**

`docs: close Phase 8 reliability and distribution`
