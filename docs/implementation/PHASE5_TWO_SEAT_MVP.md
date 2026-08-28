# Phase 5 — Real Two-Seat Gaming MVP

## Phase objective

Turn the Phase 3/4 foundations into the smallest complete real gaming product proof: exactly two local Seats, real games, independent Seat lifecycle, objective input evidence, required controller/audio routing, and verified return to ordinary Windows.

Phase 5 is deliberately small. It does not require a large game catalog, community service, polished installer, or full same-game setup manager. It proves that the end-to-end technical product works on real hardware before expanding UX/catalog scope.

## Phase exit gate

Phase 5 closes only when:

1. v1 rejects more than two active Seats;
2. selected-game preflight checks only required devices/capabilities;
3. two different real non-protected games run concurrently on separate physical Seat display groups;
4. receiver-aware input evidence shows no measured cross-Seat bleed for the declared test configuration;
5. controller/audio routes work where the selected scenarios require them;
6. Seat 1 can stop/change while Seat 2 continues and vice versa;
7. target restart/device reconnect is deterministic;
8. both Seats ending or explicit Return to Windows restores ordinary Windows with verified postconditions;
9. resource/latency/queue/drop data is recorded truthfully;
10. a Phase-close verification passes.

---

## P5-AUD-01 — Audio endpoint inventory and Seat assignment validation

**State:** CODE_COMPLETE

**D-051 automated-development rule**

P4-CLOSE-01 is still blocked by deferred physical/manual acceptance, but this packet is read-only and independently testable. Its implementation may reach `CODE_COMPLETE` without implying that Phase 4 is closed or that two physical audio outputs have been acceptance-tested.

**Goal**

Provide stable read-only audio endpoint identity and validate optional Seat audio assignments without requiring audio for games that do not need a declared separate route.

**Depends on**

- P4-CLOSE-01

**Implement**

- Core Audio render/capture enumeration;
- stable endpoint ID plus bounded friendly metadata;
- default/disabled/disconnected state;
- Seat assignment validation for at most two active Seats;
- change notifications and deterministic snapshots.

**Invariants**

- friendly name/order is not stable identity;
- a saved Seat may omit audio;
- launch preflight decides whether the selected game/scenario requires a distinct endpoint;
- no endpoint routing mutation in this packet.

**Done when**

Audio inventory/assignment validation passes deterministic tests and a real two-endpoint machine records stable identities without mutation.

**Automated implementation evidence — 2026-08-28**

- `hydra_audio_inventory` separates portable normalization/Seat validation from the native Core Audio source. Persistent identity is the Core Audio endpoint ID; friendly name, default role, enumeration order, and state remain observations only.
- Native enumeration is read-only and covers render/capture endpoints across active, disabled, not-present, and unplugged states. Default console/multimedia/communications roles are observed without changing Windows policy.
- `IMMNotificationClient` runs behind a bounded generation counter. Snapshot refresh retries only when topology changes during the read, so callbacks never perform UI/disk/network/routing work.
- Audio may remain unset on a saved Seat. A configured but unplugged/disabled endpoint is structurally valid but currently not ready; missing/wrong-flow IDs and more than two active Seats fail explicitly.
- `hydra_audio_diag --list` is read-only. On the current Windows development machine it successfully observed 37 Core Audio endpoints with notifications available; this is native integration evidence, not the deferred two-physical-output acceptance gate.
- MSVC x64 and Win32/x86 focused P5-AUD-01 builds/tests pass 2/2. The strict MinGW P5-AUD-01 targets also build and pass 2/2.
- No default-device, volume, session, or per-process routing mutation exists in this packet. Those capabilities remain P5-AUD-02.

Physical confirmation of two selected endpoints, reconnect behavior, and any audible routing result remains deferred manual evidence before `VALIDATED`.

---

## P5-AUD-02 — Per-process audio routing capability backends

**State:** CODE_COMPLETE

**D-051 automated-development rule**

The routing contract, exact process/session ownership checks, fake/controlled backends, apply/verify/rollback state machine, and documented supported provider/Windows backends may be implemented before audible two-output acceptance. A missing safe public Windows mutation path remains an explicit unsupported capability rather than being replaced with undocumented/private policy COM interfaces.

**Goal**

Route each selected game's supported audio sessions to its Seat endpoint only when the exact Windows/game/provider path can be verified.

**Depends on**

- P5-AUD-01
- P4-PROC-01

**Implement**

- typed audio-routing capability backend;
- process/session ownership validation;
- apply/verify/rollback state;
- app-created-later session observation;
- unsupported path stays explicit.

**Invariants**

- process ownership precedes audio-session mutation;
- unrelated sessions are untouched;
- unsupported per-app routing cannot be promoted to success;
- previous endpoint policy is captured/restored where the Windows API permits it.

**Done when**

Two controlled/real application sessions can be routed to separate physical outputs and restored without modifying unrelated audio sessions.

**Automated implementation evidence — 2026-08-28**

- `hydra_audio_routing` adds a typed route request/status/backend transaction separate from endpoint inventory. It can wait for a late-created game session, recognize an already-correct route without mutation, apply a selected mutable backend, receiver-verify the result, and verify rollback against captured pre-apply endpoint evidence.
- Ownership is fail-closed: a candidate session must match an exact HydraSeat-owned process by PID plus process creation time. PID-only evidence, PID reuse, system-sounds sessions, and Core Audio sessions reported as spanning multiple processes are never promoted to mutable ownership.
- The native Windows observer uses the documented `IAudioSessionManager2` / `IAudioSessionEnumerator` / `IAudioSessionControl2` read-only path across active render endpoints. `hydra_audio_diag --sessions` succeeds on the development Windows machine and records exact process identity where Windows permits it.
- The production `ObserveOnlyRouteBackend` reports `SatisfiedWithoutMutation` only when all exact owned sessions already render on the requested endpoint. If they do not, it reports explicit `Unsupported`; HydraSeat does not use undocumented/private audio-policy COM interfaces to pretend arbitrary cross-endpoint movement is supported.
- A typed fake mutable backend covers capture-before-mutation, exact root/child ownership, unrelated-session preservation, successful apply verification, partial-apply failure rollback, false-success rejection, rollback verification, and `RecoveryRequired` when rollback cannot be proven.
- Microsoft-documented process-loopback activation was reviewed as a possible future experimental backend. It provides process-tree render **capture** independent of endpoint; it is not treated as proof of arbitrary endpoint routing and is not enabled by this packet.
- MSVC x64 and Win32/x86 full suites pass 78/78. Focused P5-AUD-02 tests pass 1/1 on x64, x86, and strict MinGW.

Physical audible two-output routing remains deferred manual evidence. `CODE_COMPLETE` therefore means the capability boundary, observer, transaction, controlled backend behavior, and explicit unsupported production path are complete; it does not claim that arbitrary Windows games can already be moved between audio endpoints.

---

## P5-CTRL-01 — Production controller source and per-process routing

**State:** CODE_COMPLETE

**D-051 automated-development rule**

P3-CTRL-01 and P3-CTRL-02 are `VALIDATED`, and P4-SEAT-01 is `CODE_COMPLETE`. Production controller inventory/polling, stable source identity, per-Seat mapping, synthetic/controlled state/vibration routing, and reconnect generation handling may proceed to `CODE_COMPLETE`; physical controller/vibration acceptance remains a later validation gate.

**Goal**

Move controller compatibility from controlled Phase 3 models into the production two-Seat runtime for explicitly supported controller API paths.

**Depends on**

- P4-SEAT-01
- P3-CTRL-01
- P3-CTRL-02

**Implement**

- physical source inventory/poll worker outside latency-sensitive Raw Input callbacks;
- stable source identity separate from transient XInput slot hint;
- per-Seat logical controller mapping;
- exact capability selection for XInput/DirectInput paths;
- vibration routing only to the exact owned source;
- disconnect/reconnect generation handling.

**Invariants**

- XInput, DirectInput, Raw HID, SDL, and vendor APIs remain distinct capabilities;
- no controller is silently shared across exclusive Seats;
- reconnect cannot resurrect stale generation state;
- physical vibration evidence is separate from synthetic state tests.

**Done when**

The chosen MVP controller scenarios demonstrate correct Seat ownership/state/vibration where applicable with zero cross-Seat receiver evidence.

**Automated implementation evidence — 2026-08-28**

- `hydra_controller_runtime` separates bounded source polling from the Raw Input callback path, tracks deterministic source snapshots, and advances a source generation on disconnect/reconnect or runtime-route change so stale controller state cannot silently resurrect.
- XInput slot numbers are explicitly `RuntimeOnly` hints. They can be selected only as an explicit current-session mapping and are never promoted to a persistent controller ID by this layer. DirectInput attached game-controller instance GUIDs are exposed as stable API-specific identities; a future optional GameInput backend can supply its stronger device identity through the same source interface without changing the Seat contract.
- Binding plans enforce exactly two v1 Seats, reject duplicate/ambiguous/shared exclusive sources, keep API surfaces distinct, and refuse a DirectInput source to masquerade as XInput state.
- `SeatControllerRuntime` feeds selected normalized XInput state/capabilities/battery into independent process-local `VirtualXInputContext`s and owns the entire vibration request sequence. Vibration is emitted only after current Seat binding, source key, source generation, mapping generation, and API capability all agree.
- Controlled tests prove two Seat contexts retain distinct state, Seat 1 vibration cannot be replayed onto Seat 2, disconnect clears state with the old exact generation, reconnect requires a newer generation, and post-reconnect vibration is regenerated from the current mapping.
- The native backend read-only scan uses XInput for four runtime slots and DirectInput for stable attached-controller GUID inventory. `hydra_controller_diag --list` on the current development PC reports all four XInput slots as `runtime-only / disconnected`, which is truthful native integration evidence rather than a fabricated physical-controller pass.
- Microsoft GameInput was reviewed for a future stable-ID backend. Current public device information includes `deviceId`/`deviceRootId`/`containerId`, but the latest PC deployment model adds a redistributable dependency; P5-CTRL-01 therefore keeps that backend optional rather than making it a v1 core prerequisite.
- MSVC x64 and Win32/x86 full suites pass 80/80. Focused P5-CTRL-01 tests pass 2/2 on x64, x86, and strict MinGW with no strict-warning output after cleanup.

Physical controller connection/reconnect and physical vibration observation remain deferred manual evidence before `VALIDATED` or a compatibility claim for a concrete controller/game path.

---

## P5-LAUNCH-01 — Minimal two-Seat launch plan and activation transaction

**State:** CODE_COMPLETE

**D-051 automated-development rule**

P3-CLOSE-01 remains blocked by deferred physical/game evidence, but the immutable plan compiler, fake/controlled resource backends, activation/rollback ordering, exact two-Seat bound, and controlled-target integration may reach `CODE_COMPLETE`. No missing physical Phase 3 evidence is re-labelled as validated by this packet.

**Goal**

Compile and execute one immutable launch/activation plan for exactly two v1 Seats using the selected game requirements and already-validated runtime capabilities.

**Depends on**

- P4-SEAT-01
- P5-AUD-02
- P5-CTRL-01
- P3-CLOSE-01

**Plan inputs**

- Seat hardware configuration;
- selected Game/target metadata;
- exact required input/controller/audio/display capabilities;
- per-Seat launch identity;
- recovery requirements;
- user-approved high-risk options if any.

**Transaction outline**

```text
Validate
 -> prepare exact owned process/display/input/controller/audio resources
 -> arm recovery where required
 -> launch Seat process trees
 -> establish window/input/controller/audio ownership
 -> verify receiver/runtime postconditions
 -> Playing

failure at any step -> reverse rollback -> verified prior/safe state
```

**Invariants**

- maximum two active Seats;
- a missing keyboard/mouse is not an error unless the selected game requires it;
- no mutation occurs from an unsupported plan;
- all mutable actions are correlated, verified, and reversible;
- one Seat's start/stop does not automatically rebuild the other healthy Seat.

**Done when**

The production host can execute a deterministic two-Seat plan with fake/controlled targets and rollback every injected failure index without cross-Seat mutation.

**Automated implementation evidence — 2026-08-28**

- `compileTwoSeatLaunchPlan` accepts exactly two active Seats, canonicalizes Seat order, strips legacy/transient `targetHwnd`, and fingerprints the immutable Seat/game/process/requirements/capability/resource contract. Stale HWND/input order does not change the plan fingerprint, while a material capability change does.
- Preflight checks only selected-game requirements: keyboard/mouse/controller/audio may be omitted when not required, while required missing devices, a primary display outside its group, unsupported required capabilities, high-risk options without explicit approval, third/one-Seat plans, and duplicate exclusive display/input/controller/audio ownership fail before any resource factory/mutation is reached.
- `PlannedSeatGameInstance` prepares every typed Seat-local resource before activation, activates/verifies in deterministic order, and rolls every resource back in exact reverse order on any injected activation failure. If rollback cannot be verified, exact resource ownership is retained and the instance remains recovery-required for a later retry.
- RuntimeHost integration uses the existing authoritative `SeatGameLifecycle`: both Seats can become `Playing`, Seat 2 can stop/restart while Seat 1 remains unchanged, and a controlled Seat 2 start failure rolls back only Seat 2 while Seat 1's exact resource set remains active.
- Natural game process exit no longer assumes that display/input/controller/audio state is already safe. `observeTargetExit` and reconcile now execute the same idempotent Seat-local `stop()` rollback before accepting the Seat as `Idle`; controlled tests prove the other Seat remains `Playing` during this cleanup.
- The controlled resource matrix covers recovery, process, window, display, input, controller, and audio resources. Every activation failure index is injected and verified to reverse-roll back the complete prepared Seat resource set.
- MSVC x64 and Win32/x86 full suites pass 81/81. Focused P5-LAUNCH-01 passes on x64, x86, and strict MinGW; the existing P4 Seat/IPC/watchdog regressions remain green after the natural-exit cleanup strengthening.

This is controlled/fake resource evidence. Real-game process/window/input/controller/audio/display activation remains deferred physical/game validation and is not implied by `CODE_COMPLETE`.

---

## P5-MET-01 — Integrated session metrics and zero-bleed report

**State:** READY

**D-051 automated-development rule**

The report schema, bounded recorder/aggregator, controlled receiver-evidence correlation, queue/drop/loss and launch/rollback timing, controller/audio outcome fields, privacy redaction, and synthetic/controlled resource samples may proceed to `CODE_COMPLETE`. Physical zero-bleed and real-game latency/resource claims remain deferred validation evidence.

**Goal**

Extend Phase 3 metrics into a complete two-Seat runtime report that distinguishes expected routing from receiver-verified evidence.

**Depends on**

- P5-LAUNCH-01
- P3-MET-01

**Record**

- physical observation and route stages;
- target receiver/apply evidence where available;
- cross-Seat/process counts;
- queue high-water/drop/loss;
- input latency percentiles;
- launch/start/stop/rollback durations;
- bounded CPU/memory samples;
- controller/audio route outcomes for the scenario.

**Privacy**

Default report remains redacted: no typed text, credentials, Player names, personal paths, or unnecessary stable serials.

**Done when**

A machine-readable report can prove or disprove the declared MVP isolation/resource criteria without inferring zero bleed from missing receiver evidence.

---

## P5-MVP-01 — Controlled/open-source end-to-end two-Seat session

**State:** BLOCKED

**Goal**

Run the complete production host path first against deterministic controlled/open-source targets before commercial game validation.

**Depends on**

- P5-LAUNCH-01
- P5-MET-01

**Acceptance**

- two physical Seat display groups;
- two physical input sets where the profile requires them;
- independent Seat start/stop/restart;
- target windows remain in owned groups;
- receiver-verified zero-cross evidence;
- selected audio/controller paths where declared;
- one Seat exits while the other continues;
- both end -> verified ordinary Windows restore;
- no owned orphan process/window/helper.

**Done when**

The complete production lifecycle passes with a reproducible evidence bundle on real hardware using controlled/open-source targets.

---

## P5-MVP-02 — Two different non-anti-cheat game MVP

**State:** BLOCKED

**Goal**

Prove the actual v1 baseline: two people run two different real non-protected games concurrently on one sufficiently capable PC.

**Depends on**

- P5-MVP-01
- P3-E-02
- P3-E-03

**Acceptance**

For two explicitly named games/builds/providers:

- both launch through the production host;
- each is placed on the correct Seat display group;
- required input/controller/audio path is declared and verified;
- measured cross-Seat input evidence is zero for the test run;
- one game can exit/change without stopping the other;
- repeated launch/stop cycles do not leak owned helpers;
- CPU/GPU/memory/load are recorded as evidence, not promised universally;
- protection/account/license limitations are documented.

**Done when**

A real two-game/two-player scenario completes the full independent Seat lifecycle with objective evidence and verified final rollback.

---

## P5-HOT-01 — Runtime restart and device reconnect recovery

**State:** BLOCKED

**Goal**

Prove that normal household disruptions do not require reboot/manual developer cleanup.

**Depends on**

- P5-MVP-02
- P4-REC-01

**Matrix**

- one game exits and restarts;
- one Seat keyboard/mouse disconnect/reconnect;
- controller disconnect/reconnect;
- audio endpoint disappearance/reappearance where used;
- display reconnect regression;
- launcher child recreation;
- UI close/reopen;
- host/recovery fault path.

**Invariants**

- healthy other Seat continues whenever shared global safety is intact;
- stable identity, not enumeration order, controls reassociation;
- unsafe ambiguity becomes degraded/recovery, not silent reroute.

**Done when**

The declared reconnect/restart matrix completes with no cross-Seat reassignment and no manual process killing/reboot.

---

## P5-UI-01 — MVP start/stop/status control surface

**State:** BLOCKED

**Goal**

Expose only the minimal user controls required to run and diagnose the MVP without turning the main UI into a low-level Windows configuration console.

**Depends on**

- P5-MVP-02
- P4-CTRL-01
- P4-SEAT-01

**Normal surface**

- Seat 1 / Seat 2 state;
- current Player placeholder/identity where available;
- current selected game;
- Play/Stop for the relevant Seat;
- whole-machine `Return to Windows`;
- obvious missing-requirement message with link to Seat settings;
- degraded/recovery state.

Backend/protocol/device-path detail remains under Diagnostics.

**Done when**

A non-developer tester can run the Phase 5 scenarios and understand which Seat/game failed without using a developer CLI for normal operation.

---

## P5-COMPAT-01 — MVP compatibility and hardware evidence matrix

**State:** BLOCKED

**Goal**

Publish exact evidence for the small MVP set without inventing an official support badge.

**Depends on**

- P5-MVP-02
- P5-HOT-01

**Record**

- game/build/provider;
- Windows/HydraSeat version;
- test hardware class/topology;
- required compatibility paths;
- launch/input/controller/audio/exit results;
- measured latency/bleed/resource values;
- limitations/protection state;
- evidence date and report reference.

`Compatible in this evidence set` is not a universal guarantee and is not `HydraSeat Certified`.

**Done when**

The MVP evidence matrix can be reproduced and clearly distinguishes tested, failed, untested, and protected/experimental scenarios.

---

## P5-CLOSE-01 — Phase 5 closure

**State:** BLOCKED

**Goal**

Run a dedicated Phase-close verification across the complete real two-Seat gaming MVP.

**Depends on**

- P5-COMPAT-01
- P5-UI-01

**Verify**

- exactly two active Seats;
- requirement-aware preflight;
- real two-game concurrent run;
- independent Seat stop/change/restart;
- objective input evidence;
- selected audio/controller evidence;
- recovery/reconnect;
- ordinary Windows final state;
- resource/latency evidence;
- README/roadmap/status truth.

**Done when**

Phase 5 has a recorded passing close review and Phase 6 can build the repeatable game/Player/setup product UX on a proven technical MVP.