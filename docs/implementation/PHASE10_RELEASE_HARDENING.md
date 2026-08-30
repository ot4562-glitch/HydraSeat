# Phase 10 — v1 Release Hardening

## Phase objective

Qualify HydraSeat v1 as a real two-player Windows gaming product for people who want to use the spare performance of one capable PC rather than buy a second complete desktop solely for simultaneous local gaming.

The release gate is the complete user journey and evidence, not a large number of officially certified games.

## v1 release gate

Release candidate/GA must prove at least:

- exactly two supported active Seats;
- clean Windows install/repair/uninstall;
- optional first-run Seat wizard with `Set later`;
- real two-display/two-input physical acceptance;
- objective tested zero-bleed evidence;
- separate controller/audio routing for the declared scenarios;
- two different real games concurrently;
- at least one lawful real same-title/two-instance demonstration where game/provider rules permit it;
- Game-first main UI and Player profiles;
- local installed-game discovery plus manual EXE fallback;
- automatic and guided manual TwoPlayerSetup paths;
- one Seat can exit/change games while the other continues;
- minimal idle Seat Launcher;
- both Seats end or explicit Return to Windows -> verified ordinary Windows restore;
- watchdog/crash/emergency recovery;
- offline core operation;
- local-first compatibility results + optional redacted community sharing;
- compatibility/setup data refresh independent from core program update;
- user-approved executable/runtime/driver updates;
- least privilege;
- measured resource/performance budgets;
- security/privacy/dependency review;
- release artifacts/checksums/SBOM/provenance as practical;
- project license/contribution terms resolved before describing the GA release as open source.

---

## P10-SCOPE-01 — Freeze supported platform and v1 product scope

**State:** CODE_COMPLETE

**Goal**

Freeze what HydraSeat v1 actually promises so release testing is finite and honest.

**Depends on**

- P9-CLOSE-01

**Freeze**

- maximum two active Seats;
- supported Windows versions/build families;
- supported CPU/GPU/architecture baseline;
- physical-display requirement and optional components;
- declared input/controller/audio compatibility paths;
- initial provider/game discovery scope;
- game-only product boundary;
- minimal Seat Launcher boundary;
- protected-title experimental policy;
- offline/network/update behavior;
- installer/elevation model.

**Explicit non-goals remain**

- N-Seat v1 support;
- general independent Windows desktops/taskbars/clipboard/wallpaper;
- VM/RDP/streaming requirement;
- universal same-game multi-instance;
- anti-cheat/DRM/account/launcher/single-instance bypass;
- anti-cheat safety certification;
- mandatory cloud account/telemetry;
- compatibility with every game.

**Done when**

One release-scope document/machine-readable matrix defines every v1 supported/experimental/deferred platform boundary and all other release packets test that same scope.

**Implementation evidence — 2026-08-29**

- `config/release-scope-v1.json` freezes the qualification matrix separately from support claims: exactly two active Seats, x64 Windows host, x64/x86 target-process compatibility, explicit Windows build families, physical-display/input/controller/audio boundaries, Steam/custom-EXE initial provider scope, same-title/protected-title policy, offline/update/privilege rules, installer guarantees, and canonical v1 non-goals.
- `release-target` explicitly means "inside release qualification" rather than "already supported". Physical zero-bleed, real-game/provider/audio/controller/display, clean-machine installer, performance, security, and RC evidence therefore remain mandatory downstream and cannot be inferred from this packet.
- `tools/validate_release_scope.py` is fail-closed on unknown/missing fields and freezes high-risk product invariants. Its self-test proves rejection of three-Seat promotion, x86 GA-host promotion, implicit Windows 10 GA promotion, virtual-display promotion, general privileged command execution, and removal of the no-bypass non-goal.
- `tools/validate_implementation_roadmap.py` now validates the release-scope matrix on every normal roadmap validation run, so later roadmap edits cannot silently drift from the frozen product boundary.
- `docs/RELEASE_SCOPE_V1.md` is the human-readable companion and records the official Microsoft platform-documentation provenance used only for the dated Windows build-family snapshot; no external source code was copied or adapted.
- Local `python.exe tools/validate_release_scope.py --self-test`, direct scope validation, and the complete implementation-roadmap validator pass. This packet defines the automated qualification contract only; P10-COMPAT-01 must re-check vendor servicing status and actual release evidence before any GA support claim.

---

## P10-PERF-01 — Performance, latency, resource, and two-workload qualification

**State:** BLOCKED

**Goal**

Measure whether sharing one PC still leaves useful gaming performance instead of merely proving functional isolation.

**Depends on**

- P10-SCOPE-01
- P5-MET-01
- P8-SOAK-01

**Measure**

- HydraSeat host/watchdog/UI/Seat UI idle and active CPU/memory/handle/thread footprint;
- input routing/receiver latency p50/p95/p99;
- queue/drop/loss;
- launch/stop/rollback times;
- two concurrent game CPU/GPU/memory pressure;
- frame-time/FPS impact where reproducibly measurable;
- audio/controller routing overhead where applicable;
- long-run growth/leak indicators.

**Product interpretation**

HydraSeat cannot promise that every PC can run two games. Release docs should explain that the benefit applies when the user's PC has sufficient headroom for the selected workloads and should expose evidence/diagnostics rather than a magical minimum-spec claim unsupported by measurements.

**Done when**

Reference hardware classes have repeatable measurements, pass/fail budgets, and clearly documented conditions under which the two-player use case remains practical.

---

## P10-COMPAT-01 — Full v1 compatibility and regression matrix

**State:** BLOCKED

**Goal**

Run the complete release matrix and publish evidence without creating a fake universal `Supported` badge.

**Depends on**

- P10-SCOPE-01
- P9-CLOSE-01

**Matrix dimensions**

- Windows/build;
- HydraSeat version;
- x64/x86 target path where relevant;
- provider/game/version;
- different-game/same-game scenario;
- input/controller/audio/display compatibility path;
- two-Seat hardware topology;
- protection state;
- install/update/offline mode;
- recovery cases.

**Required product scenarios**

- two different real non-protected games;
- at least one permitted same-title/two-instance setup;
- Player swap between Seats;
- Seat-local exit/change while other game continues;
- missing optional hardware and requirement-aware preflight;
- device/display/audio/controller reconnect;
- protected-title warning/experimental attempt path without bypass;
- offline cached catalog operation;
- clean final rollback.

**Done when**

Release evidence identifies exactly what was tested, with success/failure/sample/limitations, and no README/UI wording implies more than the matrix supports.

---

## P10-SEC-01 — Product threat model, hardening, and security review

**State:** BLOCKED

**Goal**

Review the complete installed product, especially process interposition, recovery, privilege, community data, provider integration, and update boundaries.

**Depends on**

- P10-SCOPE-01
- P9-SEC-01
- P8-CLOSE-01

**Threats**

- malformed local/provider/community metadata;
- privilege-broker abuse;
- path/DLL search/reparse attacks;
- update/catalog tamper/rollback;
- process/PID/HWND reuse;
- named-pipe/IPC spoof/replay/flood;
- adapter misuse outside declared target scope;
- community setup attempting code execution;
- diagnostic/privacy exfiltration;
- installer/uninstaller collateral modification;
- unsafe automatic activation/recovery race;
- protected-game experimentation misrepresented as bypass/safety.

**Done when**

Critical/high findings are fixed or the affected capability is removed/deferred from v1, with negative tests and a public security/reporting policy ready.

---

## P10-PRIV-01 — Privacy, data retention, and optional community-sharing policy

**State:** IN_PROGRESS

**Goal**

Make the local-first privacy model explicit and testable across logs, diagnostics, compatibility evidence, Players, provider references, and updates.

**Depends on**

- P9-CLOSE-01
- P8-DIAG-01

**Policy**

- no mandatory HydraSeat cloud account;
- no community compatibility upload by default without explicit opt-in;
- preview exact redacted JSON before sharing;
- no provider passwords/tokens/cookies in HydraSeat storage;
- no raw typed text in default traces/reports;
- no Player display names/account IDs/personal paths in community evidence by default;
- retention/rotation/delete/export behavior documented;
- optional network checks/submission can be disabled;
- local cache/game/setup/runtime continues offline.

**Done when**

Privacy fixtures, UI settings/preview/delete/export flows, and documentation agree on what is stored locally, what may leave the machine, and how the user controls it.

**Implementation progress — 2026-08-29**

- `CompatibilityShareModel` owns explicit privacy settings with community sharing disabled by default plus bounded local-result retention (default 32, maximum 64). Zero/oversized retention values fail closed rather than causing implicit deletion or unbounded growth.
- Privacy settings now have a versioned 1 KiB persistence contract. Strict restore accepts field reordering/whitespace but rejects duplicate/unknown/future/wrong-type/oversized input transactionally without changing the previous settings.
- Exact redacted preview approval remains independent from the network setting: submission cannot begin while sharing is disabled, and disabling sharing before transport guarantees the transport is not called.
- The model exposes canonical local-result export, delete-by-result-ID, and clear-all operations. Deleting an active result never silently promotes stale history into the active sharing state.
- Local technical-result history now has a bounded versioned JSONL persistence contract for up to 64 canonical `CompatibilityResult` records. It deliberately excludes preview/receipt/transport state, rejects malformed/future/duplicate/empty/oversized stores transactionally, preserves Protected/Experimental truth, applies the current retention limit on restore, and restarts the latest technical result at `LocalResultAvailable` so network consent is never restored across restart. Focused testing exposed and fixed a `header\n` empty-result truncation bug that could otherwise have cleared prior evidence.
- The Win32 Management Games surface now renders localized `en-US`/`ko-KR`/`zh-CN` privacy controls for optional community sharing and retained-result count. Settings load from a fixed per-user `%LOCALAPPDATA%\\HydraSeat\\privacy-settings.json` path; writes use a bounded staged JSON file and `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` so a failed save does not mutate the in-memory privacy state.
- The same Management surface now opens the fixed per-user `CompatibilityLocalStore`, restores canonical retained technical results, renders the local-result list, exports the selected canonical JSON, and performs delete/clear through staged model mutation plus transactional store save. Save failure restores the prior in-memory history rather than presenting an unpersisted deletion as success; corrupt history is not silently overwritten.
- `tests/test_compatibility_share_model.cpp` and `tests/test_compatibility_local_store.cpp` cover default-off sharing, disable-before-transport, settings persistence, retention rotation/bounds, canonical offline export, delete/clear, strict local-history persistence, duplicate/future/malformed stores, transactional file replacement, and prior offline/retry/protected-experiment semantics. Portable/focused tests, localization tests, and the actual Windows `HydraSeat.exe` target build path cover the current controls.
- `docs/PRIVACY.md` records current local data/sharing/diagnostic/provider/update boundaries and explicitly distinguishes local deletion from remote-service withdrawal.
- Remaining before `CODE_COMPLETE`: finish installed-product log/trace/support/provider/update/installer-retention review, prove the exact visible payload/approval path end to end, and define deployed community-service retention/withdrawal terms if such a service ships. No finished deployed-service privacy claim is made yet.

---

## P10-REL-01 — Long-duration release reliability campaign

**State:** BLOCKED

**Goal**

Repeat the complete installed v1 lifecycle under realistic duration and failure conditions.

**Depends on**

- P10-PERF-01
- P10-COMPAT-01
- P10-SEC-01

**Campaign**

- repeated install/repair/update/rollback/uninstall;
- repeated two-Seat start/Seat-stop/change/restart/global-return;
- long simultaneous real-game session;
- UI/Seat UI/target/host/watchdog fault cases;
- sign-out/restart/shutdown;
- device/display/controller/audio reconnect;
- network unavailable/catalog unavailable;
- disk-full/permission/corrupt-state fixtures;
- emergency reset;
- memory/handle/thread/log growth measurement.

**Done when**

The release campaign meets the published reliability/resource budgets and every failure reaches a known safe/recovery state without unexplained collateral mutation.

---

## P10-UX-01 — Clean-machine onboarding, help, accessibility, and recovery documentation

**State:** BLOCKED

**Goal**

Make the full product usable by someone who does not know Raw Input, CMake, IAT hooks, display APIs, or HydraSeat packet IDs.

**Depends on**

- P7-CLOSE-01
- P8-INST-01
- P10-COMPAT-01

**Required journey docs/UI**

- what HydraSeat is and why it exists: share spare PC performance instead of buying a second complete desktop;
- hardware/resource expectations;
- install/first launch;
- optional Seat setup and `Set later`;
- create/select Players;
- choose a game and Seat 1/Seat 2/Both;
- automatic/manual same-game TwoPlayerSetup;
- protected/experimental warning meaning;
- community compatibility percentage meaning;
- one player exits/changes while the other continues;
- Return to Windows;
- recovery/reset;
- offline behavior;
- program update approval versus data catalog refresh;
- uninstall.

**Done when**

Clean-machine usability acceptance in the release locales completes the main journey and common failure/recovery paths without developer assistance.

---

## P10-LIC-01 — Project license, contribution terms, and third-party notices

**State:** BLOCKED

**Goal**

Resolve the legal gate currently preventing the repository/release from being described as open source.

**Depends on**

- P10-SCOPE-01
- clean-room/dependency inventory

**Required decisions**

- choose and add the project license only through an explicit project/user decision;
- define contribution terms/process and whether a CLA/DCO/other attestation is used;
- audit bundled/source-linked dependencies and compatible licenses;
- produce third-party notices/attribution where required;
- confirm community profile/setup/evidence contribution licensing/provenance;
- confirm game artwork/icon handling does not cause redistribution problems;
- preserve clean-room boundaries for proprietary/unlicensed references.

**Invariants**

- do not invent/assume a project license automatically;
- do not call the repository/release legally open source before this packet passes;
- no unlicensed/GPL-incompatible/proprietary source copied into the chosen core license path;
- third-party binaries/data have explicit redistribution/trust treatment.

**Done when**

A reviewed project license/contribution/notices set is committed and the repository's public open-source description is legally aligned with it.

---

## P10-PKG-01 — Reproducible release artifacts, SBOM, provenance, and checksums

**State:** BLOCKED

**Goal**

Produce verifiable release artifacts from the tested release commit.

**Depends on**

- P10-LIC-01
- P8-SIGN-01
- P8-INST-01

**Artifacts**

- installer/uninstaller/repair path;
- application/runtime architecture files;
- checksums;
- SBOM;
- source/revision/version metadata;
- signatures/provenance where configured;
- license/notices;
- compatibility/setup seed catalog as a separate data artifact where used.

**Done when**

A clean release build produces artifact hashes/provenance matching the declared source commit and installation smoke tests pass from those exact artifacts.

---

## P10-SUP-01 — Support, issue intake, regression triage, and compatibility withdrawal

**State:** BLOCKED

**Goal**

Define sustainable maintenance for a one-developer project and a community-driven compatibility catalog.

**Depends on**

- P10-LIC-01
- P9-CLOSE-01
- P8-DIAG-01

**Process**

- bug/security/compatibility report templates;
- redacted diagnostic/result attachments;
- game/setup evidence requirements;
- stale/broken compatibility cohort handling;
- protected-game reports never reframed as safety claims;
- setup/catalog withdrawal/revocation for unsafe/broken entries;
- duplicate/community report triage;
- maintainer capacity expectations and best-effort wording.

**Done when**

A contributor/user can report a reproducible issue or compatibility result without exposing sensitive information and the maintainer has a bounded triage/withdrawal workflow.

---

## P10-RC-01 — Release candidate gate

**State:** BLOCKED

**Goal**

Freeze a candidate and run the entire v1 acceptance matrix on the exact candidate artifacts.

**Depends on**

- P10-PKG-01
- P10-REL-01
- P10-UX-01
- P10-SUP-01
- P10-PRIV-01

**RC rule**

No feature expansion after RC freeze unless required to fix a release-blocking defect; such a fix resets the affected validation matrix.

**Done when**

The exact RC commit/artifacts pass installation, two-Seat physical/game lifecycle, recovery, offline/update, privacy/security, localization/accessibility, performance, and documentation gates with all known release blockers closed.

---

## P10-GA-01 — General availability / v1 release

**State:** BLOCKED

**Goal**

Publish the tested v1 artifacts and truthful public product status.

**Depends on**

- P10-RC-01
- P10-LIC-01

**GA publish**

- exact tested installer/artifacts/checksums/SBOM/provenance;
- release notes and known limitations;
- clear two-Seat/game-only scope;
- physical/resource requirements and performance caveats;
- compatibility evidence/catalog version;
- protected/experimental policy;
- recovery/uninstall docs;
- license/contribution/security/support links.

**Done when**

Published artifacts match the passing RC evidence and the public README/release page makes no broader compatibility, open-source, or anti-cheat-safety claim than the release evidence/legal state permits.

---

## P10-MAINT-01 — Maintenance and future roadmap policy

**State:** BLOCKED

**Goal**

Define how the v1 branch is maintained and how post-v1 scope expansion is evaluated.

**Depends on**

- P10-GA-01

**Policy topics**

- patch/update support window;
- Windows/game/provider regression re-test rules;
- compatibility catalog refresh cadence/process;
- emergency security/compatibility withdrawal;
- data/schema migration support;
- when a future third/fourth Seat, full desktop apps, virtual display driver, or binary extension SDK is worth reactivating;
- one-developer scope/capacity constraints.

**Decision rule**

Post-v1 expansion must be justified by actual user benefit/evidence rather than by making the architecture generically larger.

**Done when**

Maintenance/retest/EOL and post-v1 decision rules are published and can be followed without destabilizing the v1 two-Seat product.

---

## P10-CLOSE-01 — Master roadmap closure for v1

**State:** BLOCKED

**Goal**

Record that the release actually satisfied the complete product contract.

**Depends on**

- P10-MAINT-01
- P10-GA-01

**Final verification**

- the reason for the product and intended household user is clearly documented;
- exactly two v1 Seats;
- complete real game-first user journey;
- independent Seat lifecycle;
- physical zero-bleed evidence;
- lawful same-game demonstration;
- installer/recovery/offline/update/privacy/security/performance gates;
- community evidence model without official-certification claims;
- license/open-source legal gate resolved;
- all README/roadmap/status/release claims match actual evidence.

**Done when**

A dedicated final review records that HydraSeat v1 is a usable, legally publishable, evidence-backed two-person local gaming product and moves remaining ambitions into a post-v1 roadmap rather than silently including them in 1.0.