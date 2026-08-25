# HydraSeat 🎮

**English** | [한국어](README.ko.md) | [简体中文](README.zh-CN.md)

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

### Management Seat and background operation

HydraSeat is planned as a background runtime plus an on-demand control console, not as one permanently visible configuration window.

By default, **Seat 1 is the Management Seat**. When the split-PC session is active, opening `HydraSeat.exe` places the management console on Seat 1's primary display (LG in the example above). Closing that window does **not** stop the split session; `hydra_host.exe` and `hydra_watchdog.exe` continue in the background. Reopening the controller returns it to the Management Seat display. Other Seat shells are read-mostly for whole-machine controls unless the profile explicitly grants more authority.

The normal control surface is intentionally small and obvious:

```text
HydraSeat — Management Seat
├─ Current state: Normal Windows / Starting / Split Active / Degraded / Recovery Required
├─ Seat 1: LG + Samsung | Keyboard A | Mouse A | Controller A | Headset
├─ Seat 2: BenQ         | Keyboard B | Mouse B | Controller B | Speakers
│
├─ [ Start split session ]
├─ [ Stop / Return to Windows ]
├─ [ Reconfigure monitors and input ]
├─ [ Identify / test devices ]
├─ [ Startup mode ]  Manual | Background Idle | Auto-Activate Validated Session
├─ [ Diagnostics ]
└─ [ Recovery / Reset ]
```

`Stop / Return to Windows` is not just a UI close button. It is a verified host rollback transaction: Seat-specific input/device/window/display/audio/controller/shell state is removed or restored, all monitors and ordinary keyboard/mouse behavior return to one normal Windows desktop, and only then does the runtime report `Stopped`/`Idle`.

`Reconfigure` uses the safe path by default:

```text
Active split session
  -> Stop / Return to Windows and verify rollback
  -> open configuration on the Management Seat display
  -> identify/test/reassign monitors, keyboards, mice, controllers, and audio
  -> validate + save a new plan
  -> Start Now, or remain in normal Windows mode
```

Three startup modes are planned:

- **Manual** — HydraSeat does nothing until the user opens it and presses Start.
- **Background Idle** — host/watchdog start silently at logon, but the PC remains normal until the Management Seat opens the controller and presses Start.
- **Auto-Activate Validated Session** — after logon, HydraSeat may automatically restore one explicitly selected, previously validated Seat layout only if crash-journal, safe-mode, hardware/topology, capability, privilege, watchdog, and rollback preflight all pass. Otherwise it stays safely idle instead of partially splitting the PC.

This gives both intended usage styles: an always-available appliance-like split-PC setup after boot, or a program the user launches only when multiseat gaming is needed.

### Language support

The canonical UI language is English (`en-US`). The planned initial release UI/UX locales are English, Korean (`ko-KR`), and Simplified Chinese (`zh-CN`). User-visible strings will use stable localization IDs with English fallback, while source-code comments, protocols, schema keys, CLI switches, diagnostic codes, and other machine/developer identifiers remain English. See [Localization Policy](docs/LOCALIZATION.md).

The README is available in [English](README.md), [Korean](README.ko.md), and [Simplified Chinese](README.zh-CN.md). README translation availability does not mean the runtime UI localization packet is already implemented; that work is tracked as `P7-I18N-01`.

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
- **Phase 3 — Input Compatibility / Isolation:** current. The capability planner, Gate A/B input observation, and the Gate C controlled-process protocol/adapter foundation are implemented. Physical acceptance, actual Windows API interposition, device cloaking, and verified zero-bleed game enforcement are **not** complete yet.
- **Later phases:** background runtime and display/window ownership, two-game MVP, launcher/profile manager, Seat shell, watchdog/installer/update productization, compatibility SDK, and release hardening through Phase 10.

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

### Gate A/B Input Lab

`hydra_input_lab` is the first executable Phase 3 feasibility harness. It opens two HydraSeat-owned Seat windows, observes Raw Input from stable physical device identities, tracks key/button and hot-plug state, and routes exclusive Seat-owned input to exactly one diagnostic target window.

```powershell
.\build\Release\hydra_input_lab.exe --no-profile
.\build\Release\hydra_input_lab.exe --profile workspace_config.json
.\build\Release\hydra_input_lab.exe --profile workspace_config.json --trace phase3-input-lab.jsonl
.\build\Release\hydra_input_lab.exe --self-test
```

The lab is deliberately limited:

- it does not inject a game process;
- it does not install or control HidHide;
- it does not suppress normal Windows input;
- it does not virtualize polling, cursor, capture, or foreground state;
- shared input devices are treated as ambiguous and fail closed;
- every JSONL route record states that native OS input remains unsuppressed.

Implementation is complete, but the physical acceptance checklist still needs to be run with the user's actual two-keyboard/two-mouse setup. See [Phase 3 Gate A/B testing](docs/PHASE3_GATE_A_B_TESTING.md).

### Gate C Controlled Process Lab

Gate C adds a versioned host/target protocol and a separate process-local adapter DLL. It launches only HydraSeat-owned controlled targets; it does not inject into a game.

```powershell
.\build\Release\hydra_gate_c_host.exe `
  --self-test `
  --target .\build\Release\hydra_gate_c_target.exe

.\build\Release\hydra_gate_c_host.exe `
  --profile workspace_config.json `
  --trace hydra_gate_c_host.jsonl
```

Or use the **Gate C Process Lab** button in the main UI.

Implemented Gate C state:

- versioned little-endian protocol with bounded frames and monotonic sequences;
- local named pipes with timeout handling, remote-client rejection and full session-token validation;
- Seat/PID/architecture handshake;
- cryptographic Windows session-token generation through `BCryptGenRandom`;
- process-local `hydra_gate_c_adapter.dll` with a versioned C ABI;
- `GetAsyncKeyState`-style one-shot edges;
- `GetKeyState` / `GetKeyboardState`-style high bits;
- mouse button and wheel state;
- virtual cursor, clip, foreground and capture state;
- bounded per-process virtual Raw Input registrations, immutable synthetic
  keyboard/mouse packets, generation-checked opaque `HRAWINPUT` tokens, and
  an eight-byte-aligned queue;
- two separate controlled target processes with different Seat states;
- bounded per-target writer queues so a slow target cannot block Raw Input indefinitely;
- Windows CI verification that A/B keys, mouse state, cursor state and virtual foreground do not cross between the two controlled processes.

Important boundary:

- controlled targets call the adapter API directly;
- HydraSeat-owned probes may opt into a startup-loaded, process-local IAT shim
  for polling plus cursor/clip/logical-focus/capture APIs; both surfaces are
  Windows-validated in controlled x64/x86 CI;
- the same shim has an explicit, separate Raw Input capability for only
  `RegisterRawInputDevices`, `GetRegisteredRawInputDevices`,
  `GetRawInputData`, and `GetRawInputBuffer`; it is Windows-validated on native
  x64/x86 and the x64-host-to-x64/x86 controlled matrix;
- adapter ABI v4 now has a bounded four-slot normalized XInput-style state,
  logical-slot/source mapping, capabilities, battery, disconnect/reconnect
  generations, and source-only vibration routing; this controlled synthetic
  slice is `CODE_COMPLETE`, with native Windows process acceptance pending;
- the standalone Raw Input behavior probe and bounded trace/parser are
  Windows-validated on x64/x86 run `32800513365`;
- no detour, remote injection, driver control, physical suppression, or
  third-party/commercial-process patch is installed;
- Gate C is not complete until the Raw Input, recovery, physical, and later
  compatibility gates pass.

See [Phase 3 Gate C controlled-process testing](docs/PHASE3_GATE_C_TESTING.md).

Research and implementation specifications:

- [Related systems and source/license matrix](docs/RELATED_SYSTEMS_RESEARCH.md)
- [Phase 3 input-isolation architecture](docs/PHASE3_INPUT_ISOLATION_DESIGN.md)
- [Clean-room and third-party source policy](docs/CLEAN_ROOM_POLICY.md)
- [Gate A/B physical input-lab procedure](docs/PHASE3_GATE_A_B_TESTING.md)
- [Gate C controlled-process protocol and acceptance](docs/PHASE3_GATE_C_TESTING.md)


---

## 🧱 Codex Implementation Roadmap

Future implementation is split into bounded work packets so Codex can write code without re-inventing the architecture or falsely completing physical/game gates.

Current default packet:

```text
P3-CTRL-01 — XInput controlled state and slot remapping
```

Start every coding task by reading:

1. [Agent rules](.agents/AGENTS.md)
2. [Non-negotiable decisions](docs/implementation/DECISIONS.md)
3. [Master implementation roadmap](docs/implementation/README.md)
4. [Current packet status](docs/implementation/STATUS.md)
5. The active [Phase 3–10 packet specification](docs/implementation/README.md#4-phase-model)
6. [Codex implementation playbook](docs/implementation/CODEX_PLAYBOOK.md)

A packet defines its prerequisites, exact files/types, implementation order, invariants, automated tests, manual acceptance, rollback behavior, non-goals, and objective completion gate. Manual hardware/game/install/reboot checks remain pending until a human records real evidence.

Inspect or generate the current Codex task, then validate before and after every packet:

```text
python tools/show_implementation_packet.py --current
python tools/show_implementation_packet.py --current --prompt
python tools/show_implementation_packet.py --ready
python tools/validate_implementation_roadmap.py
git diff --check
```

The `--prompt` command is the preferred way to hand a task to Codex because it validates the roadmap and emits the exact packet, prerequisites, scope restrictions, tests, status update, and manual-gate rules.

The original product requirements and the exact packets/evidence that prove them are mapped in [Product Requirement Traceability](docs/implementation/TRACEABILITY.md).

---

## 🛠️ Build Prerequisites

- **OS**: Windows 10 / Windows 11 (64-bit)
- **Compiler**: Visual Studio 2022 (MSVC with C++20 support)
- **Build System**: CMake 3.20+
- **Framework**: Qt 6.x (Widgets / Core)
- **Windows SDK**: Windows 10/11 SDK (Win32 Raw Input, DXGI, SetupAPI)

---

## 🚀 Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md) for the Phase 0–10 summary.

See [docs/implementation/README.md](docs/implementation/README.md) for the packet-level master plan, [docs/implementation/STATUS.md](docs/implementation/STATUS.md) for the current task, and [docs/implementation/CODEX_PLAYBOOK.md](docs/implementation/CODEX_PLAYBOOK.md) for the Codex workflow.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for internal component designs.

See [docs/PHASE0_RESEARCH.md](docs/PHASE0_RESEARCH.md) for the early technology evaluation.

See [docs/RELATED_SYSTEMS_RESEARCH.md](docs/RELATED_SYSTEMS_RESEARCH.md) and [docs/PHASE3_INPUT_ISOLATION_DESIGN.md](docs/PHASE3_INPUT_ISOLATION_DESIGN.md) for the current compatibility research and Phase 3 design.

See [docs/CLEAN_ROOM_POLICY.md](docs/CLEAN_ROOM_POLICY.md) before using external source or binaries.

See [docs/PHASE3_GATE_A_B_TESTING.md](docs/PHASE3_GATE_A_B_TESTING.md) before claiming Gate A/B physical acceptance.

See [docs/PHASE3_GATE_C_TESTING.md](docs/PHASE3_GATE_C_TESTING.md) before claiming Gate C API or physical acceptance.
