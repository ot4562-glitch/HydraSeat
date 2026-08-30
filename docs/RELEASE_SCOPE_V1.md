# HydraSeat v1 Release Qualification Scope

Status: **qualification freeze for P10-SCOPE-01**. This document defines what must be tested for a v1 release candidate. A `release-target` entry is **not** a support claim by itself; the corresponding manual, physical, real-game, installer, performance, security, and release-candidate evidence must still pass before GA wording can call it supported.

The machine-readable source of truth is [`../config/release-scope-v1.json`](../config/release-scope-v1.json). Validate it with:

```text
python tools/validate_release_scope.py
python tools/validate_release_scope.py --self-test
```

## 1. Product boundary

HydraSeat v1 is frozen to:

- at most **two active Seats**;
- a **game-only** local multiseat product;
- one normal Windows interactive session;
- a minimal per-Seat launcher only when useful;
- no general independent per-Seat desktop, taskbar, clipboard, wallpaper, VM, RDP, or streaming requirement;
- no mandatory HydraSeat cloud account;
- no anti-cheat, DRM, protected-process, credential, account, launcher, or deliberate single-instance bypass.

These boundaries narrow release testing deliberately so one developer can qualify a real household two-player product instead of an unbounded general multiseat platform.

## 2. Host platform freeze

### Release-target host

The v1 GA host architecture is **x64 Windows**. HydraSeat keeps x86 compatibility coverage for target-process/adapter paths, but a 32-bit Windows host is not a v1 GA target.

The current Windows 11 build-family qualification matrix is:

| Build family | Base build | Disposition | Release meaning |
| --- | ---: | --- | --- |
| Windows 11 24H2 | 26100 | release-target | Must pass the applicable v1 release matrix while the tested edition remains vendor-serviced |
| Windows 11 25H2 | 26200 | release-target | Must pass the applicable v1 release matrix |
| Windows 11 26H1 | 28000 | release-target | Must pass the applicable v1 release matrix |
| Windows 10 22H2 / ESU | 19045 | experimental | Development/compatibility evidence may be recorded, but this is not a normal GA support claim |

The list is a dated release qualification snapshot, not a promise to support a Windows build after its vendor servicing lifecycle ends. Before an RC is published, P10-COMPAT-01 must re-check the matrix and deliberately revise the scope if a build family has left the supported Windows lifecycle.

CPU/GPU support is capability- and workload-based until P10-PERF-01 provides measured budgets. HydraSeat does not invent a minimum CPU/GPU generation from source review alone. A release-target machine must satisfy the selected Windows build, expose the required physical display topology, and have sufficient hardware headroom for the two games being qualified.

## 3. Display, input, controller, and audio boundary

### Release targets

- **Physical display groups** are the normal v1 path. Real placement, mixed-DPI, reconnect, and rollback evidence is still required.
- **Two physical keyboard/mouse sets** are in the release target only with receiver-aware physical zero-bleed evidence.
- **XInput** and **DirectInput** are separate release-target controller paths. Each requires exact scenario evidence; one does not imply the other.
- **Core Audio** is release-target only for exact tested endpoint/session scenarios. Unsupported arbitrary endpoint movement must fail closed or use explicit user-assisted configuration.

### Experimental or deferred

- Virtual display / custom IDD support is **deferred** from the normal v1 path.
- Raw HID, SDL, and vendor controller APIs are **experimental** until separately evidenced for an exact game/API path.
- Optional device-driver integration remains **experimental** until signing, recovery, clean-machine, and physical-safety gates exist. No driver is silently promoted into the normal package.

## 4. Game/provider boundary

The initial v1 release-target discovery/launch paths are:

- read-only **Steam** local discovery plus the normal exact-AppID Steam launch URI path;
- bounded **custom EXE** fallback with an explicit executable, argument vector, and no shell-command surface.

Epic, EA, and GOG remain provider-neutral extension targets but are **deferred** from the initial v1 live-provider promise unless a later scope revision adds and validates them.

Same-title/two-instance support is a release target only in the narrow product sense already defined by `PRODUCT_V1.md`: v1 must demonstrate at least one real, lawful title/provider combination where two instances are already permitted. HydraSeat does not promise universal same-game multi-instance support.

Protected titles remain **explicit experiments**. They require visible warning/opt-in and can never be used as anti-cheat safety or bypass evidence.

## 5. Offline, network, update, and privilege boundary

Core operation remains offline-first:

- Seat/Player/setup state, local game discovery, local launch/runtime/recovery, and local compatibility evidence do not require a HydraSeat cloud account;
- community sharing is off by default and requires exact redacted preview plus explicit approval;
- compatibility/setup data refresh is a separate trust/update domain from program/runtime/driver updates;
- program/runtime/driver updates require explicit user approval;
- normal Management UI/runtime operation is unelevated whenever the required Windows API permits it;
- privileged operations are narrowly limited to install, repair, uninstall, optional components, and explicit system recovery;
- no privileged component may expose general command execution.

## 6. Installer boundary

v1 requires a real Windows installer/repair/uninstaller. The release qualification scope requires:

- installation without Visual Studio/CMake;
- optional first-run Seat wizard;
- `Set later` for optional/incomplete Seat hardware categories;
- repair and uninstall;
- return to ordinary Windows before risky removal;
- removal of HydraSeat-owned state only;
- clean-machine and interrupted/failure acceptance before GA.

Developer builds and synthetic transaction tests are useful evidence but do not substitute for clean-machine installer acceptance.

## 7. Explicit v1 non-goals

The release scope validator permanently rejects silent promotion of these non-goals:

- N-Seat v1 support;
- general independent Windows desktops;
- required VM/RDP/streaming;
- universal same-game multi-instance support;
- anti-cheat/DRM/account/launcher/single-instance bypass;
- anti-cheat safety certification;
- mandatory HydraSeat cloud account or telemetry;
- compatibility with every game.

A future version may revisit a deferred feature through an explicit product/roadmap decision. It must not enter v1 accidentally through a config edit.

## 8. External research provenance

Platform lifecycle/build-family information was consulted only from Microsoft official platform documentation:

- Microsoft Learn — `Supported versions of Windows client`: https://learn.microsoft.com/en-us/windows/release-health/supported-versions-windows-client
- consulted: 2026-08-29;
- classification under `CLEAN_ROOM_POLICY.md`: official platform documentation;
- use: Windows release/build-family qualification snapshot only;
- no source code was copied or adapted.

At the time of the scope snapshot, Microsoft lists Windows 11 24H2 (build 26100), 25H2 (26200), and 26H1 (28000) as serviced client build families, while ordinary Windows 10 22H2 general servicing has ended and ESU/LTSC paths have separate lifecycles. P10-COMPAT-01 must re-check vendor servicing status before RC publication.

## 9. Change control

`config/release-scope-v1.json` is intentionally strict. Any change to:

- Seat count;
- host architecture;
- Windows build families;
- release-target capability disposition;
- provider scope;
- no-bypass/non-goal policy;
- offline/update/elevation semantics;
- installer guarantees;

must be an explicit reviewed product-scope change. `tools/validate_release_scope.py` rejects unknown fields, duplicate identifiers, implicit Windows 10 GA promotion, x86-host promotion, virtual-display promotion, general privileged command execution, and removal of canonical non-goals.

This gives P10-PERF-01, P10-COMPAT-01, P10-SEC-01, P10-PRIV-01, P10-PKG-01, and the RC gate one finite matrix to test instead of each packet inventing its own idea of v1.
