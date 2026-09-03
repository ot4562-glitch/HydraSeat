#!/usr/bin/env python3
"""Bounded, non-destructive worktree hygiene gate for HydraSeat integration.

The validator inspects Git status metadata, added lines from safe candidate text files,
and bounded untracked candidate text. It never deletes/moves files, follows repository
symlinks for content, or scans secret-like file contents. Ignored build state and the
intentional .agents coordination tree are informational rather than blockers.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import tempfile
from typing import Iterable, Sequence

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_PATH = ROOT / "tools" / "testdata" / "worktree_hygiene" / "cases.json"

MAX_GIT_OUTPUT_BYTES = 8 * 1024 * 1024
MAX_CANDIDATE_PATHS = 512
MAX_TEXT_BYTES = 256 * 1024
MAX_DUPLICATE_BYTES = 1024 * 1024

BLOCKER = "BLOCKER"
INFO = "INFO"

CAT_ROOT_ARTIFACT = "ROOT_BUILD_ARTIFACT"
CAT_GENERATED = "UNIGNORED_GENERATED_OUTPUT"
CAT_SCRATCH = "THROWAWAY_OUTPUT"
CAT_AI_BRIDGE = "AI_BRIDGE_PUBLICATION_CANDIDATE"
CAT_PERSONAL_PATH = "PERSONAL_ABSOLUTE_PATH"
CAT_DUPLICATE = "DUPLICATE_THROWAWAY_OUTPUT"
CAT_RELEASE = "UNTRACKED_RELEASE_PAYLOAD"
CAT_SENSITIVE_PATH = "SENSITIVE_PATH_CANDIDATE"
CAT_TOO_MANY = "CANDIDATE_SET_TOO_LARGE"
CAT_AUTHORITY = "GIT_METADATA_UNAVAILABLE"
CAT_INTERNAL_AGENTS = "INTERNAL_AGENTS_COORDINATION"
CAT_IGNORED_GENERATED = "IGNORED_GENERATED_STATE"
CAT_IGNORED_AI_BRIDGE = "IGNORED_AI_BRIDGE_STATE"
CAT_SKIPPED_CONTENT = "CONTENT_SCAN_SKIPPED"

ALL_CATEGORIES = {
    CAT_ROOT_ARTIFACT,
    CAT_GENERATED,
    CAT_SCRATCH,
    CAT_AI_BRIDGE,
    CAT_PERSONAL_PATH,
    CAT_DUPLICATE,
    CAT_RELEASE,
    CAT_SENSITIVE_PATH,
    CAT_TOO_MANY,
    CAT_AUTHORITY,
    CAT_INTERNAL_AGENTS,
    CAT_IGNORED_GENERATED,
    CAT_IGNORED_AI_BRIDGE,
    CAT_SKIPPED_CONTENT,
}

ROOT_ARTIFACT_SUFFIXES = {
    ".obj", ".o", ".exe", ".dll", ".sys", ".pdb", ".ilk", ".lib", ".a",
    ".zip", ".7z", ".rar", ".tar", ".tgz", ".gz", ".bz2", ".xz", ".zst",
    ".msi", ".msix", ".appx", ".cab", ".nupkg",
}
GENERATED_SUFFIXES = {
    ".vcxproj", ".filters", ".sln", ".user", ".obj", ".o", ".pdb", ".ilk",
    ".pch", ".tlog",
}
GENERATED_NAMES = {
    "CMakeCache.txt", "CTestTestfile.cmake", "cmake_install.cmake", "build.ninja",
    ".ninja_deps", ".ninja_log",
}
GENERATED_DIR_PARTS = {"CMakeFiles", ".vs", ".idea"}
GENERATED_TOP_LEVEL_PREFIXES = ("build", "out", "cmake-build")
THROWAWAY_SUFFIXES = {".tmp", ".temp", ".bak", ".orig", ".rej", ".log", ".dmp", ".dump", ".trace", ".etl"}
THROWAWAY_PART_RE = re.compile(
    r"(?i)^(?:scratch|tmp|temp|throwaway|test[-_]?output|debug[-_]?output|"
    r"evidence[-_]?(?:output|scratch)|acceptance[-_]?output|qa[-_]?output)(?:[-_.].*)?$"
)
RELEASE_TOP_LEVELS = {"release", "releases", "dist", "artifacts", "release-artifacts", "staged-release"}
BINARY_SUFFIXES = ROOT_ARTIFACT_SUFFIXES | GENERATED_SUFFIXES | {
    ".png", ".jpg", ".jpeg", ".gif", ".webp", ".ico", ".bmp", ".bin", ".dat",
}

WINDOWS_HOME_RE = re.compile(
    r"(?i)(?<![A-Za-z0-9_])[A-Za-z]:[\\/]+Users[\\/]+(?P<user>[^\\/\s\"'`<>]+)"
)
UNIX_HOME_RE = re.compile(r"(?<![A-Za-z0-9_])/(?:home|Users)/(?P<user>[^/\s\"'`<>]+)")
PLACEHOLDER_USERS = {
    "user", "users", "username", "name", "example", "example-user", "example_user",
    "yourname", "your-name", "your_name", "someone", "runner", "runneradmin",
    "alice", "bob", "testuser", "test-user", "private", "source", "소스",
}


@dataclass(frozen=True)
class StatusEntry:
    code: str
    path: str

    @property
    def ignored(self) -> bool:
        return self.code == "!!"

    @property
    def untracked(self) -> bool:
        return self.code == "??"

    @property
    def tracked_candidate(self) -> bool:
        return not self.ignored and not self.untracked


@dataclass(frozen=True)
class Finding:
    severity: str
    category: str
    path: str
    detail: str
    line: int = 0


class HygieneError(RuntimeError):
    pass


def _run_git(root: Path, arguments: Sequence[str], timeout_seconds: int = 20) -> bytes:
    try:
        completed = subprocess.run(
            ["git", "-C", os.fspath(root), *arguments],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_seconds,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise HygieneError(f"Git metadata inspection failed to start: {exc}") from exc
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace").strip()[:512]
        raise HygieneError(f"Git metadata inspection failed: {detail}")
    if len(completed.stdout) > MAX_GIT_OUTPUT_BYTES:
        raise HygieneError(
            f"Git metadata output exceeds bounded limit {MAX_GIT_OUTPUT_BYTES} bytes"
        )
    return completed.stdout


def _normalize_git_path(raw: bytes) -> str:
    try:
        text = raw.decode("utf-8", errors="strict").replace("\\", "/")
    except UnicodeDecodeError as exc:
        raise HygieneError("Git status returned a non-UTF-8 path") from exc
    pure = PurePosixPath(text)
    if (
        pure.is_absolute()
        or not pure.parts
        or any(part in {"", ".", ".."} for part in pure.parts)
        or "\n" in text
        or "\r" in text
        or "\0" in text
    ):
        raise HygieneError(f"Git status returned an unsafe path: {text!r}")
    return pure.as_posix()


def status_entries(root: Path) -> list[StatusEntry]:
    raw = _run_git(
        root,
        ["status", "--porcelain=v1", "-z", "--ignored=matching", "--untracked-files=all"],
    )
    records = raw.split(b"\0")
    entries: list[StatusEntry] = []
    index = 0
    while index < len(records):
        record = records[index]
        index += 1
        if not record:
            continue
        if len(record) < 4 or record[2:3] != b" ":
            raise HygieneError("Git porcelain status returned malformed metadata")
        try:
            code = record[:2].decode("ascii", errors="strict")
        except UnicodeDecodeError as exc:
            raise HygieneError("Git porcelain status returned a non-ASCII status code") from exc
        path = _normalize_git_path(record[3:])
        entries.append(StatusEntry(code, path))
        if "R" in code or "C" in code:
            if index >= len(records) or not records[index]:
                raise HygieneError("Git porcelain rename/copy metadata is incomplete")
            index += 1  # original path metadata is not a publication candidate
    return sorted(entries, key=lambda item: (item.path.casefold(), item.path, item.code))


def _finding(severity: str, category: str, path: str, detail: str, line: int = 0) -> Finding:
    if severity not in {BLOCKER, INFO} or category not in ALL_CATEGORIES:
        raise AssertionError("invalid worktree-hygiene finding")
    return Finding(severity, category, path, detail, line)


def _parts(path: str) -> tuple[str, ...]:
    return PurePosixPath(path).parts


def _suffix(path: str) -> str:
    lower = path.casefold()
    for compound in (".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst"):
        if lower.endswith(compound):
            return compound
    return PurePosixPath(lower).suffix


def _is_agents_path(path: str) -> bool:
    parts = _parts(path)
    return bool(parts) and parts[0].casefold() == ".agents"


def _is_ai_bridge_path(path: str) -> bool:
    return any(part.casefold() == ".ai-bridge" for part in _parts(path))


def _is_generated_path(path: str) -> bool:
    pure = PurePosixPath(path)
    if not pure.parts:
        return False
    first = pure.parts[0].casefold()
    if any(first == prefix or first.startswith(prefix + "-") for prefix in GENERATED_TOP_LEVEL_PREFIXES):
        return True
    if pure.name in GENERATED_NAMES:
        return True
    if any(part in GENERATED_DIR_PARTS for part in pure.parts):
        return True
    lower_name = pure.name.casefold()
    return any(lower_name.endswith(suffix) for suffix in GENERATED_SUFFIXES)


def _is_root_artifact(path: str) -> bool:
    pure = PurePosixPath(path)
    return len(pure.parts) == 1 and _suffix(path) in ROOT_ARTIFACT_SUFFIXES


def _is_throwaway_path(path: str) -> bool:
    pure = PurePosixPath(path)
    if _suffix(path) in THROWAWAY_SUFFIXES:
        return True
    return any(THROWAWAY_PART_RE.fullmatch(part) is not None for part in pure.parts)


def _is_release_payload(path: str) -> bool:
    parts = _parts(path)
    return bool(parts) and parts[0].casefold() in RELEASE_TOP_LEVELS


def _is_secret_like_path(path: str) -> bool:
    name = PurePosixPath(path).name.casefold()
    if name == ".env.example" or name.startswith(".env.example."):
        return False
    if name == ".env" or name.startswith(".env."):
        return True
    if name in {
        "id_rsa", "id_dsa", "id_ecdsa", "id_ed25519", "credentials.json",
        "secrets.json", "secrets.yaml", "secrets.yml", "service-account.json",
    }:
        return True
    if name.endswith((".pem", ".key", ".p12", ".pfx", ".jks", ".keystore")):
        return True
    return False


def _is_text_candidate(path: str) -> bool:
    if _is_agents_path(path) or _is_ai_bridge_path(path) or _is_secret_like_path(path):
        return False
    suffix = _suffix(path)
    if suffix in BINARY_SUFFIXES:
        return False
    return True


def _is_personal_user(segment: str) -> bool:
    normalized = segment.strip().rstrip(").,;:]}")
    lowered = normalized.casefold()
    if not lowered or lowered in PLACEHOLDER_USERS:
        return False
    return not any(marker in normalized for marker in ("<", ">", "$", "%", "{", "}"))


def personal_path_line(text: str) -> bool:
    for pattern in (WINDOWS_HOME_RE, UNIX_HOME_RE):
        for match in pattern.finditer(text):
            if _is_personal_user(match.group("user")):
                return True
    return False


def _classify_status(entry: StatusEntry) -> list[Finding]:
    path = entry.path
    findings: list[Finding] = []

    if _is_agents_path(path):
        findings.append(
            _finding(INFO, CAT_INTERNAL_AGENTS, path, "intentional .agents coordination state is internal/control-tower material")
        )
        return findings

    if _is_ai_bridge_path(path):
        if entry.ignored:
            findings.append(
                _finding(INFO, CAT_IGNORED_AI_BRIDGE, path, "ignored .ai-bridge coordination state is not a publication candidate")
            )
        else:
            findings.append(
                _finding(BLOCKER, CAT_AI_BRIDGE, path, ".ai-bridge content is unignored/tracked and could enter publication")
            )
        return findings

    if entry.ignored:
        if _is_generated_path(path):
            findings.append(
                _finding(INFO, CAT_IGNORED_GENERATED, path, "generated build/project state is ignored and informational")
            )
        return findings

    if _is_secret_like_path(path):
        findings.append(
            _finding(BLOCKER, CAT_SENSITIVE_PATH, path, "secret-like candidate path detected; file content was not inspected")
        )
    if _is_root_artifact(path):
        findings.append(
            _finding(BLOCKER, CAT_ROOT_ARTIFACT, path, "root object/binary/archive output is an integration hygiene blocker")
        )
    elif _is_generated_path(path):
        findings.append(
            _finding(BLOCKER, CAT_GENERATED, path, "generated IDE/build output is not ignored")
        )
    if _is_throwaway_path(path):
        findings.append(
            _finding(BLOCKER, CAT_SCRATCH, path, "throwaway scratch/test output is an unignored candidate")
        )
    if entry.untracked and _is_release_payload(path):
        findings.append(
            _finding(BLOCKER, CAT_RELEASE, path, "untracked release/dist/artifact payload requires explicit integration handling")
        )
    return findings


def _safe_candidate_paths(entries: Iterable[StatusEntry]) -> list[str]:
    paths = {
        entry.path
        for entry in entries
        if not entry.ignored and _is_text_candidate(entry.path)
    }
    return sorted(paths, key=lambda value: (value.casefold(), value))


def _parse_added_lines(patch: bytes, allowed_paths: set[str]) -> list[tuple[str, int, str]]:
    try:
        text = patch.decode("utf-8", errors="replace")
    except UnicodeError as exc:
        raise HygieneError(f"candidate diff could not be decoded: {exc}") from exc
    current_path = ""
    line_number = 0
    output: list[tuple[str, int, str]] = []
    for line in text.splitlines():
        if line.startswith("+++ "):
            value = line[4:]
            if value == "/dev/null":
                current_path = ""
            elif value.startswith("b/"):
                candidate = value[2:].replace("\\", "/")
                current_path = candidate if candidate in allowed_paths else ""
            else:
                current_path = ""
            continue
        if line.startswith("@@"):
            match = re.search(r"\+(\d+)(?:,(\d+))?", line)
            line_number = int(match.group(1)) if match else 0
            continue
        if not current_path:
            continue
        if line.startswith("+") and not line.startswith("+++"):
            output.append((current_path, line_number, line[1:]))
            line_number += 1
        elif line.startswith(" "):
            line_number += 1
    return output


def changed_added_lines(root: Path, entries: Sequence[StatusEntry]) -> list[tuple[str, int, str]]:
    paths = [
        path
        for path in _safe_candidate_paths(entries)
        if any(entry.path == path and entry.tracked_candidate for entry in entries)
    ]
    if not paths:
        return []
    if len(paths) > MAX_CANDIDATE_PATHS:
        raise HygieneError(f"tracked candidate path count exceeds {MAX_CANDIDATE_PATHS}")
    common = [
        "-c", "core.quotePath=false", "diff", "--no-ext-diff", "--no-textconv",
        "--unified=0", "--no-renames",
    ]
    unstaged = _run_git(root, [*common, "--", *paths])
    staged = _run_git(root, [*common, "--cached", "--", *paths])
    allowed = set(paths)
    return _parse_added_lines(staged, allowed) + _parse_added_lines(unstaged, allowed)


def _safe_repo_file(root: Path, relative: str) -> Path | None:
    path = root.joinpath(*PurePosixPath(relative).parts)
    try:
        info = path.lstat()
    except OSError:
        return None
    if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
        return None
    try:
        resolved = path.resolve(strict=True)
        resolved.relative_to(root.resolve(strict=True))
    except (OSError, ValueError):
        return None
    return resolved


def _read_untracked_text(root: Path, path: str) -> tuple[str | None, str | None]:
    resolved = _safe_repo_file(root, path)
    if resolved is None:
        return None, "not a regular in-repository file or path resolution is unsafe"
    try:
        size = resolved.stat().st_size
    except OSError:
        return None, "cannot stat candidate text file"
    if size > MAX_TEXT_BYTES:
        return None, f"candidate text exceeds {MAX_TEXT_BYTES} byte content-scan bound"
    try:
        data = resolved.read_bytes()
    except OSError:
        return None, "cannot read bounded candidate text file"
    if b"\0" in data[:65536]:
        return None, "candidate has binary/NUL content"
    try:
        return data.decode("utf-8", errors="strict"), None
    except UnicodeDecodeError:
        return None, "candidate is not UTF-8 text"


def _hash_throwaway(root: Path, path: str) -> str | None:
    resolved = _safe_repo_file(root, path)
    if resolved is None:
        return None
    try:
        size = resolved.stat().st_size
        if size > MAX_DUPLICATE_BYTES:
            return None
        return hashlib.sha256(resolved.read_bytes()).hexdigest()
    except OSError:
        return None


def scan_worktree(root: Path) -> list[Finding]:
    root = root.resolve()
    entries = status_entries(root)
    nonignored = [entry for entry in entries if not entry.ignored]
    findings: list[Finding] = []

    if len(nonignored) > MAX_CANDIDATE_PATHS:
        findings.append(
            _finding(
                BLOCKER,
                CAT_TOO_MANY,
                "<worktree>",
                f"nonignored candidate path count {len(nonignored)} exceeds {MAX_CANDIDATE_PATHS}",
            )
        )
        return findings

    for entry in entries:
        findings.extend(_classify_status(entry))

    personal_paths: set[str] = set()
    for path, line, text in changed_added_lines(root, entries):
        if personal_path_line(text) and path not in personal_paths:
            personal_paths.add(path)
            findings.append(
                _finding(
                    BLOCKER,
                    CAT_PERSONAL_PATH,
                    path,
                    "candidate added line contains a literal personal user-home absolute path",
                    line,
                )
            )

    throwaway_hashes: dict[str, list[str]] = {}
    for entry in entries:
        if not entry.untracked or entry.ignored:
            continue
        path = entry.path
        if _is_agents_path(path) or _is_ai_bridge_path(path) or _is_secret_like_path(path):
            continue
        if _is_text_candidate(path):
            text, reason = _read_untracked_text(root, path)
            if text is None:
                if reason:
                    findings.append(_finding(INFO, CAT_SKIPPED_CONTENT, path, reason))
            elif personal_path_line(text) and path not in personal_paths:
                personal_paths.add(path)
                findings.append(
                    _finding(
                        BLOCKER,
                        CAT_PERSONAL_PATH,
                        path,
                        "untracked candidate text contains a literal personal user-home absolute path",
                    )
                )
        if _is_throwaway_path(path):
            digest = _hash_throwaway(root, path)
            if digest is not None:
                throwaway_hashes.setdefault(digest, []).append(path)

    for paths in throwaway_hashes.values():
        if len(paths) < 2:
            continue
        ordered = sorted(paths, key=lambda value: (value.casefold(), value))
        for path in ordered:
            findings.append(
                _finding(
                    BLOCKER,
                    CAT_DUPLICATE,
                    path,
                    "duplicate untracked throwaway output has identical bounded content: "
                    + ", ".join(ordered),
                )
            )

    unique = {(item.severity, item.category, item.path, item.detail, item.line): item for item in findings}
    return sorted(
        unique.values(),
        key=lambda item: (
            0 if item.severity == BLOCKER else 1,
            item.category,
            item.path.casefold(),
            item.path,
            item.line,
            item.detail,
        ),
    )


def blocker_findings(findings: Sequence[Finding]) -> list[Finding]:
    return [finding for finding in findings if finding.severity == BLOCKER]


def exit_code(findings: Sequence[Finding]) -> int:
    return 1 if blocker_findings(findings) else 0


def _fixture_content(kind: str | None, content: str | None) -> str:
    if kind is None:
        return content or ""
    if content is not None:
        raise AssertionError("fixture content and contentKind are mutually exclusive")
    if kind == "personal-windows":
        return "cache = " + "C:" + "/Users/" + "LocalDev3927" + "/AppData/Local/HydraSeat/cache\n"
    if kind == "personal-unix":
        return "cache = " + "/home/" + "localdev3927" + "/.cache/hydraseat\n"
    raise AssertionError(f"unknown fixture contentKind: {kind}")


def _fixture_git(root: Path, *arguments: str) -> None:
    try:
        completed = subprocess.run(
            ["git", "-C", os.fspath(root), *arguments],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=20,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise AssertionError(f"fixture Git command could not run: {exc}") from exc
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace").strip()
        raise AssertionError(f"fixture Git command failed {arguments!r}: {detail}")


def _fixture_write(root: Path, record: dict, baseline: bool = False) -> None:
    if not isinstance(record, dict) or set(record) != {"path", "state", "content", "contentKind", "baseline"}:
        raise AssertionError("fixture file record fields are invalid")
    path_value = record["path"]
    if not isinstance(path_value, str) or not path_value:
        raise AssertionError("fixture path is invalid")
    pure = PurePosixPath(path_value)
    if pure.is_absolute() or any(part in {"", ".", ".."} for part in pure.parts):
        raise AssertionError("fixture path is unsafe")
    target = root.joinpath(*pure.parts)
    target.parent.mkdir(parents=True, exist_ok=True)
    if baseline:
        text = record["baseline"] if isinstance(record["baseline"], str) else "baseline\n"
    else:
        text = _fixture_content(record["contentKind"], record["content"])
    target.write_text(text, encoding="utf-8")


def self_test() -> None:
    try:
        document = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise AssertionError(f"cannot load worktree-hygiene fixtures: {exc}") from exc
    if (
        not isinstance(document, dict)
        or document.get("schemaVersion") != 1
        or not isinstance(document.get("cases"), list)
    ):
        raise AssertionError("worktree-hygiene fixture schema is invalid")

    seen: set[str] = set()
    for case in document["cases"]:
        if not isinstance(case, dict) or set(case) != {
            "name", "gitignore", "files", "expectedBlockers", "expectedInfo",
        }:
            raise AssertionError("worktree-hygiene fixture case fields are invalid")
        name = case["name"]
        if not isinstance(name, str) or not name or name in seen:
            raise AssertionError("worktree-hygiene fixture case name is invalid/duplicate")
        seen.add(name)
        if not isinstance(case["gitignore"], list) or any(not isinstance(item, str) for item in case["gitignore"]):
            raise AssertionError(f"fixture {name}: gitignore must be a string array")
        if not isinstance(case["files"], list):
            raise AssertionError(f"fixture {name}: files must be an array")
        for expected_key in ("expectedBlockers", "expectedInfo"):
            expected = case[expected_key]
            if not isinstance(expected, list) or any(item not in ALL_CATEGORIES for item in expected):
                raise AssertionError(f"fixture {name}: {expected_key} is invalid")

        with tempfile.TemporaryDirectory(prefix="hydraseat-worktree-hygiene-") as temporary:
            root = Path(temporary) / "repo"
            root.mkdir()
            _fixture_git(root, "init", "-q")
            _fixture_git(root, "config", "user.email", "fixture@example.invalid")
            _fixture_git(root, "config", "user.name", "HydraSeat Fixture")
            gitignore_text = "\n".join(case["gitignore"])
            if gitignore_text:
                gitignore_text += "\n"
            (root / ".gitignore").write_text(gitignore_text, encoding="utf-8")
            (root / "baseline.txt").write_text("baseline\n", encoding="utf-8")
            _fixture_git(root, "add", ".gitignore", "baseline.txt")
            _fixture_git(root, "commit", "-q", "-m", "baseline")

            for record in case["files"]:
                state = record.get("state")
                if state == "modified":
                    _fixture_write(root, record, baseline=True)
                    _fixture_git(root, "add", "--", record["path"])
                    _fixture_git(root, "commit", "-q", "-m", "fixture baseline")
                    _fixture_write(root, record, baseline=False)
                elif state == "staged":
                    _fixture_write(root, record, baseline=False)
                    _fixture_git(root, "add", "--", record["path"])
                elif state in {"untracked", "ignored"}:
                    _fixture_write(root, record, baseline=False)
                else:
                    raise AssertionError(f"fixture {name}: unsupported state {state!r}")

            findings = scan_worktree(root)
            blockers = sorted({item.category for item in findings if item.severity == BLOCKER})
            info = sorted({item.category for item in findings if item.severity == INFO})
            if blockers != sorted(case["expectedBlockers"]):
                raise AssertionError(
                    f"fixture {name}: blocker mismatch expected={sorted(case['expectedBlockers'])!r} "
                    f"observed={blockers!r}; findings={[asdict(item) for item in findings]!r}"
                )
            if info != sorted(case["expectedInfo"]):
                raise AssertionError(
                    f"fixture {name}: info mismatch expected={sorted(case['expectedInfo'])!r} "
                    f"observed={info!r}; findings={[asdict(item) for item in findings]!r}"
                )
            expected_exit = 1 if case["expectedBlockers"] else 0
            if exit_code(findings) != expected_exit:
                raise AssertionError(f"fixture {name}: exit status does not match blocker presence")

    print(f"Worktree hygiene self-test passed: {len(seen)} deterministic dirty-worktree cases.")


def render_text(findings: Sequence[Finding]) -> str:
    blockers = blocker_findings(findings)
    info_count = sum(item.severity == INFO for item in findings)
    lines = [
        "HydraSeat worktree hygiene: " + ("BLOCKED" if blockers else "PASS")
        + f"; blockers={len(blockers)}; info={info_count}"
    ]
    if blockers:
        lines.append("Blocker categories: " + ", ".join(sorted({item.category for item in blockers})))
    for item in findings:
        location = item.path + (f":{item.line}" if item.line else "")
        lines.append(f"  [{item.severity}/{item.category}] {location}: {item.detail}")
    lines.append(
        "WORKTREE_HYGIENE_SUMMARY "
        + ("BLOCKED" if blockers else "PASS")
        + f" blockers={len(blockers)} info={info_count}"
    )
    return "\n".join(lines)


def render_json(findings: Sequence[Finding]) -> str:
    blockers = blocker_findings(findings)
    document = {
        "schemaVersion": 1,
        "status": "BLOCKED" if blockers else "PASS",
        "blockerCategories": sorted({item.category for item in blockers}),
        "blockerCount": len(blockers),
        "infoCount": sum(item.severity == INFO for item in findings),
        "findings": [asdict(item) for item in findings],
    }
    return json.dumps(document, sort_keys=True, indent=2)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (default: this checkout)")
    parser.add_argument("--json", action="store_true", help="emit deterministic JSON")
    parser.add_argument("--self-test", action="store_true", help="run deterministic dirty-worktree fixtures")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.self_test:
            self_test()
            return 0
        findings = scan_worktree(args.root)
    except (HygieneError, OSError, subprocess.SubprocessError) as exc:
        findings = [
            _finding(BLOCKER, CAT_AUTHORITY, "<worktree>", str(exc))
        ]
    if args.json:
        print(render_json(findings))
    else:
        print(render_text(findings))
    return exit_code(findings)


if __name__ == "__main__":
    raise SystemExit(main())
