# HydraSeat Codex Entry Point

OpenAI Codex and other repository agents must follow the canonical project instructions in:

- [`.agents/AGENTS.md`](.agents/AGENTS.md)
- [`docs/implementation/DECISIONS.md`](docs/implementation/DECISIONS.md)
- [`docs/implementation/README.md`](docs/implementation/README.md)
- [`docs/implementation/STATUS.md`](docs/implementation/STATUS.md)
- [`docs/implementation/CODEX_PLAYBOOK.md`](docs/implementation/CODEX_PLAYBOOK.md)

## Required workflow

1. Read `.agents/AGENTS.md` completely.
2. Read `docs/implementation/STATUS.md` to identify the current default packet and its true state.
3. Read the packet section in the relevant `docs/implementation/PHASE*.md` document.
4. Implement exactly one named packet unless that packet explicitly permits coupled work.
5. Do not implement later packets, weaken fail-closed behavior, or mark manual hardware/game/install/reboot gates complete.
6. Run the packet's tests, the existing regression suite, `python tools/validate_implementation_roadmap.py`, and `git diff --check`.
7. Update `STATUS.md` with truthful automated evidence and leave unperformed manual acceptance pending.
8. Do not push, create or merge pull requests, or otherwise mutate remote state unless the user explicitly authorizes it.

## Current task lookup

Do not hard-code a remembered packet here. The authoritative current packet is always the **Current default packet** in [`docs/implementation/STATUS.md`](docs/implementation/STATUS.md).

Use the validated packet helper:

```text
python tools/show_implementation_packet.py --current
python tools/show_implementation_packet.py --current --prompt
```

The second command produces the preferred packet-scoped Codex prompt.

## Product boundary

HydraSeat is a Seat-first, multi-monitor Windows local gaming multiseat framework. It does not pursue anti-cheat, DRM, protected-process, credential, or security-product bypass. Controlled HydraSeat-owned probes precede third-party process work. Unsupported capabilities fail closed, and recovery is implemented and tested alongside risky mutations.
