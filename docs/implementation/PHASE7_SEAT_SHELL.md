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

**State:** BLOCKED

**Goal**

Create `hydra_seat_ui.exe` as a small per-Seat client for idle/start/warning/error/end-playing states, not a Windows shell replacement.

**Depends on**

- P6-CLOSE-01
- P4-SEAT-01

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

---

## P7-LAUNCH-01 — Idle Seat game/Player selector

**State:** BLOCKED

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

**State:** BLOCKED

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

---

## P7-HOTKEY-01 — Seat-scoped launcher and recovery hotkeys

**State:** BLOCKED

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

---

## P7-NOTIFY-01 — Seat-scoped runtime notifications

**State:** BLOCKED

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

**State:** BLOCKED

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

---

## P7-A11Y-01 — DPI, accessibility, input modality, and localized-layout readiness

**State:** BLOCKED

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

**State:** BLOCKED

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