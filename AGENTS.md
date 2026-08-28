# HydraSeat Codex Entry Point

OpenAI Codex and other repository agents must follow the canonical project instructions in:

- [`.agents/AGENTS.md`](.agents/AGENTS.md)
- [`docs/PRODUCT_V1.md`](docs/PRODUCT_V1.md)
- [`docs/implementation/DECISIONS.md`](docs/implementation/DECISIONS.md)
- [`docs/implementation/README.md`](docs/implementation/README.md)
- [`docs/implementation/STATUS.md`](docs/implementation/STATUS.md)
- [`docs/implementation/CODEX_PLAYBOOK.md`](docs/implementation/CODEX_PLAYBOOK.md)

## Required workflow

1. Read `.agents/AGENTS.md` completely.
2. Read `docs/implementation/STATUS.md` to identify the current default packet and its true state.
3. Read the starting packet section and any additional actionable packet sections selected from the relevant `docs/implementation/PHASE*.md` documents.
4. Implement one or more actionable packets in declared dependency order. After completing one packet, continue directly into additional READY/actionable packets when useful; a new user turn is not required solely to cross a packet boundary.
5. Do not skip undeclared prerequisites, weaken fail-closed behavior, or mark manual hardware/game/install/reboot gates complete.
6. Run focused tests for every changed packet plus the applicable regression checks, `python tools/validate_implementation_roadmap.py`, and `git diff --check`.
7. Update `STATUS.md` with truthful per-packet automated evidence and leave unperformed manual acceptance pending.
8. Do not push, create or merge pull requests, or otherwise mutate remote state unless the user explicitly authorizes it.

## Current task lookup

Do not hard-code a remembered packet here. The **Current default packet** in [`docs/implementation/STATUS.md`](docs/implementation/STATUS.md) is the starting implementation frontier, not an exclusive task boundary. Additional actionable packets may be selected in dependency order.

Use the validated packet helper:

```text
python tools/show_implementation_packet.py --current
python tools/show_implementation_packet.py --current --prompt
python tools/show_implementation_packet.py --ready
```

The prompt command produces a starting-packet Codex prompt. `--ready` lists additional candidates that may be implemented in the same task once their declared prerequisites are satisfied.

## Product boundary

HydraSeat v1 is a two-Seat, game-first Windows local gaming multiseat product for households that want to use the spare performance of one capable gaming PC instead of buying a second complete desktop solely for simultaneous local play. Seat hardware, Player identity, Game identity, Two-player setup, and Runtime Session are separate concepts. One Seat may stop/change games while the other continues. v1 uses a minimal idle Seat Launcher rather than a general independent desktop shell, is offline-first and least-privilege, and reports compatibility as transparent evidence rather than an official certification badge. Same-game multi-instance automation is allowed only where the game/provider permit it; protected-title attempts require explicit warnings and never imply anti-cheat safety. HydraSeat does not pursue anti-cheat, DRM, protected-process, credential, launcher/account, single-instance, or security-product bypass. Unsupported capabilities fail closed, and recovery is implemented and tested alongside risky mutations.
