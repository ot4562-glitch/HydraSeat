# Related Systems Research

## Purpose and scope

This document records the systems studied for HydraSeat's Phase 3 input-isolation work. It separates:

1. **Observed facts** from public documentation or source code.
2. **Reusable engineering lessons** that can guide HydraSeat.
3. **License boundaries** that determine whether code may be reused, wrapped as an optional dependency, or only studied as behavior.
4. **HydraSeat decisions** for the next implementation stages.

The reference repositories used for this study were cloned outside the HydraSeat source tree under `C:\HydraSeat\references`. They are research inputs, not vendored dependencies. Proprietary software was studied only through public documentation and ordinary observable behavior.

> Important: the HydraSeat repository does not contain a tracked `LICENSE` file. The README no longer presents the previous MIT badge as an established license. Until the project license is made explicit, no third-party source should be copied into HydraSeat. Architecture and clean-room implementation can continue, but direct code reuse must wait for a license decision and dependency audit.

## Studied revisions

The comparison below is tied to these shallow-clone revisions. A future refresh must record new commits and re-check licenses before changing implementation decisions.

| Reference | Studied commit | Commit date | Note |
| --- | --- | --- | --- |
| ProtoInput | `dd29bf8` | 2021-09-15 | Top-level MIT; bundled dependencies need separate audit |
| Nucleus Co-op | `3d22c0e` | 2026-07-08 | GPL-3.0 |
| Universal Split Screen | `d0618b8` | 2020-04-25 | MIT |
| HidHide | `2b950fd` | 2026-07-05 | MIT |
| devreorder | `bc7f96d` | 2024-12-28 | No top-level license found in the studied clone |
| Duo | `6bfea1d` | 2026-08-14 | No top-level license found in the studied clone |

## Executive decision

No existing product is a universal drop-in backend for HydraSeat. The practical design is a **profile-driven compatibility stack**:

- HydraSeat owns Seat composition, hardware identity, process ownership, diagnostics, recovery, and backend planning.
- A host Raw Input layer observes and identifies physical devices.
- A per-process compatibility backend virtualizes the APIs a particular game reads.
- An optional device-visibility backend suppresses the original physical device when required.
- Controller ordering/visibility uses a separate XInput or DirectInput strategy.
- Games with anti-cheat or protected processes default to unsupported unless a non-invasive, explicitly tested path exists.

The strongest public implementation reference for per-process keyboard/mouse compatibility is **ProtoInput**. The strongest orchestration/profile reference is **Nucleus Co-op**. **HidHide** is the strongest candidate for temporary physical-device cloaking through its documented session blacklist. ASTER supplies useful product-level Seat/workplace behavior, but none of its private implementation may be used.

## Comparison matrix

| System | Primary model | Source/license status | Strongest lesson for HydraSeat | Planned use |
| --- | --- | --- | --- | --- |
| ASTER | Multiple local workplaces on one Windows host | Proprietary; public docs only | Device grouping, multi-monitor workplaces, composite-device UX, recovery, independent cursor/audio/user concepts | Behavior and UX benchmark only |
| ProtoInput | Injected per-process API hooks and synthetic input state | MIT top-level; bundled dependency licenses require audit | Raw Input interception, polled-key state, cursor/focus virtualization, XInput mapping | Optional adapter or clean-room design reference after license resolution |
| Universal Split Screen | Earlier injected split-screen input hooks | MIT | Historical proof that multiple local keyboards/mice can be routed through game hooks | Secondary design reference; prefer ProtoInput architecture |
| Nucleus Co-op | Multi-instance launcher and per-game compatibility profiles | GPL-3.0 | Capability/profile matrix, process orchestration, window/focus lifecycle, game-specific exceptions | Architecture/behavior reference only unless HydraSeat adopts GPL |
| HidHide | Kernel-mode HID visibility firewall | MIT | Hide original physical devices while a feeder/orchestrator retains access; session-scoped cleanup | Optional external backend through documented IOCTL API |
| devreorder | Per-process DirectInput 8 wrapper | No license file found in studied clone | Per-game device ordering and visibility for legacy DirectInput | Behavior reference only; implement independently or require user-supplied external tool |
| Duo | Independent Windows desktops streamed to clients | No license file found in studied clone | Strong isolation through separate desktop/session architecture; operational recovery lessons | Product comparison only; architecture conflicts with HydraSeat's no-streaming local-display goal |
| Microsoft Windows APIs | Host platform primitives | Microsoft documentation and SDK terms | Raw Input, SetupAPI, focus/cursor constraints, Job Objects, Core Audio, display topology | Primary supported implementation foundation |

---

## ASTER

### Observed public behavior

ASTER presents a **workplace** as a group of devices rather than a single monitor. Public configuration documentation describes:

- assigning displays, keyboards, mice, game controllers, storage and audio devices to workplaces;
- assigning more than one display to a workplace, subject to graphics-adapter constraints;
- keeping related parts of a composite USB device together;
- assigning an entire USB hub so newly attached devices inherit a workplace;
- independent hardware/software cursor modes;
- workplace-specific Windows-user assignment;
- shared or workplace-specific audio;
- recovery controls and hotkeys intended to restore access after a bad device assignment.

### Reusable lesson

HydraSeat should treat **Seat composition and recovery** as first-class product features. The important product insight is not a specific hook: users need to see devices grouped into an understandable logical PC, and they need a safe way to undo a bad assignment.

### Boundary

ASTER is proprietary. HydraSeat must not decompile its binaries, extract private code, bypass protections, or reproduce implementation details obtained from non-public material. Only public behavior, public documentation, and ordinary black-box observations may inform clean-room requirements.

### HydraSeat decision

Use ASTER as a UX benchmark for:

- multi-monitor Seat composition;
- composite-device grouping;
- shared devices;
- cursor and audio ownership;
- startup/recovery UX;
- explicit user warnings before input suppression.

Do not treat ASTER as a source-code dependency or implementation reference.

Public documentation reference: `https://dokwiki.ibiksoft.com/en/v3/core/ugd/ugd_config`

---

## ProtoInput

### Observed architecture

ProtoInput injects code into the target process and hooks the Windows APIs that games use. The top-level repository is MIT licensed. Its README explicitly describes redirecting multiple keyboards, mice and controllers and exposes a C-style loader API.

Key source areas in the studied clone:

| Concern | Source area | Observed strategy |
| --- | --- | --- |
| Game Raw Input registration | `src/ProtoInput/ProtoInputHooks/RegisterRawInputHook.cpp` | Intercepts `RegisterRawInputDevices`, records the target game window/usages, unregisters the game's direct registration, and registers ProtoInput's own sink |
| Synthetic Raw Input data | `src/ProtoInput/ProtoInputHooks/GetRawInputDataHook.cpp` | Recognizes synthetic `HRAWINPUT` values and returns buffered `RAWINPUT` structures |
| Async keyboard polling | `GetAsyncKeyStateHook.cpp` | Returns a process-local fake key state and edge bit |
| Keyboard-state arrays | `GetKeyboardStateHook.cpp` | Fills the 256-byte keyboard-state array from process-local state |
| Key-state polling | `GetKeyStateHook.cpp` | Supplies local state for APIs that bypass window messages |
| Cursor query/update | `GetCursorPosHook.cpp`, `SetCursorPosHook.cpp` | Stores a fake cursor in target-window client coordinates |
| Cursor confinement | `ClipCursorHook.cpp` | Maintains a fake clip rectangle instead of changing the global cursor clip |
| Foreground/focus queries | `FocusHook.cpp` | Returns the selected game window from focus/foreground/capture APIs and neutralizes setters |
| Controller mapping | `XinputHook.cpp` | Maps the game's requested XInput slot to a selected physical controller; optionally bridges DirectInput/OpenXInput |
| Game message filtering | `MessageFilterHook.*` and Raw Input modules | Blocks or forwards selected messages per profile |
| Window and namespace compatibility | `SetWindowPosHook.*`, `WindowStyleHook.*`, `RenameHandlesHook.*` | Handles games that assume global window placement or singleton named objects |

### Why this matters

Raw Input alone identifies a device, but many games also call global state APIs such as `GetAsyncKeyState`, `GetKeyboardState`, `GetCursorPos`, `ClipCursor`, or foreground-window APIs. ProtoInput demonstrates that compatible games may require a **bundle of coordinated hooks**, not one input-forwarding function.

### Risks

- Process injection and API hooking are anti-cheat sensitive.
- A game may use APIs that the profile did not anticipate.
- The repository bundles Blackbone, EasyHook, AsmJit, ImGui and other dependencies, each requiring separate license and security review.
- The studied upstream commit is old compared with current Windows releases, so source compatibility and runtime behavior must be revalidated.
- Directly vendoring the code before HydraSeat's own license is clarified would be premature.

### HydraSeat decision

1. Model ProtoInput-like features as **capabilities**, not unconditional global behavior.
2. Build an optional `ProtoInputAdapter` boundary rather than coupling HydraSeat core to injected implementation details.
3. Initially support externally supplied, version-pinned binaries or a separately built component; do not silently download or inject.
4. Require an explicit game profile, architecture match, target-process consent, rollback, and anti-cheat denial policy.
5. Permit direct source reuse only after HydraSeat has a real license file, all transitive licenses are audited, attribution is added, and the reused source is isolated in a clearly identified component.

Repository reference: `https://github.com/Ilyaki/ProtoInput`

---

## Universal Split Screen

### Observed architecture

Universal Split Screen is ProtoInput's MIT-licensed predecessor. The repository contains separate x86/x64 injector and hook projects (`InjectorLoader`, `HooksCPP`, `StartupHook`) plus a host UI. Its README describes multiple keyboards, mice and controllers and requires architecture-specific injected DLLs.

### Reusable lesson

USS confirms the long-standing need for:

- architecture-specific injection artifacts;
- process startup versus runtime injection paths;
- game-specific hook selection;
- independent fake input state inside each target process.

### HydraSeat decision

Use USS only as a secondary historical reference. Prefer the more modular ProtoInput API and modernize independently. Any useful MIT source must still pass the same license/dependency/security review described above.

Repository reference: `https://github.com/UniversalSplitScreen/UniversalSplitScreen`

---

## Nucleus Co-op

### Observed architecture

Nucleus Co-op is GPL-3.0 and acts as a high-level orchestrator. It launches multiple game instances, manages windows and process lifecycle, and applies per-game compatibility settings. The following source areas are especially relevant:

| Source area | Lesson |
| --- | --- |
| `Master/NucleusGaming/Coop/ProtoInput/ProtoInputOptions.cs` | A game profile needs independently selectable hooks for Raw Input, key polling, cursor, focus, messages, controller APIs, window behavior and namespace workarounds |
| `ProtoInputLauncher.cs` | Injection method, selected keyboard/mouse handles, controller indices and hook installation are configured per process instance |
| `Generic/GameHookInfo.cs` and `GenericGameInfo.cs` | Compatibility is a large policy matrix, not a single universal mode |
| `Tools/WindowFakeFocus/WindowFakeFocus.cs` | Some games require repeated activation/focus messages even after API hooks |
| `InputManagement/LockInputRuntime.cs` | Global input locking and Explorer suspension are high-impact operations that require explicit lifecycle handling |
| `Tools/DevReorder/DevReorder.cs` | Legacy controller visibility/order may need a separate per-game wrapper |

### Reusable lesson

The major Nucleus insight is **profile-driven orchestration**. A compatibility system must record exactly which interventions a game needs and must be able to disable risky interventions independently.

### License boundary

Nucleus is GPL-3.0. Copying its implementation into a differently licensed HydraSeat core could impose GPL obligations on the combined work. HydraSeat may study behavior, interfaces, profile concepts and test cases, but should independently implement its planner and runtime unless the project explicitly chooses GPL compatibility.

### HydraSeat decision

- Independently implement a typed compatibility profile and planner.
- Never copy GPL implementation text into the core by default.
- Record Nucleus source paths as research evidence only.
- Consider interoperability through external files or tools only after a legal and technical boundary review.

Repository reference: `https://github.com/SplitScreen-Me/splitscreenme-nucleus`

---

## HidHide

### Observed architecture

HidHide is an MIT-licensed Windows kernel-mode HID visibility firewall. It hides selected physical devices from ordinary applications while allowing approved feeder/orchestrator applications to retain access.

The studied `DEVELOPER.md` documents:

- control device `\\.\HidHide` and a device-interface GUID;
- IOCTL operations for allow/deny lists, active state and inverse-whitelist behavior;
- `MULTI_SZ` payloads containing device instance IDs;
- a **session blacklist** intended for feeder applications;
- session entries owned by the calling process and automatically removed when that process exits.

### Reusable lesson

The session blacklist is attractive because it limits persistent machine mutation and has crash cleanup. It can solve one part of the problem: preventing another application from reading the original physical HID.

It does **not** by itself create Seat-local keyboard/mouse state or deliver replacement input to the target game. It must be paired with a feeder or per-process compatibility backend.

### Risks and gates

- Kernel driver installation and support lifecycle.
- Administrator/deployment requirements.
- Composite-device behavior.
- Keyboard and mouse filtering must be proven; the product is primarily described around gaming devices.
- A mistaken policy can temporarily remove user input; recovery must be independent of the hidden devices.
- Some applications may access a lower or different device layer.

### HydraSeat decision

Implement HidHide as an **optional external backend through the documented control API**, preferably using session-scoped entries. Begin with read-only availability/capability probing. Device hiding must remain disabled until a recovery guard, emergency timeout, test mode and explicit user confirmation are in place.

### P3-D-01 read-only contract record

The read-only probe is tied to upstream revision
`2b950fd9393e1644b4199f6eb4999e1720f0c6e9`. It consulted the public
`DEVELOPER.md`, the exact `HidHide` service declaration in
`HidHide/HidHide.inf`, the file-version resource in `HidHide/Version.rc`, and
the minimal documented IOCTL/interface declarations in
`Shared/HidHideIoctlContract.h`. HidHide is MIT-licensed, but HydraSeat's
license remains unresolved, so no upstream implementation is copied, adapted,
linked, or made a build input.

The independently implemented Windows observer enumerates interface GUID
`{0C320FF7-BD9B-42B6-BDAF-49FEB9C91649}`, obtains the driver file version from
the read-only service configuration, opens the returned control interface for
read access, and sends only function 2052 (`GET_ACTIVE`) and function 2054
(`GET_WLINVERSE`). It never defines or calls set/list/session mutation
operations and never reads allowlist, persistent blacklist, or session-list
contents.

Exact tag commits `98ccf1724d5960d98fc31af9714433df964f462f`
(`1.7.339.0`), `0aa3c946e7629a47d5465a1bc96de846395ba3f9`
(`1.7.344.0`), and `22a1ff5fdce550ec789f7b229ad4c59d6709ab61`
(`1.7.346.0`) each publish functions 2056/2057 for the session blacklist.
HydraSeat recognizes only those exact versions. The production release
`1.5.230.0` and all other/unknown versions remain installed-unverified; no
minimum/maximum version range is guessed.

Repository reference: `https://github.com/nefarius/HidHide`

---

## devreorder

### Observed behavior

The studied repository provides a `dinput8.dll` wrapper that changes DirectInput 8 enumeration order and can hide controllers per application using `devreorder.ini`. Its documentation notes that the technique does not affect XInput, Raw Input, older DirectInput versions, WinMM joystick APIs or direct HID access.

The studied clone contained no top-level license file.

### Reusable lesson

Controller compatibility must be API-specific. A game that reads DirectInput needs a different strategy from a game that reads XInput or Raw HID. Stable device instance IDs are preferable to friendly names or unstable ordering.

### HydraSeat decision

Treat devreorder as a behavior reference only. Do not copy or redistribute its source without a verified license. HydraSeat can independently implement a DirectInput visibility/order adapter later, or allow users to configure an external tool.

Repository reference: `https://github.com/briankendall/devreorder`

---

## Duo

### Observed public model

Duo gives each remote client an independent Windows desktop/session and display stream. This produces stronger desktop-level isolation than a single interactive session, but it depends on a streaming client and a session architecture that differs from HydraSeat's direct local-monitor goal. The studied repository contained release files and documentation but no license file.

### Reusable lesson

Separate Windows sessions simplify focus, shell, profile and process isolation, but create different display, licensing, GPU and streaming constraints. Duo also illustrates the operational importance of session startup, display lifecycle, recovery and per-user process environments.

### HydraSeat decision

Do not base the default architecture on Duo because HydraSeat explicitly targets direct local displays and no streaming requirement. Keep it as a product-level comparison and possible future optional session backend, not as source material.

Repository reference: `https://github.com/DuoStream/Duo`

---

## Windows API implications

### Raw Input registration is process-global per device class

Windows permits one registered target window per Raw Input device class per process. If a library registers on behalf of a game, it can interfere with the game's own registration. Therefore a process backend must interpose both registration and data access coherently rather than merely registering another hidden window.

### Polled keyboard state is not Seat-local

`GetAsyncKeyState`, `GetKeyState`, and `GetKeyboardState` expose thread/desktop/global state assumptions that do not correspond to physical Raw Input device identity. A game that polls these APIs needs per-process virtual state derived from its Seat's selected devices.

### Cursor and foreground are shared resources

The ordinary Windows cursor, clipping rectangle and foreground window are shared concepts. Two games cannot both safely own the global cursor and foreground state. A compatible backend must maintain process-local cursor/focus views or use truly separate sessions.

### Controller APIs differ

- XInput uses logical user slots and commonly needs slot remapping.
- DirectInput exposes enumerated device objects and commonly needs visibility/order control.
- Raw HID or SDL may bypass both.

A game profile must identify the actual API surface rather than assuming every controller follows the same path.

---

## HydraSeat backend decisions

HydraSeat Phase 3 should expose the following logical backends:

1. **Raw Input Host** — stable device observation and diagnostics; low risk; already partly implemented.
2. **Legacy Message Router** — sends selected window messages; useful only for test applications and simple games; must never be labeled complete isolation.
3. **Process Compatibility Adapter** — virtualizes Raw Input registration/data, keyboard polling, cursor, focus, message and controller APIs for a selected process. ProtoInput is the primary reference/optional adapter.
4. **HidHide Session Cloak** — optionally hides original devices after recovery checks; does not inject replacement state by itself.
5. **Controller Visibility Adapter** — XInput slot mapping or DirectInput order/visibility, selected per game API.
6. **Window/Namespace Adapter** — window placement, focus messages and named-object isolation for games that assume a single global instance.
7. **Unsupported/Observation Backend** — explicitly refuses enforcement when requirements cannot be met.

The planner must select only capabilities actually supplied by available backends. A missing capability makes the plan unsupported; it must not silently fall back to global input or claim zero bleed.

## Research artifacts and attribution

If HydraSeat later incorporates compatible source:

- preserve all required copyright and license notices;
- record the exact upstream commit;
- audit every bundled dependency;
- isolate imported code in a clearly named component;
- add a `THIRD_PARTY_NOTICES` file;
- document local modifications;
- keep a clean-room path available for components whose license is incompatible or unclear.

See [CLEAN_ROOM_POLICY.md](CLEAN_ROOM_POLICY.md) for the enforceable contribution rules and [PHASE3_INPUT_ISOLATION_DESIGN.md](PHASE3_INPUT_ISOLATION_DESIGN.md) for the implementation plan.
