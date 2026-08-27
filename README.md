# HydraSeat 🎮

**English** | [한국어](README.ko.md) | [简体中文](README.zh-CN.md)

> **One capable Windows gaming PC. Two local players. Separate monitors, input, controllers, and audio — without a VM, Remote Desktop, or game streaming.**

HydraSeat is an experimental Windows local gaming multiseat project being developed for homes where one gaming PC has performance left over, but buying and maintaining a second full desktop just so two people can play at the same time is expensive or inconvenient.

The v1 goal is intentionally narrow:

> **Let two people use the spare headroom of one sufficiently capable Windows gaming PC as two local gaming stations.**

HydraSeat is being developed in public toward an open-source distribution model. **The repository license and contribution terms are not yet formally declared**, so the current repository must not yet be represented as legally open source. See [Clean-room and licensing policy](docs/CLEAN_ROOM_POLICY.md).

---

## Why this project exists

Gaming PCs keep getting faster, but many games do not continuously consume every CPU core, every gigabyte of memory, and the full capability of a modern GPU. In many households that leaves useful performance unused while a second person still needs another complete PC to play locally at the same time.

HydraSeat explores a different tradeoff:

```text
One Windows gaming PC
│
├─ Seat 1
│  ├─ Monitor A
│  ├─ Keyboard / Mouse A
│  ├─ Controller A
│  └─ Audio A
│
└─ Seat 2
   ├─ Monitor B
   ├─ Keyboard / Mouse B
   ├─ Controller B
   └─ Audio B
```

The target users are couples, siblings, roommates, families, and friends who already own one strong PC and would rather share its unused performance than buy a second desktop.

HydraSeat does **not** promise that every PC can run two games well. The machine still needs enough CPU, GPU, memory, storage, display outputs, and peripherals for the workloads selected by the users.

---

## v1: exactly two Seats

HydraSeat v1 targets **two local gaming Seats**.

The internal code may keep generic collections where that makes engineering sense, but the v1 UI, installer, test matrix, product promises, and compatibility evidence are designed for at most two active Seats.

Supporting three or four local stations would multiply monitors, keyboards, mice, controllers, audio endpoints, physical setup complexity, and validation combinations while serving a much smaller household use case. HydraSeat therefore prioritizes making two Seats genuinely usable before considering a larger count.

---

## The product model

HydraSeat deliberately separates **Seat**, **Player**, **Game**, **Two-player setup**, and **Runtime Session**.

### 🪑 Seat = the physical station

A Seat is like a seat in a PC cafe. It describes hardware, not a person or a game.

```text
Seat 1
├─ Display(s)
├─ Keyboard       optional until required
├─ Mouse          optional until required
├─ Controller(s)  optional
└─ Audio           optional
```

Seat settings may be incomplete. The first-run wizard can be skipped, individual device categories can be left unset, and the user can finish setup later. HydraSeat checks the requirements of the selected game at launch instead of requiring every possible device up front.

### 👤 Player = the person

A Player is a lightweight profile independent from the Seat.

A Player may remember:

- a display name and optional local avatar;
- recent games;
- recent Seat preference;
- per-game instance/data-directory preferences;
- references to already authenticated launcher/provider accounts where supported.

Players can swap Seats without losing those associations.

HydraSeat should not become a general password vault. Whenever practical, login credentials and authentication tokens remain owned by the original game launcher/provider. HydraSeat stores only the minimum reference needed to select an already authenticated identity.

### 🎮 Game = an installed title

HydraSeat aims to discover installed games automatically from supported launchers and local installation metadata. Power users can still add an unknown game or executable manually.

The UI should use icons already available from the local installation, shortcut, or provider metadata where possible rather than bundling a large third-party artwork library.

### 🔁 Two-player setup = same-game multi-instance recipe

If Seat 1 and Seat 2 run different games, no special same-game profile needs to be visible to the user.

If both Seats select the **same game**, HydraSeat looks for or creates a two-player setup describing the lawful separation required by that title, for example:

- separate instance/config/data directories;
- launch arguments and working directories;
- provider-specific launch choices;
- account references where the provider supports them;
- start order;
- window matching and placement;
- input/controller/audio compatibility requirements;
- known limitations.

HydraSeat should attempt to create this setup automatically, while also providing a guided manual editor for games automation does not yet understand.

> **HydraSeat automates multi-instance gaming where the game and provider permit it. It does not defeat restrictions to create multi-instance support.**

### ▶ Runtime Session = what is happening now

```text
Seat 1 + Mario + Minecraft instance A
Seat 2 + Luigi + Minecraft instance B
```

The Seat remains hardware. The Player remains the person. The two-player setup remains reusable game knowledge. The runtime mapping exists only while the current gaming session is active.

See the canonical [HydraSeat v1 Product Specification](docs/PRODUCT_V1.md).

---

## Game-first UX

HydraSeat should feel like a lightweight game launcher, not a Windows administration console.

The normal flow is:

```text
Open HydraSeat
    ↓
Choose a game
    ↓
Play on Seat 1 / Seat 2 / Both
    ↓
Choose Player(s)
    ↓
Play
```

Click/tap selection is the primary interaction. Dragging a game icon onto a Seat card may be supported as a convenience shortcut.

Low-level terms such as Raw Input, device interface paths, compatibility backends, IAT interposition, plan hashes, and recovery manifests belong in Diagnostics or Expert settings, not in the normal path.

A possible normal home screen is intentionally simple:

```text
HydraSeat
──────────────────────────────────────

Games
[Minecraft] [Terraria] [Stardew] [...]

Seat 1                         Seat 2
Mario                          Luigi
Minecraft                      Terraria
Ready                          Ready

                 ▶ PLAY
```

When the same game is selected for both Seats:

```text
Seat 1                         Seat 2
Mario                          Luigi
Minecraft ═══════════════════ Minecraft

        Two-player setup ready

                 ▶ PLAY
```

---

## Optional first-run Seat wizard

On first launch HydraSeat may offer:

```text
Welcome
  → identify displays
  → assign Seat 1 input
  → assign Seat 2 input
  → optional controllers
  → optional audio
  → test
  → save
```

The user can choose **Set later** or skip the wizard entirely.

A missing keyboard does not automatically make a Seat invalid if the selected game only needs a controller. Requirements are evaluated per game during preflight.

---

## Independent Seat lifecycle

The two players do not have to start and stop at exactly the same time.

If Luigi closes Terraria while Mario continues Minecraft:

```text
Seat 1                         Seat 2
Mario                          Luigi
Minecraft                      Idle
Playing                        Choose another game
```

Seat 2 can launch another game or end playing without interrupting Seat 1.

While at least one Seat remains active, an idle/ended Seat stays on a minimal HydraSeat waiting or game-selection screen instead of being returned to an unrestricted ordinary Windows desktop. When both Seats finish, or the Management UI explicitly requests it, HydraSeat performs verified rollback and restores normal one-PC Windows behavior.

---

## Minimal Seat Launcher — not a second desktop

HydraSeat v1 is **game-only**.

It manages the selected game, its required launcher, helpers, child processes, windows, input/controller/audio routing, and recovery. It does not try to provide a complete independent Windows desktop for each player.

A v1 idle Seat may show only:

```text
Seat 2
Luigi

Minecraft
Terraria
Stardew Valley
More Games...

End Playing
```

Once the game is running, the Seat UI should disappear or stay non-intrusive.

The following are intentionally deferred beyond the v1 product contract:

- independent Seat taskbars;
- per-Seat wallpaper/desktop zones;
- general arbitrary apps such as Chrome/Office/Discord as independently managed Seat apps;
- per-Seat clipboard virtualization;
- a full Windows shell replacement.

This keeps the one-developer project focused on the actual goal: **two people gaming locally on one PC**.

---

## Input and runtime approach

Windows normally exposes global concepts such as foreground focus, cursor position, keyboard state, and merged input. Games also use different APIs and behavior patterns.

HydraSeat therefore treats input isolation as a compatibility problem rather than a simple event-forwarding problem. Depending on the title, current research/implementation includes or evaluates:

- Win32 Raw Input device identity and routing;
- HID / SetupAPI / ConfigMgr stable device identity;
- controlled process-local input virtualization for declared APIs;
- XInput and DirectInput compatibility policies;
- window/process ownership;
- optional device visibility/isolation backends such as HidHide where appropriate;
- watchdog, crash journal, reset, and rollback paths.

HydraSeat does **not** hide, bypass, disable, or evade anti-cheat, DRM, protected-process, account, launcher, or deliberate single-instance restrictions.

---

## Protected games: warn, do not pretend

A protected game may still be useful to test because future versions, provider changes, or a cleaner compatibility path may make a configuration work. HydraSeat therefore does not need to permanently hard-block every protected title.

Instead, known protected titles should show a strong warning before any experimental HydraSeat launch:

> This game uses an anti-cheat, DRM, or protection system. HydraSeat has not established that this configuration is safe or compatible. The game or protection system may block the software, refuse to launch, disconnect, or take other action under its own policy. HydraSeat does not bypass or disable protection.

The user may explicitly opt into an advanced experiment.

**A successful launch does not prove anti-cheat safety.** Protected-title results remain clearly marked as experimental and must never be presented as an anti-cheat safety certification.

---

## Compatibility is evidence, not a badge

HydraSeat does not need an official `HydraSeat Certified` label for games.

A more honest model is to show what real users observed:

```text
Community results
87% succeeded (45 reports)
39 success / 6 failure

Launch              98%
Two instances       91%
Input isolation     89%
Audio routing       96%
Clean shutdown      99%
```

Evidence can be grouped by game version, HydraSeat version, provider, Windows version, and relevant compatibility path so materially different environments are not mixed into a misleading percentage.

States visible to users can remain simple:

- **Community results available** — show success/failure evidence and sample size;
- **Untested** — no useful evidence yet; local testing is available;
- **Protected / Experimental** — protection is known; explicit risk acknowledgement is required.

Percentages are observations, not guarantees.

---

## Local-first compatibility testing

HydraSeat should make testing easy enough that the community can expand compatibility without one maintainer owning every game.

A local compatibility run may record bounded evidence such as:

- game/provider/version;
- HydraSeat version;
- process/instance launch result;
- expected instance count;
- Seat/process/window ownership;
- receiver-verified input and measured cross-Seat bleed;
- audio route result;
- clean exit and rollback;
- relevant Windows and backend information.

Results are stored **locally by default**.

Community submission is explicit opt-in and should show the user the redacted JSON before upload. Default submissions must avoid credentials, passwords, tokens, raw typed text, Player names, personal paths, and unnecessary stable device identifiers.

---

## Offline first

Core HydraSeat operation should not require a HydraSeat account or an Internet connection.

Offline:

- configure Seats;
- create Players;
- discover locally installed games from available local metadata;
- use the local game library;
- create automatic/manual two-player setups;
- run local compatibility tests;
- launch saved games/setups;
- diagnose and recover.

Optional online features:

- compatibility/profile catalog updates;
- opt-in community result upload;
- HydraSeat software update checks.

Compatibility/profile data can initially be distributed as versioned JSON/catalog artifacts instead of requiring a custom always-on backend service.

---

## Update policy

Compatibility data and the HydraSeat program are updated separately.

**Compatibility/profile updates** can be lightweight and frequent because they improve game knowledge without replacing the core executable. Users can disable automatic refresh and keep using the local cache.

**Program/runtime/driver updates** require clear user approval. A working installed version should not become unusable merely because a newer build exists.

Downloaded artifacts must be version/hash/trust checked before use.

---

## Least privilege

HydraSeat should use normal user privileges whenever Windows allows the required operation.

UAC/administrator access should be requested only for narrowly defined tasks that actually require elevation, such as installation, optional driver/service setup, or specific system-level recovery/configuration actions.

The ordinary game-selection and Play flow should not require running the main UI as administrator.

---

## Installer is mandatory for a usable release

A user should not need Visual Studio, MSVC, Qt, CMake, or a developer shell just to try HydraSeat.

A v1 Windows installer/uninstaller is part of the product and must provide:

- prerequisite and architecture checks;
- required runtime installation;
- optional elevated components only when selected/required;
- first-run Seat setup;
- repair/uninstall;
- safe update/rollback;
- useful diagnostics when setup fails.

Uninstall must remove HydraSeat-owned persistent state and leave ordinary Windows usable.

---

## Current engineering status

HydraSeat is **not yet a finished end-user product**.

Current work has already built substantial research and engineering infrastructure, including:

- Phase 0 research and clean-room policy;
- stable Windows hardware identity/detection;
- two-Seat hardware composition/configuration foundations;
- controlled Raw Input, polling/cursor/focus, XInput, and DirectInput compatibility experiments;
- input metrics and physical acceptance tooling;
- watchdog, crash journal, and emergency reset foundations;
- an open-source application compatibility test path;
- early Phase 4 background runtime / IPC / process / window / display foundations.

Important real-world gates remain pending, especially physical two-input acceptance and real game validation. Synthetic or HydraSeat-owned controlled tests are not presented as generic game support.

See:

- [Implementation status](docs/implementation/STATUS.md)
- [Development roadmap](docs/ROADMAP.md)
- [Architecture](docs/ARCHITECTURE.md)
- [v1 product specification](docs/PRODUCT_V1.md)
- [Compatibility evidence](docs/COMPATIBILITY_MATRIX.md)

---

## Roadmap direction

The roadmap is now optimized for a one-developer, two-Seat gaming product rather than a general Windows multiseat desktop platform.

High-level order:

```text
Phase 3  Input isolation + physical evidence
   ↓
Phase 4  Background runtime + independent Seat lifecycle + display/window ownership
   ↓
Phase 5  Real two-Seat gaming MVP
   ↓
Phase 6  Game discovery + Player profiles + automatic/manual same-game setups
   ↓
Phase 7  Minimal idle Seat Launcher UX
   ↓
Phase 8  Installer + reliability + least privilege + offline updates/catalog sync
   ↓
Phase 9  Community compatibility/profile ecosystem
   ↓
Phase 10 Release/legal/security/performance hardening
```

The complete packet-level roadmap is in [`docs/implementation/`](docs/implementation/README.md).

---

## v1 release success

A real v1 release requires the complete user journey, including:

- clean install/uninstall;
- exactly two v1 Seats;
- real two-display/two-input physical validation;
- objective input isolation evidence for tested configurations;
- two different real games running at the same time;
- at least one lawful real same-title/two-instance demonstration;
- Player profiles and game-first selection;
- automatic game discovery plus manual fallback;
- automatic and manual two-player setup creation;
- one Seat exiting/changing games without stopping the other;
- idle Seat Launcher behavior;
- verified return to ordinary Windows when both players finish;
- watchdog/crash/emergency recovery;
- local-first compatibility JSON and optional community sharing;
- offline operation with locally available data;
- a user-approved core update path;
- resolved project license/contribution terms before calling the release open source.

The goal is **not** to claim official support for a huge catalog on day one. Compatibility should grow from transparent evidence and reusable community knowledge.

---

## Explicit v1 non-goals

HydraSeat v1 does not promise:

- more than two active Seats;
- virtual machines or independent Windows installations;
- one Windows logon session per Seat;
- a general-purpose independent desktop per Seat;
- universal same-game multi-instance support;
- anti-cheat/DRM/account/launcher/single-instance bypass;
- anti-cheat safety certification;
- cloud accounts or mandatory telemetry;
- compatibility with every game.

---

## Related systems and clean-room boundary

HydraSeat studies public behavior and documentation from related systems such as ASTER, ProtoInput, Nucleus Co-op, Universal Split Screen, HidHide, devreorder, Duo, and official Microsoft Windows APIs.

Research references do not automatically become HydraSeat code. Proprietary systems are behavior/documentation references only, and third-party source reuse must follow license compatibility and the repository's [clean-room policy](docs/CLEAN_ROOM_POLICY.md).

Reference checkouts under `C:\HydraSeat\references` are research inputs only and are not build inputs.

---

## Development and verification

Developer builds currently use C++20, CMake, MSVC on Windows, and optional Qt 6 UI work.

The repository intentionally distinguishes:

- **automated controlled evidence**;
- **real-process evidence**;
- **physical/manual evidence**;
- **community compatibility evidence**.

A successful synthetic test is never promoted into a generic physical/game claim.

Start with:

```text
python tools/show_implementation_packet.py --current
python tools/validate_implementation_roadmap.py
```

Repository agents must read [`.agents/AGENTS.md`](.agents/AGENTS.md) before implementation work.

---

## License and contributions

The intended end state is a broadly reusable open-source project, but the repository does not yet have formally declared project license and contribution terms.

Until that gate is resolved:

- do not describe the current repository as legally open source;
- do not assume source reuse rights;
- follow [`docs/CLEAN_ROOM_POLICY.md`](docs/CLEAN_ROOM_POLICY.md);
- treat the license/contribution decision as a required release task, not paperwork to ignore.

Once the legal gate is resolved, HydraSeat's profile, compatibility, documentation, and contribution workflows should be designed to make lawful community participation easy.