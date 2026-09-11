# HydraSeat

[한국어](README.ko.md) · [简体中文](README.zh-CN.md)

HydraSeat is an experimental Windows application for turning one sufficiently capable PC into two local gaming stations. Each Seat has its own display and input/controller/audio choices while both games run inside the same interactive Windows session.

The project is being developed with [Pranshu45883/HydraSeat](https://github.com/Pranshu45883/HydraSeat) as the upstream repository. The current code is a working engineering build, not a finished public release.

## Current scope

HydraSeat v1 is deliberately narrow:

- Windows 10/11 on x64 PCs;
- at most two active Seats;
- game-focused use rather than two independent Windows desktops;
- independent game start/stop for each Seat where the game and launcher allow it;
- local profiles and compatibility data;
- recovery back to ordinary Windows when setup or a game fails.

HydraSeat does not bypass anti-cheat, DRM, protected processes, account or launcher restrictions, deliberate single-instance restrictions, or other security boundaries. Unsupported isolation paths fail rather than pretending to work.

## How it is put together

```text
HydraSeat.exe
    |
    | local IPC
    v
hydra_host.exe
    +-- Seat 1 game/process/window/resources
    +-- Seat 2 game/process/window/resources
    +-- input, display, controller and audio routing
    +-- rollback/recovery

hydra_seat_ui.exe     minimal Seat-local controls
hydra_watchdog.exe    recovery supervision
hydra_reset.exe       emergency cleanup
```

`hydra_host.exe` owns the runtime session. The management UI and Seat UI request changes through the host instead of owning game processes or hardware state themselves. Process ownership uses Windows process/job primitives, and hardware assignment is based on stable device identity rather than enumeration order.

The repository also contains compatibility adapters, diagnostics and acceptance tooling. Those are support tools, not separate product layers.

## Project status

Most of the planned two-Seat runtime path exists and is covered by automated tests, including host IPC, process ownership, Seat lifecycle, display/window policy, controller/audio plumbing, profiles/providers, recovery and installer contracts.

The remaining work is mainly integration and real-machine validation: physical two-keyboard/two-mouse isolation, representative real games, display/audio edge cases, clean-machine install/UAC/reboot behavior and production signing. Automated tests are not treated as substitutes for those checks.

The architecture is currently being simplified before more feature work is added. In particular, the project is moving away from roadmap-driven module boundaries and development-agent scaffolding toward a smaller set of product-oriented components that a human contributor can review without learning the history of the implementation plan.

## Build

Requirements:

- Windows 10/11
- Visual Studio 2022 with the Desktop development with C++ workload
- CMake 3.20+
- Python 3 for repository validation tools

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

A Win32/x86 build is also maintained for the compatibility/shim boundary:

```powershell
cmake -S . -B build-x86 -G "Visual Studio 17 2022" -A Win32
cmake --build build-x86 --config Release --parallel
ctest --test-dir build-x86 -C Release --output-on-failure
```

## Repository guide

- `src/` and `include/hydra/` — application/runtime code
- `tests/` — automated tests and small test executables
- `tools/` — diagnostics, validation and release helpers
- `docs/PRODUCT_V1.md` — intended v1 behavior and limits
- `docs/ARCHITECTURE.md` — current architecture notes
- `docs/COMPATIBILITY_MATRIX.md` — compatibility evidence
- `docs/CLEAN_ROOM_POLICY.md` — rules for third-party reference research

Historical implementation plans under `docs/implementation/` describe how the current code was built. They are not intended to define permanent module boundaries.

## Contributing

The project is being reorganized into smaller, reviewable changes before the large experimental branch is proposed for upstream integration. New changes should prefer simple ownership, direct code and focused tests over new framework layers.

The upstream repository owner has indicated that a clear open-source license and contribution terms will be added. Until those terms are published, do not assume source-reuse rights beyond what GitHub itself permits.
