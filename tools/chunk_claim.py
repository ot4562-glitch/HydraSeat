#!/usr/bin/env python3
"""Atomic local chunk claims for HydraSeat multi-agent work.

Tracked work definitions live in .agents/CHUNKS.md. Runtime claim state lives
under .ai-bridge/chunk-claims/ and is intentionally ignored by git.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import re
import sys
from contextlib import contextmanager
from typing import Any

ROOT = pathlib.Path(__file__).resolve().parents[1]
CATALOG = ROOT / ".agents" / "CHUNKS.md"
CLAIM_DIR = ROOT / ".ai-bridge" / "chunk-claims"
LOCK_PATH = CLAIM_DIR / ".claim.lock"
CHUNK_RE = re.compile(r"^###\s+(CHUNK-[A-Z0-9-]+)\s+-\s+(.+?)\s*$")


def now_utc() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def load_catalog() -> dict[str, str]:
    chunks: dict[str, str] = {}
    try:
        lines = CATALOG.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError:
        raise SystemExit(f"chunk catalog not found: {CATALOG}")
    for line in lines:
        match = CHUNK_RE.match(line)
        if match:
            chunks[match.group(1)] = match.group(2)
    if not chunks:
        raise SystemExit("chunk catalog contains no CHUNK-* headings")
    return chunks


def claim_path(chunk_id: str) -> pathlib.Path:
    return CLAIM_DIR / f"{chunk_id}.json"


def normalize_touched_paths(paths: list[str]) -> list[str]:
    normalized: list[str] = []
    for raw in paths:
        value = raw.strip().replace("\\", "/").rstrip("/")
        if not value or value.startswith("/") or re.match(r"^[A-Za-z]:", value):
            raise SystemExit(f"touched path must be repository-relative: {raw!r}")
        parts = pathlib.PurePosixPath(value).parts
        if any(part in {"", ".", ".."} for part in parts):
            raise SystemExit(f"touched path must be normalized and may not traverse: {raw!r}")
        if any(character in value for character in "*?[]"):
            raise SystemExit(f"touched path must be concrete, not a glob: {raw!r}")
        canonical = "/".join(parts)
        if canonical not in normalized:
            normalized.append(canonical)
    if not normalized:
        raise SystemExit("at least one --paths entry is required")
    return normalized


def paths_overlap(left: str, right: str) -> bool:
    return left == right or left.startswith(right + "/") or right.startswith(left + "/")


@contextmanager
def claim_lock() -> Any:
    CLAIM_DIR.mkdir(parents=True, exist_ok=True)
    try:
        fd = os.open(LOCK_PATH, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError:
        raise SystemExit("claim board is being updated; refresh and retry")
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(f"pid={os.getpid()} at={now_utc()}\n")
        yield
    finally:
        LOCK_PATH.unlink(missing_ok=True)


def active_path_conflict(chunk_id: str, touched_paths: list[str]) -> str | None:
    for path in CLAIM_DIR.glob("CHUNK-*.json"):
        claim = read_claim(path)
        if not claim or claim.get("state") != "CLAIMED" or claim.get("chunk") == chunk_id:
            continue
        for existing in claim.get("touched_paths", []):
            for requested in touched_paths:
                if paths_overlap(str(existing), requested):
                    return (
                        f"path conflict with {claim.get('chunk', path.stem)} "
                        f"owned by {claim.get('owner', '?')}: {requested!r} overlaps {existing!r}"
                    )
    return None


def read_claim(path: pathlib.Path) -> dict[str, Any] | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return None
    except (json.JSONDecodeError, OSError) as exc:
        return {"state": "CORRUPT", "error": str(exc)}


def atomic_write_new(path: pathlib.Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    flags = os.O_CREAT | os.O_EXCL | os.O_WRONLY
    fd = os.open(path, flags, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(payload, handle, ensure_ascii=False, indent=2, sort_keys=True)
            handle.write("\n")
    except BaseException:
        try:
            path.unlink(missing_ok=True)
        finally:
            raise


def replace_claim(path: pathlib.Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(f".tmp.{os.getpid()}")
    temp.write_text(json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8", newline="\n")
    os.replace(temp, path)


def require_owner(claim: dict[str, Any], owner: str) -> None:
    actual = str(claim.get("owner", ""))
    if actual != owner:
        raise SystemExit(f"claim belongs to {actual!r}, not {owner!r}")


def cmd_list(_: argparse.Namespace) -> int:
    chunks = load_catalog()
    CLAIM_DIR.mkdir(parents=True, exist_ok=True)
    for chunk_id, title in chunks.items():
        claim = read_claim(claim_path(chunk_id))
        if claim is None:
            print(f"READY    {chunk_id:<20} {title}")
            continue
        state = str(claim.get("state", "CLAIMED"))
        owner = str(claim.get("owner", "?"))
        updated = str(claim.get("updated_at", claim.get("claimed_at", "?")))
        paths = ",".join(str(item) for item in claim.get("touched_paths", [])) or "legacy-unspecified"
        detail = str(claim.get("summary", claim.get("reason", claim.get("note", ""))))
        suffix = f" | detail={detail}" if detail else ""
        print(
            f"{state:<8} {chunk_id:<20} {title} | owner={owner} | "
            f"updated={updated} | paths={paths}{suffix}"
        )
    return 0


def cmd_claim(args: argparse.Namespace) -> int:
    chunks = load_catalog()
    if args.chunk not in chunks:
        raise SystemExit(f"unknown chunk: {args.chunk}")
    touched_paths = normalize_touched_paths(args.paths)
    claimed_at = now_utc()
    payload = {
        "schema_version": 2,
        "chunk": args.chunk,
        "title": chunks[args.chunk],
        "owner": args.owner,
        "state": "CLAIMED",
        "claimed_at": claimed_at,
        "updated_at": claimed_at,
        "touched_paths": touched_paths,
        "note": args.note or "",
    }
    path = claim_path(args.chunk)
    with claim_lock():
        conflict = active_path_conflict(args.chunk, touched_paths)
        if conflict:
            raise SystemExit(conflict)
        try:
            atomic_write_new(path, payload)
        except FileExistsError:
            existing = read_claim(path)
            raise SystemExit(
                f"chunk is not READY: {args.chunk}: "
                f"owner={existing.get('owner', '?') if existing else '?'} "
                f"state={existing.get('state', '?') if existing else '?'}"
            )
    print(f"CLAIMED {args.chunk} by {args.owner}")
    return 0


def cmd_heartbeat(args: argparse.Namespace) -> int:
    path = claim_path(args.chunk)
    claim = read_claim(path)
    if claim is None:
        raise SystemExit(f"chunk is not claimed: {args.chunk}")
    require_owner(claim, args.owner)
    if claim.get("state") != "CLAIMED":
        raise SystemExit(f"chunk is not active: state={claim.get('state')}")
    claim["updated_at"] = now_utc()
    if args.note is not None:
        claim["note"] = args.note
    replace_claim(path, claim)
    print(f"UPDATED {args.chunk} by {args.owner}")
    return 0


def cmd_done(args: argparse.Namespace) -> int:
    path = claim_path(args.chunk)
    claim = read_claim(path)
    if claim is None:
        raise SystemExit(f"chunk is not claimed: {args.chunk}")
    require_owner(claim, args.owner)
    if claim.get("state") != "CLAIMED":
        raise SystemExit(f"chunk is not active: state={claim.get('state')}")
    claim["state"] = "DONE"
    claim["updated_at"] = now_utc()
    claim["completed_at"] = claim["updated_at"]
    claim["summary"] = args.summary
    claim["verification"] = args.verification
    claim["follow_up"] = args.follow_up
    replace_claim(path, claim)
    print(f"DONE {args.chunk} by {args.owner}")
    return 0


def cmd_blocked(args: argparse.Namespace) -> int:
    path = claim_path(args.chunk)
    claim = read_claim(path)
    if claim is None:
        raise SystemExit(f"chunk is not claimed: {args.chunk}")
    require_owner(claim, args.owner)
    if claim.get("state") != "CLAIMED":
        raise SystemExit(f"chunk is not active: state={claim.get('state')}")
    claim["state"] = "BLOCKED"
    claim["updated_at"] = now_utc()
    claim["completed_at"] = claim["updated_at"]
    claim["reason"] = args.reason
    claim["verification"] = args.verification
    claim["follow_up"] = args.follow_up
    replace_claim(path, claim)
    print(f"BLOCKED {args.chunk} by {args.owner}")
    return 0


def cmd_validate(_: argparse.Namespace) -> int:
    chunks = load_catalog()
    errors: list[str] = []
    active: list[dict[str, Any]] = []
    for path in CLAIM_DIR.glob("CHUNK-*.json"):
        claim = read_claim(path)
        if not claim or claim.get("state") == "CORRUPT":
            errors.append(f"corrupt claim: {path.name}")
            continue
        if claim.get("chunk") not in chunks:
            errors.append(f"unknown chunk in {path.name}: {claim.get('chunk')}")
        if claim.get("state") not in {"CLAIMED", "DONE", "BLOCKED"}:
            errors.append(f"invalid state in {path.name}: {claim.get('state')}")
        if claim.get("schema_version", 1) >= 2:
            try:
                claim["touched_paths"] = normalize_touched_paths(list(claim.get("touched_paths", [])))
            except SystemExit as exc:
                errors.append(f"invalid paths in {path.name}: {exc}")
        if claim.get("state") == "CLAIMED":
            active.append(claim)
    for index, left in enumerate(active):
        for right in active[index + 1:]:
            for left_path in left.get("touched_paths", []):
                for right_path in right.get("touched_paths", []):
                    if paths_overlap(str(left_path), str(right_path)):
                        errors.append(
                            f"active path collision: {left.get('chunk')}:{left_path} "
                            f"vs {right.get('chunk')}:{right_path}"
                        )
    if errors:
        for error in errors:
            print(f"ERROR {error}", file=sys.stderr)
        return 1
    print(f"VALID chunk board: {len(chunks)} catalog chunks, {len(active)} active claims")
    return 0


def cmd_release(args: argparse.Namespace) -> int:
    path = claim_path(args.chunk)
    claim = read_claim(path)
    if claim is None:
        raise SystemExit(f"chunk is not claimed: {args.chunk}")
    require_owner(claim, args.owner)
    path.unlink()
    print(f"RELEASED {args.chunk} by {args.owner}")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    sub = result.add_subparsers(dest="command", required=True)
    sub.add_parser("list", help="show all chunks and live claim state").set_defaults(func=cmd_list)
    sub.add_parser("validate", help="validate catalog, claim states and active path overlap").set_defaults(func=cmd_validate)

    claim = sub.add_parser("claim", help="atomically claim one READY chunk")
    claim.add_argument("chunk")
    claim.add_argument("--owner", required=True)
    claim.add_argument("--paths", nargs="+", required=True,
                       help="concrete repository-relative files/directories to protect")
    claim.add_argument("--note")
    claim.set_defaults(func=cmd_claim)

    heartbeat = sub.add_parser("heartbeat", help="refresh an active claim")
    heartbeat.add_argument("chunk")
    heartbeat.add_argument("--owner", required=True)
    heartbeat.add_argument("--note")
    heartbeat.set_defaults(func=cmd_heartbeat)

    done = sub.add_parser("done", help="mark a claimed chunk DONE for control-tower review")
    done.add_argument("chunk")
    done.add_argument("--owner", required=True)
    done.add_argument("--summary", required=True)
    done.add_argument("--verification", required=True)
    done.add_argument("--follow-up", required=True,
                      help="follow-up chunk/issue, or 'none'")
    done.set_defaults(func=cmd_done)

    blocked = sub.add_parser("blocked", help="mark a claimed chunk BLOCKED with handoff evidence")
    blocked.add_argument("chunk")
    blocked.add_argument("--owner", required=True)
    blocked.add_argument("--reason", required=True)
    blocked.add_argument("--verification", required=True)
    blocked.add_argument("--follow-up", required=True)
    blocked.set_defaults(func=cmd_blocked)

    release = sub.add_parser("release", help="release a claim without marking it done")
    release.add_argument("chunk")
    release.add_argument("--owner", required=True)
    release.set_defaults(func=cmd_release)
    return result


def main() -> int:
    args = parser().parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    sys.exit(main())
