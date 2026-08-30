# Nucleus Co-op 2.4.2 Source Research Record

## Purpose

This document preserves the detailed source-review findings from the local Nucleus Co-op 2.4.2 archive studied on 2026-08-29. It is a clean-room research artifact for HydraSeat, not an implementation source and not a dependency specification.

The useful output of this review is the set of observable architecture requirements, failure modes, compatibility surfaces, and regression-test ideas that Nucleus exposes after years of practical multi-instance game compatibility work.

HydraSeat must not copy, translate line-by-line, vendor, compile against, or otherwise make this GPL source a build input unless the project explicitly adopts a compatible licensing decision. The intended use is:

```text
Nucleus source observation
  -> neutral behavior/capability requirement
  -> HydraSeat-native design
  -> independently written implementation
  -> controlled/physical/game evidence
```

`docs/CLEAN_ROOM_POLICY.md`, the current product specification, and the non-negotiable design decisions remain authoritative.

## Provenance and license boundary

Local research archive:

```text
C:\HydraSeat\references\NucleusCoop\splitscreenme-nucleus-2.4.2\
  splitscreenme-nucleus-2.4.2\
```

Archive label: `splitscreenme-nucleus-2.4.2`.

The archive does not contain `.git` metadata, so an exact upstream commit cannot be derived from this archive alone. Do not conflate this record with the separate Nucleus revision already recorded in `docs/RELATED_SYSTEMS_RESEARCH.md`.

Upstream repository reference:

```text
https://github.com/SplitScreen-Me/splitscreenme-nucleus
```

The archive's top-level `LICENSE` is GNU GPL version 3. The review copied no implementation text into HydraSeat code, adapted no source, changed no reference file, and executed no third-party binary.

Nucleus also contains or integrates multiple third-party components with their own provenance. Their presence in the Nucleus tree is not permission for HydraSeat to reuse them.

## Source areas consulted

The detailed review consulted the following archive files. SHA-256 values are recorded to make the evidence reproducible even if another archive with the same directory label later appears.

| Concern | Source file | SHA-256 |
| --- | --- | --- |
| Project behavior overview | `README.md` | `9612ecc35cf76ce3924e8d06d2f78c7541151dcce1664cd47d1a8b330f291b9d` |
| GPL license | `LICENSE` | `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986` |
| Main orchestration | `Master/NucleusGaming/Coop/Generic/GenericGameHandler.cs` | `c090ec06f98e859009efc976ffa27ee561e2d3e70897bdc8dce83cb643de3c2c` |
| Handler capability/profile model | `Master/NucleusGaming/Coop/Generic/GenericGameInfo.cs` | `0221f317a921ae029a331585a9598bba710bb2b9e7935f9f5c58df73db9ae688` |
| Handler metadata | `Master/NucleusGaming/Coop/Generic/Handler.cs` | `742625ab1f89c8a453b6750da1c7be9c3c67ffc247c96c5d2966d01d8cbac88d` |
| ProtoInput instance setup | `Master/NucleusGaming/Coop/ProtoInput/ProtoInputLauncher.cs` | `646d14c7616e04abfb5e2a54c90ce4166f84bbd23edf72bb17b48cd0bcd71e59` |
| Raw Input registration/device inventory | `Master/NucleusGaming/Coop/InputManagement/RawInputManager.cs` | `d851dfc132c7eb22bc597765b9675971edd050215f8b0bc281b2619049ac2b59` |
| Raw Input routing | `Master/NucleusGaming/Coop/InputManagement/RawInputProcessor.cs` | `76f14fecd749ac85a15d76991a781fc4359faedbeaa6da3ef1f941a9d7125ecc` |
| Message-only Raw Input sink | `Master/NucleusGaming/Coop/InputManagement/RawInputWindow.cs` | `dc2a9606e514da5f4f2143465a6706c67cdb7d72e0fda93a301f1542e4c82313` |
| Per-instance window/input state | `Master/NucleusGaming/Coop/Window.cs` | `b480337946010569fade73cf4cce1dbe591c97c0da12580e69f8e6d8a7a4c0f6` |
| Global input lock lifecycle | `Master/NucleusGaming/Coop/InputManagement/LockInputRuntime.cs` | `ac2ccd1de33f1bd29cdbccd3f165edb0da188e510a65be8b96946e83a7ab72dc` |
| Window placement/style/focus helpers | `Master/NucleusGaming/Tools/GlobalWindowMethods/GlobalWindowMethods.cs` | `26b9dcb0383f0eb59cba283ec643dc0209a82602de02e212a35ab6e1435a2afe` |
| Runtime hook injector invocation | `Master/NucleusGaming/Tools/DllsInjector/DllsInjector.cs` | `aa6ce320b567c9ef7bc27ba990a4af38c87880862da77f67db7d0a7dce5658c4` |
| EasyHook injector helper | `Master/Nucleus.Inject/Program.cs` | `19d5c057f3b30a9abb392cfe50f5e7322e8542101b2e53f5adc3c1489c350166` |
| Native hook entrypoint | `Master/Nucleus.Hook/Hook.cpp` | `2327a8d3956790cb7eda1b3890465400df59bf3475168a6f1386fcc5c0805917` |
| Raw/message/focus filtering | `Master/Nucleus.Hook/MessageFilter.cpp` | `4cfd179fda458d506b12c0e4f8c4eda8e2f35ee334172a19ca4875cdd4ac63c1` |
| Focus API virtualization | `Master/Nucleus.Hook/FocusHooks.cpp` | `7257afbdded6d529cdcf2edad333a590c8408d345e2ad986b63b5ee8675f84aa` |
| XInput/DInput remapping | `Master/Nucleus.Hook/XInputHooks.cpp` | `2b32ef6f0d641811bd40bc3233d6f5d13278673a1f1111bec72c629b5bb0dd19` |
| Hook IPC/shared memory | `Master/Nucleus.Hook/Piping.cpp` | `0cf0e9a528be9d3beeba30a3c04ca3e2ce69bf8a9e635f32be14994ceb0dbb26` |
| Start-game helper client | `Master/NucleusGaming/Tools/GameStarter/StartGameUtil.cs` | `0b589ca1c81abcfd09891138d22eb1d0d38ab82e44d918650dd9db5086b92fff` |
| Start-game helper process | `Master/StartGame/Program.cs` | `fc65c50a0b81dba1e0cf5ba533e29c2ac07fe8240c82c67caf03880cbfd5f9af` |
| Named-object inspection/close | `Master/NucleusGaming/Util/ProcessUtil.cs` | `67657e93d7eb515a236733792211473431020a3d2cea559ec4e0db12264f7be9` |

This is not a complete audit of every Nucleus component. It is a focused architecture review of the parts most relevant to HydraSeat input, launch, process/window ownership, multi-instance preparation, and recovery.

## High-level observed execution model

The practical Nucleus flow can be summarized as:

```text
JavaScript game handler
  -> GenericGameInfo mutable compatibility matrix
  -> GenericGameHandler orchestration
  -> instance filesystem/environment preparation
  -> process creation / launcher handling
  -> actual game-process discovery or reacquisition
  -> game HWND discovery
  -> startup/runtime hook selection
  -> per-instance input/controller/focus virtualization
  -> window placement and audio routing
  -> runtime maintenance
  -> broad shutdown/restoration sequence
```

The exact details vary per game handler. The important architectural fact is that successful local multi-instance gaming is not one mechanism. It is a composition of game-specific requirements across filesystem, process, window, input API, controller API, focus, audio, and cleanup surfaces.

## Finding 1: Nucleus is a compatibility orchestrator, not one isolation primitive

`GenericGameInfo.cs` exposes a very large mutable option surface covering executable selection, symlink/hardlink/copy behavior, startup arguments, process selection, window/focus behavior, Raw Input, keyboard polling, XInput, DirectInput, profile directories, separate users, network changes, hooks, timing, audio, and many game-specific exceptions.

`GenericGameHandler.cs` is a 3,629-line orchestration class that consumes this matrix and performs most of the launch lifecycle.

Engineering lesson for HydraSeat:

- compatibility must remain capability-planned;
- no single backend can truthfully claim generic input/game compatibility;
- profile/setup data must say exactly what a game requires;
- risky capabilities must be independently enableable and independently rejectable;
- HydraSeat should keep orchestration split across typed modules rather than recreate a monolithic game handler.

This strongly supports existing D-005, D-017, P6 planning, and the production launch registry design.

## Finding 2: the Nucleus handler model is powerful but too permissive for HydraSeat

`GenericGameInfo` constructs a Jint JavaScript engine with CLR access and can load additional assemblies from handler configuration. This makes a handler effectively executable extension code rather than bounded compatibility data.

That flexibility explains why Nucleus can encode unusual game-specific procedures, but it is incompatible with HydraSeat's default trust model.

HydraSeat implication:

- do not make community game/setup data arbitrary executable JavaScript, PowerShell, batch, CLR plugins, or unrestricted commands;
- preserve typed, versioned, bounded `GameRecord`, `TwoPlayerSetup`, requirement, provider-plan, and capability contracts;
- if a future escape hatch is unavoidable, make it a separately trusted optional component with explicit origin/version/hash/license/capability review rather than a normal profile field.

## Finding 3: keyboard/mouse separation is a two-layer problem

Nucleus combines host-side physical-device observation/routing with target-process compatibility hooks.

### Host-side observation and routing

`RawInputManager.cs` creates a message-only window and registers keyboard and mouse Raw Input with `RIDEV_INPUTSINK`.

`RawInputProcessor.cs` builds maps from physical Raw Input `hDevice` values to per-instance `Window` objects. Keyboard and mouse events are then routed to only the mapped instance. The implementation can send ordinary window messages, forward selected `WM_INPUT`, and send synthetic key/cursor state to the injected hook over an instance channel.

Conceptually:

```text
physical keyboard/mouse
  -> host Raw Input sink
  -> runtime hDevice lookup
  -> target instance binding
  -> target message/state delivery
```

### Target-process filtering and state virtualization

`MessageFilter.cpp` examines `WM_INPUT` and allows only the configured mouse or keyboard Raw Input handle for that injected instance when Raw Input filtering is enabled. It also filters focus/mouse messages depending on profile options.

`FocusHooks.cpp` can virtualize process-visible foreground/active/focus/capture APIs so a background game behaves as though its own window is active.

The hook IPC also carries process-local key and cursor state.

HydraSeat implication:

- physical observation, replacement delivery, target-process filtering, polling-state virtualization, and physical suppression are distinct capabilities;
- a successful Raw Input route does not prove zero bleed through `GetAsyncKeyState`, DirectInput, XInput, Raw HID, SDL, vendor APIs, or another path;
- Gate C/D evidence must continue to test the exact APIs used by the target and must not infer physical suppression from cloaking or hook installation alone.

This reinforces D-005, D-008, D-017, the existing Gate C split, and the production runtime's explicit `IProductionInputResourceBridge` fail-closed boundary.

## Finding 4: process-local polling/focus state is as important as message delivery

Nucleus does not assume that forwarding keyboard/mouse window messages is sufficient. It maintains per-instance state for APIs and behaviors that otherwise expose shared Windows state.

Observed surfaces include:

- `GetAsyncKeyState`;
- `GetKeyState`;
- `GetKeyboardState`;
- `GetCursorPos` / `SetCursorPos`;
- foreground/active/focus/capture queries;
- selected activation/focus messages;
- Raw Input message filtering.

HydraSeat implication:

A compatibility plan must model API surfaces, not a generic `KeyboardMouseIsolation=true` bit. The existing P3 polling, cursor/focus, Raw Input, XInput, and DirectInput separation is the correct direction.

## Finding 5: XInput compatibility includes more than `XInputGetState`

`XInputHooks.cpp` intercepts multiple XInput DLL variants and redirects the visible controller index per instance. It covers:

- `XInputGetState`;
- `XInputSetState` for vibration;
- `XInputGetCapabilities`;
- the commonly used extended state export at ordinal 100 (`XInputGetStateEx`) for supported XInput DLLs.

It also contains a DirectInput-to-XInput compatibility path.

HydraSeat implication:

The Seat controller contract should be consistent across state, capabilities, and vibration. Broad XInput compatibility claims should also explicitly decide whether ordinal-100 `XInputGetStateEx` is supported/tested.

A targeted search of the HydraSeat tree on 2026-08-29 found no `XInputGetStateEx` or `ordinal 100` reference. This is a **research-derived regression candidate**, not by itself proof that every current controller backend is incorrect.

The Nucleus DirectInput path selects devices using enumeration order. HydraSeat must not adopt that persistence model; D-010 stable identity remains the stronger design.

## Finding 6: the process returned by spawn is not necessarily the game process

Nucleus contains substantial logic for launcher/loader hand-off and process reacquisition. It does not assume that the first process ID returned by a launch operation remains the authoritative game process.

Observed strategies include:

- discarding known launcher/helper process handles;
- checking whether the returned PID is still alive;
- scanning by executable name;
- comparing executable path where available;
- excluding already-attached process IDs;
- using main-window presence/title for some game-specific cases;
- allowing explicit process selection as a fallback;
- waiting for an authoritative game window after process discovery.

HydraSeat implication:

```text
spawned process != authoritative game process != authoritative game window
```

must remain a first-class possibility.

The current HydraSeat process/runtime design already uses stronger PID + creation identity and owned process trees than the Nucleus code reviewed here. However, launcher/loader hand-off deserves a dedicated controlled regression scenario before HydraSeat claims broad provider/game launch coverage.

## Finding 7: startup injection and runtime injection are separate compatibility modes

Nucleus supports both:

- create-and-inject/startup paths, where hooks are active before normal game execution; and
- runtime injection after the game process has been discovered.

This matters because some compatibility requirements must exist before the game initializes singleton state, input registration, window behavior, or other process-global assumptions, while other games can be modified later.

HydraSeat implication:

A future optional process adapter must advertise injection/activation timing explicitly. `StartupRequired`, `RuntimeAllowed`, or equivalent typed capabilities are safer than silently trying both.

Protected titles remain outside the bypass path regardless of timing.

## Finding 8: target HWND discovery cannot assume one obvious main window

Nucleus waits for process windows, tracks actual process/window ownership, and contains special handling for games that receive Raw Input on a secondary/invisible window such as `DIEmWin` rather than the visible game window.

HydraSeat implication:

- window ownership must continue to follow validated process ownership, as required by D-014;
- `main HWND` is a presentation hint, not necessarily the only input sink;
- controlled tests should include a target with a secondary Raw Input/message window;
- window placement and input-target discovery should remain separate concepts.

## Finding 9: multi-instance preparation spans filesystem, environment, identity, and timing

Nucleus can prepare instances through combinations of:

- symlinked game trees;
- hardlinked game trees;
- full copies;
- file/folder exclusions and copy-instead rules;
- per-instance working folders;
- per-user profile/document/environment directories;
- separate Windows users for some handlers;
- per-instance network changes;
- bounded/unbounded waits and prompts;
- launcher-specific paths.

The useful lesson is not to copy these mechanisms. It is that a lawful same-game `TwoPlayerSetup` may need more than command-line arguments.

HydraSeat implication:

Keep automatic/manual same-game setup as typed data describing only explicitly supported, lawful operations. Every filesystem or environment mutation must have validation, preview, ownership, rollback, and provider/game policy checks.

Generic mutex killing/renaming, DRM emulation, anti-cheat bypass, deliberate single-instance bypass, or arbitrary handler scripts are **not** added to HydraSeat by this research.

## Finding 10: window placement is a lifecycle, not a one-shot `SetWindowPos`

Nucleus repeatedly deals with:

- waiting for a real HWND;
- removing border/caption styles;
- resizing;
- positioning;
- DPI and monitor scaling;
- top-most behavior;
- games that later move/reset their own windows;
- resetting/reapplying placement;
- optional focus-message behavior.

HydraSeat implication:

The existing separation of process ownership, window tracking, Seat-local coordinate transforms, display layout, and rollback is preferable to embedding placement inside launch code. Real-game acceptance should include games that recreate or move their window after launch.

## Finding 11: input lock/global mutation is high impact and should not be the normal path

`LockInputRuntime.cs` can clip the Windows cursor to a 1x1 rectangle, invoke ProtoInput's global input lock, and optionally suspend Explorer. Nucleus later restores these resources.

These operations show the kind of emergency compatibility techniques a mature split-screen tool may accumulate, but they are not appropriate as HydraSeat's default architecture.

HydraSeat implication:

Prefer:

```text
physical identity
  -> scoped routing/replacement path
  -> scoped process compatibility
  -> guarded physical suppression last
```

over global cursor or shell mutation.

Any unavoidable machine-global mutation must be explicit, journaled, verified, reversible, and protected by an independent recovery path.

## Finding 12: shutdown provides a valuable mutation/cleanup checklist

`GenericGameHandler.End()` attempts to unwind a large set of state, including combinations of:

- target processes;
- hook pipes and process-local helpers;
- input lock and cursor clipping;
- Raw Input split state;
- temporary files and instance trees;
- backed-up files;
- registry changes;
- monitor DPI changes;
- temporary Windows users/profile folders;
- network/IP changes;
- auxiliary compatibility tools;
- game data backup/restoration.

This is useful as a **failure-mode checklist**. HydraSeat should not copy the monolithic cleanup implementation.

HydraSeat's stronger target is:

```text
capture prior state
  -> persist recovery intent
  -> mutate one owned resource
  -> verify mutation
  -> commit resource ownership
  ...
  -> reverse-order rollback
  -> verify ordinary-Windows postconditions
```

with watchdog/reset recovery able to continue after UI/host failure.

## Finding 13: Nucleus demonstrates the cost of a monolithic mutable orchestrator

The reviewed Nucleus architecture contains several patterns HydraSeat should avoid even though they enabled broad compatibility over time:

- one extremely large orchestration class with unrelated responsibilities;
- extensive process-global/static mutable state;
- game behavior represented by hundreds of mutable fields;
- arbitrary handler scripting and optional CLR assembly loading;
- many `Sleep`-based timing assumptions;
- helper-process command protocols encoded as ad-hoc strings;
- raw runtime identifiers passed through integer/string boundaries;
- broad catch-and-continue patterns;
- cleanup spread across a large shutdown routine;
- invasive global compatibility actions mixed into ordinary launch flow.

HydraSeat's typed result/state-machine/protocol/resource-factory split should be preserved even if that means supporting fewer games initially.

## Current HydraSeat mapping after the 2026-08-29 implementation batch

This section deliberately maps the research against the current tree rather than the pre-batch state.

| Research lesson | Current HydraSeat direction | Research conclusion |
| --- | --- | --- |
| Per-game capability matrix | P6 provider-aware planning, requirement resolver, compatibility evidence | Preserve typed/bounded planning; do not adopt arbitrary JS handler execution |
| Host is authoritative | `hydra_host`, host protocol/transport, per-Seat lifecycle | Stronger than UI-owned Nucleus orchestration; preserve |
| Provider plan must reach runtime | `HostProviderPlanRegistry` + `HostProviderPlanInstaller` | The earlier missing production plan-install boundary now exists in the current tree |
| Runtime requirements must be trusted evidence | `GameRuntimeRequirementStore`, trusted requirement snapshots | The earlier empty/guessed requirement gap now has a typed authority path; keep community popularity non-authoritative |
| Input activation must be real or fail closed | `IProductionInputResourceBridge` | Good boundary; no no-op/synthetic success should substitute for isolation |
| Raw/polling/focus/controller APIs differ | P3 Gate C split and controller-specific policy | Preserve separate guarantees and tests |
| Stable device identity | D-010 / hardware identity foundation | Stronger than Nucleus runtime enumeration assumptions |
| Spawn PID may not be final game PID | owned process tree + process/window tracking | Add focused launcher/loader hand-off regression evidence |
| Secondary input HWND may matter | process/window ownership + Raw Input adapter layers | Add controlled secondary-window input target regression |
| XInput extended ordinal may be used | no current `XInputGetStateEx` string found | Candidate adapter/test coverage gap before broad XInput claims |
| Cleanup spans many resource classes | P5 activation resources + P8 journal/watchdog/reset | Use Nucleus list as an audit checklist for mutation coverage |
| Same-game preparation can be complex | typed `TwoPlayerSetup` automatic/manual paths | Expand only through lawful typed reversible operations; never script/bypass by default |

## Research-derived regression candidates

These are not roadmap packet state changes. They are test/design candidates produced by the review and should be consumed by the owning packet/module when implementation resumes.

### NCR-01 — XInput extended-state parity

Before claiming broad XInput remapping for real games, verify the selected Seat controller mapping remains consistent across:

- `XInputGetState`;
- `XInputGetCapabilities`;
- `XInputSetState` / vibration;
- `XInputGetStateEx` ordinal 100 where the target uses it.

### NCR-02 — Launcher/loader hand-off

Create a HydraSeat-owned controlled fixture where the initial launcher process starts a different executable and exits. Verify:

- ownership remains exact;
- the final game process is identified without PID-reuse ambiguity;
- Seat 1/Seat 2 cannot steal each other's child;
- authoritative game HWND follows the final process;
- stop/rollback removes only the owned tree.

### NCR-03 — Secondary Raw Input window

Create a controlled target whose visible game window is not the window registered for Raw Input. Verify the Seat input adapter discovers/routes the correct owned target without using window location as ownership.

### NCR-04 — Startup-required versus runtime-compatible adapter timing

Model and test a controlled process whose input registration occurs before normal window initialization. Ensure the planner can fail closed when a compatibility backend can only attach too late.

### NCR-05 — Full input API no-bleed matrix

For every physical/manual Gate C/D claim, keep receiver-visible checks for the exact active surfaces:

- Raw Input;
- legacy keyboard/mouse messages;
- polling key state;
- cursor/focus/capture;
- XInput;
- DirectInput;
- Raw HID/SDL/vendor API when the selected game uses one.

No single green path upgrades the others.

### NCR-06 — Window recreation/reposition regression

Use a controlled process that destroys/recreates or moves its own main window after launch. Verify ownership, Seat placement, DPI transform, and rollback remain correct.

### NCR-07 — Mutation inventory audit

Cross-check every production activation resource against the Nucleus shutdown checklist and verify HydraSeat has explicit ownership, prior-state capture, reverse rollback, and postcondition checks for every mutation HydraSeat actually performs.

Do not add a mutation merely because Nucleus performs it.

### NCR-08 — Handle-width and protocol-width review

Nucleus contains boundaries where runtime handles are converted through integer/string helper protocols. HydraSeat should continue to reject pointer-sized/native handles as persisted identity and verify every cross-process ABI uses explicit fixed-width fields with x86/x64 tests.

## Explicit non-goals preserved after this review

The following Nucleus capabilities or techniques do not become HydraSeat requirements merely because they exist upstream:

- DRM bypass or emulation;
- anti-cheat bypass/evasion;
- stealth injection to avoid detection;
- deliberate single-instance protection bypass;
- generic mutex-killing/renaming as a way around game/provider policy;
- credential extraction or launcher-account bypass;
- arbitrary executable game-handler scripts;
- arbitrary CLR plugin loading from community profiles;
- silently downloaded third-party injectors, wrappers, emulators, or drivers;
- global Explorer suspension as ordinary product behavior;
- global cursor clipping as the normal Seat cursor model;
- enumeration-order device identity;
- treating one successful launch or hook as proof of physical zero bleed.

## What this research changes

This source review does **not** change a roadmap packet state and does not claim new runtime support.

It does change the quality of the implementation checklist:

1. The earlier conceptual Nucleus entry in `RELATED_SYSTEMS_RESEARCH.md` now has reproducible source-level evidence.
2. The production plan-install and trusted-requirement gaps identified before the final agent batch are confirmed as addressed by the current `production_launch_*` and `game_runtime_requirement_resolver` interfaces.
3. XInput ordinal-100 coverage, launcher/loader hand-off, secondary input-window routing, and window recreation are now explicit regression candidates rather than implicit edge cases.
4. The Nucleus shutdown path is retained as a resource-mutation audit checklist for P5/P8 review.
5. The clean-room line is made explicit before future agents use the reference checkout.

## Future use of this document

When more related repositories are added under `C:\HydraSeat\references`, create or extend research records using the same pattern:

```text
exact source/version/license
  -> consulted files/hashes
  -> observed behavior
  -> neutral engineering lesson
  -> current HydraSeat mapping
  -> regression candidates
  -> explicit non-goals
```

Do not let a reference repository become a build input, and do not convert implementation details from an incompatible source directly into HydraSeat code.

See also:

- `docs/RELATED_SYSTEMS_RESEARCH.md`
- `docs/CLEAN_ROOM_POLICY.md`
- `docs/PHASE3_INPUT_ISOLATION_DESIGN.md`
- `docs/PHASE3_GATE_C_TESTING.md`
- `docs/PRODUCT_V1.md`
- `docs/implementation/DECISIONS.md`
