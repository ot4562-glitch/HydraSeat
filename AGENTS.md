# HydraSeat development notes

HydraSeat v1 is a Windows, game-first multiseat application for at most two active Seats in one interactive Windows session.

## Product invariants

- Seat hardware, Player identity, Game identity, two-player setup, and runtime bindings are separate concepts.
- Either Seat must be able to stop or change its game without tearing down the other Seat.
- Core operation is offline-first and least-privilege.
- Do not bypass anti-cheat, DRM, protected processes, credentials, launcher/account policy, deliberate single-instance restrictions, or security products.
- Unsupported or ambiguous isolation must fail closed.
- Risky Windows mutations require a verified rollback path.
- Automated tests never substitute for physical multi-input/display/audio, clean-machine install, reboot, or signing evidence.

## Architecture rules

- Prefer direct, boring code over a new manager, coordinator, registry, factory, adapter, or policy layer.
- Add an interface only for a real OS/test seam or when there are multiple meaningful implementations.
- Keep one authoritative owner for each piece of mutable runtime state. Do not mirror state machines across layers.
- Organize modules by product responsibility, not roadmap packet IDs or implementation phases.
- Public headers under `include/hydra` are for reusable product/runtime APIs. Diagnostics, acceptance harnesses, experiments, and test support belong under `tools`, `tests`, or internal source directories.
- Keep Win32 resource ownership RAII-based and capture system errors at the failing API boundary.
- Cross-process protocols and persisted schemas must be explicit, bounded, versioned, and pointer-free.
- Stable hardware identity owns Seat assignment; enumeration order and friendly names do not.
- Latency-sensitive input paths must not perform disk, network, UI, or unbounded queue work.

## Change discipline

1. Read the owning source and its focused tests before changing behavior.
2. Make the smallest coherent change that simplifies ownership or fixes a user-visible problem.
3. Do not add abstraction solely to make a future possibility configurable.
4. Run focused tests first. Use broad x64/x86/release validation only for integration or release work.
5. Keep physical/manual acceptance claims explicitly pending until a human performs them.
6. Preserve unrelated worktree changes.

For product scope, architecture decisions, compatibility constraints, or release policy, consult only the relevant section of the corresponding document under `docs/`. Avoid treating historical implementation plans as runtime architecture authority.
