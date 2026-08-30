#!/usr/bin/env python3
"""Fail-closed release-facing QA contract for the native HydraSeat launcher.

The default validation proves that the reviewed automated coverage still exists in the
repository and that manual/visual evidence is not promoted.  Passing focused CTests is
an additional execution step; use --ctest-dir to run the exact registered tests.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
from typing import Any

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "tools" / "testdata" / "launcher_release_readiness"
DEFAULT_MATRIX = FIXTURE_DIR / "expected.json"
DEFAULT_REPORT = ROOT / "docs" / "qa" / "LAUNCHER_RELEASE_QA.md"

EXPECTED_LOCALES = ["en-US", "ko-KR", "zh-CN"]
EXPECTED_DPI = [96, 120, 144, 192]
EXPECTED_WINDOWS = ["standard-980x720", "narrow-640x640"]
EXPECTED_AUTOMATED = {
    "localization-contract": {
        "test": "UiLocalizationTests",
        "proves": [
            "three-locale-catalog",
            "english-fallback",
            "critical-label-width-dpi-scaling",
            "single-owner-drawn-status-marker",
        ],
    },
    "layout-accessibility-contract": {
        "test": "UiAccessibilityTests",
        "proves": [
            "three-locale-layout-matrix",
            "dpi-96-120-144-192",
            "standard-and-narrow-reflow",
            "game-list-first-focus-order",
            "long-blocking-reason-visible",
            "high-contrast-system-color-policy",
        ],
    },
    "disabled-readiness-contract": {
        "test": "LauncherUiModelTests",
        "proves": [
            "missing-requirement-blocks-play",
            "validated-plan-required-for-activation",
        ],
    },
}
EXPECTED_MANUAL_IDS = [
    "real-first-window",
    "real-screenshots-standard-and-narrow",
    "real-keyboard-only-navigation",
    "real-high-contrast-visual",
    "real-korean-chinese-glyph-rendering",
    "real-mixed-dpi",
]
EXPECTED_DEFECTS = {
    "korean-selected-game-eyebrow-clipping": "COVERED",
    "both-seats-label-truncation": "COVERED",
    "duplicate-seat-status-marker": "COVERED",
    "blocking-warning-ellipsis": "COVERED",
    "windowless-startup": "EXTERNAL-STARTUP-CHUNK",
}
EXPECTED_TESTS = tuple(value["test"] for value in EXPECTED_AUTOMATED.values())

SOURCE_PROBES = {
    "CMakeLists.txt": [
        "add_test(NAME UiLocalizationTests COMMAND ui_localization_tests)",
        "add_test(NAME UiAccessibilityTests COMMAND ui_accessibility_tests)",
        "add_test(NAME LauncherUiModelTests COMMAND launcher_ui_model_tests)",
    ],
    "tests/test_ui_localization.cpp": [
        "void testCompleteThreeLocaleCatalog()",
        "Locale::KoreanKorea",
        "Locale::ChineseSimplified",
        "void testOwnerDrawStatusUsesOneShapeCue()",
        "void testLocalizedLauncherWidthFloorIsNotEnglishSized()",
    ],
    "tests/test_ui_accessibility.cpp": [
        "result.focusOrder.front() == FocusAction::GameList",
        "96u, 120u, 144u, 192u",
        "void testLocalizedCriticalTextDrivesLauncherLayout()",
        "void testLongBlockingReasonKeepsPrimaryFlowVisible()",
        "LauncherButtonSurface::System",
        "highContrast.useSystemColors",
    ],
    "tests/test_launcher_ui_model.cpp": [
        "preview.summary.canActivate",
        "missing compatibility evidence blocks Play instead of inventing capability",
    ],
    "src/launcher_win32.cpp": [
        "bool highContrastEnabled() noexcept",
        "GetSysColor(",
        "SetFocus(state.gameList)",
        "SetWindowTextW(launchReason",
        "EnableWindow(playButton",
    ],
}

REPORT_REQUIRED_MARKERS = [
    "# Launcher Release QA",
    "UiLocalizationTests",
    "UiAccessibilityTests",
    "LauncherUiModelTests",
    "real-first-window",
    "real-high-contrast-visual",
    "windowless-startup",
    "PENDING",
    "Automated regression coverage is not screenshot evidence",
]


def fail(message: str) -> None:
    raise ValueError(message)


def require_object(value: Any, context: str, expected_keys: set[str]) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(f"{context} must be an object")
    actual = set(value)
    if actual != expected_keys:
        fail(
            f"{context} fields mismatch; missing={sorted(expected_keys - actual)}, "
            f"extra={sorted(actual - expected_keys)}"
        )
    return value


def require_text(value: Any, context: str, *, maximum: int = 2048) -> str:
    if not isinstance(value, str) or not (1 <= len(value) <= maximum) or "\x00" in value:
        fail(f"{context} must be bounded non-empty text without NUL")
    return value


def validate_manifest(path: pathlib.Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    root = require_object(
        data,
        "launcher release readiness matrix",
        {
            "schemaVersion",
            "surface",
            "locales",
            "dpi",
            "windowProfiles",
            "automatedEvidence",
            "manualEvidence",
            "defectLedger",
        },
    )
    if root["schemaVersion"] != 1:
        fail("unsupported launcher release readiness schemaVersion")
    if root["surface"] != "native-win32-launcher":
        fail("release QA surface must remain native-win32-launcher")
    if root["locales"] != EXPECTED_LOCALES:
        fail(f"locale matrix must be exactly {EXPECTED_LOCALES}")
    if root["dpi"] != EXPECTED_DPI:
        fail(f"DPI matrix must be exactly {EXPECTED_DPI}")
    if root["windowProfiles"] != EXPECTED_WINDOWS:
        fail(f"window profiles must be exactly {EXPECTED_WINDOWS}")

    automated = root["automatedEvidence"]
    if not isinstance(automated, list) or len(automated) != len(EXPECTED_AUTOMATED):
        fail("automatedEvidence must contain the exact reviewed focused-test set")
    seen_automated: set[str] = set()
    for raw in automated:
        entry = require_object(raw, "automatedEvidence[]", {"id", "test", "proves"})
        item_id = require_text(entry["id"], "automated evidence id", maximum=96)
        if item_id in seen_automated:
            fail(f"duplicate automated evidence id: {item_id}")
        seen_automated.add(item_id)
        expected = EXPECTED_AUTOMATED.get(item_id)
        if expected is None:
            fail(f"unreviewed automated launcher claim: {item_id}")
        if entry["test"] != expected["test"]:
            fail(f"{item_id}: expected CTest {expected['test']}")
        if entry["proves"] != expected["proves"]:
            fail(f"{item_id}: automated claim list drifted")
    if seen_automated != set(EXPECTED_AUTOMATED):
        fail("automated launcher evidence is incomplete")

    manual = root["manualEvidence"]
    if not isinstance(manual, list) or len(manual) != len(EXPECTED_MANUAL_IDS):
        fail("manualEvidence must contain the exact reviewed pending set")
    seen_manual: list[str] = []
    for raw in manual:
        entry = require_object(raw, "manualEvidence[]", {"id", "state", "required"})
        item_id = require_text(entry["id"], "manual evidence id", maximum=96)
        seen_manual.append(item_id)
        if entry["state"] != "PENDING":
            fail(f"{item_id}: manual/visual evidence cannot be promoted without recorded execution")
        require_text(entry["required"], f"{item_id}.required")
    if seen_manual != EXPECTED_MANUAL_IDS:
        fail("manual launcher evidence order/set drifted")

    defects = root["defectLedger"]
    if not isinstance(defects, list) or len(defects) != len(EXPECTED_DEFECTS):
        fail("defectLedger must contain the exact reviewed launcher findings")
    seen_defects: set[str] = set()
    for raw in defects:
        entry = require_object(
            raw,
            "defectLedger[]",
            {"id", "automatedRegression", "visualRecheck"},
        )
        item_id = require_text(entry["id"], "defect id", maximum=96)
        if item_id in seen_defects:
            fail(f"duplicate defect id: {item_id}")
        seen_defects.add(item_id)
        expected_automation = EXPECTED_DEFECTS.get(item_id)
        if expected_automation is None:
            fail(f"unreviewed launcher defect ledger entry: {item_id}")
        if entry["automatedRegression"] != expected_automation:
            fail(f"{item_id}: automated regression disposition drifted")
        if entry["visualRecheck"] != "PENDING":
            fail(f"{item_id}: real visual recheck must remain PENDING until performed")
    if seen_defects != set(EXPECTED_DEFECTS):
        fail("launcher defect ledger is incomplete")
    return root


def validate_repository_contracts() -> None:
    for relative, markers in SOURCE_PROBES.items():
        path = ROOT / relative
        text = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in text:
                fail(f"{relative}: reviewed launcher QA marker is missing: {marker}")


def validate_report() -> None:
    text = DEFAULT_REPORT.read_text(encoding="utf-8")
    for marker in REPORT_REQUIRED_MARKERS:
        if marker not in text:
            fail(f"QA report is missing required release-review marker: {marker}")
    if "real-high-contrast-visual | PASS" in text or "real-first-window | PASS" in text:
        fail("QA report cannot promote unperformed manual evidence to PASS")


def focused_ctest_arguments(build_dir: str) -> list[str]:
    pattern = "^(" + "|".join(EXPECTED_TESTS) + ")$"
    return [
        "ctest",
        "--test-dir",
        build_dir,
        "-C",
        "Release",
        "-R",
        pattern,
        "--output-on-failure",
    ]


def focused_ctest_command(build_dir: pathlib.Path) -> list[str]:
    cache = build_dir / "CMakeCache.txt"
    if cache.is_file():
        cache_values: dict[str, str] = {}
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            for key in ("CMAKE_GENERATOR:INTERNAL", "CMAKE_CACHEFILE_DIR:INTERNAL"):
                prefix = key + "="
                if line.startswith(prefix):
                    cache_values[key] = line[len(prefix):].strip()
        generator = cache_values.get("CMAKE_GENERATOR:INTERNAL", "")
        windows_build_dir = cache_values.get("CMAKE_CACHEFILE_DIR:INTERNAL", "")
        if generator.startswith("Visual Studio"):
            if not windows_build_dir:
                fail("MSVC CMake cache is missing CMAKE_CACHEFILE_DIR")
            # The validator is commonly launched from WSL through CodexPro while
            # the configured build tree is native MSVC. Linux ctest cannot execute
            # the Windows .exe paths recorded by that tree, so invoke ctest.exe
            # directly. Do not pass through cmd.exe: the focused-test regex contains
            # `|`, which cmd would reinterpret as a shell pipe.
            command = focused_ctest_arguments(windows_build_dir)
            command[0] = "ctest.exe"
            return command
    return focused_ctest_arguments(str(build_dir))


def run_focused_ctest(build_dir: pathlib.Path) -> None:
    if not build_dir.is_dir():
        fail(f"CTest build directory does not exist: {build_dir}")
    completed = subprocess.run(
        focused_ctest_command(build_dir),
        cwd=ROOT,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        fail(f"focused launcher CTest execution failed with exit code {completed.returncode}")


def expect_rejected(path: pathlib.Path, label: str) -> None:
    try:
        validate_manifest(path)
    except (OSError, json.JSONDecodeError, ValueError):
        return
    fail(f"self-test failed to reject {label}")


def self_test() -> None:
    validate_manifest(DEFAULT_MATRIX)
    validate_repository_contracts()
    validate_report()
    command = focused_ctest_command(pathlib.Path("build-probe"))
    if command[3:5] != ["-C", "Release"]:
        fail("focused CTest command must pin the Release configuration for MSVC multi-config builds")
    expect_rejected(FIXTURE_DIR / "bad_manual_pass.json", "manual PASS fabrication")
    expect_rejected(FIXTURE_DIR / "bad_locale_matrix.json", "incomplete locale matrix")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "matrix",
        nargs="?",
        default=str(DEFAULT_MATRIX),
        help="launcher release readiness matrix JSON",
    )
    parser.add_argument(
        "--ctest-dir",
        type=pathlib.Path,
        help="optional configured build directory; runs the exact three focused CTests",
    )
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        if args.self_test:
            self_test()
            print("Launcher release readiness validator self-test passed.")
            return 0

        matrix = pathlib.Path(args.matrix).resolve()
        root = validate_manifest(matrix)
        validate_repository_contracts()
        if matrix == DEFAULT_MATRIX.resolve():
            validate_report()
        print(
            "Launcher release QA contract valid: "
            f"{len(root['automatedEvidence'])} automated contracts, "
            f"{len(root['manualEvidence'])} manual/visual items PENDING."
        )
        if args.ctest_dir is None:
            print(
                "Focused CTests were not executed by this invocation; "
                "pass --ctest-dir <configured-build> to require runtime PASS."
            )
        else:
            run_focused_ctest(args.ctest_dir.resolve())
            print("Focused launcher CTests passed: " + ", ".join(EXPECTED_TESTS))
        return 0
    except (OSError, json.JSONDecodeError, ValueError, subprocess.SubprocessError) as exc:
        print(f"Launcher release readiness invalid: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
