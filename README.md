# HydraSeat

[한국어](README.ko.md) · [简体中文](README.zh-CN.md)

HydraSeat is a Windows 10/11 x64 project for running **two local gaming Seats on one PC**. A Seat is a physical station (display, input, controller and audio choices), not another Windows desktop or user session. The normal flow is game-first: choose a game, choose Seat 1, Seat 2 or Both, choose players, then launch.

This repository is an advanced implementation and test platform, **not a finished public release**. Runtime, IPC, launch, rollback, recovery, provider, compatibility and release foundations are implemented and heavily covered by controlled tests. Physical two-keyboard/two-mouse evidence, real-game campaigns, clean-machine installer/UAC/reboot evidence and protected production signing remain release gates.

## What exists now

| Component | Built target | Current responsibility |
| --- | --- | --- |
| Management UI | `HydraSeat.exe` | Native Win32 game-first UI, Seat/profile/setup selection, host control and diagnostics |
| Runtime authority | `hydra_host.exe` | Session authority, bounded/versioned IPC, immutable launch plans, process trees and Seat lifecycle |
| Seat UI | `hydra_seat_ui.exe` | Minimal per-Seat launcher/status surface; not a second desktop shell |
| Recovery | `hydra_watchdog.exe`, `hydra_reset.exe` | Crash-journal recovery, owned-state rollback and emergency reset |
| Operator tools | `hydra_hostctl.exe`, `hydraseat_profilectl.exe`, diagnostics and acceptance tools | Protocol inspection, profile/provider work, controlled probes and evidence capture |
| Optional adapters | Gate C adapter/shim targets | Explicitly gated Raw Input and Win32 polling/focus/cursor compatibility work |

The old Qt prototype UI is not part of the build. The current UI is native Win32 and uses the same host protocol as the command-line and Seat surfaces.

## Runtime structure

```text
HydraSeat.exe (normal-user management UI)
        | bounded host protocol v4
        v
hydra_host.exe (authoritative background runtime)
        +-- validates profile/session/Seat generations
        +-- resolves trusted requirements and immutable launch plans
        +-- launches and owns each Seat process tree independently
        +-- coordinates window/display/controller/audio/input policy
        +-- journals mutations and verifies rollback
        +-- hydra_seat_ui.exe (minimal Seat-local surface)
        +-- hydra_watchdog.exe / hydra_reset.exe (recovery boundary)
```

The host, not either UI, is authoritative. A UI request is not reported as a successful game launch until the production path has installed the exact plan and the host has accepted and executed it. Seat 1 may stop or restart without unnecessarily terminating Seat 2. Protocols are bounded, versioned and fixed-width so x86/x64 adapters cannot silently reinterpret handles or payloads.

## Product boundary

HydraSeat v1 supports at most two active Seats and separates **Seat**, **Player**, **Game**, **TwoPlayerSetup** and the current immutable **RuntimeSession**. It does not promise virtual machines, independent Windows logons, a general multiseat desktop, universal same-game support, or bypasses for DRM, anti-cheat, account, launcher, single-instance or other protection boundaries. Unsupported requirements fail closed and remain visible.

Implemented foundations include stable hardware identity; controlled input-isolation adapters and evidence tooling; authoritative host IPC and process-tree ownership; independent Seat stop/restart; window/display/controller/audio policy; Steam/custom providers, profiles, trusted requirements and typed setup packages; local compatibility evidence and bounded optional community contracts; crash/reset, least-privilege, privacy, transactional installer/update and signing/provenance validation.

The exact packet states and remaining evidence are maintained in [Implementation Status](docs/implementation/STATUS.md). `CODE_COMPLETE` may still require physical, game, installer, reboot, signing or public-service evidence before it can be `VALIDATED`.

## Build and test

Requirements: Windows, Visual Studio 2022 with MSVC C++ workload, CMake, Python 3 and PowerShell.

```powershell
# clean x64
cmake -S . -B C:\HydraSeat\build-x64 -G "Visual Studio 17 2022" -A x64
cmake --build C:\HydraSeat\build-x64 --config Release --parallel
ctest --test-dir C:\HydraSeat\build-x64 -C Release --output-on-failure

# clean Win32/x86 compatibility build
cmake -S . -B C:\HydraSeat\build-x86 -G "Visual Studio 17 2022" -A Win32
cmake --build C:\HydraSeat\build-x86 --config Release --parallel
ctest --test-dir C:\HydraSeat\build-x86 -C Release --output-on-failure
```

On 2026-08-30, fresh x64 and Win32 trees each built without warnings and passed **133/133** CTest tests. The focused production-launch/IPC regression set also passed. That automated result does not replace pending physical and real-game gates.

```powershell
python tools/validate_implementation_roadmap.py
python tools/validate_release_scope.py
python tools/validate_v1_acceptance_campaign.py
python tools/chunk_claim.py validate
git diff --check
```

## Installation and release state

`tools/install_hydraseat.ps1` implements x64 package validation, install/repair/uninstall transactions, owned-path checks and rollback. The release contract contains `HydraSeat.exe`, `hydra_host.exe`, `hydra_seat_ui.exe`, `hydra_watchdog.exe` and `hydra_reset.exe`.

Do not treat the installer as production-validated yet. Clean Windows machines, UAC, reboot/interruption recovery, uninstall postconditions, a protected signing environment, a production certificate/timestamp provider and a signed release-candidate run are still required.

## Documentation map

- [Product v1 contract](docs/PRODUCT_V1.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Implementation status](docs/implementation/STATUS.md)
- [Packet roadmap](docs/implementation/README.md)
- [Reference research index](docs/REFERENCE_RESEARCH_INDEX.md)
- [Related-systems design memo](docs/RELATED_SYSTEMS_RESEARCH.md)
- [Clean-room policy](docs/CLEAN_ROOM_POLICY.md)
- [Compatibility matrix](docs/COMPATIBILITY_MATRIX.md)
- [Parallel chunk board](.agents/CHUNKS.md)

Reference trees under `C:\HydraSeat\references` are read-only research inputs and never build inputs. Research is translated into neutral requirements and independently written tests; incompatible third-party implementation is not copied into HydraSeat core.

## License

The repository does not yet declare final project license and contribution terms. Do not describe it as legally open source or assume source-reuse rights until that release gate is resolved.
