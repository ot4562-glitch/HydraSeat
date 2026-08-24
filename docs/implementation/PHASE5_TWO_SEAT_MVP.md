# Phase 5 — Two-Seat Gaming MVP

## Phase objective

Deliver the first end-to-end local multiseat session that a user can intentionally start and stop: two different supported targets on disjoint Seat display groups with independent keyboard/mouse, controller where required, audio routing where supported, runtime diagnostics, and clean recovery.

This phase is an MVP with an explicit compatibility matrix, not universal support.

## Phase exit gate

Phase 5 is complete when:

1. two configured Seats can start from one validated launch plan;
2. target A and target B are different applications/games;
3. windows remain in their Seat display groups;
4. keyboard/mouse zero-bleed criteria from Phase 3 pass;
5. selected controller API routing passes when used;
6. audio is routed to declared endpoints or the plan refuses unsupported routing;
7. host/UI/watchdog state agrees throughout start/active/stop;
8. one target restart and one input/display/audio reconnect recover;
9. latency/drop/bleed/resource metrics are recorded;
10. compatibility claims are versioned matrix entries;
11. stop/reset restores normal Windows behavior.

## Dependency graph

```text
P4-CLOSE-01 + P3-CLOSE-01
          |
          +-> P5-AUD-01 -> P5-AUD-02
          +-> P5-CTRL-01
          +-> P5-LAUNCH-01

P5-AUD-02 + P5-CTRL-01 + P5-LAUNCH-01
          -> P5-MVP-01 -> P5-MVP-02

P5-MET-01 -------------------+
P5-HOT-01 -------------------+-> P5-COMPAT-01 -> P5-CLOSE-01
P5-UI-01 --------------------+
```

---

## P5-AUD-01 — Audio endpoint inventory and Seat assignment validation

**State:** BLOCKED

**Goal**

Enumerate audio render/capture endpoints with stable identities and validate Seat ownership without changing routing.

**Depends on**

- P4-RUN-01
- P2-SEAT-01

**Create/modify**

- `include/hydra/audio_topology.hpp`
- `src/audio_topology.cpp`
- `include/hydra/audio_identity.hpp`
- `hydra_audio_diag.exe` or `hydra_diag audio`;
- tests with fake endpoint enumerator.

**Data model**

```cpp
struct AudioEndpointIdentity;
struct AudioEndpoint;
struct AudioTopologySnapshot;
struct SeatAudioAssignment;
```

Fields:

- stable endpoint ID;
- render/capture role;
- active/disabled/unplugged state;
- friendly/device/interface metadata;
- default role flags;
- supported format summary where safely queryable;
- topology generation.

**Implementation skeleton**

1. initialize COM on a dedicated audio thread;
2. enumerate endpoints and subscribe to topology/default/state/property notifications;
3. queue bounded events into the host;
4. resolve profile endpoint IDs;
5. validate exclusive/share policy;
6. expose missing/degraded audio assignment;
7. perform no route mutation.

**Invariants**

- endpoint ID, not friendly name/order, is persistent identity;
- COM callbacks do minimal work;
- endpoint disappearance is explicit;
- render and capture ownership are separate;
- default device is not silently substituted for a required endpoint.

**Automated tests**

- duplicate friendly names;
- render/capture distinction;
- state change/default change/reconnect;
- missing profile endpoint;
- deterministic snapshot order;
- COM thread startup/shutdown.

**Manual acceptance**

USB headset + speakers + two microphones where available.

**Done when**

Seat audio assignments resolve to stable current endpoints and expose precise availability.

**Suggested commit**

`feat: implement P5-AUD-01 audio topology inventory`

---

## P5-AUD-02 — Per-process audio routing capability backends

**State:** BLOCKED

**Goal**

Route supported Seat-owned processes to their assigned render/capture endpoints, or fail before launch when the current backend/Windows build cannot guarantee it.

**Depends on**

- P5-AUD-01
- P4-PROC-01
- P8-WATCH-01 for persistent/elevated backends

**Create/modify**

- `include/hydra/audio_routing_backend.hpp`
- `src/audio_routing_registry.cpp`
- documented backend implementations/probes;
- fake backend/tests;
- planner capabilities and profile fields.

**Backend contract**

```cpp
struct AudioRouteRequest;
struct AudioRouteSnapshot;
struct AudioBackendDescriptor;
class IAudioRoutingBackend;
```

Operations:

- probe support for Windows build/process type;
- capture current route/default context;
- apply render/capture route;
- verify active audio session endpoint where possible;
- restore prior state;
- report restart requirement or unsupported behavior.

**Implementation skeleton**

1. research and select only a lawful/testable backend path;
2. separate documented, undocumented-but-user-approved, bridge, and user-assisted capabilities;
3. make the default backend read-only/unsupported rather than guessing;
4. tie routes to process identity, not just executable name;
5. wait for audio session creation with bounded timeout;
6. restore persistence changes on stop/rollback;
7. expose actual/desired route in runtime state.

**Invariants**

- unsupported routing blocks profiles that require it;
- no global default endpoint change is used as silent per-process routing;
- a Seat route never captures another Seat's session;
- audio bridge latency is measured and capability-specific;
- rollback is idempotent.

**Automated tests**

- fake process/session route lifecycle;
- process restart/new audio session;
- endpoint disconnect;
- partial render/capture support;
- apply failure rollback;
- unsupported Windows/build result.

**Manual acceptance**

Two target processes simultaneously producing audio to two endpoints; capture routing if claimed.

**Done when**

At least one tested routing path works for the MVP matrix and unsupported cases fail before session activation.

**Suggested commit**

`feat: implement P5-AUD-02 process audio routing`

---

## P5-CTRL-01 — Production controller source and per-process routing

**State:** BLOCKED

**Goal**

Move controlled Phase 3 controller state into the production host/profile/backend path.

**Depends on**

- P3-CTRL-01
- P4-RUN-01
- P4-PROC-01

**Create/modify**

- `include/hydra/controller_topology.hpp`
- `src/controller_topology.cpp`
- `include/hydra/controller_routing_backend.hpp`
- physical XInput polling worker;
- adapter/profile integration;
- tests.

**Implementation skeleton**

1. enumerate/poll controller sources outside input callbacks;
2. normalize state with device generation/connection identity;
3. map Seat physical sources to process-local logical slots;
4. route vibration back to the owning source;
5. select XInput/DirectInput/Raw HID backend by profile;
6. clear state on disconnect and require reconnect confirmation;
7. expose unsupported API paths.

**Invariants**

- physical source is owned by at most one exclusive Seat;
- logical slot mapping is per process/profile;
- vibration never crosses Seats;
- hot-plug does not reuse stale state;
- Raw HID/SDL is not claimed from XInput success.

**Automated tests**

- two physical/synthetic sources to two process-local slot 0 views;
- disconnect/reconnect/generation;
- vibration routing;
- process restart;
- backend unavailable;
- metrics correlation.

**Manual acceptance**

Two different controllers where possible; identical-model controllers included in the matrix.

**Done when**

The MVP selected targets receive only their declared controllers through the tested API.

**Suggested commit**

`feat: implement P5-CTRL-01 production controller routing`

---

## P5-LAUNCH-01 — Minimal two-Seat launch plan and activation transaction

**State:** BLOCKED

**Goal**

Create one immutable plan that prepares and activates two Seat targets with input, displays, windows, controllers, audio, diagnostics, and rollback actions.

**Depends on**

- P4-RUN-01, P4-PROC-01, P4-POL-01
- P3 planner/compatibility contracts
- P5-AUD-02 where audio required
- P5-CTRL-01 where controller required

**Create/modify**

- `include/hydra/seat_launch_plan.hpp`
- `src/seat_launch_plan.cpp`
- `include/hydra/activation_transaction.hpp`
- `src/activation_transaction.cpp`
- fake action/backend tests.

**Plan structure**

```cpp
struct TargetLaunchPlan;
struct SeatLaunchPlan;
struct SessionLaunchPlan;
struct PreparedAction;
struct RollbackAction;
```

**Ordered transaction**

1. validate profile/topology/backends/privilege/recovery;
2. capture prior process/window/display/audio/device state;
3. start watchdog lease and diagnostics;
4. create process groups and controlled adapters;
5. launch targets and confirm windows;
6. place windows/display policy;
7. apply audio/controller routes;
8. enable input replacement path;
9. enable physical cloaking/suppression last when required;
10. run self-test and commit `Active`;
11. rollback reverse order on any failure.

**Invariants**

- plan is immutable after preparation;
- action has prepare/apply/verify/rollback result;
- rollback runs even after partial startup;
- one Seat failure cannot leave the other secretly active unless profile explicitly permits degraded mode;
- session state reflects the actual committed action set.

**Automated tests**

- fail each action index and verify reverse rollback;
- rollback action failure -> `RecoveryRequired`;
- duplicate start/stop;
- target exits during activation;
- display/audio/device disappears;
- deterministic plan hash.

**Done when**

A fake and controlled two-Seat session starts/stops through one transaction with complete evidence.

**Suggested commit**

`feat: implement P5-LAUNCH-01 session activation transaction`

---

## P5-MET-01 — Integrated session metrics and zero-bleed report

**State:** BLOCKED

**Goal**

Combine Phase 3 input metrics with process/window/display/controller/audio/runtime evidence into one MVP report.

**Depends on**

- P3-MET-01
- P4 runtime snapshots
- P5 audio/controller contracts

**Create/modify**

- `include/hydra/session_metrics.hpp`
- `src/session_metrics.cpp`
- diagnostic bundle/report schema;
- report CLI/tests.

**Report sections**

- session/profile/build/topology;
- activation action timings/results;
- per-Seat input latency/drop/bleed;
- process/window/display state;
- controller mapping/vibration events;
- audio endpoint/session routing and measured delay where available;
- CPU/memory/queue high-water;
- reconnect/restart/rollback events;
- guarantee/support level.

**Invariants**

- report names missing measurements;
- no unsupported metric defaults to zero;
- timestamps and correlation IDs align;
- bounded storage/rotation;
- privacy mode documented.

**Done when**

Every MVP run produces a comparable machine-readable and human summary.

**Suggested commit**

`feat: implement P5-MET-01 session evidence report`

---

## P5-MVP-01 — Controlled/open-source end-to-end two-Seat session

**State:** BLOCKED

**Goal**

Exercise the production runtime and activation transaction with two controlled or open-source targets before commercial games.

**Depends on**

- P5-LAUNCH-01
- P5-MET-01
- required audio/controller packets
- P4-REC-01

**Test topology**

- two physical display groups;
- two keyboard/mouse sets;
- two audio endpoints when audio routing claimed;
- two controllers when controller routing claimed;
- two distinct target applications.

**Acceptance**

- planner result supported/experimental as intended;
- activation/rollback transaction passes;
- no cross-Seat input/controller events;
- windows stay in display groups;
- audio routes as declared;
- latency and resource budgets recorded;
- target restart and one device reconnect succeed;
- no orphan process/window/route after stop.

**Done when**

The complete MVP architecture works with inspectable targets and a reproducible evidence bundle.

**Suggested commit**

`test: validate P5-MVP-01 controlled two-Seat session`

---

## P5-MVP-02 — Two different non-anti-cheat game MVP

**State:** BLOCKED

**Goal**

Run two different game profiles concurrently through the production runtime.

**Depends on**

- P5-MVP-01
- P3-E-02/P3-E-03
- P5-AUD-02/P5-CTRL-01 as required

**Rules**

- exact versions and launcher/provider recorded;
- no protected-process bypass;
- each workaround is a profile field/backend capability;
- first result is `Experimental`;
- unsupported exclusive fullscreen/audio/controller path blocks or changes the declared profile mode;
- user sees risk/requirements before start.

**Manual acceptance**

Minimum initial run:

- 30 minutes active interaction;
- 30 start/stop cycles;
- one target restart;
- one input/display/controller/audio reconnect relevant to the profile;
- emergency reset drill;
- zero measured cross-Seat input/controller bleed.

**Done when**

Two explicit games meet the MVP criteria and appear in the compatibility matrix.

**Suggested commit**

`test: validate P5-MVP-02 two-game session`

---

## P5-HOT-01 — Runtime restart and device reconnect recovery

**State:** BLOCKED

**Goal**

Prove the active MVP can survive expected transient failures without silent cross-Seat fallback.

**Depends on**

- P5-LAUNCH-01
- P4-DIS-03
- P5-AUD-01
- P5-CTRL-01
- P8-WATCH-01

**Matrix**

- keyboard/mouse reconnect;
- controller reconnect;
- audio endpoint reconnect;
- secondary/primary display reconnect;
- target process restart;
- UI restart;
- host restart/reconcile according to supported policy.

**Invariants**

- missing device does not migrate to another Seat;
- stale state clears;
- recovery uses stable identity/generation;
- unsupported recovery produces degraded/stop decision;
- reconnect actions are bounded and visible.

**Done when**

The selected MVP matrix survives the declared transient failures or stops safely.

**Suggested commit**

`test: implement P5-HOT-01 MVP reconnect recovery`

---

## P5-UI-01 — MVP start/stop/status control surface

**State:** BLOCKED

**Goal**

Provide a truthful user workflow for planning, starting, monitoring, stopping, and resetting the MVP session.

**Depends on**

- P4-IPC-01
- P5-LAUNCH-01
- P5-MET-01

**UI states**

- profile/topology selection;
- preflight backend/risk/unsupported report;
- action progress;
- per-Seat process/window/display/input/controller/audio state;
- latency/drop/bleed warning summary;
- degraded/recovery-required state;
- stop and emergency reset;
- link/export diagnostic bundle.

**Invariants**

- UI reflects host snapshot, not optimistic local state;
- start disabled on missing required capabilities;
- risky operations require explicit confirmation;
- stop/reset remains accessible during degraded state;
- closing UI does not stop the host/session unless requested.

**Automated tests**

- view-model/state tests with fake host client;
- disconnect/reconnect/resnapshot;
- progress/error/recovery states;
- accessibility/DPI basics.

**Done when**

A user can run the MVP without command-line orchestration and without false success text.

**Suggested commit**

`feat: implement P5-UI-01 MVP control surface`

---

## P5-COMPAT-01 — MVP compatibility and hardware matrix

**State:** BLOCKED

**Goal**

Publish explicit scope instead of universal claims.

**Create/modify**

- `docs/compatibility/README.md`
- machine-readable matrix schema/data;
- validation CLI/test;
- hardware matrix and evidence links.

**Entry fields**

- target/game/version/provider;
- Windows build;
- CPU/GPU/driver;
- displays/DPI/mode;
- input/controller/audio devices;
- profile/backends/versions;
- support level;
- required manual steps;
- known limits;
- evidence date/run/bundle;
- regression status.

**Invariants**

- README claims derive from matrix data;
- expired/untested entries are not `Supported`;
- protected/anti-cheat target is clearly blocked/observation-only;
- evidence is reproducible and privacy-reviewed.

**Done when**

The MVP has two target entries and one reference hardware topology entry.

**Suggested commit**

`docs: publish P5-COMPAT-01 MVP compatibility matrix`

---

## P5-CLOSE-01 — Phase 5 closure

**State:** BLOCKED

**Closure checklist**

- two different games/applications meet the declared MVP criteria;
- input/controller/audio/display support is explicit and measured;
- start/stop/reset UI uses host state;
- reconnect/restart/recovery evidence passes;
- performance/drop/bleed report retained;
- compatibility matrix is machine-validated;
- unsupported configurations block before activation;
- normal Windows state restored after stop/reset;
- Phase 6 receives stable activation/profile contracts.

**Done when**

Phase 5 is complete and Phase 6 becomes current.

**Suggested commit**

`docs: close Phase 5 two-Seat gaming MVP`
