# HydraSeat Development Roadmap

This is the phase-level product roadmap. Packet-level implementation truth lives in [`docs/implementation/`](implementation/README.md), and current evidence/state lives in [`docs/implementation/STATUS.md`](implementation/STATUS.md).

The canonical v1 product definition is [`PRODUCT_V1.md`](PRODUCT_V1.md).

## Product north star

HydraSeat is built for households where one Windows gaming PC has useful CPU/GPU/memory/I/O headroom, but buying and maintaining a second complete desktop only so two people can play at the same time is expensive or inconvenient.

v1 therefore has a narrow target:

> **Two people, one sufficiently capable Windows gaming PC, two local gaming Seats.**

HydraSeat v1 is not a general Windows multiseat desktop platform. It is game-first, offline-first, limited to two active Seats, and designed so one person can stop/change games without interrupting the other.

Core product concepts stay separate:

```text
Seat              = physical gaming station
Player            = lightweight person profile
Game              = installed/discovered title
Two-player setup  = optional same-game two-instance recipe
Runtime Session   = temporary Seat + Player + Game binding
```

HydraSeat does not bypass anti-cheat, DRM, protected processes, account rules, launcher policy, or deliberate single-instance restrictions.

## Phase completion rule

A numbered phase is not complete merely because implementation packets compile. The phase closes only after its declared automated, Windows, physical/manual, recovery, and cross-packet acceptance gates pass. Synthetic or HydraSeat-owned controlled evidence never substitutes for a required physical/game gate.

Selected reliability/recovery prerequisites may be implemented early when a risky earlier packet depends on them.

---

## Phase 0 — Research and Foundation — Complete

Goals achieved:

- C++20 / CMake foundation and Windows CI direction;
- Windows input/display architecture research;
- related-system research including ASTER, ProtoInput, Nucleus Co-op, Universal Split Screen, HidHide, devreorder, Duo, and documented Microsoft APIs;
- clean-room / third-party source policy;
- agent workflow and evidence rules.

Product consequence:

- HydraSeat does not assume the idea is unprecedented;
- proprietary systems remain behavior/documentation references rather than implementation sources;
- anti-cheat/DRM bypass is outside the product boundary.

---

## Phase 1 — Hardware Detection — Complete

Goals achieved:

- physical display discovery;
- stable keyboard/mouse/HID identity through Raw Input + SetupAPI/ConfigMgr;
- controller discovery foundations;
- deterministic identity tests;
- Windows/MSVC CI evidence.

The detector observes hardware only. Detection is not isolation.

---

## Phase 2 — Two-Seat Hardware Composition — Complete foundation

Current foundation includes:

- multi-display Seat composition;
- explicit primary display;
- keyboard/mouse/controller/render/capture identities;
- exclusive resource ownership by default;
- transactional UTF-8 JSON persistence;
- current Win32 drag/drop configuration prototype.

v1 product adjustment:

- product activation is capped at two Seats;
- Seat means physical station only, not Player/Game ownership;
- hardware assignments may be incomplete/unset until a selected game requires them;
- the end-user flow becomes an optional first-run wizard plus later Seat settings rather than a developer configuration screen.

A later schema migration will separate physical Seat persistence from Player/Game/Two-player setup data.

---

## Phase 3 — Input Compatibility and Isolation — Current

### Completed/validated controlled foundations

- Raw Input observation and stable physical-device identity;
- composite-HID-aware hot-plug tracking;
- fail-closed exclusive Seat routing in HydraSeat-owned labs;
- capability planner and backend descriptors;
- versioned Gate C protocol/adapter boundary;
- controlled polling/cursor/focus/capture virtualization;
- controlled Raw Input API virtualization;
- x86/x64 controlled matrix;
- controlled XInput state/remapping semantics;
- controlled DirectInput visibility/order policy;
- input latency/bleed metrics;
- watchdog/crash journal/emergency reset prerequisites;
- one pinned open-source external application acceptance path.

### Required remaining gates

- real two-keyboard/two-pointing-device Gate A/B/C physical acceptance;
- guarded device-cloaking/suppression experiment only after recovery prerequisites and physical evidence;
- first real non-protected game compatibility entry;
- two different real-game zero-bleed evidence;
- Phase 3 close verification.

### Product rule

Phase 3 should stop expanding merely to chase universal game compatibility. Once the declared physical and real-game gates prove a viable safe path, work advances toward the complete two-player user journey.

Known protected titles may later be offered only as explicit advanced experiments with a strong warning; a successful launch is not anti-cheat safety evidence.

---

## Phase 4 — Production Runtime, Independent Seat Lifecycle, and Display/Window Ownership — Early foundation in progress

Phase 4 moves validated primitives into production runtime authority.

Required outcomes:

- `hydra_host.exe` owns runtime state independently from the visible UI;
- versioned UI/CLI/watchdog IPC;
- temporary Seat-owned process trees and windows;
- stable physical display topology and Seat-local display groups;
- deterministic window placement/restore;
- Management UI can close/reopen without stopping games;
- safe `Return to Windows` and reconfiguration flow;
- **independent Seat game lifecycle**:
  - Seat 1 may remain Playing while Seat 2 becomes Idle;
  - Seat 2 may launch another game without restarting Seat 1;
  - one Seat game exit is not a whole-machine Stop;
  - while one Seat remains active, the other stays in HydraSeat idle/wait state;
  - both Seats ended or explicit whole-machine return triggers verified rollback;
- display hot-plug and runtime recovery preserve ownership boundaries.

Current branch truth:

- background runtime host, host IPC, Seat process/window ownership, and early display/control policy foundations exist locally/on the development branch;
- they are not allowed to imply Phase 4 completion while Phase 3 physical close gates remain pending;
- the current global-session command model still needs explicit independent per-Seat game-lifecycle work.

Optional virtual displays remain secondary. Physical local monitors are the required v1 path.

---

## Phase 5 — Real Two-Seat Gaming MVP — Planned

This phase proves HydraSeat as a gaming product rather than a collection of primitives.

Required outcomes:

- exactly two v1 Seats;
- requirement-aware hardware preflight;
- production controller routing for selected test games;
- per-process audio routing where required;
- transactional input/process/window/display/controller/audio activation;
- two different real non-protected games running concurrently;
- objective receiver-aware input bleed/latency evidence;
- one player exits while the other game continues;
- idle Seat can start another game;
- game restart/device reconnect behavior;
- both players finish -> verified ordinary Windows restore;
- truthful error/status/recovery UI.

The MVP does not require dozens of games. It requires a small number of real scenarios that prove the full lifecycle.

A lawful same-title/two-instance demonstration may be recorded here when available, but the reusable user-facing setup system belongs to Phase 6.

---

## Phase 6 — Game Library, Player Profiles, and Two-Player Setup — Planned

Phase 6 turns the MVP into a repeatable game-first product.

### Game discovery

- provider-neutral installed-game catalog;
- local read-only discovery first;
- Steam path/provider integration first where practical;
- Epic/EA/GOG as separate bounded provider work;
- manual `Add game / EXE` fallback;
- local executable/shortcut/provider icons where possible;
- provider credentials remain provider-owned.

### Player profiles

- lightweight display name/avatar;
- recent games and recent Seat preference;
- per-game instance/data preferences;
- provider account references only where supported;
- Player remains independent from Seat;
- no HydraSeat password vault.

### Two-player setup

When both Seats choose the same game:

- find a known local/community setup;
- validate it against local provider/version/environment;
- otherwise attempt bounded automatic setup creation;
- otherwise offer a guided manual editor;
- represent instance directories, args, provider choices, start order, window/input/audio/controller requirements, limitations, and evidence;
- never bypass game/provider restrictions;
- validate/import/export through typed schemas rather than arbitrary JSON/script execution.

### UX

Normal UI uses `Game`, `Player`, `Seat`, and `Two-player setup` terminology. Internal Target/Compatibility/Session schemas may remain separate engineering concepts but should not be imposed on normal users.

---

## Phase 7 — Minimal Seat Launcher and Game-First UX — Planned

The old vision of a full per-Seat desktop shell is intentionally reduced for v1.

Required v1 surface:

- lightweight game-library main UI;
- click/tap first; drag-and-drop as optional shortcut;
- optional first-run two-Seat wizard with `Set later`;
- minimal `hydra_seat_ui.exe` on an idle Seat;
- Player selection/change;
- recent/available game selection;
- launch/preflight progress;
- protected-game warning/advanced experiment acknowledgement;
- errors and bounded recovery actions;
- `End Playing`;
- UI disappears or remains non-intrusive while the game runs;
- English, Korean, and Simplified Chinese localization readiness;
- DPI/accessibility testing.

Deferred beyond v1:

- general Seat taskbar;
- wallpaper/desktop zones;
- arbitrary general app launcher;
- clipboard virtualization;
- full Windows shell replacement.

---

## Phase 8 — Reliability, Installer, Least Privilege, and Updates — Planned with some prerequisites already validated

Existing cross-phase foundations include watchdog, crash journal, and emergency reset work.

Remaining product outcomes:

- least-privilege runtime: normal UI/runtime without elevation when Windows permits;
- narrow typed privileged broker only for operations genuinely requiring UAC;
- real Windows installer/repair/uninstaller;
- optional driver/service setup only when required;
- first-run wizard integration;
- clean uninstall with ordinary Windows postconditions;
- staged signed/hash-verified executable updates;
- **program/runtime/driver update requires user approval**;
- compatibility/setup catalog refresh is a separate data update path;
- compatibility catalog can be disabled and local cache continues working;
- core remains fully usable offline with already available local games/setups;
- diagnostics/support bundle redaction;
- reboot/logon/fault/soak acceptance.

A developer-only CMake/MSVC install path cannot satisfy Phase 8.

---

## Phase 9 — Community Compatibility and Setup Ecosystem — Planned

The first ecosystem goal is not a large plugin SDK. It is scalable compatibility knowledge for a one-maintainer project.

Required outcomes:

- versioned local compatibility-result JSON schema;
- explicit opt-in community submission;
- visible redacted JSON preview before upload;
- no credentials/raw typed text/Player names/personal paths by default;
- versioned compatibility/setup catalog format;
- success/failure counts, sample size, percentages, and sub-results;
- aggregation keyed/segmented by materially relevant environment such as game version, HydraSeat version, provider, Windows, and compatibility path;
- protected-game results remain `Protected / Experimental` and never imply anti-cheat safety;
- no mandatory `HydraSeat Certified` support badge;
- initial static/versioned JSON artifact distribution so v1 does not require a custom always-on backend;
- signed/hash/trust policy for downloaded community setup data;
- contribution validation/provenance/redaction rules.

A broader binary extension SDK may remain a later sub-track only where real product needs justify its complexity.

---

## Phase 10 — v1 Release Hardening — Planned

v1 is complete when the user journey works on real hardware, not when a packet counter reaches 100%.

Required release gate:

- clean Windows install and uninstall;
- exactly two supported v1 Seats;
- real two-display/two-input physical acceptance;
- objective tested zero-bleed evidence;
- separate audio/controller routing for declared scenarios;
- two different real games concurrently;
- at least one lawful real same-title/two-instance demonstration;
- Game-first UX + Player profiles;
- automatic local game discovery + manual fallback;
- automatic + manual two-player setup paths;
- one Seat ending/changing games while the other continues;
- idle Seat Launcher;
- both Seats end -> verified ordinary Windows restore;
- watchdog/crash/emergency recovery;
- offline core operation;
- local-first compatibility JSON + optional community sharing;
- user-approved executable update path;
- latency/CPU/memory/resource budgets measured;
- security/privacy/dependency review;
- clean-machine onboarding and recovery docs;
- checksums/SBOM/provenance/reproducible release artifacts where practical;
- project license/contribution terms resolved before describing the release as open source.

There is no required count of officially certified games. HydraSeat should publish evidence and limitations rather than a misleading universal support promise.

---

## Cross-cutting rules

### Compatibility truth

- `Untested` means untested, not unsupported.
- Community success percentage is evidence, not a guarantee.
- Protected experimental success does not prove anti-cheat safety.
- Synthetic/controlled CI does not replace physical/game evidence.

### Privacy

Compatibility collection is local-first. Upload is explicit opt-in and previewable/redacted.

### Legal boundary

HydraSeat is intended to become an open-source project, but the current repository license/contribution terms are unresolved. Source reuse and public claims must continue to follow [`CLEAN_ROOM_POLICY.md`](CLEAN_ROOM_POLICY.md) until the release legal gate is complete.

### One-developer scope rule

When a proposed feature does not materially improve the two-Seat game-first user journey, its recovery/safety, or its compatibility evidence, defer it rather than expanding HydraSeat into a general multiseat desktop system.