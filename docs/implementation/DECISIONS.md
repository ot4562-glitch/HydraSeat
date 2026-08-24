# HydraSeat Non-Negotiable Design Decisions

These decisions prevent future agents from repeatedly reopening settled architecture. A decision may change only through an explicit roadmap/document update that explains migration and compatibility impact.

## D-001 — Seat is the primary product abstraction

A Seat is a logical local PC, not a single monitor and not merely a game window.

A Seat may own:

- multiple physical or virtual displays;
- one explicit primary display;
- zero or more keyboards, mice, touchpads, controllers, audio endpoints, and microphones;
- a process/window group;
- virtual cursor/focus/input state;
- launcher/profile state;
- an optional Seat shell.

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
- optional per-Seat shells.

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

## D-030 — Upstream intent is preserved, product scope is extended

HydraSeat remains a Windows local gaming multiseat framework based on physical-device detection, assignment, Raw Input compatibility, display routing, and game profiles. The Seat-first multi-monitor environment, background host/watchdog, shell, and capability ecosystem extend that intent rather than replacing it with an unrelated remote-desktop or virtual-machine product.

## D-031 — One Management Seat owns the visible control plane

HydraSeat separates the background runtime from the user's control surface. Exactly one configured `ManagementSeatId` owns the default visible control plane for the active session. The default is Seat 1 unless the user explicitly selects another Seat.

The management control surface opens on the Management Seat's primary display and provides session status, device/display composition, Start, Stop/Return to Windows, Reconfigure, diagnostics, and recovery controls. Other Seat shells are read-mostly by default and cannot terminate or reconfigure the whole machine without an explicit permission policy.

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
