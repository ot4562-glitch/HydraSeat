#!/usr/bin/env python3
"""Show one HydraSeat implementation packet or generate a Codex task prompt.

The script validates the complete roadmap before returning packet content so an
agent never starts from an inconsistent packet graph.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import validate_implementation_roadmap as roadmap


def repository_root() -> Path:
    return Path(__file__).resolve().parents[1]


def current_packet_id(root: Path) -> str:
    status_path = root / "docs" / "implementation" / "STATUS.md"
    text = status_path.read_text(encoding="utf-8")
    match = roadmap.CURRENT_PACKET_RE.search(text)
    if not match:
        raise RuntimeError("STATUS.md has no Current default packet")
    return match.group(1)


def relative_path(path: Path, root: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def print_validation_errors(result: roadmap.Validation) -> None:
    for warning in result.warnings:
        print(f"WARNING: {warning}", file=sys.stderr)
    for error in result.errors:
        print(f"ERROR: {error}", file=sys.stderr)


def codex_prompt(packet: roadmap.Packet, root: Path) -> str:
    phase_path = relative_path(packet.path, root)
    dependency_text = (
        ", ".join(sorted(packet.dependencies))
        if packet.dependencies
        else "none declared"
    )
    return f"""Implement packet {packet.packet_id} exactly as specified in
  {phase_path}

Packet title:
  {packet.title}

Declared prerequisites:
  {dependency_text}

Read and obey before editing:
- AGENTS.md
- .agents/AGENTS.md
- docs/implementation/DECISIONS.md
- docs/implementation/README.md
- docs/implementation/STATUS.md
- docs/implementation/CODEX_PLAYBOOK.md
- {phase_path}
- every design/source/test file linked by the packet

Scope rules:
- implement only {packet.packet_id} and explicitly permitted coupled tests;
- do not implement later packets;
- do not weaken fail-closed behavior or recovery requirements;
- do not mark manual hardware/game/install/reboot acceptance complete;
- do not claim capabilities not proven by the packet tests;
- no anti-cheat, DRM, protected-process, credential, or security-product bypass;
- no third-party source copying outside docs/CLEAN_ROOM_POLICY.md;
- do not push or create/merge PRs unless the user explicitly authorizes it.

Required workflow:
1. verify the packet is READY and every prerequisite has sufficient evidence;
2. inspect existing implementation and tests;
3. record files/types, ownership/thread model, OS state touched, rollback path,
   tests, and pending manual gates before editing;
4. implement the smallest coherent packet scope;
5. add normal, boundary, malformed, stale/duplicate, failure, teardown,
   rollback, and no-cross-Seat tests as applicable;
6. run the packet checks and existing regression suite;
7. run python tools/validate_implementation_roadmap.py;
8. run git diff --check;
9. update docs/implementation/STATUS.md with truthful evidence;
10. summarize changed files, tests, known limits, and unperformed manual gates.

Do not treat CODE_COMPLETE as VALIDATED when manual acceptance remains pending.
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    selector = parser.add_mutually_exclusive_group()
    selector.add_argument("packet_id", nargs="?", help="packet ID to show")
    selector.add_argument(
        "--current", action="store_true", help="use STATUS.md current packet"
    )
    selector.add_argument(
        "--ready", action="store_true", help="list all READY packet definitions"
    )
    parser.add_argument(
        "--prompt",
        action="store_true",
        help="print a ready-to-paste Codex implementation prompt",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=repository_root(),
        help="repository root (default: inferred from script location)",
    )
    args = parser.parse_args()
    root = args.root.resolve()

    result, packets = roadmap.validate(root)
    print_validation_errors(result)
    if result.errors:
        print("Refusing to show a packet from an invalid roadmap.", file=sys.stderr)
        return 1

    if args.ready:
        ready = sorted(
            (
                packet
                for packet in packets.values()
                if roadmap.first_state_component(packet.state) == "READY"
            ),
            key=lambda packet: packet.packet_id,
        )
        for packet in ready:
            print(
                f"{packet.packet_id}\t{packet.title}\t"
                f"{relative_path(packet.path, root)}"
            )
        return 0

    packet_id = args.packet_id
    if args.current or packet_id is None:
        packet_id = current_packet_id(root)

    packet = packets.get(packet_id)
    if packet is None:
        print(f"Unknown packet: {packet_id}", file=sys.stderr)
        return 2

    if args.prompt:
        print(codex_prompt(packet, root), end="")
        return 0

    print(
        f"Packet: {packet.packet_id}\n"
        f"Title: {packet.title}\n"
        f"State: {packet.state}\n"
        f"File: {relative_path(packet.path, root)}:{packet.line}\n"
        f"Dependencies: "
        f"{', '.join(sorted(packet.dependencies)) if packet.dependencies else 'none'}\n"
    )
    print(packet.block.rstrip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
