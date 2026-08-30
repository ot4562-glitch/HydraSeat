#!/usr/bin/env python3
"""Bounded Controlled soak runner for HydraSeat process/window lifecycle tests.

The runner executes only the existing fixed process_group/window_tracker/window_policy
controlled test executables found under explicitly supplied build directories. It never
searches for or terminates unrelated processes. If a test exceeds its per-test/global
budget, only the exact test process started by this runner is terminated; descendant
cleanup remains the responsibility of the controlled test's existing Job/teardown path.

This is developer-machine Controlled evidence only. It is not real-game, physical
hardware, installer, or release-validation evidence.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import threading
import time
from dataclasses import asdict, dataclass
from typing import BinaryIO, Iterable

SCHEMA_VERSION = 1
EVIDENCE_CLASS = "Controlled"
EVIDENCE_SCOPE = (
    "developer-machine controlled process/window lifecycle tests; "
    "not real-game or physical evidence"
)
TEST_STEMS = (
    "process_group_tests",
    "window_tracker_tests",
    "window_policy_tests",
)
MAX_ITERATIONS = 100
MAX_TEST_TIMEOUT_SECONDS = 300
MAX_TOTAL_SECONDS = 7200
MAX_CAPTURE_BYTES = 64 * 1024
MAX_FAILURE_LINES = 8
MAX_FAILURE_LINE_CHARS = 512
DRAIN_CHUNK_BYTES = 4096
DRAIN_JOIN_SECONDS = 2.0
KILL_WAIT_SECONDS = 5.0


class SoakError(RuntimeError):
    pass


@dataclass(frozen=True)
class SignalSummary:
    timeout: bool = False
    orphan_or_teardown: bool = False
    pid_reuse_or_identity: bool = False
    handoff_or_descendant: bool = False
    hwnd_or_reacquisition: bool = False


@dataclass(frozen=True)
class TestRun:
    build: str
    iteration: int
    test: str
    status: str
    exit_code: int | None
    duration_ms: int
    capture_truncated: bool
    failure_lines: tuple[str, ...]
    signals: SignalSummary


class BoundedCollector:
    def __init__(self, stream: BinaryIO) -> None:
        self._stream = stream
        self._data = bytearray()
        self.truncated = False
        self._thread = threading.Thread(target=self._drain, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def join(self) -> None:
        self._thread.join(DRAIN_JOIN_SECONDS)
        if self._thread.is_alive():
            self.truncated = True

    def text(self) -> str:
        return bytes(self._data).decode("utf-8", errors="replace")

    def _drain(self) -> None:
        try:
            while True:
                chunk = self._stream.read(DRAIN_CHUNK_BYTES)
                if not chunk:
                    return
                remaining = MAX_CAPTURE_BYTES - len(self._data)
                if remaining > 0:
                    self._data.extend(chunk[:remaining])
                if len(chunk) > remaining:
                    self.truncated = True
        except (OSError, ValueError):
            self.truncated = True


def repository_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[1]


def _normal(path: pathlib.Path) -> str:
    return os.path.normcase(os.path.abspath(os.fspath(path)))


def _is_under(root: pathlib.Path, path: pathlib.Path) -> bool:
    try:
        return os.path.commonpath([_normal(root), _normal(path)]) == _normal(root)
    except ValueError:
        return False


def display_build_path(root: pathlib.Path, build_dir: pathlib.Path) -> str:
    resolved_root = root.resolve()
    resolved_build = build_dir.resolve()
    if _is_under(resolved_root, resolved_build):
        return resolved_build.relative_to(resolved_root).as_posix()
    return f"<external-build>/{resolved_build.name}"


def resolve_build_dir(root: pathlib.Path, raw: str) -> pathlib.Path:
    candidate = pathlib.Path(raw)
    if not candidate.is_absolute():
        candidate = root / candidate
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as exc:
        raise SoakError(f"build directory does not exist: {raw}") from exc
    if not resolved.is_dir():
        raise SoakError(f"build path is not a directory: {raw}")
    return resolved


def resolve_test_executable(build_dir: pathlib.Path, stem: str) -> pathlib.Path:
    if stem not in TEST_STEMS:
        raise SoakError(f"unexpected test executable requested: {stem}")
    candidates = (
        build_dir / "Release" / f"{stem}.exe",
        build_dir / f"{stem}.exe",
        build_dir / "Release" / stem,
        build_dir / stem,
    )
    for candidate in candidates:
        try:
            resolved = candidate.resolve(strict=True)
        except OSError:
            continue
        if not resolved.is_file():
            continue
        if not _is_under(build_dir, resolved):
            raise SoakError(
                f"refusing test executable that resolves outside its build directory: {stem}"
            )
        return resolved
    raise SoakError(
        f"missing {stem} under {display_build_path(repository_root(), build_dir)} "
        "(expected Release/<name>.exe or <name>.exe)"
    )


def resolve_build_matrix(root: pathlib.Path, raw_build_dirs: Iterable[str]) -> list[tuple[str, dict[str, pathlib.Path]]]:
    resolved_dirs: list[pathlib.Path] = []
    seen: set[str] = set()
    for raw in raw_build_dirs:
        build_dir = resolve_build_dir(root, raw)
        key = _normal(build_dir)
        if key in seen:
            raise SoakError(f"duplicate build directory: {display_build_path(root, build_dir)}")
        seen.add(key)
        resolved_dirs.append(build_dir)

    resolved_dirs.sort(key=lambda path: display_build_path(root, path).casefold())
    matrix: list[tuple[str, dict[str, pathlib.Path]]] = []
    for build_dir in resolved_dirs:
        label = display_build_path(root, build_dir)
        executables = {stem: resolve_test_executable(build_dir, stem) for stem in TEST_STEMS}
        matrix.append((label, executables))
    return matrix


def bounded_failure_lines(stdout: str, stderr: str) -> tuple[str, ...]:
    result: list[str] = []
    for raw_line in (stderr + "\n" + stdout).splitlines():
        line = raw_line.strip()
        if not line:
            continue
        lowered = line.casefold()
        if (
            line.startswith("FAIL:")
            or " test(s) failed" in lowered
            or "tests failed" in lowered
            or "timed out" in lowered
            or "timeout" in lowered
        ):
            result.append(line[:MAX_FAILURE_LINE_CHARS])
            if len(result) >= MAX_FAILURE_LINES:
                break
    return tuple(result)


def classify_signals(status: str, timed_out: bool, stdout: str, stderr: str) -> SignalSummary:
    if status == "Pass":
        return SignalSummary()
    text = (stderr + "\n" + stdout).casefold()
    return SignalSummary(
        timeout=timed_out or bool(re.search(r"\btime(?:d)?\s*out\b|\btimeout\b", text)),
        orphan_or_teardown=bool(
            re.search(r"orphan|\bleak\b|still[- ]running|running owned|cleanup|clean[- ]?up|waitforempty", text)
        ),
        pid_reuse_or_identity=bool(
            re.search(r"pid reuse|same pid|creation identity|creation-time|exact identity|stale/pid", text)
        ),
        handoff_or_descendant=bool(re.search(r"handoff|launcher|loader|descendant", text)),
        hwnd_or_reacquisition=bool(
            re.search(r"hwnd|window|reacquir|recreat|tracker generation|binding generation", text)
        ),
    )


def classify_run(
    *,
    build: str,
    iteration: int,
    test: str,
    exit_code: int | None,
    timed_out: bool,
    duration_ms: int,
    stdout: str,
    stderr: str,
    capture_truncated: bool,
) -> TestRun:
    if timed_out:
        status = "Timeout"
    elif exit_code == 0:
        status = "Pass"
    else:
        status = "Fail"
    return TestRun(
        build=build,
        iteration=iteration,
        test=test,
        status=status,
        exit_code=exit_code,
        duration_ms=duration_ms,
        capture_truncated=capture_truncated,
        failure_lines=bounded_failure_lines(stdout, stderr),
        signals=classify_signals(status, timed_out, stdout, stderr),
    )


def run_one(
    *,
    build: str,
    iteration: int,
    stem: str,
    executable: pathlib.Path,
    timeout_seconds: float,
) -> TestRun:
    started = time.monotonic()
    try:
        process = subprocess.Popen(
            [os.fspath(executable)],
            cwd=os.fspath(executable.parent),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as exc:
        raise SoakError(f"unable to start fixed controlled test {stem}: {exc}") from exc

    if process.stdout is None or process.stderr is None:
        try:
            process.kill()
        except OSError:
            pass
        raise SoakError(f"unable to capture fixed controlled test output: {stem}")

    stdout_collector = BoundedCollector(process.stdout)
    stderr_collector = BoundedCollector(process.stderr)
    stdout_collector.start()
    stderr_collector.start()

    timed_out = False
    try:
        exit_code = process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        timed_out = True
        try:
            # Safety boundary: terminate only the exact test process this runner spawned.
            # Never enumerate or terminate descendants/by-name processes here.
            process.kill()
            exit_code = process.wait(timeout=KILL_WAIT_SECONDS)
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise SoakError(
                f"timed-out controlled test could not be stopped cleanly: {stem}"
            ) from exc
    except KeyboardInterrupt as exc:
        try:
            # Interactive cancellation follows the same ownership boundary as timeout:
            # stop only the exact controlled test process created by this runner.
            process.kill()
            process.wait(timeout=KILL_WAIT_SECONDS)
        except (OSError, subprocess.TimeoutExpired) as stop_exc:
            raise SoakError(
                f"interrupted controlled test could not be stopped cleanly: {stem}"
            ) from stop_exc
        raise KeyboardInterrupt from exc
    finally:
        stdout_collector.join()
        stderr_collector.join()
        try:
            process.stdout.close()
            process.stderr.close()
        except OSError:
            pass

    duration_ms = max(0, int(round((time.monotonic() - started) * 1000.0)))
    return classify_run(
        build=build,
        iteration=iteration,
        test=stem,
        exit_code=exit_code,
        timed_out=timed_out,
        duration_ms=duration_ms,
        stdout=stdout_collector.text(),
        stderr=stderr_collector.text(),
        capture_truncated=stdout_collector.truncated or stderr_collector.truncated,
    )


def run_to_json(run: TestRun) -> dict[str, object]:
    value = asdict(run)
    value["failure_lines"] = list(run.failure_lines)
    return value


def summarize(
    *,
    iterations: int,
    timeout_seconds: int,
    max_total_seconds: int,
    started: float,
    runs: list[TestRun],
    budget_exhausted: bool,
) -> dict[str, object]:
    passed = sum(run.status == "Pass" for run in runs)
    failed = sum(run.status == "Fail" for run in runs)
    timed_out = sum(run.status == "Timeout" for run in runs)
    first_problem = next((run for run in runs if run.status != "Pass"), None)
    completed_iterations = 0
    if runs:
        builds = {run.build for run in runs}
        for iteration in range(1, iterations + 1):
            expected = len(builds) * len(TEST_STEMS)
            observed = sum(run.iteration == iteration for run in runs)
            if observed == expected and all(
                run.status == "Pass" for run in runs if run.iteration == iteration
            ):
                completed_iterations = iteration
            else:
                break

    status = "Pass"
    if first_problem is not None:
        status = first_problem.status
    elif budget_exhausted:
        status = "BudgetExceeded"

    total_duration_ms = max(0, int(round((time.monotonic() - started) * 1000.0)))
    return {
        "schema_version": SCHEMA_VERSION,
        "evidence_class": EVIDENCE_CLASS,
        "evidence_scope": EVIDENCE_SCOPE,
        "status": status,
        "iterations_requested": iterations,
        "iterations_completed": completed_iterations,
        "per_test_timeout_seconds": timeout_seconds,
        "max_total_seconds": max_total_seconds,
        "total_duration_ms": total_duration_ms,
        "counts": {
            "pass": passed,
            "fail": failed,
            "timeout": timed_out,
            "executed": len(runs),
        },
        "first_problem": None if first_problem is None else run_to_json(first_problem),
        "runs": [run_to_json(run) for run in runs],
    }


def run_soak(args: argparse.Namespace) -> int:
    if args.iterations < 1 or args.iterations > MAX_ITERATIONS:
        raise SoakError(f"iterations must be between 1 and {MAX_ITERATIONS}")
    if args.timeout_seconds < 1 or args.timeout_seconds > MAX_TEST_TIMEOUT_SECONDS:
        raise SoakError(
            f"timeout-seconds must be between 1 and {MAX_TEST_TIMEOUT_SECONDS}"
        )
    if args.max_total_seconds < 1 or args.max_total_seconds > MAX_TOTAL_SECONDS:
        raise SoakError(
            f"max-total-seconds must be between 1 and {MAX_TOTAL_SECONDS}"
        )

    root = repository_root()
    matrix = resolve_build_matrix(root, args.build_dir)
    if args.list_only:
        payload = {
            "schema_version": SCHEMA_VERSION,
            "evidence_class": EVIDENCE_CLASS,
            "builds": [
                {
                    "build": label,
                    "tests": [
                        {"test": stem, "file": executable.name}
                        for stem, executable in executables.items()
                    ],
                }
                for label, executables in matrix
            ],
        }
        print(json.dumps(payload, sort_keys=True, separators=(",", ":")))
        return 0

    started = time.monotonic()
    deadline = started + float(args.max_total_seconds)
    runs: list[TestRun] = []
    budget_exhausted = False

    for iteration in range(1, args.iterations + 1):
        for build, executables in matrix:
            for stem in TEST_STEMS:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    budget_exhausted = True
                    summary = summarize(
                        iterations=args.iterations,
                        timeout_seconds=args.timeout_seconds,
                        max_total_seconds=args.max_total_seconds,
                        started=started,
                        runs=runs,
                        budget_exhausted=True,
                    )
                    print(json.dumps(summary, sort_keys=True, separators=(",", ":")))
                    return 1
                effective_timeout = min(float(args.timeout_seconds), remaining)
                run = run_one(
                    build=build,
                    iteration=iteration,
                    stem=stem,
                    executable=executables[stem],
                    timeout_seconds=effective_timeout,
                )
                runs.append(run)
                if not args.json_only:
                    print(
                        f"[{run.status}] build={run.build} iteration={run.iteration} "
                        f"test={run.test} duration_ms={run.duration_ms} exit={run.exit_code}",
                        flush=True,
                    )
                if run.status != "Pass":
                    summary = summarize(
                        iterations=args.iterations,
                        timeout_seconds=args.timeout_seconds,
                        max_total_seconds=args.max_total_seconds,
                        started=started,
                        runs=runs,
                        budget_exhausted=False,
                    )
                    print(json.dumps(summary, sort_keys=True, separators=(",", ":")))
                    return 1

    summary = summarize(
        iterations=args.iterations,
        timeout_seconds=args.timeout_seconds,
        max_total_seconds=args.max_total_seconds,
        started=started,
        runs=runs,
        budget_exhausted=budget_exhausted,
    )
    print(json.dumps(summary, sort_keys=True, separators=(",", ":")))
    return 0


def self_test_fixture_path() -> pathlib.Path:
    return repository_root() / "tools" / "testdata" / "process_window_soak" / "classifier_cases.json"


def run_self_test() -> int:
    fixture = self_test_fixture_path()
    try:
        payload = json.loads(fixture.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise SoakError(f"unable to read self-test fixture: {exc}") from exc
    if not isinstance(payload, dict) or set(payload) != {"schema_version", "cases"}:
        raise SoakError("self-test fixture has unknown or missing top-level fields")
    if payload["schema_version"] != SCHEMA_VERSION or not isinstance(payload["cases"], list):
        raise SoakError("self-test fixture schema is invalid")

    failures: list[str] = []
    for case in payload["cases"]:
        required = {
            "name",
            "exit_code",
            "timed_out",
            "stdout",
            "stderr",
            "expected_status",
            "expected_signals",
        }
        if not isinstance(case, dict) or set(case) != required:
            raise SoakError("self-test case has unknown or missing fields")
        run = classify_run(
            build="fixture",
            iteration=1,
            test="process_group_tests",
            exit_code=case["exit_code"],
            timed_out=case["timed_out"],
            duration_ms=1,
            stdout=case["stdout"],
            stderr=case["stderr"],
            capture_truncated=False,
        )
        actual_signals = asdict(run.signals)
        if run.status != case["expected_status"] or actual_signals != case["expected_signals"]:
            failures.append(str(case["name"]))

    result = {
        "schema_version": SCHEMA_VERSION,
        "status": "Pass" if not failures else "Fail",
        "cases": len(payload["cases"]),
        "failed_cases": failures,
    }
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0 if not failures else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Run a bounded Controlled soak of existing HydraSeat process/window tests. "
            "No unrelated process discovery or cleanup is performed."
        )
    )
    parser.add_argument(
        "--build-dir",
        action="append",
        default=[],
        help=(
            "build directory containing Release/process_group_tests.exe, "
            "window_tracker_tests.exe and window_policy_tests.exe; repeat for x64/x86"
        ),
    )
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--timeout-seconds", type=int, default=45)
    parser.add_argument("--max-total-seconds", type=int, default=900)
    parser.add_argument("--json-only", action="store_true")
    parser.add_argument(
        "--list-only",
        action="store_true",
        help="validate and print the fixed executable matrix without running tests",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="validate deterministic output classification against bounded fixtures",
    )
    return parser


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.self_test:
            if args.build_dir:
                raise SoakError("--self-test does not accept --build-dir")
            return run_self_test()
        if not args.build_dir:
            raise SoakError("at least one --build-dir is required")
        return run_soak(args)
    except SoakError as exc:
        print(
            json.dumps(
                {
                    "schema_version": SCHEMA_VERSION,
                    "evidence_class": EVIDENCE_CLASS,
                    "status": "Rejected",
                    "message": str(exc),
                },
                sort_keys=True,
                separators=(",", ":"),
            ),
            file=sys.stderr,
        )
        return 2
    except KeyboardInterrupt:
        print(
            json.dumps(
                {
                    "schema_version": SCHEMA_VERSION,
                    "evidence_class": EVIDENCE_CLASS,
                    "status": "Interrupted",
                },
                sort_keys=True,
                separators=(",", ":"),
            ),
            file=sys.stderr,
        )
        return 130


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
