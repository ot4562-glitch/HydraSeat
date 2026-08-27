# HydraSeat v1 Product Specification

## 1. Why HydraSeat exists

Modern gaming PCs often have more CPU, GPU, memory, and I/O headroom than one person or one game uses continuously. Buying a second complete desktop only so two people in the same home can play at the same time can be expensive, wasteful, and physically inconvenient.

HydraSeat is being developed for that gap:

> Let two people share the unused headroom of one capable Windows gaming PC as two local gaming stations, using separate monitors, input devices, controllers, and audio, without requiring a VM, Remote Desktop, or game streaming.

The intended users are households, couples, siblings, roommates, and friends who already own one sufficiently capable PC and would rather use its spare performance than purchase and maintain a second desktop.

HydraSeat is being developed in public toward an open-source distribution model. The repository license and contribution terms are not yet formally declared, so the current repository must not be represented as legally open source until the tracked license gate is resolved.

## 2. v1 scope: exactly two Seats

HydraSeat v1 is a **two-Seat gaming product**.

The internal data model may continue to use collections where that keeps the architecture clean, but the v1 product contract, installer, UI, acceptance tests, compatibility evidence, and support documentation target a maximum of two active Seats.

This is deliberate. A third or fourth local station requires many more monitors, input devices, audio devices, physical space, power, and test combinations. Supporting those combinations would increase development and validation cost while serving a much smaller household use case.

A future version may revisit the limit only after the two-Seat product is mature. v1 must not be delayed by N-Seat generalization.

## 3. Core product concepts

HydraSeat keeps five user concepts separate.

### Seat

A Seat is a physical gaming station, similar to a seat in a PC cafe.

A Seat may contain:

- one or more displays;
- an optional keyboard;
- an optional mouse or pointing device;
- optional controllers;
- optional audio output/input endpoints.

Seat assignments describe hardware only. A game, launcher account, save, character, or player identity is not permanently owned by a Seat.

Seat hardware fields may be unset. The first-run setup wizard is optional, individual device categories may be skipped, and the user can finish configuration later. At launch time HydraSeat validates the selected game's actual requirements against the current Seat hardware instead of requiring every possible device up front.

### Player

A Player is a lightweight person profile independent from a Seat.

A Player may remember:

- display name and optional local avatar;
- recent games;
- recent Seat preference;
- provider/launcher account **references** where a provider supports selecting an existing authenticated account;
- per-game instance or data-directory preferences;
- recent session choices.

A Player can move between Seat 1 and Seat 2 without losing those associations.

HydraSeat should not become a general credential vault. Passwords, provider secrets, and authentication tokens should remain owned by the supported launcher/provider whenever practical. HydraSeat stores only the minimum reference required to ask that provider to launch using an already authenticated identity.

### Game

A Game is an installed game or user-added executable known to HydraSeat.

Normal UX discovers games automatically from supported providers and local installation metadata. Manual `Add game` / executable selection remains available for power users and unknown titles.

Where possible the UI uses icons already present in the local game installation, shortcut, or provider metadata. Community profile packages should not redistribute third-party game artwork by default.

### Two-player game setup

A two-player game setup is the optional compatibility recipe required when **the same game is selected for both Seats**.

It can describe lawful, game-permitted separation such as:

- separate instance/data/config directories;
- command-line arguments and working directories;
- provider launch choices;
- per-player account references where the provider supports them;
- start order and bounded waits;
- window matching and placement;
- audio/controller/input compatibility requirements;
- known limitations and protection status.

HydraSeat tries to create this setup automatically when enough information can be derived safely. A guided manual editor remains available so technically persistent users are not blocked by incomplete automation.

A two-player setup never authorizes HydraSeat to defeat DRM, anti-cheat, account rules, protected processes, launcher policy, or a game's deliberate single-instance restriction.

### Runtime Session

A Runtime Session is the temporary mapping of Player + Seat + Game that exists while HydraSeat is active.

Example:

```text
Seat 1 + Mario + Minecraft instance A
Seat 2 + Luigi + Minecraft instance B
```

Seat configuration, Player identity, Game metadata, two-player setup, and runtime state must remain separate persisted/runtime concepts.

## 4. Game-first user experience

HydraSeat v1 behaves visually like a lightweight game launcher, not a Windows administration console.

The normal flow is:

```text
Open HydraSeat
    -> choose a game
    -> choose Seat 1, Seat 2, or Both
    -> choose the Player for each selected Seat
    -> review only necessary warnings
    -> Play
```

Click/tap selection is the primary interaction. Drag-and-drop from the game library onto Seat cards is an optional shortcut, not the only way to use the application.

Technical terms such as Raw Input, HidHide, IAT patching, backend selection, plan hashes, device interface paths, and protocol details stay out of the normal flow. They are available only in diagnostics or expert configuration when needed.

## 5. First-run Seat setup

The installer or first launch offers a guided two-Seat setup wizard.

The user may:

- configure both Seats immediately;
- configure one Seat and leave the other partially empty;
- skip the wizard entirely and configure Seats later.

Suggested flow:

```text
Welcome
  -> identify displays
  -> assign Seat 1 input
  -> assign Seat 2 input
  -> assign optional controllers
  -> assign optional audio
  -> test devices
  -> save
```

Every step supports `Set later` where the resource is not intrinsically required to save a Seat.

When a game is launched, HydraSeat performs requirement-aware preflight. For example, a controller-only title does not fail because the Seat has no keyboard, while a keyboard/mouse title clearly identifies the missing assignments and links directly to Seat configuration.

## 6. Independent Seat lifecycle

HydraSeat runtime is machine-wide, but v1 game lifecycle is Seat-local.

If one player exits their game, the other player's game continues.

Example:

```text
Seat 1: Mario / Minecraft / Playing
Seat 2: Luigi / Terraria / exits

becomes

Seat 1: Mario / Minecraft / Playing
Seat 2: Idle
```

The idle Seat shows a minimal Seat Launcher on its assigned display. The player may:

- choose another game;
- choose another Player;
- end playing for that Seat.

While at least one Seat remains active, an ended/idle Seat stays in a simple HydraSeat waiting/launcher state rather than returning that display to an unrestricted ordinary Windows desktop. This avoids mixing a partially active split session with uncontrolled global Windows interaction.

When both Seats end, or the Management UI explicitly requests `Return to Windows`, HydraSeat performs verified rollback and restores ordinary one-PC Windows behavior.

## 7. v1 is game-only

HydraSeat v1 officially manages games, their required launchers, helpers, child processes, and bounded support processes.

It does **not** promise an independent general-purpose Windows desktop per Seat. Arbitrary Seat-scoped Chrome, Discord, office applications, independent taskbars, clipboard virtualization, per-Seat wallpaper, and a complete alternative desktop shell are outside the v1 product contract.

This keeps the project focused on the reason it exists: two people locally gaming on one PC.

A future version may revisit broader application use only after the gaming product is stable.

## 8. Minimal Seat UI, not a full desktop shell

The v1 per-Seat UI is intentionally small.

When no game is running on a Seat it may show:

```text
Seat 2
Luigi

Minecraft
Terraria
Stardew Valley
More Games...

End Playing
```

During game startup it may show compact readiness/progress information. Once the game is running, the Seat UI should disappear or remain non-intrusive.

v1 does not need a HydraSeat taskbar, wallpaper system, desktop zones, clipboard manager, or full shell replacement.

## 9. Same-game multi-instance as a differentiator

Running different games on two Seats is the baseline product requirement.

Running two independent instances of the same title is a high-value differentiator when the title and provider already permit it. HydraSeat should reduce the manual work users normally perform themselves by automating repeatable instance separation through a two-player game setup.

The product promise is:

> HydraSeat automates multi-instance gaming where the game and provider permit it. It does not defeat restrictions to create multi-instance support.

Automatic setup and manual setup are both first-class paths. Automation lowers the barrier for normal users; the manual path preserves experimentation and community problem-solving for titles the automatic system does not yet understand.

## 10. Compatibility evidence: reports, not official support badges

HydraSeat v1 does not need an official `HydraSeat Certified` game badge.

Compatibility is better represented as transparent evidence from real runs.

A game can show:

- number of submitted reports;
- successful and failed report counts;
- overall success percentage;
- optional breakdowns such as launch, two-instance start, input isolation, audio routing, clean exit/recovery;
- HydraSeat version, game version, provider, Windows version, and relevant backend grouping for the evidence;
- whether the title is known to use anti-cheat, DRM, or another protection system.

Example:

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

A percentage is evidence, not a guarantee. Old or materially different environments must not be mixed blindly into a current percentage.

## 11. Protected games and experimental attempts

HydraSeat does not hide, disable, bypass, or evade anti-cheat or DRM.

Known protected titles are not silently presented as ordinary compatible games. The UI must show a prominent warning before a HydraSeat experiment. The warning explains that the title or protection system may block the software, refuse to launch, disconnect, or take other action under its own policy, and that HydraSeat has not established anti-cheat safety.

The user may explicitly opt into an advanced experiment instead of being hard-blocked. This preserves the ability to discover future compatibility through real evidence without claiming safety.

Results from protected-title experiments must remain clearly marked as protected/experimental. Technical launch success does not prove anti-cheat safety and must never be displayed as such.

## 12. Local-first compatibility testing

Compatibility testing runs locally first.

HydraSeat may automatically measure bounded facts such as:

- process/instance launch success;
- expected instance count;
- Seat-to-process/window ownership;
- input route/receiver evidence;
- measured cross-Seat input bleed;
- audio route result;
- exit and rollback result;
- relevant version and environment metadata.

A local result is saved locally by default.

Community submission is explicit opt-in. Before upload the user can preview the redacted JSON that will be shared.

Default community evidence must not contain:

- Windows user names;
- Player display names;
- account IDs where avoidable;
- passwords, tokens, cookies, or credentials;
- raw typed text;
- personal absolute paths;
- unrelated process data;
- stable device serial numbers unless a documented non-identifying class value is required.

The upload format is versioned and bounded so community evidence can be reprocessed when scoring rules improve.

## 13. Offline-first operation

HydraSeat core operation does not require an account or continuous Internet connection.

Offline capabilities include:

- Seat configuration;
- Player profiles;
- installed-game discovery from local providers/metadata;
- local game library;
- saved two-player game setups;
- manual/automatic local compatibility testing;
- launching previously available games and profiles;
- local diagnostics and recovery.

Optional online capabilities include:

- compatibility/profile catalog synchronization;
- opt-in community evidence upload;
- HydraSeat software update checks.

Community compatibility data should be distributable as versioned JSON/catalog artifacts so the project can begin without requiring a custom always-on backend service.

## 14. Update policy

Compatibility/profile data and the HydraSeat program have separate update lifecycles.

Compatibility/profile data may be checked and refreshed independently because it is small, data-driven, and expected to improve frequently. The user must be able to disable automatic compatibility data refresh and continue with the local cache.

Executable, installer, driver, or core runtime updates require clear user approval. HydraSeat should remain usable without immediately installing a new program version when the installed version and local profiles still meet the user's needs.

All downloaded artifacts are version/hash/trust checked before use.

## 15. Privilege model

HydraSeat should run with ordinary user privileges whenever Windows permits the required operation.

Administrator/UAC elevation is requested only for narrowly defined operations that genuinely require it, such as installation, optional driver/service installation or configuration, or specific system-level recovery/setup actions.

The normal flow should not require launching the main game UI as administrator.

Any privileged component must have a narrow allowlisted contract, explicit rollback, and no general command-execution interface.

## 16. Installer is part of the product

A usable v1 requires a real Windows installer/uninstaller. Building with MSVC, CMake, and Qt is a developer workflow, not an end-user workflow.

The installer/first-run experience should handle:

- prerequisites and architecture checks;
- installation of required runtime components;
- optional elevated components only when selected/required;
- first-run Seat wizard;
- safe repair/uninstall;
- update staging and rollback;
- diagnostics for failed setup.

Uninstall must restore ordinary Windows behavior and remove only HydraSeat-owned persistent state.

## 17. v1 release definition

v1 is complete only when the complete physical user journey works, not merely when isolated code packets compile.

Release acceptance includes at least:

- a clean installer and uninstaller;
- exactly two v1 Seats;
- real two-display/two-input physical acceptance;
- independent input with objective zero-bleed evidence for the tested configuration;
- separate audio/controller routing where required by the chosen games;
- two different real games running concurrently;
- at least one real same-title/two-instance demonstration where the game/provider permit it;
- Player profiles and game-first selection;
- automatic local game discovery plus manual add fallback;
- automatic and manual two-player game setup paths;
- one Seat ending/restarting a game without stopping the other Seat;
- idle Seat Launcher behavior;
- both Seats ending and verified return to ordinary Windows;
- crash/watchdog/emergency recovery;
- local-first compatibility evidence and optional redacted community submission format;
- offline use of already installed games/profiles;
- user-approved program updates;
- license/contribution/legal distribution gate resolved before describing the release as open source.

The release gate is evidence-driven and does not depend on claiming official support for a large number of games. Community reports are expected to expand the compatibility knowledge base after release.

## 18. Explicit non-goals for v1

HydraSeat v1 does not promise:

- more than two active Seats;
- a VM or independent Windows installation per Seat;
- a second Windows logon session per Seat;
- general-purpose independent desktops;
- independent per-Seat Windows taskbars, clipboard, wallpaper, or full shell replacement;
- universal same-title multi-instance support;
- anti-cheat, DRM, account, launcher, or single-instance bypass;
- anti-cheat safety certification;
- cloud accounts or mandatory telemetry;
- compatibility with every game;
- hiding technical risk behind a generic `Supported` badge.

## 19. Product decision rule

When two technical choices are otherwise reasonable, prefer the choice that makes this workflow more reliable and easier for a non-developer:

```text
Install HydraSeat
    -> optionally configure two Seats
    -> choose a game
    -> choose where to play
    -> choose Players
    -> Play
    -> one player may stop/change games independently
    -> both players finish
    -> return safely to ordinary Windows
```

If a proposed feature does not materially improve this v1 journey, its safety, its compatibility evidence, or its recovery path, it should normally be deferred.