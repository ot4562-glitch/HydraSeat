# Phase 10 — Release Hardening and 1.0 Readiness

## Phase objective

Convert the validated product into a supportable release with explicit platform scope, measurable performance and reliability, secure defaults, signed/reproducible artifacts, complete license/provenance information, onboarding and recovery documentation, and release gates that cannot be waived by optimistic wording.

Phase 10 does not add broad new architecture. New functionality discovered here becomes a separate packet/phase or remains experimental.

## Phase exit gate

A production release is approved only when:

1. supported Windows/architecture/hardware/profile scope is frozen and documented;
2. release build, tests, installer, signing, SBOM, and provenance are reproducible;
3. performance and latency budgets pass on reference topologies;
4. compatibility regression matrix passes or failures are explicitly withdrawn;
5. security/privacy/threat-model findings are resolved according to policy;
6. reliability/soak/fault/reboot/update/rollback campaigns pass;
7. clean-machine onboarding and emergency recovery are documented and tested;
8. diagnostics/support workflows work without exposing private data;
9. all third-party licenses, notices, and project license/contribution terms are resolved;
10. release candidate runs for the declared stabilization period with no unresolved release blocker;
11. rollback/uninstall restores normal Windows behavior;
12. version/support/maintenance policy is published.

## Dependency graph

```text
P8-CLOSE-01 + P9-CLOSE-01 + validated P5 compatibility scope
                          |
       +------------------+------------------+
       |                  |                  |
 P10-SCOPE-01       P10-PERF-01        P10-SEC-01
       |                  |                  |
       +-> P10-COMPAT-01  +-> P10-REL-01 <-+
                |                 |
          P10-UX-01       P10-PKG-01/P10-LIC-01
                |                 |
                +-------> P10-RC-01 -> P10-GA-01 -> P10-MAINT-01
```

---

## P10-SCOPE-01 — Freeze supported platform and product scope

**State:** BLOCKED

**Goal**

Define exactly what 1.0 supports before final hardening.

**Depends on**

- validated compatibility/hardware matrix;
- Phase 8 installer/support constraints;
- Phase 9 extension trust state.

**Freeze fields**

- Windows editions/build ranges;
- x64/x86 target support;
- CPU/GPU/driver families and minimums where relevant;
- physical display count/topology/DPI constraints;
- input/controller/audio device classes;
- supported/experimental/blocked profiles;
- required/optional components and versions;
- virtual display support status;
- Seat count and process/window limits;
- install/update/startup modes;
- unsupported protected/anti-cheat scope;
- known limitations.

**Implementation skeleton**

1. generate proposed scope from compatibility/hardware/component matrices;
2. identify entries lacking fresh evidence;
3. run or remove claims;
4. lock matrix schema and support level for release branch;
5. make installer/preflight enforce the same scope;
6. publish explicit unsupported behavior and upgrade path.

**Invariants**

- README/installer/UI/profile planner derive from the same scope data;
- no “Windows 10/11 supported” without tested build range;
- experimental extensions/profiles cannot silently enter production scope;
- missing hardware evidence removes or downgrades claim;
- future broader scope does not block a smaller honest 1.0.

**Done when**

One machine-readable release-scope manifest drives docs, installer, preflight, and regression selection.

**Suggested commit**

`docs: implement P10-SCOPE-01 release support scope`

---

## P10-PERF-01 — Performance, latency, resource, and scalability qualification

**State:** BLOCKED

**Goal**

Measure and optimize end-to-end overhead against declared budgets without weakening correctness.

**Depends on**

- production host/shell/adapters/metrics;
- P10-SCOPE-01 reference topologies.

**Create/modify**

- benchmark executables and scenarios;
- repeatable reference topology manifest;
- performance report generator;
- CI smoke thresholds and scheduled hardware benchmark procedure;
- profiling documentation.

**Measurements**

- physical event to host observation;
- route enqueue/dequeue/transport/apply/API query;
- cross-Seat bleed/drop/queue high-water;
- controlled and game input latency p50/p95/p99/max;
- host/watchdog/UI/shell CPU and memory idle/active;
- handle/thread/process count and growth;
- window/display reaction time;
- audio latency/route delay where supported;
- controller polling/response/vibration delay;
- startup/stop/rollback duration;
- 2/3/4 Seat scaling if in scope.

**Invariants**

- correctness/zero-bleed failure cannot be traded for lower latency silently;
- benchmark clock/method calibrated and versioned;
- warmup/outlier/sample count documented;
- unsupported/missing stage is not recorded as zero;
- traces bounded and privacy-limited;
- regressions have thresholds and owners.

**Acceptance**

- meet budgets in master roadmap or update budget with evidence/scope decision;
- no unbounded growth over soak;
- no input callback blocking regression;
- release notes include measured reference results and limits.

**Done when**

Performance report passes for every release reference topology/profile.

**Suggested commit**

`perf: complete P10-PERF-01 release qualification`

---

## P10-COMPAT-01 — Full compatibility and regression matrix

**State:** BLOCKED

**Goal**

Run every supported profile/topology/backend/provider combination against the release candidate and detect withdrawn/regressed support.

**Depends on**

- P10-SCOPE-01
- compatibility matrix and fixture suite;
- stable installer/build.

**Matrix dimensions**

- supported Windows builds;
- x64/x86 target architecture;
- GPU/display-driver families;
- display topology/DPI/mode;
- input/controller/audio devices;
- provider/client versions;
- profile/backend/extension versions;
- install/update source state;
- start/stop/reconnect/restart/reboot;
- safe mode/reset/uninstall.

**Implementation skeleton**

1. generate test plan from release-scope + matrix data;
2. run automated controlled fixtures in CI;
3. run physical/game cases using standardized runner;
4. record pass/fail/evidence timestamp;
5. compare with prior release and flag regression;
6. fix, downgrade, block, or withdraw affected entry;
7. ensure docs/preflight/install scope updates automatically.

**Invariants**

- missing result is not pass;
- stale evidence expires according to policy;
- game update invalidates exact-version support until retested;
- extension/backend update recorded;
- protected profile remains blocked/observation-only;
- matrix data is machine-validated.

**Done when**

All release-scope entries have current passing evidence or are removed/downgraded.

**Suggested commit**

`test: complete P10-COMPAT-01 release matrix`

---

## P10-SEC-01 — Product threat model, hardening, and security review

**State:** BLOCKED

**Goal**

Review the complete product boundary and fix release-blocking security/privacy issues.

**Depends on**

- production runtime, IPC, watchdog, broker, installer/update, SDK/package system;
- P9 extension threat model.

**Threat areas**

- local IPC authentication/spoof/replay/flood;
- adapter/shim/DLL search/load hijack;
- package/update/signature/catalog compromise;
- elevated broker abuse/path/reparse/TOCTOU;
- driver/device-control misuse;
- profile/import/provider metadata injection;
- process/window ownership confusion/PID/HWND reuse;
- crash journal/rollback action tampering;
- extension escape/data exfiltration;
- diagnostics/raw input/privacy;
- startup persistence/uninstall residue;
- untrusted game/launcher process interactions;
- denial of service and resource exhaustion.

**Create/modify**

- `docs/security/PRODUCT_THREAT_MODEL.md`;
- security requirements/checklist;
- parser/protocol fuzz targets;
- static analysis/sanitizer where applicable;
- dependency/SBOM/vulnerability scan;
- code-signing/runtime verification;
- disclosure and security-update policy.

**Invariants**

- no claim of sandbox where Windows controls do not provide one;
- normal operation standard-user;
- privileged operations narrow and authenticated;
- safe defaults disable experimental/invasive components;
- no secrets/tokens in logs/crash bundles;
- critical/high unresolved finding blocks release;
- medium accepted risk documented with scope/mitigation.

**Done when**

Threat models are current, fuzz/static/dependency checks run, and critical/high findings are resolved or affected scope removed.

**Suggested commit**

`security: complete P10-SEC-01 product hardening`

---

## P10-PRIV-01 — Privacy, data retention, and optional telemetry policy

**State:** BLOCKED

**Goal**

Define and implement privacy-safe diagnostics and any optional telemetry before release.

**Depends on**

- diagnostics/metrics/extension exporter/update systems;
- P10-SEC-01.

**Data inventory**

- hardware stable IDs and friendly metadata;
- application/profile/provider/version;
- process/window titles/paths;
- input event classes/key codes;
- audio/controller/display topology;
- error/crash/metrics/update events;
- extension/package inventory;
- user-provided descriptions.

**Policy**

- telemetry off by default unless a later explicit product decision changes it;
- diagnostics local by default;
- user preview/export/delete controls;
- raw keystroke/text never collected by telemetry;
- key codes only in explicit local diagnostic mode;
- retention/rotation per data category;
- network endpoints/data schema/version/public policy if telemetry exists;
- no advertising/behavior profiling.

**Automated/manual tests**

- redaction corpus;
- opt-in/opt-out/delete;
- network disabled/offline behavior;
- bundle preview;
- extension exporter permissions;
- update check versus telemetry separation;
- privacy policy matches actual fields.

**Done when**

A machine-readable data inventory and user documentation match implementation, and no undeclared network/data flow exists.

**Suggested commit**

`privacy: implement P10-PRIV-01 data policy`

---

## P10-REL-01 — Reliability and long-duration release campaign

**State:** BLOCKED

**Goal**

Run release-level soak, fault, reboot, recovery, and resource-leak tests beyond Phase 8 qualification.

**Depends on**

- P8-SOAK-01;
- release candidate build/scope;
- P10-PERF-01 instrumentation.

**Minimum campaign**

- 1000 controlled start/stop cycles;
- 250 supported MVP start/stop cycles;
- 72-hour idle host/watchdog/tray/shell;
- 24-hour active two-Seat supported session;
- repeated target/UI/shell/host/watchdog kills;
- input/controller/audio/display hot-plug loops;
- provider/client restart/update simulation;
- reboot/logoff/safe-mode/startup;
- installer repair/update rollback/uninstall;
- low disk, permission failure, corrupt journal/profile/extension;
- network unavailable for optional catalog/update;
- emergency reset at random transition stages.

**Pass criteria**

- no orphan process/window/overlay/device/audio/display route;
- no unbounded memory/handle/thread/file/log growth;
- no silent cross-Seat fallback;
- recovery budget met or explicit recovery-required with successful reset;
- every injected fault produces diagnosable result;
- normal Windows usable after final uninstall/reset.

**Done when**

The campaign report has no unresolved release blocker and resource trends remain within thresholds.

**Suggested commit**

`test: complete P10-REL-01 release reliability campaign`

---

## P10-UX-01 — Clean-machine onboarding, help, accessibility, and recovery docs

**State:** BLOCKED

**Goal**

Make the supported product usable from install through recovery without relying on development knowledge.

**Depends on**

- frozen scope;
- installer/profile manager/shell/reset/diagnostics;
- P7-I18N-01 and P7-A11Y-01.

**User journey**

1. install and choose startup/optional components;
2. detect hardware and explain stable device identification;
3. compose multi-monitor Seats;
4. test/flash each input/controller/audio/display;
5. select/import supported profiles;
6. preview risk/backend requirements;
7. run guided hardware acceptance;
8. start/monitor/stop session;
9. respond to disconnect/degraded/recovery-required state;
10. export diagnostics/reset/update/uninstall.

**Deliverables**

- onboarding wizard and skip/resume;
- in-app help/error remediation links;
- quick start and advanced architecture guide;
- physical acceptance instructions;
- compatibility matrix navigation;
- emergency reset/recovery card;
- installer/update/uninstall guide;
- accessibility/localization review for `en-US`, `ko-KR`, and `zh-CN`;
- maintained `README.md`, `README.ko.md`, and `README.zh-CN.md` with language-switch links and release-status parity;
- screenshots/videos only when they match current UI.

**Invariants**

- unsupported profile cannot be presented as recommended;
- warning identifies exact mutation/recovery;
- no false “isolated/active” success before host verification;
- help works offline for core/recovery;
- emergency reset instructions visible without active session;
- docs generated/versioned with release;
- English, Korean, and Simplified Chinese documentation agree on product goal, current support status, recovery behavior, commands, license state, and release version; machine-readable identifiers remain unchanged across translations.

**Manual tests**

- new user on clean supported machine in `en-US`, `ko-KR`, and `zh-CN`;
- keyboard-only/high-DPI/high-contrast flows;
- failure/recovery task completion;
- uninstall/data export;
- documentation link/version accuracy.

**Done when**

A new tester can complete the supported setup/session/reset without developer assistance.

**Suggested commit**

`docs: implement P10-UX-01 release onboarding`

---

## P10-LIC-01 — Project license, contribution terms, and third-party notices

**State:** BLOCKED

**Goal**

Resolve the repository's undeclared license before production release and verify all included code/assets/dependencies are distributable.

**Depends on**

- project owner/legal decision;
- complete dependency/component inventory;
- clean-room provenance records.

**Deliverables**

- tracked `LICENSE` file;
- contribution/license policy or DCO/CLA decision;
- `THIRD_PARTY_NOTICES` with exact component/version/license/source;
- asset/font/icon/sample/profile provenance;
- SBOM license fields;
- binary redistribution terms;
- extension/package license requirements;
- README/badge/package metadata consistency.

**Invariants**

- no MIT or other badge without matching tracked license;
- no copied code with unclear/incompatible terms;
- GPL/copyleft boundary decisions explicit;
- proprietary/unlicensed references remain behavior-only;
- source/binary packages include required notices;
- release is blocked until ownership/permission is clear.

**Done when**

A license/provenance review finds no unresolved redistribution blocker.

**Suggested commit**

`legal: implement P10-LIC-01 license and notices`

---

## P10-PKG-01 — Reproducible release artifacts, SBOM, provenance, and checksums

**State:** BLOCKED

**Goal**

Produce deterministic, verifiable release packages for each supported architecture/channel.

**Depends on**

- P8 signing/installer/update;
- P10-LIC-01;
- frozen release scope.

**Artifacts**

- signed installer/package;
- portable/developer bundle if supported;
- application/adapter architecture layout;
- symbols or private/public symbol policy;
- source archive;
- SHA-256 checksums;
- SPDX/CycloneDX SBOM;
- signed release/update manifest;
- build provenance/commit/toolchain/dependency versions;
- license/notices/docs/compatibility matrix.

**Implementation skeleton**

1. pin/record build toolchain/dependencies;
2. clean checkout build;
3. normalize generated timestamps/order where feasible;
4. build/test/sign/package;
5. verify package on clean machine;
6. compare reproducibility within declared scope;
7. scan contents for secrets/private paths/debug artifacts;
8. publish checksums/SBOM/provenance with artifact.

**Invariants**

- exact commit and dirty state recorded;
- release package contains only intended files;
- optional components match manifests/hashes;
- no signing secrets/logs;
- artifact version consistent across binary/manifest/docs;
- rollback package/previous version available according to policy.

**Done when**

A clean release workflow produces installable, verifiable artifacts with complete provenance.

**Suggested commit**

`build: implement P10-PKG-01 release artifacts`

---

## P10-SUP-01 — Support, issue intake, regression triage, and compatibility withdrawal

**State:** BLOCKED

**Goal**

Define how real failures become reproducible fixes without overclaiming support.

**Depends on**

- diagnostic bundle;
- compatibility/release scope;
- security/privacy policy.

**Deliverables**

- issue templates for hardware/profile/runtime/install/update/security;
- diagnostic bundle instructions and privacy warning;
- reproduction/evidence checklist;
- severity and release-blocker policy;
- compatibility regression/withdrawal process;
- supported version and end-of-support policy;
- security disclosure contact/process;
- FAQ/known issues generated from matrix.

**Invariants**

- reports never request passwords/tokens/full memory dumps/raw private text by default;
- protected/anti-cheat requests are triaged to policy, not bypass work;
- compatibility entry can be downgraded/withdrawn quickly;
- regression has owner/status/evidence;
- support documents match active release.

**Done when**

A tester can file a sanitized, reproducible issue and maintainers can map it to component/profile/matrix evidence.

**Suggested commit**

`docs: implement P10-SUP-01 support workflow`

---

## P10-RC-01 — Release candidate gate

**State:** BLOCKED

**Goal**

Create a release candidate only when every mandatory gate is evidenced and freeze new features during stabilization.

**Depends on**

- P10 scope/perf/compat/security/privacy/reliability/UX/license/package/support packets;
- all release-blocking Phase 8/9 packets.

**RC checklist**

- clean release branch/tag candidate;
- full CI and artifact pipeline pass;
- clean-machine installer/update/rollback/uninstall pass;
- scope/matrix current;
- performance/reliability/security/privacy reports approved;
- license/notices/SBOM/provenance complete;
- no critical/high issue or unknown rollback failure;
- all user docs/version links match;
- diagnostic/reset/support flow tested;
- release notes list support, limitations, changes, migration, rollback;
- feature freeze active except blocker fixes.

**Stabilization period**

Initial recommendation: at least 14 days and multiple external/independent testers on declared topologies. A shorter period requires an explicit documented release decision.

**Done when**

RC has no unresolved release blocker and all blocker fixes rerun affected/full gates.

**Suggested commit**

`release: prepare P10-RC-01 release candidate`

---

## P10-GA-01 — General availability / 1.0 release

**State:** BLOCKED

**Goal**

Publish a supportable release without overstating capability.

**Depends on**

- P10-RC-01 stabilization complete.

**Release actions**

- create signed/versioned tag;
- publish verified artifacts/checksums/SBOM/provenance/source/notices;
- publish release notes, compatibility matrix, known issues, rollback/uninstall instructions;
- publish security/privacy/support/version policy;
- verify update channel and fresh install;
- retain prior rollback artifact;
- monitor first-run issues and compatibility regressions;
- do not silently expand support scope in announcement.

**Post-release immediate validation**

- download and verify public artifacts;
- install/update/uninstall on clean supported machine;
- run one supported two-Seat session and emergency reset;
- validate documentation/download links;
- verify no secret/internal debug path in artifacts;
- confirm release/update manifests resolve correctly.

**Done when**

The public release is reproducible, verifiable, installable, recoverable, and accurately scoped.

**Suggested commit/tag**

`release: HydraSeat 1.0.0`

---

## P10-MAINT-01 — Maintenance and future roadmap policy

**State:** BLOCKED

**Goal**

Define how the project remains reliable after 1.0.

**Depends on**

- P10-GA-01.

**Policy**

- supported release branches and duration;
- security patch process;
- Windows/game/provider/driver update retest triggers;
- compatibility evidence expiry;
- extension SDK deprecation/version policy;
- telemetry/privacy change approval;
- bugfix versus feature release gates;
- database/profile/schema migrations;
- nightly/scheduled matrix tests;
- issue triage and regression withdrawal;
- future phases such as additional Seats, sessions, platforms, or custom IDD require new packet plans.

**Invariants**

- maintenance release does not bypass release gates relevant to changed components;
- compatibility claim can be withdrawn faster than a code release;
- security/privacy change documented;
- unsupported EOL version visible;
- roadmap retains packet/evidence discipline.

**Done when**

Published maintenance policy and scheduled validation workflows exist.

**Suggested commit**

`docs: implement P10-MAINT-01 maintenance policy`

---

## P10-CLOSE-01 — Master roadmap closure for 1.0

**State:** BLOCKED

**Closure checklist**

- GA release and post-release validation pass;
- maintenance/security/support policy active;
- release scope and compatibility matrix remain accurate;
- no unresolved license/provenance blocker;
- installer/update/reset/rollback verified from public artifacts;
- performance/reliability/security/privacy evidence archived;
- all phases/status/README/docs agree;
- future work moved to new versioned roadmap packets rather than silently extending 1.0 scope.

**Done when**

The 1.0 roadmap is closed with public evidence and a new maintenance/future roadmap is initialized.

**Suggested commit**

`docs: close HydraSeat 1.0 implementation roadmap`
