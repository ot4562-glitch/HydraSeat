# Phase 7 — Minimal Seat Launcher and Game-First UX

## Phase objective

Deliver the actual v1 user experience after the runtime/game/setup foundations work.

Earlier roadmap versions envisioned a broad per-Seat desktop shell with taskbar, wallpaper, clipboard, arbitrary app surfaces, and extension points. D-041 deliberately narrows that scope. HydraSeat v1 is game-only, so the required per-Seat UI is a **minimal Seat Launcher** that appears when a Seat is idle, starting, warning, or recovering and stays out of the way while the game is playing.

## Phase exit gate

Phase 7 closes only when:

1. the main UI is game-first and a normal Play flow does not expose low-level backend jargon;
2. click/tap is the primary interaction, with drag-and-drop only an optional shortcut;
3. first-run two-Seat setup can be skipped and individual device categories can be `Set later`;
4. an idle Seat can choose Player/game and start independently while the other Seat keeps playing;
5. the minimal Seat Launcher disappears or remains non-intrusive while the game is active;
6. protected-game experiments show explicit risk acknowledgement;
7. errors/recovery actions are understandable without developer CLI use;
8. English/Korean/Simplified Chinese localization framework and layouts pass declared checks;
9. DPI/accessibility/input modality checks pass;
10. deferred full-desktop features are not accidentally promoted back into v1;
11. Phase-close verification passes.

---

## P7-SHELL-01 — Minimal per-Seat launcher process and model

**State:** CODE_COMPLETE

**Goal**

Create `hydra_seat_ui.exe` as a small per-Seat client for idle/start/warning/error/end-playing states, not a Windows shell replacement.

**Depends on**

- P6-UI-01
- P6-REG-01
- P4-SEAT-01

P6-CLOSE-01 remains a mandatory Phase 6 validation/release gate. Under D-051,
its real-game/manual acceptance does not block this packet's controlled/fake
automated implementation.

**Surface**

- authoritative Seat ID/runtime state;
- current/selected Player;
- recent/available games;
- start progress;
- missing requirement/warning;
- `End Playing`;
- bounded recovery action/link;
- reconnect/resnapshot to host.

**Invariants**

- Seat UI has no whole-machine runtime authority by default;
- one Seat UI cannot stop another Seat's game;
- it never becomes a general arbitrary application launcher;
- closing/crashing it does not corrupt the active game/runtime;
- it cannot escape its assigned display group through stale placement state.

**Done when**

Two controlled Seat UI processes can independently display authoritative idle/playing/recovery state and restart without affecting games or each other.

**Implementation evidence (2026-08-29)**

- `hydra_seat_ui.exe` consumes only full protocol-v3 host snapshots through a
  fixed-Seat model and a capability-restricted `SeatHostClient` adapter;
- protocol role `SeatControl` may issue only bounded assign/start/stop game
  lifecycle requests whose payload Seat matches the authenticated handshake
  Seat. Global commands, reconcile, unknown Seats, and cross-Seat commands are
  rejected by the host with `PermissionDenied`;
- the pure model rejects duplicate/missing Seat state, v1-limit violations,
  authority changes without reconnect, generation/sequence rollback, malformed
  display groups, and stale display redirection transactionally;
- the Win32 client exposes Seat status, current/selected Player/game,
  recent/available game presentation, progress/warnings, `End Playing`, and a
  bounded reconnect/resnapshot action. Playing collapses to a small status bar;
- the real window stays hidden until every assigned display resolves against a
  fresh topology snapshot, so an unresolved/stale group cannot place it on an
  arbitrary display;
- controlled x64/x86 process tests run two clients simultaneously, verify exact
  independent Seat reports for idle/playing/recovery, close and restart one
  client, and prove host game state and whole-machine return policy are unchanged;
- focused MinGW, MSVC Release x64, and MSVC Release Win32/x86 protocol/IPC/model/
  process tests pass 4/4 per architecture.

This is controlled evidence only. Physical display placement, DPI/input
operation, and real-game interaction remain Phase 7 validation/close gates.

---

## P7-LAUNCH-01 — Idle Seat game/Player selector

**State:** CODE_COMPLETE

**Goal**

Let an idle Seat choose another Player/game and start again without bringing down the other active Seat.

**Depends on**

- P7-SHELL-01
- P6-UI-01

**UX**

```text
Seat 2
Luigi

Minecraft
Terraria
Stardew Valley
More Games...

Change Player
End Playing
```

Selection may use click/tap/controller navigation. It invokes the same Game/Player/preflight/plan contracts as the Management UI.

**Invariants**

- no separate hidden launch logic in Seat UI;
- same-game selection invokes the same TwoPlayerSetup resolver;
- missing devices link to Seat settings/preflight, not silent default hardware;
- the other Seat's active session remains unchanged.

**Done when**

An idle Seat can switch Player/game and return to Playing in an end-to-end controlled test while the other Seat process remains alive and stable.

**Implementation evidence (2026-08-29)**

- `LauncherUiModel::initializeShared` transactionally loads the same validated
  Seat/Player/catalog/setup/provider/requirement snapshots for another client;
  recent games, preferred Seat, avatar, and cross-document references remain
  bounded and fail closed;
- `SeatLaunchFlow` composes that exact P6 selection, TwoPlayerSetup resolver,
  provider-aware compiler, and normal preflight. It contains no second launch
  parser or arbitrary command surface;
- activation resnapshots and requires the same host session, authority
  generation, transition sequence, configured Seat document, own idle
  generation, and exact other-Seat state before installing a typed Seat plan;
- only the selected idle Seat receives assign/start/rollback calls. Presentation
  recents/preferences are recorded only for the newly activated Seat;
- controlled tests cover different games, same-game two-instance setup, missing
  devices, missing setup, stale authority/other-Seat changes, install/assign/start
  failures, and Seat-local rollback;
- Windows x64/x86 Job Object E2E starts Seat 1 root+descendant processes, launches
  idle Seat 2 through the shared flow, proves Seat 1 PID+creation-time identity
  remains live and unchanged, stops Seat 2 independently, and finishes with zero
  owned process orphans;
- strict MinGW plus MSVC Release x64 and Win32/x86 focused flow tests pass 2/2
  per architecture, with the shared P6 UI model regression also passing.

The production requirement-evidence store and provider-plan installer remain
injected boundaries; no real provider/game launch is claimed. This packet is
therefore controlled `CODE_COMPLETE`, not `VALIDATED`.

---

## P7-TASK-01 — General Seat taskbar/window surface

**State:** DEFERRED

**Goal**

Historical full-desktop task/window surface.

**Depends on**

- P7-SHELL-01

**v1 decision**

A general per-Seat taskbar/window switcher is not required for a game-only two-Seat product and is deferred beyond v1.

**Done when**

Deferred. Reactivate only if post-v1 general-application use becomes an explicit product goal.

---

## P7-DESK-01 — Seat wallpaper and desktop zones

**State:** DEFERRED

**Goal**

Historical per-Seat desktop/wallpaper surface.

**Depends on**

- P7-SHELL-01

**v1 decision**

Wallpaper/desktop zones add implementation/UX complexity without materially improving the v1 gaming journey.

**Done when**

Deferred beyond v1.

---

## P7-CURSOR-01 — Seat Launcher cursor/input presentation where required

**State:** CODE_COMPLETE

**Goal**

Ensure the minimal Seat Launcher can be operated independently on its Seat when Windows global cursor semantics would otherwise make the idle-Seat UX unusable.

**Depends on**

- P7-SHELL-01
- P4-DIS-02
- P3-API-03

**Scope**

Implement only the cursor/input presentation actually required by the Seat Launcher. A general independent desktop cursor system is not a goal.

**Invariants**

- rendering/input ownership stays inside the Seat display group;
- active game input isolation is not weakened;
- system cursor state is restored/left unchanged according to the selected validated path;
- controller-only navigation may avoid extra cursor complexity entirely.

**Done when**

The Seat Launcher is independently usable with the declared mouse/controller input methods on both Seats without cross-Seat control bleed.

**Implementation evidence — 2026-08-29**

- `SeatNavigationModel` accepts only exact Seat-scoped pointer samples whose display ID, coordinates, and authority generation match the configured Seat display group. Cross-Seat, stale, unassigned-display, and out-of-region samples fail transactionally.
- The model never calls or exposes `SetCursorPos`, `ClipCursor`, `ShowCursor`, hooks, or global cursor mutation. Controller focus navigation is the declared no-extra-cursor fallback and remains bounded to the launcher's item count.
- Focused strict MinGW `SeatNavigationModelTests` pass. Physical two-pointer/controller usability and no-bleed evidence remain manual/physical acceptance and are not claimed here.

---

## P7-HOTKEY-01 — Seat-scoped launcher and recovery hotkeys

**State:** CODE_COMPLETE

**Goal**

Provide only the small set of Seat-local shortcuts necessary for the v1 game flow and emergency recovery.

**Depends on**

- P7-SHELL-01
- P8-RESET-01

**Possible actions**

- open/show Seat Launcher while idle where applicable;
- `End Playing` confirmation;
- surface recovery/help;
- never grant normal non-management Seat a whole-machine stop/reconfigure shortcut accidentally.

**Done when**

Hotkey ownership is explicit, cannot cross Seats, and emergency reset remains independently available.

**Implementation evidence — 2026-08-29**

- `SeatHotkeyModel` consumes only already Seat-scoped semantic chords with exact Seat and authority generation/sequence; cross-Seat and stale contexts fail without producing an action.
- The Win32 Seat Launcher maps only focused-window F5/F1/Ctrl+Shift+E/Ctrl+Shift+R input. No global `RegisterHotKey` or hook is installed. `End Playing` requires confirmation and remains Seat-local.
- The emergency shortcut can only show localized recovery/reset guidance; it cannot execute whole-machine reset. The independently validated `hydra_reset.exe` retains its own explicit boundary.
- Focused `SeatHotkeyModelTests` and the Seat UI target build pass. Physical keyboard ownership remains part of the pending input acceptance matrix.

---

## P7-NOTIFY-01 — Seat-scoped runtime notifications

**State:** CODE_COMPLETE

**Goal**

Display minimal actionable per-Seat status without a general notification center.

**Depends on**

- P7-SHELL-01

**Examples**

- game failed to start;
- required controller/audio/input missing;
- device disconnected;
- setup requires review;
- protected/experimental warning;
- Seat recovery required.

**Invariants**

- notifications derive from host/preflight state;
- no credentials/private paths/raw typed text;
- unrelated Windows notification interception is out of scope.

**Done when**

Declared runtime/preflight failures produce readable Seat-local UI and no stale success state.

**Implementation evidence — 2026-08-29**

- `SeatNotificationModel` maps only stable host/preflight codes into bounded Seat-local notification IDs/actions. Raw host diagnostics, Expert text, private paths, typed text, account material, and other-Seat messages are never copied into presentation output.
- Notifications are authority-generation/transition-sequence pinned; stale success cannot clear newer warning/recovery state, while a fresh authoritative success clears obsolete notices completely. Disconnect resets the authority epoch for safe host restart/resnapshot.
- Output is deduplicated, deterministically ordered, and bounded to 32 entries. The Seat UI renders localized text from the stable notification ID rather than the raw diagnostic.
- Focused `SeatNotificationModelTests` pass on the local Windows x64 MinGW build.

---

## P7-CLIP-01 — Optional Seat clipboard policy

**State:** DEFERRED

**Goal**

Historical per-Seat clipboard concept.

**Depends on**

- P7-SHELL-01

**v1 decision**

Clipboard virtualization is outside the game-only product contract.

**Done when**

Deferred beyond v1 unless a concrete supported game/setup later requires a narrowly scoped clipboard behavior.

---

## P7-I18N-01 — UI localization framework and English/Korean/Chinese catalogs

**State:** CODE_COMPLETE

**Goal**

Make user-visible UI strings localizable while keeping developer/machine identifiers stable English.

**Depends on**

- P7-SHELL-01
- P6-UI-01

**Locales**

- `en-US` canonical fallback;
- `ko-KR`;
- `zh-CN`.

**Invariants**

- protocol/schema/CLI switch/diagnostic code/capability/backend/profile/packet identifiers are not localized;
- source comments remain English;
- missing translation falls back safely to English;
- UI strings use stable IDs;
- long translated text cannot hide safety/recovery buttons.

**Done when**

Main game-first UI, Seat Launcher, first-run setup, warnings, recovery, and installer-facing shared strings render correctly in all three locales in the declared test matrix.

**Implementation evidence — 2026-08-29**

- `hydra_ui_localization` provides stable `TextId` entries for the game-first Management UI, Seat Launcher, `Set later`/optional Seat setup, protection confirmation, recovery, and common safety actions in `en-US`, `ko-KR`, and `zh-CN`, with English fallback for unsupported locales.
- The Win32 Games UI and Seat Launcher consume the catalog directly. Player creation records the selected UI locale, and preflight/notification presentation resolves from stable machine codes/IDs rather than translating protocol/schema/diagnostic identifiers.
- System locale detection is presentation-only and resolves to the three declared locales; machine identifiers, packet IDs, CLI switches, and backend names remain canonical English.
- `UiLocalizationTests` plus both Win32 UI targets build/pass under the focused Windows x64 MinGW check. Installer UI itself remains a later Phase 8 surface; the shared strings needed by it are already catalogued.

---

## P7-A11Y-01 — DPI, accessibility, input modality, and localized-layout readiness

**State:** CODE_COMPLETE

**Goal**

Ensure the compact game-first UI is usable across real Seat display configurations and input methods.

**Depends on**

- P7-I18N-01
- P7-LAUNCH-01

**Verify**

- mixed DPI/scaling;
- keyboard-only and controller navigation where supported;
- readable focus/state indicators;
- localized text expansion;
- screen-reader/accessibility metadata where practical for chosen UI framework;
- no critical action off-screen after display changes;
- safe confirmation semantics for protected experiments and recovery.

**Done when**

The declared accessibility/DPI/input/localization matrix passes on both Management UI and Seat Launcher.

**Implementation evidence — 2026-08-29**

- `assessLayout` is a pure DPI/input/localization readiness contract for Management Games, expanded Seat Launcher, and compact Playing surfaces. It computes minimum scaled bounds, deterministic focus order, and critical action visibility.
- Automated matrix coverage spans `en-US`/`ko-KR`/`zh-CN` at 96/120/144/192/288 DPI, keyboard/pointer/controller modality declarations, localized action expansion, and explicit Protected/Recovery actions. Too-small surfaces, no input modality, unsupported DPI, or a compact surface that would hide required safety actions fail closed.
- Focused `UiAccessibilityTests` pass. Real mixed-DPI display movement, controller navigation plumbing, screen-reader metadata, and physical input-device behavior remain manual/physical evidence before `VALIDATED`.

---

## P7-EXT-01 — Seat shell extension points, not public SDK yet

**State:** DEFERRED

**Goal**

Historical shell extension boundary.

**Depends on**

- P7-SHELL-01

**v1 decision**

A public/general Seat shell extension system is not needed for the minimal v1 launcher. Community value is prioritized in compatibility/setup data first.

**Done when**

Deferred. Any future extension boundary requires a concrete post-v1 use case and security/trust review.

---

## P7-REC-01 — Seat Launcher crash/restart and Explorer coexistence

**State:** CODE_COMPLETE

**Goal**

Prove the minimal Seat UI is disposable and coexists with ordinary Windows/Explorer without pretending to replace the shell.

**Depends on**

- P7-SHELL-01
- P7-LAUNCH-01
- P4-REC-01

**Matrix**

- kill/restart idle Seat UI;
- kill Seat UI during launch progress;
- UI reconnect/resnapshot while game remains Playing;
- one Seat UI crash while other Seat remains unchanged;
- host reconnect/failure;
- display reconnect;
- final Return to Windows leaves ordinary Explorer/desktop usable.

**Done when**

Seat UI crashes are recoverable without restarting games or Explorer and final global rollback leaves normal Windows behavior intact.

**Implementation evidence — 2026-08-29**

- The Seat UI is a disposable capability-restricted host client: authoritative game state remains in `hydra_host.exe`; closing/crashing a Seat UI does not own or terminate the game lifecycle.
- `SeatUiProcessTests` now forcibly terminate one host-connected Seat UI, verify authoritative two-Seat game/whole-machine state remains unchanged, restart that Seat UI and resnapshot successfully, and separately terminate a controlled Seat UI while it presents `Starting` state.
- Existing model/transport paths reset the authority epoch on disconnect and require a complete fresh snapshot before restoring success state. The two Seat UI processes remain independent.
- Controlled process evidence is `CODE_COMPLETE` only. Real host loss/restart while a game is Playing, physical display reconnect, and final Explorer/desktop coexistence/Return-to-Windows remain manual acceptance before validation.

---

## P7-CLOSE-01 — Phase 7 closure

**State:** BLOCKED

**Goal**

Verify the game-first v1 UI scope and ensure deferred full-desktop ambitions have not leaked into the required release path.

**Depends on**

- P7-REC-01
- P7-I18N-01
- P7-A11Y-01
- P7-NOTIFY-01

**Verify**

- main Game -> Seat -> Player -> Play flow;
- optional first-run setup with `Set later`;
- independent idle Seat Launcher and game change;
- no general arbitrary app/taskbar/wallpaper/clipboard dependency;
- protected warning flow;
- errors/recovery;
- localization/DPI/accessibility;
- UI crash/restart;
- real two-Seat demonstration.

**Done when**

A non-developer can operate the complete v1 gaming flow through simple UI, while all broader desktop-shell features remain explicitly deferred.
