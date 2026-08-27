# HydraSeat Non-Negotiable Design Decisions

These decisions prevent future agents from repeatedly reopening settled architecture. A decision may change only through an explicit roadmap/document update that explains migration and compatibility impact.

## D-001 — Seat is the primary physical-station abstraction

A Seat is the physical local gaming station, not a single monitor, person, game, account, or game window.

A Seat may own:

- multiple physical or virtual displays;
- one explicit primary display;
- zero or more keyboards, mice, touchpads, controllers, audio endpoints, and microphones;
- physical station hardware only at persistence time;
- runtime process/window, cursor/focus/input, Player, Game, and setup state are associated later and are not permanent Seat identity.
- saved Seat configuration may be incomplete; missing devices are checked against the selected game's requirements at launch.
- a minimal Seat Launcher may be associated at runtime when the Seat is idle, but it is not permanent Seat identity and is not a general desktop shell.

Persistent and public new code uses `Seat`, `SeatId`, `SeatConfig`, or `SeatRuntimeState`. Existing `Workspace*` names may remain temporarily for compatibility but must not drive new architecture.

## D-002 — Multi-monitor Seats are first-class

The model must support:

```text
Seat 1 = LG + Samsung + keyboard A + mouse A + headset A
Seat 2 = BenQ + keyboard B + mouse B + speakers B
```

No future API may assume one display per Seat. Every display-routing type must represent a set of outputs and a primary member.

## D-003 — One Windows session first, separate sessions only as optional backends

The default goal is one interactive Windows session with logical Seat isolation. Virtual machines, RDP, and mandatory streaming are not the primary architecture.

A future separate-session or streaming backend may exist only behind the same capability/profile interface and must not replace direct local-monitor support.

## D-004 — Hybrid process topology

The intended product is split into:

- configuration/tray UI;
- per-user background host;
- independent watchdog;
- emergency reset CLI;
- optional process-local adapters;
- minimal per-Seat launcher clients used only for idle/start/warning/recovery UX in v1.

The UI is never the authority for runtime isolation and may close while Seats remain active. The host may never rely on a visible GUI message loop for recovery-critical work.

## D-005 — Capability-planned and fail-closed

Every backend advertises only verified capabilities. A profile states required and optional capabilities. Planning fails when any required capability is missing or denied by policy.

Forbidden fallbacks:

- `PostMessage` pretending to be Raw Input virtualization;
- device cloaking pretending to be replacement input delivery;
- process-local state pretending that an unmodified game calls it;
- controller detection pretending to be per-process controller routing;
- a successful synthetic test pretending to be physical or game acceptance;
- “best effort” rerouting of unassigned/shared input to an arbitrary Seat.

## D-006 — Controlled probes before third-party processes

Input API interposition, startup shims, hooks, adapters, and physical-device control are implemented and validated in this order:

1. pure state/protocol tests;
2. HydraSeat-owned target process;
3. HydraSeat-owned API probe calling real Windows APIs;
4. open-source non-protected test application;
5. explicit non-anti-cheat game profile;
6. two different supported games.

Commercial or protected processes are not used to discover basic correctness.

## D-007 — No anti-cheat, DRM, or protected-process bypass

HydraSeat does not hide modules, evade integrity checks, disable protection, or provide bypass instructions. A protected profile is `ObservationOnly` or `Blocked` unless a documented non-invasive path covers all required capabilities.

## D-008 — Physical input suppression is distinct from device cloaking

Hiding a HID device from selected processes is not proof that all ordinary keyboard/mouse paths are suppressed. `PhysicalDeviceCloaking` and `PhysicalInputSuppression` remain separate capabilities until physical Gate D tests prove the guarantee.

## D-009 — Physical mutation occurs last

For any activation transaction:

```text
Plan -> Preflight -> Start helpers -> Handshake -> Verify replacement path
     -> Enable routes -> Cloak/suppress physical device last -> Self-test
```

Rollback reverses this order. No driver/device/display mutation occurs before a replacement path and independent recovery path are verified.

## D-010 — Stable identities, not enumeration order

Persistent assignments use SetupAPI/ConfigMgr/display/audio stable identities. Friendly names, Raw Input handles, XInput slot order, display indices, and array position are runtime hints only unless the profile explicitly declares an ordering-based compatibility rule.

Composite HID child collections may map to one physical identity, but their individual handles remain visible for hot-plug and diagnostics.

## D-011 — Shared input fails closed unless the profile defines fan-out

A keyboard/mouse assigned to more than one Seat is ambiguous for exclusive game routing. It is not delivered to either target unless a later explicit `SharedInputPolicy` defines deterministic fan-out semantics and the profile opts in.

## D-012 — Protocols, schemas, and ABI are versioned

No public or persisted contract is an unversioned C++ memory dump.

Required properties:

- fixed-width fields;
- explicit byte order where transported;
- bounded lengths;
- schema/API version;
- malformed and future-version rejection;
- compatibility/migration tests;
- reserved fields initialized and validated;
- no pointer-size assumptions across x86/x64 boundaries.

## D-013 — Physical displays first, virtual displays optional

Phase 4 must fully support physical multi-monitor Seat groups before virtual display creation is required. Virtual displays are provided through a capability-gated adapter interface.

A custom IddCx/IDD driver is adopted only after:

- official sample/reference validation;
- signing/install/update/recovery design;
- latency and GPU compatibility measurement;
- clear benefit over a user-supplied external adapter.

## D-014 — Window ownership follows process ownership

A window belongs to the Seat that owns its process tree, unless an explicit profile overrides it. Window location alone is not ownership. The runtime tracks process/child/window lifecycle and moves only owned windows.

## D-015 — Seat-local coordinates are explicit transforms

Windows retains the real global desktop coordinate space. HydraSeat represents:

- physical global coordinates;
- Seat display-group bounds;
- Seat-local coordinates;
- per-output DPI/scaling;
- primary-display origin.

No code assumes every game accepts a virtual `(0,0)` origin. Profiles declare whether coordinate/focus/window API virtualization is required.

## D-016 — Exclusive fullscreen is capability-gated

Borderless/windowed placement is the first supported path. Exclusive fullscreen is supported only when measured and declared by profile/backend. HydraSeat never silently changes a user's graphics mode without displaying the profile policy.

## D-017 — Controllers are API-specific

XInput, DirectInput, Raw HID, SDL, and vendor APIs are different compatibility surfaces. Controller support is not complete until the profile identifies and the backend covers the API the target actually uses.

## D-018 — Audio routing is backend- and Windows-build-specific

Audio endpoint ownership is part of a Seat, but per-process routing is a capability. If no safe supported backend exists for the current Windows build/application, the planner reports unsupported or user-assisted configuration rather than silently playing through the default device.

## D-019 — Background runtime state is authoritative

Runtime status is an explicit state machine with correlation/session IDs. UI labels, button text, process existence, or profile contents are not proof that isolation is active.

The host publishes a read-only state snapshot to UI/CLI/watchdog consumers.

## D-020 — Recovery is a product feature

Every risky feature includes rollback implementation and tests in the same packet or a declared prerequisite packet. “Restart Windows” is not the normal rollback strategy.

Minimum recovery components:

- independent watchdog or expiry timeout;
- emergency reset CLI;
- captured prior state;
- crash journal;
- safe-mode startup marker;
- idempotent cleanup;
- visible `RecoveryRequired` state when cleanup cannot be confirmed.

## D-021 — The host callback path cannot block on external components

Raw Input and other latency-sensitive callbacks may enqueue bounded data but may not synchronously wait on:

- another process;
- a named pipe beyond an effectively nonblocking operation;
- disk/log flush;
- network access;
- driver installation/configuration;
- UI thread work.

Queue overflow and consumer failure are explicit errors, never silent drops or cross-Seat fallback.

## D-022 — Diagnostics are structured and privacy-limited

Diagnostics are machine-readable and correlated, but default logging avoids text entered by the user. Key codes/button transitions may be recorded only in an explicitly enabled input diagnostic mode with a visible warning and bounded trace retention.

## D-023 — Qt is a UI option, not an engine dependency

The current Win32 UI may be replaced or supplemented by Qt 6. Core hardware, planning, runtime, IPC, adapter, watchdog, profile, and recovery modules remain usable without Qt.

No packet may pull Qt into a low-level static library solely for convenience.

## D-024 — Optional dependencies are explicit and user-supplied or packaged lawfully

HydraSeat does not silently download or execute external injection DLLs, wrappers, drivers, or tools. An optional component requires:

- origin and version;
- expected hash;
- license/redistribution status;
- architecture;
- availability probe;
- explicit user approval;
- uninstall/rollback behavior.

## D-025 — The repository license must be resolved before third-party code import

Until a tracked project license and contribution terms exist, third-party source is not copied into HydraSeat. Public architecture and behavior may be studied under `docs/CLEAN_ROOM_POLICY.md`.

## D-026 — One work packet, one reviewable purpose

A normal implementation PR contains one packet or a tightly coupled group explicitly allowed by the phase document. Large formatting changes, broad renames, generated assets, dependency upgrades, and unrelated refactors are separate packets.

## D-027 — Manual gates remain manual

An agent cannot mark these complete without recorded user/hardware evidence:

- physical keyboard/mouse/controller/display/audio hot-plug acceptance;
- device cloaking/suppression recovery;
- reboot/startup behavior;
- game compatibility;
- latency and zero-bleed measurements;
- installer/uninstaller behavior on a clean Windows machine;
- signed driver/application validation.

## D-028 — Compatibility claims are matrix entries

“Supported” always names:

- game/application and version;
- launcher/provider;
- Windows build;
- CPU/GPU/driver where relevant;
- backend set and versions;
- display/input/controller/audio topology;
- known limitations;
- evidence date and support level.

## D-029 — Performance is measured, not inferred

Latency, CPU, memory, queue depth, frame timing, audio delay, and rollback duration require measurement hooks and reproducible benchmarks. Source review or “it feels responsive” is not acceptance evidence.

## D-030 — Upstream intent is preserved while v1 product scope stays focused

HydraSeat remains a Windows local gaming multiseat project based on physical-device detection, assignment, input compatibility, display routing, and repeatable game setup. v1 narrows that intent to two active gaming Seats, a background host/watchdog, game-first UI, minimal idle Seat Launcher, and evidence-driven compatibility rather than expanding into remote desktop, virtual machines, a general desktop shell, or an N-Seat platform.

## D-031 — One Management Seat owns the visible control plane

HydraSeat separates the background runtime from the user's control surface. Exactly one configured `ManagementSeatId` owns the default visible control plane for the active session. The default is Seat 1 unless the user explicitly selects another Seat.

The management control surface opens on the Management Seat's primary display and provides game/Seat/Player status, Start/Stop for the relevant Seat, Return to Windows, Reconfigure, diagnostics, and recovery controls. A non-management Seat's minimal launcher is Seat-local by default and cannot terminate or reconfigure the whole machine without an explicit permission policy.

If the configured management display is unavailable, HydraSeat falls back visibly to the current Windows primary display or a documented recovery surface; it never opens an invisible/off-screen control window.

## D-032 — Background runtime and visible controller are independent

`hydra_host.exe` and `hydra_watchdog.exe` may run with no visible application window. `HydraSeat.exe` is an on-demand controller client that can be opened, closed, or restarted without stopping an active validated session.

Supported product modes are explicit:

- `Manual`: nothing activates until the user starts HydraSeat and presses Start;
- `BackgroundIdle`: host/watchdog start at logon but no Seat session activates until requested;
- `AutoActivateValidatedSession`: host/watchdog start at logon and may activate one explicitly selected, previously validated session only after crash-journal, safe-mode, topology, capability, and recovery preflight passes.

No startup mode may silently auto-activate an unvalidated or changed topology/profile.

## D-033 — Returning to ordinary Windows is a first-class operation

The product must always expose a normal `Stop / Return to Windows` operation from the Management Seat control surface, tray/recovery surface, and emergency reset path.

A successful return-to-Windows transaction means:

1. stop new Seat launches/input routing;
2. unwind session-scoped physical cloaking/suppression according to the captured rollback plan;
3. remove process-local compatibility shims/adapters and Seat cursor/shell surfaces;
4. restore audio, controller, window, display, and other captured mutable state;
5. stop or detach owned target processes according to profile policy;
6. verify that ordinary Windows input/display/audio behavior is restored;
7. leave the host either idle in the background or exit it according to user mode.

The UI may not report `Stopped` merely because target windows disappeared. Rollback postconditions must be verified by the host/watchdog.

## D-034 — Reconfiguration is a controlled session transition

The normal user workflow for changing monitors, keyboards, mice, controllers, audio endpoints, or target composition is `Reconfigure`, not editing live internal state ad hoc.

Default safe behavior while a session is active:

```text
Reconfigure
  -> snapshot current session/profile
  -> Stop / Return to Windows with verified rollback
  -> open configuration UI on the Management Seat display
  -> identify/flash/test displays and input devices
  -> validate assignments and compile preflight plan
  -> Save
  -> Start now, or remain in ordinary Windows mode
```

A future packet may permit specific hot-reconfiguration operations, but only when that operation has its own capability, atomic transition, rollback, and physical acceptance. Unsupported live reconfiguration fails closed and uses the safe stop-edit-start path.

## D-035 — English-first localization with Korean and Simplified Chinese as release languages

HydraSeat user-facing UI and end-user documentation are internationalized from the start rather than translated after UI code is complete.

Initial supported locales are:

- `en-US` — canonical source language and unconditional fallback;
- `ko-KR` — Korean;
- `zh-CN` — Simplified Chinese.

Windows UI language may select the initial locale when it exactly matches a supported locale/family, but the user can always override it in the Management Seat control console. Unsupported locales fall back to `en-US`. Locale selection is per Windows user and does not alter protocol/schema/profile semantics.

Rules:

- user-visible UI strings use stable localization message IDs; no new shipping UI text is embedded ad hoc in widgets/window procedures;
- localization resources are UTF-8 and versioned/tested for key and placeholder parity;
- machine-readable protocol fields, schema keys, CLI option names, diagnostic/error codes, source identifiers, and compatibility IDs remain stable English identifiers;
- source-code comments, developer-facing docstrings, commit-oriented technical comments, and implementation notes are written in English;
- localized UI may translate explanatory diagnostic text, but retains/copies the stable diagnostic code for support;
- layouts must tolerate Korean/Chinese glyphs, longer translations, mixed DPI, and Windows system-font fallback without bundling private font files;
- locale changes must not require restarting an active Seat session; at most the disposable UI/shell client may relayout/reload resources;
- security/recovery buttons must remain semantically identifiable in all supported languages;
- `README.md` is the canonical English README and links to maintained `README.ko.md` and `README.zh-CN.md` versions; localized README files link back to all language variants;
- release validation checks localized README/version/link consistency and supported-locale UI resource completeness.

Traditional Chinese or additional locales may be added later through the same resource contract without changing core runtime protocols.

## D-036 — Household shared-PC gaming is the product north star

HydraSeat is not being built only as an input-isolation research harness. The product target is a household/friend shared-PC experience in which one sufficiently capable Windows gaming PC can become two or more simultaneous local gaming stations while preserving ordinary local-monitor performance and a clear path back to normal Windows.

The roadmap must preserve these user outcomes:

- two or more Seats can run different games/applications concurrently;
- a Seat may use one or multiple monitors and its own keyboard, mouse, controller, and audio endpoints;
- separate instances of the same multiplayer title are supported only when the exact game version, launcher/provider, account/license rules, single-instance behavior, and compatibility profile permit it;
- HydraSeat never bypasses DRM, anti-cheat, account restrictions, launcher restrictions, protected processes, or single-instance protections to create same-title support;
- setup, Start, Stop / Return to Windows, Reconfigure, diagnostics, and recovery must be understandable to a non-developer;
- the added CPU/memory/input-latency overhead must be measured and low enough that sharing available machine headroom remains useful for gaming.

When technical choices are otherwise equivalent, prefer the path that improves a reproducible two-Seat household workflow over a research-only capability that cannot be integrated into the user journey.

## D-037 — Public open-source distribution is a gated product objective

The intended long-term distribution model is a broadly reusable open-source project with public documentation, compatibility evidence, contribution-friendly profiles, and a community extension ecosystem.

That intent does not change the repository's current legal state. Until a tracked project license and contribution terms are resolved:

- documentation must not claim that the repository is already open source;
- external code contributions and third-party source reuse must follow D-025 and `docs/CLEAN_ROOM_POLICY.md`;
- release/community tooling may be designed for future public use, but redistribution rights must not be assumed;
- P10-LIC-01 remains the formal release gate for project license, contribution policy, notices, and provenance.

After that gate is satisfied, package, SDK, compatibility-profile, documentation, and contribution workflows should minimize friction for lawful community adoption without weakening security, provenance, compatibility, or recovery requirements.

## D-038 — Every numbered roadmap phase ends with a phase-wide code verification gate

A numbered roadmap phase is not complete merely because its final packet passes. Before Phase N is marked `Complete` and the default implementation flow advances to Phase N+1, HydraSeat performs a dedicated **Phase-close verification** across the whole phase.

The verification is intentionally broader than packet review. It rechecks all phase-owned code, cross-packet integration, tests, contracts, documentation, and declared evidence as one system. At minimum it reviews normal and failure paths, resource ownership/teardown, rollback, stale/duplicate handling, no-cross-Seat invariants, ABI/protocol/schema compatibility, Windows x64/x86 behavior when applicable, manual/physical evidence, and performance/recovery claims required by that phase.

Rules:

- Phase-close verification is a review gate, not a multi-feature implementation packet and not an exception for opportunistic feature work.
- Defects found during the audit reopen the owning packet or create a narrowly scoped repair task; the audit is rerun after the repair is validated.
- A phase with unresolved required evidence remains `Current`/incomplete even if every implementation packet is individually `VALIDATED`.
- `docs/implementation/STATUS.md` records the exact phase-close scope, reviewed commits, test/CI/manual evidence, findings, repairs, and final pass/fail result.
- Cross-phase prerequisite work explicitly declared by the roadmap may still be pulled forward, but it does not waive the prior phase's close gate.

## D-039 — HydraSeat v1 supports exactly two active Seats

HydraSeat v1 product activation, installer UX, acceptance matrix, documentation, and compatibility evidence target a maximum of two active Seats. Generic internal containers may remain where useful, but v1 must reject plans containing more than two active Seats and must not be delayed by N-Seat generalization.

This decision narrows earlier `two or more` product wording in D-030/D-036 for v1. A later version may revisit the limit after the two-Seat product is mature.

## D-040 — Seat, Player, Game, Two-player setup, and Runtime Session are separate concepts

Persistent hardware configuration must not become a catch-all user/game profile.

- `SeatConfig` describes the physical station only.
- `PlayerProfile` describes a lightweight person identity/preferences independent from Seat.
- `GameRecord` describes an installed/discovered title independent from Seat and Player.
- `TwoPlayerSetup` describes the optional same-game/two-instance compatibility recipe.
- runtime state binds Seat + Player + Game temporarily while playing.

A Player may move between Seat 1 and Seat 2. A game, account, save, process, or window is not permanently owned by a Seat.

HydraSeat does not become a general credential vault. Provider passwords/tokens remain provider-owned whenever practical; HydraSeat stores only the minimum account reference/selector metadata needed for an already authenticated provider identity.

## D-041 — Game-first UX and a minimal Seat Launcher replace the full-shell v1 goal

The normal v1 UI behaves like a lightweight game launcher:

```text
choose Game -> choose Seat 1 / Seat 2 / Both -> choose Player(s) -> Play
```

Click/tap is primary; drag-and-drop is optional convenience. Raw Input, HidHide, IAT, backend, device-path, plan-hash, and protocol terminology belongs in Diagnostics/Expert UI.

An idle Seat may run a minimal `hydra_seat_ui.exe` for Player/game selection, launch progress, warnings, errors/recovery, and `End Playing`. Once the game runs, that UI disappears or remains non-intrusive.

The following are deferred beyond v1: general per-Seat taskbar, wallpaper/desktop zones, arbitrary general-purpose app launching, per-Seat clipboard virtualization, and a full Windows shell replacement. Earlier optional-shell wording is interpreted under this narrower v1 scope.

## D-042 — Game lifecycle is independent per Seat

Whole-machine split/runtime state and per-Seat game lifecycle are distinct.

Required v1 behavior:

- Seat 1 may remain Playing while Seat 2 is Idle;
- one Seat may stop its own game without stopping the other Seat;
- an idle Seat may choose another Player/game and start again;
- while at least one Seat remains active, an idle/ended Seat stays in HydraSeat's minimal waiting/launcher state rather than returning to an unrestricted ordinary Windows desktop;
- both Seats ending, or an explicit Management UI `Return to Windows`, performs verified whole-machine rollback.

A normal game exit on one Seat is not a crash and is not a whole-machine Stop.

## D-043 — Two-player same-game setup has both automatic and guided manual paths

When both Seats choose the same game, HydraSeat resolves a `TwoPlayerSetup` describing lawful instance separation: instance/data/config directories, arguments/environment, provider choices, account references when supported, bounded launch order, process/window expectations, and input/controller/audio/display requirements.

Resolution order is:

```text
known validated local/community setup
 -> validate against local installation/provider/version
 -> bounded automatic setup generation where safe
 -> guided manual editor when automation cannot finish
```

Automatic generation is read-only until reviewed. Any mutation is typed, bounded, user-visible, reversible, and scoped. Manual configuration does not grant arbitrary script execution by default.

HydraSeat never defeats DRM, anti-cheat, account rules, launcher policy, or deliberate single-instance restrictions to make same-game multi-instance work.

## D-044 — Compatibility uses transparent evidence, not an official certification badge

HydraSeat v1 does not require a maintainer-created `HydraSeat Certified`/official support badge.

Compatibility UI may show success/failure counts, sample size, overall percentage, and sub-results such as launch, two-instance start, input isolation, audio routing, and clean shutdown. Percentages are observations, not guarantees.

Aggregation must segment materially different game versions, HydraSeat versions, providers, Windows environments, and compatibility paths. `Untested` means no useful evidence exists; it does not mean unsupported.

## D-045 — Protected games may be explicit experiments but never bypass or safety claims

This decision refines the status wording in D-007 while preserving its no-bypass boundary.

Known anti-cheat/DRM/protected titles are not silently treated like ordinary games. Before any HydraSeat experiment the UI must clearly warn that HydraSeat has not established compatibility or anti-cheat safety, and that the game/protection system may refuse launch, disconnect, block the software, or take action under its own policy.

The user may explicitly opt into an advanced experiment. HydraSeat still does not hide, evade, disable, bypass, or instruct bypass of any protection. A technically successful protected-game run remains `Protected / Experimental` and is never presented as proof of anti-cheat safety.

## D-046 — Compatibility evidence is local-first, privacy-limited, and opt-in for community sharing

Compatibility tests store results locally by default. Community upload is explicit opt-in and must support previewing the exact redacted JSON before submission.

Default shared evidence excludes credentials, passwords, tokens, cookies, raw typed text, Player display names, Windows account names, personal absolute paths, unrelated process data, and unnecessary stable device serials/account identifiers.

The evidence schema is versioned, bounded, and designed so scoring/aggregation rules can evolve without turning diagnostics into telemetry by default.

## D-047 — Core operation is offline-first and compatibility data updates are separate from program updates

HydraSeat core operation must work without a HydraSeat cloud account or continuous network access: Seat/Player configuration, local game discovery where provider metadata is available, saved two-player setups, local compatibility testing, launch/runtime, diagnostics, and recovery remain available offline.

Compatibility/setup catalogs are optional data updates. They may refresh independently and can be disabled while the local cache remains usable. The initial ecosystem should support static/versioned JSON artifact distribution so v1 does not require a custom always-on backend.

Executable/runtime/driver updates are a separate trust domain and require clear user approval plus version/hash/trust verification and rollback/health checking.

## D-048 — Least privilege and a real installer are v1 requirements

Normal `HydraSeat.exe` use and ordinary runtime operations run without elevation whenever Windows permits. UAC is requested only for narrowly defined installation, optional driver/service, explicit system mutation, or recovery operations that genuinely require administrator rights.

Any elevated broker/service exposes only a small typed allowlist and cannot become a general privileged command runner.

A developer CMake/MSVC/Qt workflow is not the end-user distribution contract. A usable v1 requires a Windows installer/repair/uninstaller with prerequisite checks, optional elevated component setup, first-run two-Seat wizard with `Set later`, safe update/rollback, and verified removal of HydraSeat-owned persistent state.

## D-049 — Local game discovery is the normal path; manual executable entry remains a fallback

The game library should discover installed titles read-only from supported local provider/install metadata before asking the user to browse for an EXE. Provider adapters are lawful integration layers, not restriction bypasses.

Power users retain a manual `Add game / EXE` path for unsupported providers and unusual installations. The UI should prefer icons already available from the local executable, shortcut, or provider metadata rather than redistributing a large third-party artwork catalog.

## D-050 — One-developer scope is a design constraint

HydraSeat is a one-developer project unless that changes explicitly. When two technical choices are otherwise reasonable, prefer the one that makes the two-Seat game-first journey, recovery/safety, or compatibility evidence simpler and more reliable.

Features that do not materially improve that v1 journey should normally be deferred rather than expanding HydraSeat into a general Windows multiseat desktop platform.

The canonical product contract for D-039 through D-050 is `docs/PRODUCT_V1.md`.
