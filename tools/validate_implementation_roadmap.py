#!/usr/bin/env python3
"""Validate HydraSeat implementation-roadmap structure and references.

Uses only the Python standard library so Codex and CI can run it before code work.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict, deque
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import unquote

from validate_release_scope import validate as validate_release_scope_file

PACKET_RE = re.compile(r"\b(P\d+-[A-Z0-9]+-\d+[A-Z]?)\b")
PACKET_HEADING_RE = re.compile(
    r"^##\s+(P\d+-[A-Z0-9]+-\d+[A-Z]?)\s+—\s+(.+?)\s*$",
    re.MULTILINE,
)
STATE_RE = re.compile(r"^\*\*State:\*\*\s+(.+?)\s*$", re.MULTILINE)
STATUS_ROW_RE = re.compile(
    r"^\|\s*(P\d+-[A-Z0-9]+-\d+[A-Z]?)\s*\|\s*([^|]+?)\s*\|",
    re.MULTILINE,
)
CURRENT_PACKET_RE = re.compile(
    r"Current default packet:\s*\*\*(P\d+-[A-Z0-9]+-\d+[A-Z]?)",
)
MARKDOWN_LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")

ALLOWED_STATES = {
    "BLOCKED",
    "READY",
    "IN_PROGRESS",
    "CODE_COMPLETE",
    "VALIDATED",
    "MERGED",
    "UPSTREAMED",
    "DEFERRED",
    "REJECTED",
}

PHASE_FILES = {
    "PHASE3_INPUT_ISOLATION.md",
    "PHASE4_RUNTIME_DISPLAY.md",
    "PHASE5_TWO_SEAT_MVP.md",
    "PHASE6_LAUNCHER_PROFILES.md",
    "PHASE7_SEAT_SHELL.md",
    "PHASE8_RELIABILITY_DISTRIBUTION.md",
    "PHASE9_COMPATIBILITY_SDK.md",
    "PHASE10_RELEASE_HARDENING.md",
}


@dataclass(frozen=True)
class Packet:
    packet_id: str
    title: str
    state: str
    path: Path
    line: int
    block: str
    dependencies: frozenset[str]


class Validation:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def error(self, message: str) -> None:
        self.errors.append(message)

    def warning(self, message: str) -> None:
        self.warnings.append(message)


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def first_state_component(raw_state: str) -> str:
    return raw_state.split("/", 1)[0].strip()


def extract_dependencies(block: str) -> frozenset[str]:
    marker = "**Depends on**"
    start = block.find(marker)
    if start < 0:
        return frozenset()
    tail = block[start + len(marker) :]
    next_section = re.search(r"^\*\*[^*]+\*\*", tail, re.MULTILINE)
    if next_section:
        tail = tail[: next_section.start()]
    return frozenset(PACKET_RE.findall(tail))


def validate_markdown_file(path: Path, text: str, result: Validation) -> None:
    if "\x00" in text:
        result.error(f"{path}: contains NUL characters")
    if "\x08" in text:
        result.error(f"{path}: contains backspace control characters")
    if "\ufffd" in text:
        result.error(f"{path}: contains Unicode replacement characters")

    for index, character in enumerate(text):
        code = ord(character)
        if code < 32 and character not in "\n\r\t":
            result.error(
                f"{path}:{line_number(text, index)}: contains control character U+{code:04X}"
            )

    if text.count("```") % 2:
        result.error(f"{path}: has an unbalanced fenced code block")

    for raw_target in MARKDOWN_LINK_RE.findall(text):
        target = raw_target.strip()
        if not target or target.startswith(("#", "http://", "https://", "mailto:")):
            continue
        target = target.split("#", 1)[0].split("?", 1)[0]
        if not target:
            continue
        resolved = (path.parent / unquote(target)).resolve()
        if not resolved.exists():
            result.error(f"{path}: broken relative link: {raw_target}")


def parse_packets(path: Path, text: str, result: Validation) -> list[Packet]:
    matches = list(PACKET_HEADING_RE.finditer(text))
    packets: list[Packet] = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        block = text[match.start() : end]
        state_match = STATE_RE.search(block)
        if not state_match:
            result.error(
                f"{path}:{line_number(text, match.start())}: {match.group(1)} has no State field"
            )
            state = ""
        else:
            state = state_match.group(1).strip()
            base_state = first_state_component(state)
            if base_state not in ALLOWED_STATES:
                result.error(
                    f"{path}:{line_number(text, state_match.start())}: "
                    f"{match.group(1)} has invalid state {state!r}"
                )

        if "**Done when**" not in block:
            result.error(
                f"{path}:{line_number(text, match.start())}: {match.group(1)} has no Done when field"
            )

        packets.append(
            Packet(
                packet_id=match.group(1),
                title=match.group(2).strip(),
                state=state,
                path=path,
                line=line_number(text, match.start()),
                block=block,
                dependencies=extract_dependencies(block),
            )
        )
    return packets


def parse_status_declarations(
    status_path: Path, text: str, result: Validation
) -> dict[str, str]:
    states: dict[str, str] = {}
    for match in STATUS_ROW_RE.finditer(text):
        packet_id = match.group(1)
        state = match.group(2).strip().strip("*")
        base_state = first_state_component(state)
        if base_state not in ALLOWED_STATES:
            result.error(
                f"{status_path}:{line_number(text, match.start())}: "
                f"{packet_id} has invalid status-table state {state!r}"
            )
        if packet_id in states and states[packet_id] != state:
            result.error(
                f"{status_path}: {packet_id} is declared with conflicting states "
                f"{states[packet_id]!r} and {state!r}"
            )
        states[packet_id] = state
    return states


def detect_cycles(
    packets: dict[str, Packet], dependencies: dict[str, set[str]], result: Validation
) -> None:
    indegree = {packet_id: 0 for packet_id in packets}
    outgoing: dict[str, set[str]] = defaultdict(set)
    for packet_id, deps in dependencies.items():
        for dependency in deps:
            if dependency not in packets:
                continue
            outgoing[dependency].add(packet_id)
            indegree[packet_id] += 1

    ready = deque(sorted(key for key, degree in indegree.items() if degree == 0))
    visited = 0
    while ready:
        current = ready.popleft()
        visited += 1
        for target in sorted(outgoing[current]):
            indegree[target] -= 1
            if indegree[target] == 0:
                ready.append(target)

    if visited == len(packets):
        return
    cyclic = sorted(packet_id for packet_id, degree in indegree.items() if degree > 0)
    result.error("packet dependency graph contains a cycle involving: " + ", ".join(cyclic))


def validate(root: Path) -> tuple[Validation, dict[str, Packet]]:
    result = Validation()
    implementation_dir = root / "docs" / "implementation"
    if not implementation_dir.is_dir():
        result.error(f"missing implementation roadmap directory: {implementation_dir}")
        return result, {}

    implementation_markdown_paths = sorted(implementation_dir.glob("*.md"))
    existing_phase_files = {
        path.name
        for path in implementation_markdown_paths
        if path.name.startswith("PHASE")
    }
    missing_phase_files = sorted(PHASE_FILES - existing_phase_files)
    if missing_phase_files:
        result.error("missing phase documents: " + ", ".join(missing_phase_files))

    readme_paths = sorted(root.glob("README*.md"))
    entrypoint_paths = [
        *readme_paths,
        root / "AGENTS.md",
        root / ".agents" / "AGENTS.md",
    ]
    documentation_paths = sorted((root / "docs").rglob("*.md"))
    all_markdown_paths = sorted(
        {path for path in entrypoint_paths + documentation_paths if path.exists()}
    )

    texts: dict[Path, str] = {}
    for path in all_markdown_paths:
        text = path.read_text(encoding="utf-8")
        texts[path] = text
        validate_markdown_file(path, text, result)

    packets_by_id: dict[str, Packet] = {}
    for path in implementation_markdown_paths:
        text = texts[path]
        for packet in parse_packets(path, text, result):
            previous = packets_by_id.get(packet.packet_id)
            if previous:
                result.error(
                    f"duplicate packet {packet.packet_id}: "
                    f"{previous.path}:{previous.line} and {packet.path}:{packet.line}"
                )
            else:
                packets_by_id[packet.packet_id] = packet

    status_path = implementation_dir / "STATUS.md"
    if not status_path.exists():
        result.error(f"missing {status_path}")
        status_states: dict[str, str] = {}
        status_text = ""
    else:
        status_text = texts.get(status_path) or status_path.read_text(encoding="utf-8")
        status_states = parse_status_declarations(status_path, status_text, result)

    declared_ids = set(packets_by_id) | set(status_states)
    all_text = "\n".join(texts.values())
    for referenced_id in sorted(set(PACKET_RE.findall(all_text)) - declared_ids):
        result.error(f"packet {referenced_id} is referenced but never declared")

    for packet_id, status_state in sorted(status_states.items()):
        packet = packets_by_id.get(packet_id)
        if not packet:
            continue
        if first_state_component(status_state) != first_state_component(packet.state):
            result.error(
                f"state mismatch for {packet_id}: STATUS.md={status_state!r}, "
                f"{packet.path.name}={packet.state!r}"
            )

    current_match = CURRENT_PACKET_RE.search(status_text)
    if not current_match:
        result.error("STATUS.md has no Current default packet")
    else:
        current_packet = current_match.group(1)
        if current_packet not in declared_ids:
            result.error(f"current default packet {current_packet} is not declared")
        current_state = status_states.get(current_packet)
        actionable_current_states = {"READY", "IN_PROGRESS", "CODE_COMPLETE"}
        if current_state and first_state_component(current_state) not in actionable_current_states:
            result.error(
                f"current default packet {current_packet} must be READY, IN_PROGRESS, "
                f"or CODE_COMPLETE, got {current_state!r}"
            )

    dependency_graph: dict[str, set[str]] = {}
    for packet_id, packet in packets_by_id.items():
        dependency_graph[packet_id] = set(packet.dependencies)
        for dependency in sorted(packet.dependencies):
            if dependency not in declared_ids:
                result.error(
                    f"{packet.path}:{packet.line}: {packet_id} depends on undeclared {dependency}"
                )
            if dependency == packet_id:
                result.error(
                    f"{packet.path}:{packet.line}: {packet_id} depends on itself"
                )

    detect_cycles(packets_by_id, dependency_graph, result)

    master_path = implementation_dir / "README.md"
    if master_path.exists():
        master_text = texts.get(master_path) or master_path.read_text(encoding="utf-8")
        for phase_file in sorted(PHASE_FILES):
            if f"({phase_file})" not in master_text:
                result.error(f"{master_path}: does not link {phase_file}")

    release_scope_path = root / "config" / "release-scope-v1.json"
    if not release_scope_path.exists():
        result.error(f"missing v1 release scope: {release_scope_path}")
    else:
        try:
            validate_release_scope_file(release_scope_path)
        except (OSError, ValueError) as exc:
            result.error(f"invalid v1 release scope: {exc}")

    return result, packets_by_id


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root (default: inferred from script location)",
    )
    args = parser.parse_args()
    root = args.root.resolve()
    result, packets = validate(root)

    for warning in result.warnings:
        print(f"WARNING: {warning}", file=sys.stderr)
    for error in result.errors:
        print(f"ERROR: {error}", file=sys.stderr)

    if result.errors:
        print(
            f"Roadmap validation failed: {len(result.errors)} error(s), "
            f"{len(result.warnings)} warning(s).",
            file=sys.stderr,
        )
        return 1

    phase_counts: dict[str, int] = defaultdict(int)
    for packet_id in packets:
        phase_counts[packet_id.split("-", 1)[0]] += 1
    summary = ", ".join(
        f"{phase}={count}" for phase, count in sorted(phase_counts.items())
    )
    print(
        f"Implementation roadmap valid: {len(packets)} packet definitions "
        f"({summary}); {len(result.warnings)} warning(s)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
