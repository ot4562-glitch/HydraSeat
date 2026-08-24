# Phase 9 — Compatibility SDK and Extension Ecosystem

## Phase objective

Allow trusted third parties and future contributors to add compatibility profiles, provider adapters, input/controller/audio/display backends, diagnostics, and Seat shell extensions without changing HydraSeat core or bypassing its planner, trust, privilege, recovery, and evidence rules.

The default extension model is declarative profiles and out-of-process adapters. In-process DLL loading is exceptional, capability-limited, versioned, trusted, and disabled by default.

## Phase exit gate

Phase 9 is complete when:

1. public SDK contracts have semantic versions and compatibility policy;
2. extension packages have signed/hash-verified manifests and explicit permissions;
3. untrusted extensions run out of process with bounded IPC/resources;
4. capability negotiation cannot grant more than host policy and evidence allow;
5. community profiles are schema-validated and cannot embed arbitrary code by default;
6. input/display/audio/controller/provider/shell extension categories have conformance tests;
7. extension failure cannot corrupt another Seat or host runtime;
8. install/update/remove are transactional and reversible;
9. sample extensions and developer documentation compile in CI;
10. a security threat model and review pass;
11. compatibility entries identify extension versions and trust level.

## Dependency graph

```text
P8-CLOSE-01 + stable Phase 3-7 contracts
            |
            +-> P9-SDK-01 -> P9-CAP-01
            +-> P9-PKG-01 -> P9-REG-01
            +-> P9-RPC-01

P9-SDK-01 + P9-RPC-01 + P9-CAP-01
            -> P9-ADAPT-01 -> P9-PROV-01 / P9-SHELL-01 / P9-DIAG-01

all extension contracts -> P9-TEST-01 -> P9-SEC-01 -> P9-DOC-01 -> P9-CLOSE-01
```

---

## P9-SDK-01 — Public SDK boundary and compatibility policy

**State:** BLOCKED

**Goal**

Select which internal contracts become public and freeze their version/lifecycle rules.

**Depends on**

- stable production contracts from Phases 3–8
- D-012, D-024

**Create/modify**

- `sdk/include/hydraseat/sdk_version.h`
- `sdk/include/hydraseat/types.h`
- `sdk/include/hydraseat/result.h`
- public protocol/manifest schemas;
- compatibility policy document;
- ABI/schema tests.

**Initial public concepts**

- fixed-width IDs and version structs;
- capability bitsets/descriptors;
- Seat/process/window/display/device/audio/controller snapshots with privacy-limited fields;
- extension result/diagnostic model;
- host/extension handshake and lifecycle;
- package/profile manifest references;
- no raw internal C++ class ABI.

**Version policy**

- semantic SDK version;
- explicit minimum/maximum host protocol versions;
- additive reserved fields only within compatible minor versions;
- structure size/version validation for C ABI;
- schema `$id`/version and migration policy;
- deprecation window and removal policy;
- host rejects incompatible major version.

**Invariants**

- public ABI is C or language-neutral IPC/schema, not compiler-specific C++ ABI;
- internal HWND/HANDLE/pointers are opaque or absent;
- extensions cannot receive secrets/raw typed text by default;
- public contract cannot mutate runtime except through typed capability-scoped requests;
- every public sample/test is built against installed SDK layout.

**Automated tests**

- C/C++ ABI size/version;
- old minor/new host and new minor/old host fixtures;
- unknown field/version;
- architecture/endian assumptions;
- installed SDK sample build;
- public header dependency hygiene.

**Done when**

A sample out-of-tree extension builds using only published SDK files and negotiates compatibility.

**Suggested commit**

`feat: implement P9-SDK-01 public SDK contracts`

---

## P9-CAP-01 — Extension capability and permission negotiation

**State:** BLOCKED

**Goal**

Ensure an extension receives only the data and operations declared by its package, supported by the host, permitted by user/policy, and required by the active plan.

**Depends on**

- P9-SDK-01
- existing planner capability model

**Create/modify**

- `sdk/include/hydraseat/capabilities.h`
- `include/hydra/extension_policy.hpp`
- `src/extension_policy.cpp`
- tests.

**Permission classes**

- read sanitized topology/runtime state;
- receive Seat-scoped input/controller state;
- propose process/window/display/audio/controller actions;
- execute a narrow backend operation;
- request elevation through broker;
- persist extension-owned bounded state;
- emit diagnostics/UI panels;
- network access is separate and denied by default.

**Negotiation inputs**

- package-declared capabilities/permissions;
- host/SDK versions;
- extension trust/signature level;
- user approval;
- active profile/plan requirements;
- Windows/architecture/backend availability;
- support/evidence level.

**Invariants**

- extension declaration cannot self-grant permission;
- host can reduce but not expand requested capability silently;
- denied required permission prevents activation;
- read scope is Seat/session-limited;
- permission changes invalidate/restart session;
- decisions are logged without secrets.

**Automated tests**

- over-request/under-request;
- unsigned experimental package;
- user denial;
- profile does not require extension;
- trust downgrade/update;
- Seat scope spoof;
- deterministic negotiation result.

**Done when**

A sample extension can do one allowed operation and is denied a non-declared/other-Seat operation.

**Suggested commit**

`feat: implement P9-CAP-01 extension permissions`

---

## P9-PKG-01 — Extension package manifest and lifecycle

**State:** BLOCKED

**Goal**

Define installable extension packages using the Phase 8 component trust model.

**Depends on**

- P8-TRUST-01/P8-INST-01/P8-UPD-01
- P9-SDK-01

**Create/modify**

- `schemas/extension-package-v1.schema.json`
- `include/hydra/extension_package.hpp`
- `src/extension_package.cpp`
- package install/remove/update transaction;
- tests.

**Manifest fields**

- package ID/name/version/publisher;
- SDK/host/protocol compatibility;
- architectures/entrypoints;
- extension categories;
- capabilities/permissions;
- file hashes/signature/license/source;
- dependencies/conflicts;
- profile/schema assets;
- data/storage/network/elevation policy;
- install/update/remove hooks are typed/allowlisted, never arbitrary scripts by default.

**Lifecycle**

1. validate signature/hash/schema/path;
2. display publisher/license/permissions/risk;
3. stage in package-specific directory;
4. run static/conformance preflight;
5. atomically register package;
6. activate only in new plans;
7. update side-by-side and health-check;
8. roll back failed update;
9. refuse removal while active or stop safely;
10. remove code/registration while preserving/exporting extension data by choice.

**Invariants**

- package cannot write outside assigned roots through installer;
- no DLL search path ambiguity;
- dependency cycle/version conflict rejected;
- modified package disabled;
- package removal cannot strand an active plan silently;
- untrusted package never becomes release-supported evidence.

**Automated tests**

- valid/tampered/path traversal/duplicate/dependency cycle;
- update/rollback/remove-active;
- architecture/SDK mismatch;
- permission changes;
- data preserve/delete/export.

**Done when**

A signed/test package installs, activates in a controlled plan, updates, rolls back, and removes transactionally.

**Suggested commit**

`feat: implement P9-PKG-01 extension packages`

---

## P9-RPC-01 — Out-of-process extension host and isolation

**State:** BLOCKED

**Goal**

Run normal extensions outside `hydra_host.exe` with bounded, authenticated IPC and failure isolation.

**Depends on**

- P9-SDK-01
- P9-CAP-01
- P8 watchdog/process/manifest foundations

**Create/modify**

- `sdk/schemas/extension-protocol-v1.*`;
- `include/hydra/extension_host.hpp`
- `src/extension_host.cpp`
- `src/extension_runner_main.cpp`
- process/IPC/resource tests.

**Architecture**

```text
hydra_host.exe
  -> one extension runner per package or risk domain
       -> extension process/module
```

**Lifecycle**

- host verifies package and creates scoped session token;
- runner starts with constrained environment/working directory;
- handshake negotiates version/capabilities/Seat scope;
- bounded request/event channels and heartbeats;
- CPU/memory/process limits via Job Object where compatible;
- timeouts/circuit breaker;
- crash produces extension failure/degraded/rollback according to active plan;
- restart only under bounded policy.

**Invariants**

- extension cannot impersonate another package/session/Seat;
- extension event cannot block host callback paths;
- arbitrary host memory is unavailable;
- process tree owned/cleaned;
- protocol malformed/stale/replay fails closed;
- network/filesystem access policy explicit, not assumed sandboxed by Job Object alone.

**Automated tests**

- handshake/spoof/version mismatch;
- flood/backpressure;
- hang/crash/restart limit;
- memory/process child limits;
- other-Seat request;
- package update while inactive/active;
- host/watchdog recovery.

**Done when**

A deliberately crashing/flooding extension cannot crash the host or affect another Seat and yields visible policy behavior.

**Suggested commit**

`feat: implement P9-RPC-01 extension runner`

---

## P9-ADAPT-01 — Backend adapter SDK

**State:** BLOCKED

**Goal**

Publish one common lifecycle pattern for optional input, controller, display, audio, provider, and diagnostics backends while keeping category-specific contracts precise.

**Depends on**

- P9-SDK-01/P9-CAP-01/P9-RPC-01
- stable internal backend interfaces

**Create/modify**

- category descriptor/request/result schemas and headers;
- host bridge implementations;
- fake/sample adapters;
- conformance tests.

**Common lifecycle**

```text
Describe -> Probe -> Plan -> Prepare -> Apply -> Verify -> Active
       -> Stop -> Rollback -> VerifyStopped
```

**Category-specific requirements**

### Input compatibility

- target process/architecture/API scope;
- startup/load method;
- replacement/suppression guarantees;
- unhook/rollback evidence.

### Controller

- source identity/API/slot mapping/vibration;
- disconnect generation.

### Display

- create/destroy/mode/topology identity/reboot/signing;
- no physical-display dependency.

### Audio

- process/session/endpoint/route persistence/latency;
- exact Windows/backend support.

### Provider

- discovery/launch/process correlation only;
- no credentials.

**Invariants**

- common interface does not erase category guarantees;
- capability advertised only after conformance/evidence;
- every mutation has verify/rollback;
- extension does not mutate host state directly;
- unsupported method explicit.

**Done when**

At least one sample fake adapter per category passes the common and category conformance suite.

**Suggested commit**

`feat: implement P9-ADAPT-01 backend adapter SDK`

---

## P9-PROV-01 — Launcher provider extension SDK

**State:** BLOCKED

**Goal**

Publish a provider-specific extension contract for lawful read-only discovery and normal launcher invocation without exposing credentials or runtime authority.

**Depends on**

- P9-ADAPT-01
- P6-PROV-01
- P9-PKG-01

**Create/modify**

- provider SDK headers/schemas;
- host provider bridge;
- fake provider sample;
- provider conformance tests.

**Public operations**

- describe/probe provider and version;
- discover bounded application/install metadata;
- resolve launch candidates;
- submit a typed normal launch request;
- report expected process correlation hints;
- cancel owned preparation/launch work;
- emit diagnostics and limitations.

**Invariants**

- no credentials, cookies, OAuth/session tokens, DRM, or anti-cheat access;
- provider extension cannot start arbitrary commands outside the approved launch candidate;
- catalog/manifest input is untrusted and bounded;
- process ownership is revalidated by the host;
- network permission is separate and denied by default;
- package trust/conformance do not automatically make an application profile supported.

**Automated tests**

- fake discovery/launch/cancel;
- malformed/hostile metadata;
- absent/updating provider;
- process ambiguity;
- permission denial;
- crash/hang/backpressure;
- package update/version incompatibility.

**Done when**

An out-of-process sample provider catalogs and launches a controlled target through the production plan while prohibited credential/arbitrary-command requests are denied.

**Suggested commit**

`feat: implement P9-PROV-01 provider SDK`

---

## P9-PROFILE-01 — Community compatibility profile format and validator

**State:** BLOCKED

**Goal**

Allow community-authored declarative profiles without arbitrary code.

**Depends on**

- Phase 6 compatibility schema
- P9-PKG-01/P9-CAP-01

**Allowed content**

- application/provider/version match rules;
- required/optional capabilities;
- backend preference/deny;
- API allowlist and typed hook flags;
- process/window selector rules;
- controller/audio/display policy;
- start delays/timeouts/retries with bounds;
- namespace rules from an allowlisted vocabulary;
- evidence/support metadata;
- no shell command/script/native code by default.

**Create/modify**

- public compatibility profile schema;
- `hydra_profile lint/test/explain`;
- static risk analyzer;
- fixture/conformance corpus;
- package integration.

**Invariants**

- profile cannot grant unavailable capability;
- every numeric/list/regex/path field bounded;
- dangerous rule requires expert permission and remains declarative;
- unknown API/workaround rejected;
- game title is not the sole match identity;
- support level requires matrix evidence.

**Automated tests**

- valid/malformed/oversized/catastrophic regex/path traversal;
- ambiguous target match;
- forbidden protected-process behavior;
- profile dependency/conflict;
- deterministic compiled plan;
- imported untrusted profile remains experimental.

**Done when**

A community profile can be linted, explained, tested against a controlled fixture, packaged, and safely rejected when unsupported.

**Suggested commit**

`feat: implement P9-PROFILE-01 community profiles`

---

## P9-SHELL-01 — Seat shell extension SDK

**State:** BLOCKED

**Goal**

Publish safe shell panels/actions/data sources without allowing UI extensions to bypass host policy.

**Depends on**

- P7-EXT-01
- P9-SDK/CAP/RPC/PKG foundations

**Extension types**

- read-only status panel;
- launcher/catalog panel;
- diagnostic visualization;
- typed Seat action provider;
- theme/asset package;
- no arbitrary in-process UI DLL by default.

**Invariants**

- data is Seat-scoped and redacted;
- action is a typed host request revalidated by policy;
- panel render/update budget and queue bounded;
- crash/hang isolated;
- inaccessible/off-screen surfaces recover;
- shell safe mode disables extensions.

**Automated tests**

- sample panel/action;
- other-Seat access denied;
- render/update flood;
- crash/restart/disable;
- package update/remove;
- accessibility metadata.

**Done when**

A sample out-of-process panel displays one Seat's metrics and can request one allowed action without direct runtime access.

**Suggested commit**

`feat: implement P9-SHELL-01 shell extension SDK`

---

## P9-DIAG-01 — Diagnostics/metrics exporter SDK

**State:** BLOCKED

**Goal**

Allow privacy-reviewed exporters and visualizers without exposing secrets or blocking runtime.

**Depends on**

- P8-DIAG-01
- P9-CAP/RPC

**Contract**

- subscribe to selected structured metric/event categories;
- receive bounded batches with schema/version;
- declare retention/network/export policy;
- host-side redaction before delivery;
- no raw keyboard text or credentials;
- exporter failure drops exporter data, not runtime events;
- user-visible enable/disable and data preview.

**Tests**

- redaction and Seat scope;
- slow/failing exporter;
- queue overflow/sample policy;
- network permission denied/approved;
- schema version update;
- uninstall deletes/preserves exporter data as selected.

**Done when**

A local sample exporter receives redacted metrics without impacting input/session latency.

**Suggested commit**

`feat: implement P9-DIAG-01 diagnostics exporter SDK`

---

## P9-REG-01 — Local extension registry and optional remote catalog contract

**State:** BLOCKED

**Goal**

Maintain installed/trusted extension inventory and define a future catalog feed without making network access mandatory.

**Depends on**

- P9-PKG-01
- P8 trust/update foundations

**Local registry**

- installed versions/hashes/signatures;
- enabled/disabled/quarantined state;
- permissions/user approvals;
- compatible SDK/host versions;
- active plan references;
- update source/channel metadata;
- last health/conformance result.

**Optional remote catalog**

- signed index and package metadata;
- no automatic install/enable;
- user-controlled refresh/channel;
- download/staging through Phase 8 update/trust flow;
- revocation/advisory metadata;
- privacy-preserving access policy.

**Invariants**

- HydraSeat core works entirely offline;
- catalog metadata cannot grant trust;
- removed/revoked/tampered extension disabled visibly;
- downgrade/rollback policy explicit;
- active plan prevents unsafe remove/update.

**Done when**

Installed extension inventory is deterministic and a signed fake catalog update/revocation flow passes tests.

**Suggested commit**

`feat: implement P9-REG-01 extension registry`

---

## P9-TEST-01 — Extension conformance kit

**State:** BLOCKED

**Goal**

Give extension authors and CI an executable way to prove contract behavior.

**Depends on**

- all public SDK/category contracts

**Deliverables**

- `hydra_sdk_test` runner;
- fake host/runtime/Seat/topology/process fixtures;
- protocol/ABI/schema validators;
- lifecycle/failure/backpressure/rollback suites;
- category-specific tests;
- report schema with host/SDK/extension versions;
- sample GitHub Actions workflow.

**Required test classes**

- describe/probe/plan no side effects;
- prepare/apply/verify/stop/rollback idempotency;
- malformed/stale/replay/other-Seat input;
- timeout/hang/crash/flood;
- resource cleanup/no orphan;
- permission denial;
- x86/x64 where relevant;
- package/update compatibility;
- diagnostics/redaction;
- performance budget declaration.

**Invariants**

- passing conformance does not automatically make a game/profile `Supported`;
- test runner cannot execute arbitrary package commands;
- failures are reproducible and machine-readable;
- host can require minimum conformance version.

**Done when**

Every sample extension category passes the kit and deliberate broken samples fail for the expected invariant.

**Suggested commit**

`test: implement P9-TEST-01 extension conformance kit`

---

## P9-SEC-01 — Extension ecosystem threat model and security review

**State:** BLOCKED

**Goal**

Review the SDK/package/runner/trust boundaries before encouraging third-party use.

**Depends on**

- P9 SDK/package/RPC/capability/registry/conformance implementations

**Threats to cover**

- malicious/tampered package;
- publisher/signing compromise;
- path/DLL search hijack;
- protocol spoof/replay/flood;
- Seat/runtime data exfiltration;
- arbitrary process/driver/elevation request;
- extension escape from assumed restrictions;
- update/catalog compromise;
- diagnostics/network privacy;
- host/plugin version confusion;
- rollback persistence and orphan processes.

**Outputs**

- `docs/security/EXTENSION_THREAT_MODEL.md`;
- mitigations and residual risks;
- fuzz targets for parsers/protocols;
- secure defaults/checklist;
- external review issues where possible;
- incident/revocation response.

**Done when**

Critical/high findings are fixed or the affected feature remains disabled/experimental.

**Suggested commit**

`docs: complete P9-SEC-01 extension threat model`

---

## P9-DOC-01 — SDK documentation and samples

**State:** BLOCKED

**Goal**

Make correct extension development easier than bypassing the architecture.

**Depends on**

- stable Phase 9 contracts and conformance kit

**Documentation**

- SDK overview and architecture;
- package/permission/trust model;
- lifecycle and rollback;
- capability planning;
- Seat scoping/privacy;
- category guides;
- profile authoring;
- diagnostics/testing/conformance;
- versioning/deprecation/migration;
- security/anti-cheat/clean-room rules;
- troubleshooting.

**Samples**

- out-of-process read-only status extension;
- fake display adapter;
- fake controller adapter;
- provider adapter using controlled test catalog;
- shell metrics panel;
- declarative compatibility profile;
- diagnostics exporter without network access.

**Invariants**

- samples compile/run in CI;
- no sample weakens trust or bypasses planner;
- no proprietary/unlicensed source copied;
- limitations and unsupported behaviors explicit.

**Done when**

A clean external checkout can build, test, package, and run the sample extensions using published instructions.

**Suggested commit**

`docs: implement P9-DOC-01 SDK guides and samples`

---

## P9-CLOSE-01 — Phase 9 closure

**State:** BLOCKED

**Closure checklist**

- public SDK/version policy stable;
- package/trust/permission/runner boundaries implemented;
- declarative profiles and adapter/shell/diagnostic SDKs available;
- conformance kit and samples pass;
- extension install/update/remove/rollback passes;
- threat model critical/high findings resolved or features disabled;
- compatibility matrix records extension versions/trust;
- core remains functional with all extensions disabled/offline;
- Phase 10 receives stable release/compatibility/security inputs.

**Done when**

Phase 9 is complete and Phase 10 becomes current.

**Suggested commit**

`docs: close Phase 9 compatibility SDK`
