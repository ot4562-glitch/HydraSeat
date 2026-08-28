# HydraSeat Architecture Specification

## 1. Product boundary

HydraSeat v1 is a **two-Seat Windows local gaming product**. It exists so two people can share the unused CPU/GPU/memory/I/O headroom of one sufficiently capable gaming PC instead of requiring a second complete desktop solely for simultaneous local gaming.

The canonical user/product contract is [`PRODUCT_V1.md`](PRODUCT_V1.md). This document defines the technical boundaries needed to implement that contract.

v1 deliberately does not implement a general-purpose independent Windows desktop per Seat. The product manages games and the launchers/helpers required by those games. Full per-Seat taskbars, wallpaper, clipboard virtualization, arbitrary general-purpose apps, and a replacement Windows shell are outside the v1 contract.

The normal path remains one Windows interactive session. HydraSeat does not require a VM, RDP, streaming, or a second Windows installation/session per Seat.

## 2. v1 invariant: two active Seats

The v1 product, installer, UI, compatibility evidence, physical acceptance, and support policy are defined for a maximum of two active Seats.

Core containers may remain collection-based where that avoids artificial `seat1`/`seat2` branching, but production activation must reject a v1 plan containing more than two active Seats. N-Seat generalization must not delay or complicate the two-Seat release path.

## 3. Separate persisted concepts

HydraSeat must not collapse hardware, people, games, and runtime state into one growing profile object.

```text
SeatConfig
  physical station only

PlayerProfile
  lightweight person preferences and provider account references

GameRecord
  installed/discovered game identity

TwoPlayerSetup
  optional same-game two-instance compatibility recipe

RuntimeSession
  temporary Seat + Player + Game bindings
```

### 3.1 SeatConfig

A Seat represents physical resources:

```text
Seat
├─ Displays[]
│  └─ PrimaryDisplay
├─ Keyboards[]       may be empty until a selected game requires one
├─ Mice[]            may be empty until required
├─ Controllers[]     optional
├─ AudioOutputs[]    optional until required
└─ AudioInputs[]     optional
```

A Seat does not permanently own a player, game, launcher account, save, or game process.

A saved Seat configuration may be incomplete. Missing resources are not automatically configuration corruption. Requirement-aware launch preflight decides whether the selected game can run with the current assignments.

### 3.2 PlayerProfile

A Player is independent from a Seat and may move between Seat 1 and Seat 2.

A lightweight Player profile may persist:

- local display name and optional local avatar reference;
- recent games and recent Seat preference;
- per-game instance/data-directory preferences;
- provider account reference/selector metadata when the provider supports choosing an already authenticated account;
- recent session choices.

HydraSeat should not persist provider passwords, general provider session secrets, or unrelated credentials. Authentication remains owned by the original launcher/provider whenever practical.

### 3.3 GameRecord

A GameRecord describes an installed/discovered game independent from Players and Seats.

Identity can include:

- provider and provider application ID where available;
- executable identity/path candidates;
- install root and metadata source;
- architecture where known;
- local title and icon source;
- version/hash/staleness information where available.

Provider metadata is untrusted bounded input. Friendly title is never sufficient identity by itself.

### 3.4 TwoPlayerSetup

A TwoPlayerSetup is only needed when the same game is assigned to both Seats or when a title has explicit compatibility requirements that must be reused.

It may declare:

- instance/data/config directory separation;
- launch arguments/environment/working directory;
- provider-specific account selection references;
- bounded start order/waits;
- process/child/window expectations;
- input/controller/audio/display compatibility requirements;
- backend allow/deny rules;
- protection status and known limitations;
- exact evidence/provenance metadata.

The normal user flow should call this a **two-player setup**, not expose internal compatibility schema terminology unless Expert mode is opened.

Automatic creation and guided manual creation are both required product paths. No setup may authorize anti-cheat, DRM, protected-process, account, launcher, or deliberate single-instance bypass.

### 3.5 RuntimeSession

Runtime state is temporary and authoritative only in `hydra_host.exe`.

Example:

```text
Runtime Session
├─ Seat 1 -> Mario -> Minecraft instance A
└─ Seat 2 -> Luigi -> Minecraft instance B
```

Runtime PIDs, HWNDs, handles, transient Raw Input handles, and live tokens are never persisted as stable profile identity.

## 4. Target production topology

```text
                           HydraSeat.exe
                     game-first management UI
                  game library / Players / Seats
                  two-player setup / diagnostics
                              │
                 versioned local control protocol
                              │
                              ▼
                        hydra_host.exe
                 authoritative per-user runtime
                              │
         ┌────────────────────┼────────────────────┐
         │                    │                    │
         ▼                    ▼                    ▼
 hydra_watchdog.exe      Seat 1 runtime       Seat 2 runtime
 recovery lease         process/window       process/window
 crash rollback         input/controller     input/controller
                        display/audio         display/audio
                        game lifecycle        game lifecycle
                              │                    │
                              ▼                    ▼
                    Game / launcher tree   Game / launcher tree

         hydra_seat_ui.exe              hydra_seat_ui.exe
         minimal idle/start UI           minimal idle/start UI
         only when needed                only when needed

hydra_reset.exe
  independent emergency reset and verification path
```

`HydraSeat.exe` and `hydra_seat_ui.exe` are disposable clients. They do not own authoritative mutation state. Closing the main UI must not terminate a running Seat.

## 5. Whole-machine runtime vs per-Seat game lifecycle

HydraSeat has two related lifecycle levels.

The P4-SEAT-01 implementation keeps the levels in separate contracts. The
whole-machine `RuntimeHost` backends continue to own shared split-environment
prepare/rollback, while `SeatGameLifecycle` owns at most two temporary
`SeatGameBinding` values and injected Seat-local instances. A Seat-local
instance may clean only its exact process/window/input/audio/controller
ownership. Its bounded protocol snapshot contains stable Seat ID, phase,
temporary Player/Game IDs, generation, and diagnostic; it contains no handles,
pointers, credentials, or persisted Seat-profile fields. Host protocol v3
transports bounded Seat commands, reconnect snapshots, and ordered
Seat-identified mutation events. `RuntimeHost` serializes these mutations with
whole-machine work and performs Seat-local cleanup before explicit shared
rollback. This controlled foundation is not a real-game, physical-device, or
minimal Seat Launcher capability claim.

### 5.1 Whole-machine host/split state

The host can be running while no game is active. A whole-machine split environment may remain active while one Seat is idle and the other is playing.

Whole-machine operations include:

- loading/validating the physical Seat layout;
- preparing global input/display/device policy;
- entering a recoverable split runtime;
- explicit `Return to Windows`;
- reconfiguration requiring verified rollback;
- watchdog/reset recovery.

### 5.2 Independent Seat lifecycle

Each Seat needs an independent game lifecycle:

```text
Idle
 -> Planning
 -> Starting
 -> Playing
 -> Stopping
 -> Idle
```

with `Degraded` / `RecoveryRequired` when guarantees cannot be preserved.

Required behavior:

- Seat 1 may be Playing while Seat 2 is Idle;
- Seat 2 may stop its game without stopping Seat 1;
- an idle Seat may choose another Player/game and start again;
- an idle Seat stays on the minimal Seat Launcher while another Seat remains active;
- when both Seats are done, policy may return the machine to ordinary Windows automatically or through the visible end-session action;
- explicit whole-machine `Return to Windows` always performs verified rollback.

### Current implementation note

The current Phase 4 branch already contains a background host foundation, host IPC, Seat process/window ownership, and early display/control policy work. Its current runtime protocol still centers several transitions around a whole-session command model. **That foundation must not be described as already implementing the independent v1 Seat game lifecycle above.** The roadmap tracks that lifecycle as additional Phase 4 work before the v1 gaming MVP can rely on it.

## 6. Management UI and minimal Seat UI

### 6.1 Main UI (`HydraSeat.exe`)

Normal users should see a lightweight launcher-like surface:

```text
Games
  [icons discovered from local installations/providers]

Seat 1                         Seat 2
<Player>                       <Player>
<Game>                         <Game>

                    Play
```

Primary interaction:

1. choose game;
2. choose Seat 1, Seat 2, or Both;
3. choose Player(s);
4. resolve only required warnings/setup;
5. Play.

Click/tap is primary. Drag-and-drop of a game onto a Seat is an optional shortcut.

Low-level backend, device-path, protocol, hook, and plan details stay under Diagnostics/Expert UI.

### 6.2 Seat UI (`hydra_seat_ui.exe`)

v1 does not need a full shell.

The per-Seat UI appears only when useful:

- idle/waiting Seat;
- game selection after a player exits;
- Player change;
- launch/preflight progress;
- compatibility/protection warning;
- bounded error/recovery action;
- `End Playing`.

Once a game is running, the UI should disappear or remain non-intrusive.

Deferred beyond v1:

- independent taskbar/window switcher as general desktop chrome;
- Seat wallpaper/desktop zones;
- arbitrary general-purpose app launcher;
- clipboard virtualization;
- full Windows shell replacement.

## 7. Hardware detector

`HardwareDetector` and related identity helpers enumerate and normalize physical resources without claiming isolation.

Responsibilities include:

- physical display enumeration and stable output identity;
- Raw Input keyboard/mouse/touchpad enumeration;
- HID, SetupAPI, and ConfigMgr identity resolution;
- XInput/generic HID controller discovery;
- audio endpoint discovery in the production audio phase;
- conservative virtual-device/display classification;
- change/hot-plug observation;
- privacy-preserving diagnostics.

Stable identity must be used instead of enumeration order or friendly name.

## 8. Input compatibility and isolation

Raw Input can identify which physical device generated an event, but that alone does not prevent Windows or games from observing merged/global input state.

The required user-visible guarantee for a tested configuration is:

```text
Seat 1 input -> Seat 1 game only
Seat 2 input -> Seat 2 game only
```

HydraSeat therefore treats input as a profile/capability problem involving explicit Windows API surfaces.

Existing Phase 3 work includes controlled evidence for:

- Raw Input observation and stable physical identity;
- fail-closed Seat routing in HydraSeat-owned labs;
- process-local polling/cursor/focus/capture virtualization in controlled probes;
- controlled Raw Input API virtualization;
- XInput state/remapping semantics;
- DirectInput visibility/order policy experiments;
- bounded latency/bleed metrics;
- watchdog/crash/reset foundations;
- one open-source external application acceptance path.

Physical two-input acceptance and real game evidence remain separate gates. A synthetic zero-cross counter is not automatically a physical zero-bleed claim.

## 9. Protection boundary

HydraSeat never implements stealth, anti-cheat evasion, integrity bypass, DRM bypass, credential bypass, or a method for defeating deliberate game/provider restrictions.

A known protected game may still be offered as an **explicit advanced experiment** because future compatibility can change. Before such an experiment, the UI must clearly state that HydraSeat has not established compatibility or anti-cheat safety and that the game/protection system may refuse or terminate the session or take action under its own policy.

Protected-game technical success remains tagged `Protected/Experimental`. It is not evidence of anti-cheat safety.

## 10. Process and window ownership

A running game creates a temporary Seat-owned process tree. The Seat hardware configuration does not permanently own that process.

Runtime process identity should use PID plus creation identity/executable checks and Job Objects where compatible.

Window ownership derives from validated process ownership. The window subsystem must:

- never move/close unrelated windows;
- handle child/recreated windows;
- reject stale/reused HWND identity;
- keep owned windows inside the Seat display group;
- handle DPI/topology changes deterministically;
- expose weaker capability explicitly when a provider/game cannot use the preferred ownership mechanism.

## 11. Display model

A Seat may own one or more physical displays, but v1 has at most two active Seats.

```text
Seat 1
  Display A -> local primary (0,0)
  Display B -> local secondary

Seat 2
  Display C -> local primary (0,0)
```

The host tracks global Windows topology and compiles Seat-local placement transforms without pretending Windows itself has two independent desktops.

Physical display support is the required path. Optional virtual displays are capability-gated later and must not become a dependency of normal local-monitor use.

## 12. Audio and controller routing

Audio and controller support are game/API specific, not generic labels.

- XInput, DirectInput, Raw HID, SDL, and vendor APIs are separate controller capabilities.
- Per-process audio endpoint routing must be verified against the selected game/provider path.
- Missing optional devices are allowed in saved Seat configuration; launch preflight decides what the selected game actually requires.

No game is called compatible merely because display/input works if the declared test scenario also requires controller/audio isolation and that evidence is absent.

## 13. Game discovery and provider boundary

The normal game library should be built from read-only local discovery before manual entry.

Provider adapters may inspect supported local metadata for:

- Steam;
- Epic;
- EA;
- GOG;
- additional providers added later;
- custom executable fallback.

Provider adapters are lawful integration layers, not restriction bypasses.

The game catalog remains provider-neutral above the adapter layer. P6-CATALOG-01 implements this layer as a pure bounded candidate-reconciliation core: adapters/manual entry supply typed metadata, provider+app identity is preferred where available, normalized Windows executable identity is the fallback, friendly title/icon never define identity, and ambiguous conflicting strong provider identities fail closed. Catalog-only icon/architecture/staleness observations remain outside the persisted `GameRecord` schema until a schema packet explicitly promotes them.

Icons should normally be obtained from locally installed executable/shortcut/provider metadata. Community setup packages should avoid redistributing third-party game artwork by default.

## 14. Two-player setup engine

When both Seats select the same GameRecord, the planner resolves a TwoPlayerSetup.

Resolution order:

```text
Known local/community setup
    -> validate against exact local installation/provider/version
    -> if unavailable, attempt bounded automatic setup discovery
    -> if still unresolved, offer guided manual setup
```

Automatic setup discovery must be read-only until the user reviews the generated plan. Any filesystem/config mutation must be explicit, bounded, reversible, and scoped to HydraSeat-owned/approved paths.

The manual path must still use typed validated fields; imported profiles must not gain arbitrary script execution by default.

## 15. Compatibility evidence architecture

HydraSeat v1 uses evidence rather than an official support badge.

### 15.1 Local result

The compatibility harness may produce a versioned local JSON record containing bounded technical evidence such as:

- game/provider/version;
- HydraSeat version;
- Windows version/build class;
- selected compatibility path/backend versions;
- scenario type (`different-games`, `same-game-two-instance`, protected experiment);
- launch/instance results;
- receiver-verified input/bleed metrics;
- controller/audio result where applicable;
- clean stop/rollback result;
- redaction schema version.

### 15.2 Community evidence

Upload is explicit opt-in. The user can preview the redacted JSON first.

Community aggregation may show:

- success and failure count;
- sample size;
- success percentage;
- sub-results such as launch, two-instance, input, audio, clean shutdown.

Aggregation must segment materially different game versions, HydraSeat versions, providers, Windows environments, and compatibility paths rather than producing a misleading universal percentage.

There is no requirement for a maintainer-created `Certified` badge.

### 15.3 Privacy

Default compatibility data excludes:

- credentials/tokens/passwords/cookies;
- raw typed text;
- Player display names;
- Windows account names;
- personal absolute paths;
- unrelated process data;
- account identifiers and stable device serials unless a narrowly documented non-identifying field is truly required.

## 16. Offline-first boundary

Core functionality must work without a HydraSeat cloud account or continuous network access.

Offline core:

- Seat setup;
- Player profiles;
- local game discovery where local provider metadata is available;
- local game library;
- saved TwoPlayerSetups;
- local compatibility test records;
- game launch/runtime/recovery.

Optional network functionality:

- compatibility/setup catalog synchronization;
- explicit community evidence submission;
- program update check/download.

The initial community catalog should be distributable as versioned static JSON/artifacts so HydraSeat does not require a custom always-on service to reach v1.

## 17. Update and trust model

Compatibility data and executable updates are different trust domains.

Compatibility/setup catalogs may refresh frequently and can support user-configurable automatic checks with local-cache fallback.

Executable/runtime/driver updates require user approval. Downloaded artifacts require version/hash/trust validation and staged rollback/health checks.

No optional binary, driver, or script is silently downloaded and executed merely because a community profile references it.

## 18. Privilege model

Least privilege is a v1 requirement.

Normal `HydraSeat.exe` use and ordinary runtime operations execute without elevation whenever the required Windows API permits it.

Elevation is requested only for narrow operations such as:

- installation/repair/uninstall steps requiring it;
- optional driver/service installation/configuration;
- explicitly privileged system mutation;
- specific recovery actions requiring administrator rights.

Any elevated broker/service has a small typed allowlist and cannot become a general privileged command runner.

## 19. Recovery model

Risky mutation is not complete without recovery.

Required layers include:

- background host transition ownership;
- independent watchdog lease;
- durable bounded crash journal/safe-mode marker;
- emergency reset path independent from normal UI;
- reverse-order rollback for input/device/display/audio/window mutations;
- exact ownership checks so unrelated processes/windows/devices are never reset;
- verified postconditions before reporting ordinary Windows restored.

A Seat game exiting normally is not a whole-machine crash and must not force the other Seat to stop.

## 20. Installer boundary

The developer CMake/MSVC/Qt workflow is not the end-user installation contract.

A v1 installer must cover:

- architecture/prerequisite checks;
- core runtime installation;
- optional privileged components only when required;
- first-run Seat wizard with `Set later` support;
- repair/uninstall;
- update staging/rollback;
- clean removal of HydraSeat-owned persistent state;
- post-uninstall ordinary Windows verification.

## 21. Component summary

```text
HardwareDetector / identity
  read-only physical inventory

Seat configuration
  hardware station persistence, maximum two active in v1

Player store
  lightweight people/preferences, no password vault

Application catalog / provider adapters
  local game discovery and local icon metadata

Two-player setup model/planner
  automatic + guided manual same-game instance separation

hydra_host.exe
  authoritative whole-machine runtime + independent Seat game lifecycle

hydra_hostctl.exe
  read-only diagnostics + Management-authorized whole-machine/Seat commands

Process/window/display/audio/controller subsystems
  runtime ownership and routing

Input compatibility subsystem
  explicit capability-selected paths with physical evidence gates

HydraSeat.exe
  game-first main UI / Seat settings / Player selection / diagnostics

hydra_seat_ui.exe
  minimal per-Seat idle/start/error launcher, not full desktop shell

hydra_watchdog.exe / hydra_reset.exe
  independent recovery

Compatibility evidence layer
  local-first JSON + optional redacted community aggregation

Installer/update layer
  least privilege, reversible, user-approved executable updates
```

## 22. Current truth and roadmap ownership

Implementation truth is tracked in [`implementation/STATUS.md`](implementation/STATUS.md). Detailed work packets live under [`implementation/`](implementation/README.md).

Historical Phase 3 testing/design documents remain evidence for the controlled input work and should not be rewritten to imply future product behavior already exists.

The architectural decision rule is simple:

> If a feature does not materially improve the two-Seat game-first journey, its safety/recovery, or its compatibility evidence for v1, defer it rather than growing HydraSeat into a general multiseat desktop platform.
