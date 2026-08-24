# HydraSeat 🎮

An experimental Windows local gaming multiseat framework. The repository license is not yet formally declared; see the clean-room policy before reusing code.

[![License: not yet declared](https://img.shields.io/badge/license-not%20yet%20declared-lightgrey.svg)](docs/CLEAN_ROOM_POLICY.md)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](https://isocpp.org/)
[![Qt 6](https://img.shields.io/badge/Qt-6.8-41CD52?logo=qt)](https://www.qt.io/)

---

## 🎯 Project Goal

HydraSeat aims to make **one physical Windows PC feel like several independent local gaming PCs** without virtual machines, Remote Desktop, or game streaming.

The core abstraction is a **Seat**. A Seat is not limited to one monitor. It is a logical local-PC environment composed from an arbitrary group of physical devices:

```text
Physical Windows PC
├─ LG Monitor
├─ Samsung Monitor
├─ BenQ Monitor
├─ Keyboard A / Mouse A
├─ Keyboard B / Mouse B
├─ Controller A / Controller B
├─ Headset A
└─ Speakers B

HydraSeat
├─ Seat 1 — "Player 1 PC"
│  ├─ LG Monitor       (Primary)
│  ├─ Samsung Monitor  (Secondary)
│  ├─ Keyboard A
│  ├─ Mouse A
│  ├─ Controller A
│  ├─ Headset A
│  └─ Games / apps owned by Seat 1
│
└─ Seat 2 — "Player 2 PC"
   ├─ BenQ Monitor     (Primary)
   ├─ Keyboard B
   ├─ Mouse B
   ├─ Controller B
   ├─ Speakers B
   └─ Games / apps owned by Seat 2
```

The intended user experience is not "three monitors attached to one PC". It is **"a dual-monitor PC and a single-monitor PC sharing the same hardware underneath."**

Windows normally exposes global concepts such as foreground focus, cursor state, keyboard state, and a merged desktop. HydraSeat's long-term job is to virtualize or mediate the parts that matter to local gaming so each Seat behaves as independently as practical.

---

## 🪑 Seat Model

A future Seat is expected to own a collection of resources rather than a single display/input tuple:

```text
Seat
├─ Displays[]
│  └─ PrimaryDisplay
├─ Keyboards[]
├─ Mice[]
├─ Controllers[]
├─ AudioOutputs[]
├─ AudioInputs[]
├─ Seat-local cursor domain
├─ Seat-local display coordinate space
├─ Process group
├─ Window placement policy
├─ Input isolation policy
└─ Desktop / launcher profile
```

This allows configurations such as:

```text
Seat 1 = LG + Samsung + Keyboard A + Mouse A + DualSense + Headset
Seat 2 = BenQ + Keyboard B + Mouse B + Xbox Controller + Speakers
Seat 3 = Living-room TV + Controller C
```

A monitor group therefore belongs to a Seat as a unit. Applications owned by that Seat should remain inside that Seat's display topology unless the user explicitly moves or reassigns them.

---

## 🖥️ Seat-Local Desktop Experience

HydraSeat is intended to provide a lightweight **Seat Shell** rather than trying to create full virtual machines or independent Windows installations.

Each Seat can eventually have its own:

- desktop/launcher surface;
- taskbar-like application switcher;
- wallpaper and profile;
- visible application/process group;
- primary and secondary monitors;
- mouse cursor restricted to that Seat's monitors;
- audio output/input devices;
- game launcher/profile settings.

For example, Windows may physically arrange three monitors in one global coordinate space:

```text
LG               Samsung                 BenQ
0..2559          2560..5119              5120..7039
```

HydraSeat can expose a logical Seat-local view instead:

```text
Seat 1
LG       -> local (0, 0), primary
Samsung  -> local (2560, 0)

Seat 2
BenQ     -> local (0, 0), primary
```

This is important for games that assume their primary monitor begins at `(0, 0)` or that fullscreen always means the system-wide primary display.

---

## ⌨️ Input Isolation Goal

Hardware detection alone is not enough. Raw Input can tell HydraSeat **which physical device generated an event**, but Windows and games may still observe global keyboard, mouse, cursor, or foreground state.

The target behavior is:

```text
Keyboard A + Mouse A -> Seat 1 applications only
Keyboard B + Mouse B -> Seat 2 applications only

Seat 1 input must not leak into Seat 2 games.
Seat 2 input must not steal control from Seat 1 games.
```

Phase 3 therefore treats input isolation as a compatibility problem, not merely an event-forwarding problem. Depending on the target game, HydraSeat may need a combination of:

- Win32 Raw Input device routing;
- HID / SetupAPI physical-device identity;
- per-process input compatibility hooks;
- virtualized keyboard/mouse/cursor state;
- controlled foreground/focus behavior;
- optional device visibility/isolation backends such as HidHide where appropriate.

Public implementations such as ProtoInput, Universal Split Screen, Nucleus Co-op, HidHide, and official Microsoft Windows API documentation are useful reference material. HydraSeat should reuse code only when licenses are compatible and otherwise perform independent clean-room implementations based on documented APIs and observable behavior.

---

## 🎮 Process and Game Ownership

Applications launched through HydraSeat should belong to a Seat, not merely to a monitor.

```text
Seat 1 Process Group
├─ minecraft.exe
├─ discord.exe
└─ chrome.exe

Seat 2 Process Group
├─ fconline.exe
├─ launcher.exe
└─ browser.exe
```

A Seat-aware window manager can then keep those windows on the correct monitor group and restore their positions when applications create new windows, switch fullscreen modes, or restart.

Windows Job Objects and related process-management APIs are candidates for grouping, lifecycle tracking, child-process ownership, crash cleanup, and optional resource policies.

---

## 🔊 Audio Isolation

A convincing local-PC experience also requires audio routing:

```text
Seat 1 games/apps -> Headset A
Seat 1 voice chat  -> Microphone A

Seat 2 games/apps -> Speakers B
Seat 2 voice chat  -> Microphone B
```

Per-application audio endpoint routing is therefore part of the long-term Seat model, even though it is not required for the earliest input-routing prototype.

---

## 🧭 Design Principles

HydraSeat development should follow these principles:

1. **No VM requirement** — games execute directly on the host Windows installation.
2. **Seat, not monitor, is the unit of ownership** — one Seat may own one or many displays.
3. **Physical-device identity must remain deterministic** — identical USB/HID products must still be distinguishable.
4. **Zero cross-seat input bleed is the target** — merely observing Raw Input is not sufficient.
5. **Games should believe they are locally active** — compatibility work may be required for foreground, cursor, keyboard-state and Raw Input APIs.
6. **Do not fake implementation status** — features that are still research/prototype work stay documented as such.
7. **Prefer official Windows APIs and compatible open source** — use clean-room reimplementation when source licenses or proprietary software prevent reuse.
8. **Gaming first** — HydraSeat is not intended to become an enterprise VDI or generic office multiseat platform.

---

## 🚫 Non-Goals

HydraSeat is focused **strictly on local PC gaming**. It will **NOT** be:

- ❌ School computer lab software
- ❌ Office multiseat enterprise software
- ❌ Remote desktop software
- ❌ Cloud gaming service
- ❌ Enterprise VM manager
- ❌ A hypervisor or replacement Windows kernel/session manager

---

## 🏗️ Architecture

The planned architecture is layered so hardware detection, Seat composition, compatibility work, and game launching can evolve independently.

```text
                       HydraSeat Host
                             │
          ┌──────────────────┼──────────────────┐
          │                  │                  │
   HardwareDetector      SeatManager      Compatibility Layer
          │                  │                  │
    ┌─────┼─────┐            │          Raw Input / Focus /
    │     │     │            │          Cursor / Game hooks
 Input Display Audio         │
                             │
                   ┌─────────┴─────────┐
                   │                   │
                 Seat 1              Seat 2
                   │                   │
          Displays / Inputs     Displays / Inputs
          Audio / Shell         Audio / Shell
                   │                   │
             Process Group        Process Group
                   │                   │
             Games / Apps          Games / Apps
```

Current/planned technology areas include:

- **GUI / Seat Shell**: Qt 6 / Win32
- **Input Detection**: Win32 Raw Input API, Windows HID API, SetupAPI / ConfigMgr
- **Input Compatibility**: Raw Input routing, process hooks where required, optional HID visibility backends
- **Controllers**: XInput plus HID/DirectInput-compatible discovery
- **Display Detection/Routing**: `EnumDisplayMonitors`, `EnumDisplayDevices`, DXGI, Windows Display Configuration API (`QueryDisplayConfig`)
- **Virtual Displays**: Windows IddCx / IDD and compatible adapters in later phases
- **Process Management**: Windows process APIs and Job Objects
- **Audio Routing**: Windows Core Audio per-application endpoint control
- **Game Launcher**: Steam, Epic, EA, GOG, and generic executable profiles

---

## 📍 Current Development Status

The roadmap is intentionally incremental.

- **Phase 0 — Research & Foundation:** complete, including related-system and clean-room research.
- **Phase 1 — Hardware Detection:** complete and validated with Windows/MSVC CI.
- **Phase 2 — Seat Composition / Assignment UI:** complete in the current Win32 prototype, including multi-display Seats, primary-display selection, exclusive device ownership, and validated JSON profiles.
- **Phase 3 — Input Compatibility / Isolation:** current. The capability planner, backend descriptors, profile templates, diagnostics CLI, and safety policies are implemented. Actual process injection, device hiding, and verified zero-bleed enforcement are **not** implemented yet.
- **Later phases:** display routing, virtual displays, two-game MVP verification, game profiles, Seat shell, and extension adapters.

The most important technical milestone is not simply launching two windows. It is proving that two people can concurrently use different games/apps on different Seat display groups **without either user's keyboard/mouse input or foreground behavior interfering with the other Seat.**

---

## 🧪 Phase 3 Planning Tool

`hydra_plan` analyzes a compatibility-profile template against an assumed backend environment. It is **diagnostic only**: it does not inject a process, install a driver, hide a device, or activate input suppression.

```text
hydra_plan --list
hydra_plan observation-harness
hydra_plan raw-input-game
hydra_plan polled-keyboard-mouse-game --protoinput --hidhide --allow-injection --admin --recovery-ready
```

The output identifies:

- selected backend descriptors and the exact capabilities assigned to each;
- unavailable or policy-rejected backends;
- covered and missing requirements;
- injection, driver, administrator, recovery, and anti-cheat constraints;
- an honest result: `Supported`, `SupportedWithWarnings`, `ObservationOnly`, or `Unsupported`.

Even when ProtoInput and HidHide are marked available, the built-in planner still reports zero-bleed profiles as unsupported until a backend has **actually demonstrated verified physical input suppression**. HidHide is modeled only as physical-device cloaking at this stage.

Research and implementation specifications:

- [Related systems and source/license matrix](docs/RELATED_SYSTEMS_RESEARCH.md)
- [Phase 3 input-isolation architecture](docs/PHASE3_INPUT_ISOLATION_DESIGN.md)
- [Clean-room and third-party source policy](docs/CLEAN_ROOM_POLICY.md)


---

## 🛠️ Build Prerequisites

- **OS**: Windows 10 / Windows 11 (64-bit)
- **Compiler**: Visual Studio 2022 (MSVC with C++20 support)
- **Build System**: CMake 3.20+
- **Framework**: Qt 6.x (Widgets / Core)
- **Windows SDK**: Windows 10/11 SDK (Win32 Raw Input, DXGI, SetupAPI)

---

## 🚀 Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md) for detailed Phase 0 to Phase 7 deliverables.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for internal component designs.

See [docs/PHASE0_RESEARCH.md](docs/PHASE0_RESEARCH.md) for the early technology evaluation.

See [docs/RELATED_SYSTEMS_RESEARCH.md](docs/RELATED_SYSTEMS_RESEARCH.md) and [docs/PHASE3_INPUT_ISOLATION_DESIGN.md](docs/PHASE3_INPUT_ISOLATION_DESIGN.md) for the current compatibility research and Phase 3 design.

See [docs/CLEAN_ROOM_POLICY.md](docs/CLEAN_ROOM_POLICY.md) before using external source or binaries.
