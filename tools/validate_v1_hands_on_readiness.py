#!/usr/bin/env python3
"""Fail-closed V1 hands-on readiness contract.

This gate intentionally separates repository/model evidence from Computer Use,
physical hardware, real-game, clean-machine and signing evidence.  It reads only
reviewed source/configuration files; generated build output is never authority.
"""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Any

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tools" / "testdata" / "v1_hands_on_readiness" / "cases.json"

SIGNAL_IDS = (
    "mock_journey_source",
    "mock_target_integrated",
    "normal_path_clean",
    "player_role_contract",
    "durable_persistence_authority",
    "launcher_persistence_wiring",
    "localized_critical_geometry",
    "display_human_identity",
    "intentional_input_identification",
    "steam_non_game_filtering",
    "back_to_games_lifecycle",
    "double_click_installer",
)
EVIDENCE_CLASSES = (
    "Automated/Mock",
    "ComputerUse",
    "Physical",
    "RealGame",
    "CleanMachine",
    "Signing",
)
MANUAL_EVIDENCE_CLASSES = EVIDENCE_CLASSES[1:]


@dataclass(frozen=True)
class Check:
    signal: str
    passed: bool
    detail: str


def read_text(root: pathlib.Path, relative: str) -> str:
    path = root / relative
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return ""


def exists(root: pathlib.Path, relative: str) -> bool:
    return (root / relative).is_file()


def contains_all(text: str, tokens: tuple[str, ...] | list[str]) -> bool:
    return all(token in text for token in tokens)


def extract_function(text: str, marker: str) -> str:
    start = text.find(marker)
    if start < 0:
        return ""
    brace = text.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    in_string = False
    string_quote = ""
    escaped = False
    in_line_comment = False
    in_block_comment = False
    index = brace
    while index < len(text):
        ch = text[index]
        next_ch = text[index + 1] if index + 1 < len(text) else ""
        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
        elif in_block_comment:
            if ch == "*" and next_ch == "/":
                in_block_comment = False
                index += 1
        elif in_string:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == string_quote:
                in_string = False
        else:
            if ch == "/" and next_ch == "/":
                in_line_comment = True
                index += 1
            elif ch == "/" and next_ch == "*":
                in_block_comment = True
                index += 1
            elif ch in {'"', "'"}:
                in_string = True
                string_quote = ch
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[start:index + 1]
        index += 1
    return ""


def player_one_guarded(function_text: str) -> bool:
    patterns = (
        r"if\s*\(\s*!\s*firstPlayer",
        r"if\s*\(\s*firstPlayer\s*==\s*std::nullopt",
        r"if\s*\(\s*!\s*selectedPlayerId\s*\(\s*seat1Player\s*\)",
    )
    return any(re.search(pattern, function_text) for pattern in patterns)


def analyze_repository(root: pathlib.Path) -> list[Check]:
    launcher = read_text(root, "src/launcher_win32.cpp")
    launcher_layout = read_text(root, "include/hydra/launcher_layout.hpp")
    accessibility_test = read_text(root, "tests/test_ui_accessibility.cpp")
    user_state_header = read_text(root, "include/hydra/launcher_user_state.hpp")
    user_state_source = read_text(root, "src/launcher_user_state.cpp")
    hardware_ui = read_text(root, "src/gui_win32.cpp")
    input_header = read_text(root, "include/hydra/input_observation.hpp")
    steam_source = read_text(root, "src/steam_provider.cpp")
    steam_test = read_text(root, "tests/test_steam_provider.cpp")
    cmake = read_text(root, "CMakeLists.txt")

    create_controls = extract_function(launcher, "void createControls()")
    apply_selection = extract_function(launcher, "void applyPrimarySelection()")

    forbidden_normal = (
        "TextId::UseSeatOne",
        "TextId::UseSeatTwo",
        "TextId::UseBothSeats",
        "TextId::SetupAndDiagnostics",
        "kSetupNavigation",
        "Runtime Host",
        "RuntimeHost",
    )
    normal_core = (
        "seat1Player = createPrimary",
        "seat2Player = createPrimary",
        "gameList = createPrimary",
        "configureButton = createPrimary",
        "launchReason = createPrimary",
        "playButton = createPrimary",
        "playerName = createPrimary",
        "addPlayerButton = createPrimary",
    )
    normal_path_clean = bool(create_controls) and contains_all(create_controls, normal_core) and not any(
        token in create_controls for token in forbidden_normal
    )

    player_two_optional = (
        "selectGame(1u" in apply_selection and
        "secondPlayer" in apply_selection and
        "ChooseSecondPlayerToContinue" not in apply_selection
    )
    player_role_contract = bool(apply_selection) and player_one_guarded(apply_selection) and player_two_optional

    durable_persistence_authority = (
        exists(root, "include/hydra/launcher_user_state.hpp") and
        exists(root, "src/launcher_user_state.cpp") and
        exists(root, "tests/test_launcher_user_state.cpp") and
        contains_all(user_state_header, (
            "struct LastPlayerSelection",
            "std::optional<std::string> player2Id",
            "loadPlayerProfiles",
            "savePlayerProfiles",
            "loadLastPlayerSelection",
            "saveLastPlayerSelection",
            "filterLastPlayerSelection",
        )) and
        contains_all(user_state_source, (
            "writeFileAtomically",
            "FlushFileBuffers",
            "ReplaceFileW",
            "MoveFileExW",
            "MOVEFILE_WRITE_THROUGH",
            "FileTooLarge",
        ))
    )

    persistence_api_tokens = (
        '#include "hydra/launcher_user_state.hpp"',
        "defaultUserStateRoot",
        "loadPlayerProfiles",
        "savePlayerProfiles",
        "loadLastPlayerSelection",
        "saveLastPlayerSelection",
        "filterLastPlayerSelection",
    )
    legacy_persistence_tokens = (
        "playerSelectionPath()",
        "loadPlayerSelectionFromDisk",
        "savePlayerSelectionToDisk",
        "playerProfilesPath()",
    )
    launcher_persistence_wiring = contains_all(launcher, persistence_api_tokens) and not any(
        token in launcher for token in legacy_persistence_tokens
    )

    localized_critical_geometry = (
        contains_all(launcher_layout, (
            "struct LauncherTextMeasurements",
            "struct LauncherTextRequirements",
            "heroTitleHeight",
            "heroStatusHeight",
            "playerLabelWidth",
            "playerStatusHeight",
            "launchReasonHeight",
        )) and
        contains_all(launcher, (
            "measuredTextWidth",
            "measuredWrappedTextHeight",
            "launcherTextRequirements",
        )) and
        contains_all(accessibility_test, (
            "Locale::EnglishUnitedStates",
            "Locale::KoreanKorea",
            "Locale::ChineseSimplified",
            "96u, 120u, 144u, 192u",
        ))
    )

    display_human_identity = (
        "Generic PnP Monitor" not in hardware_ui and
        contains_all(hardware_ui, (
            "displayPresentationName",
            "friendlyName",
            "monitorInfo.szDevice",
            "output->mode.width",
            "output->mode.height",
            "MONITORINFOF_PRIMARY",
            "PrimaryDisplay",
        ))
    )

    intentional_input_identification = (
        contains_all(input_header, (
            "class InputIdentificationCapture",
            "enum class InputIdentificationState",
            "Waiting",
            "Identified",
            "DeviceRemoved",
            "AmbiguousSharedDevice",
        )) and
        contains_all(hardware_ui, (
            "IdentifyKeyboard",
            "IdentifyMouse",
            "beginInputIdentification",
            "IdentificationWaitingKeyboard",
            "IdentificationWaitingMouse",
            "IdentificationTimedOut",
            "InputIdentificationState::Identified",
        ))
    )

    steam_non_game_filtering = (
        contains_all(steam_source, (
            'appId == "228980"',
            'L"_commonredist"',
            "nonPlayableTypes",
            '"tool"',
            '"server"',
            '"runtime"',
        )) and
        "228980" in steam_test
    )

    game_library_handler = extract_function(hardware_ui, "LRESULT CALLBACK Win32App::WindowProc")
    back_to_games_lifecycle = contains_all(game_library_handler, (
        "ID_BTN_GAME_LIBRARY",
        "SW_HIDE",
        "openGameLibrary()",
        "LauncherExitAction::OpenHardwareSetup",
        "DestroyWindow",
    ))

    installer_paths = (
        "include/hydra/installer_bootstrap.hpp",
        "src/installer_bootstrap.cpp",
        "src/installer_bootstrap_main.cpp",
        "tests/test_installer_bootstrap.cpp",
        "tools/build_installer_package.ps1",
    )
    installer_source = read_text(root, "src/installer_bootstrap.cpp") + read_text(
        root, "src/installer_bootstrap_main.cpp"
    )
    artifact_contract = read_text(root, "config/release-artifact-preflight.json") + read_text(
        root, "config/release-signing-manifest.json"
    )
    installer_validator = read_text(root, "tools/validate_release_installer_contract.py")
    double_click_installer = (
        all(exists(root, path) for path in installer_paths) and
        contains_all(installer_source, ("Install", "Repair", "Uninstall")) and
        "HydraSeatSetup.exe" in artifact_contract and
        "HydraSeatSetup.exe" in installer_validator
    )

    mock_journey_source = (
        exists(root, "tests/test_v1_user_journey.cpp") and
        contains_all(read_text(root, "tests/test_v1_user_journey.cpp"), (
            "testMockHappyPathKeepsPlayerTwoOptional",
            "testIncompleteStateHasActionableBlockingEvidence",
            "testDurablePlayerSelectionProjection",
            "testMockEvidenceDoesNotPromoteHandsOnClasses",
        ))
    )
    mock_target_integrated = (
        "tests/test_v1_user_journey.cpp" in cmake and
        "V1UserJourneyTests" in cmake and
        "hydra_launcher_ui_model" in cmake and
        "hydra_launcher_user_state" in cmake
    )

    values = {
        "mock_journey_source": (
            mock_journey_source,
            "deterministic V1 journey source exists and separates mock/manual evidence",
        ),
        "mock_target_integrated": (
            mock_target_integrated,
            "CMake registers V1UserJourneyTests with launcher model + user-state authority",
        ),
        "normal_path_clean": (
            normal_path_clean,
            "normal createControls is game/Player/setup/readiness/Play focused with no legacy Seat-use/diagnostic jargon",
        ),
        "player_role_contract": (
            player_role_contract,
            "normal selection explicitly guards missing Player 1 while retaining a Player-1-only path",
        ),
        "durable_persistence_authority": (
            durable_persistence_authority,
            "dedicated bounded atomic Player profile/last-selection authority exists",
        ),
        "launcher_persistence_wiring": (
            launcher_persistence_wiring,
            "launcher uses launcher_user_state rather than ad-hoc combo/file persistence",
        ),
        "localized_critical_geometry": (
            localized_critical_geometry,
            "critical localized text drives measured width/height geometry across shipped locale/DPI tests",
        ),
        "display_human_identity": (
            display_human_identity,
            "display cards use friendly topology name + DISPLAY identity + resolution + primary state",
        ),
        "intentional_input_identification": (
            intentional_input_identification,
            "keyboard/mouse press-click identification has waiting/success/failure semantics and controls",
        ),
        "steam_non_game_filtering": (
            steam_non_game_filtering,
            "Steam 228980/common redistributables and provider-classified tools/runtime/server entries are filtered",
        ),
        "back_to_games_lifecycle": (
            back_to_games_lifecycle,
            "Back to Games hides hardware setup and destroys it unless Games explicitly requests setup again",
        ),
        "double_click_installer": (
            double_click_installer,
            "reviewed HydraSeatSetup.exe Install/Repair/Uninstall bootstrapper is present in source and release contract",
        ),
    }
    return [Check(signal, values[signal][0], values[signal][1]) for signal in SIGNAL_IDS]


def live_snapshot(checks: list[Check]) -> dict[str, Any]:
    signals = {check.signal: check.passed for check in checks}
    automated = "PASS" if all(signals.values()) else "FAIL"
    evidence = {"Automated/Mock": automated}
    evidence.update({name: "PENDING" for name in MANUAL_EVIDENCE_CLASSES})
    return {"signals": signals, "evidence": evidence}


def snapshot_errors(snapshot: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if not isinstance(snapshot, dict) or set(snapshot) != {"signals", "evidence"}:
        return ["snapshot must contain exactly signals and evidence"]
    signals = snapshot.get("signals")
    evidence = snapshot.get("evidence")
    if not isinstance(signals, dict) or set(signals) != set(SIGNAL_IDS):
        errors.append("signal set does not match the reviewed V1 hands-on contract")
    else:
        for signal in SIGNAL_IDS:
            if signals[signal] is not True:
                errors.append(f"{signal}: readiness signal is not PASS")
    if not isinstance(evidence, dict) or set(evidence) != set(EVIDENCE_CLASSES):
        errors.append("evidence-class set does not match the reviewed contract")
    else:
        if evidence["Automated/Mock"] != "PASS":
            errors.append("Automated/Mock must be PASS for a ready snapshot")
        for name in MANUAL_EVIDENCE_CLASSES:
            if evidence[name] != "PENDING":
                errors.append(f"{name}: unperformed evidence must remain PENDING")
    return errors


def load_fixture() -> dict[str, Any]:
    data = json.loads(FIXTURE.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schemaVersion") != 1:
        raise ValueError("unsupported v1 hands-on readiness fixture schema")
    return data


def self_test() -> None:
    data = load_fixture()
    baseline = data.get("baseline")
    if snapshot_errors(baseline):
        raise ValueError("baseline fixture must be a valid ready snapshot")

    regressions = data.get("regressions")
    if not isinstance(regressions, list) or not regressions:
        raise ValueError("regression fixture list is missing")
    seen_signals: set[str] = set()
    for case in regressions:
        if not isinstance(case, dict) or set(case) != {"id", "signal"}:
            raise ValueError("regression case must contain id and signal")
        signal = case["signal"]
        if signal not in SIGNAL_IDS:
            raise ValueError(f"unknown regression signal: {signal}")
        seen_signals.add(signal)
        mutated = copy.deepcopy(baseline)
        mutated["signals"][signal] = False
        mutated["evidence"]["Automated/Mock"] = "FAIL"
        if not snapshot_errors(mutated):
            raise ValueError(f"self-test failed to reject regression: {case['id']}")
    if seen_signals != set(SIGNAL_IDS):
        raise ValueError("fixtures do not cover every reviewed readiness signal")

    promotions = data.get("manualPromotionRegressions")
    if not isinstance(promotions, list) or not promotions:
        raise ValueError("manual evidence promotion fixtures are missing")
    seen_manual: set[str] = set()
    for case in promotions:
        if not isinstance(case, dict) or set(case) != {"id", "evidenceClass"}:
            raise ValueError("manual promotion case must contain id and evidenceClass")
        evidence_class = case["evidenceClass"]
        if evidence_class not in MANUAL_EVIDENCE_CLASSES:
            raise ValueError(f"invalid manual evidence class: {evidence_class}")
        seen_manual.add(evidence_class)
        mutated = copy.deepcopy(baseline)
        mutated["evidence"][evidence_class] = "PASS"
        if not snapshot_errors(mutated):
            raise ValueError(f"self-test failed to reject manual promotion: {case['id']}")
    if seen_manual != set(MANUAL_EVIDENCE_CLASSES):
        raise ValueError("fixtures do not cover every manual evidence class")


def render(checks: list[Check], snapshot: dict[str, Any]) -> str:
    lines = ["V1 hands-on readiness:"]
    for check in checks:
        state = "PASS" if check.passed else "FAIL"
        lines.append(f"  [{state}] {check.signal}: {check.detail}")
    lines.append("Evidence classes:")
    for name in EVIDENCE_CLASSES:
        lines.append(f"  {name}: {snapshot['evidence'][name]}")
    return "\n".join(lines)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, default=ROOT,
                        help="repository root to inspect (defaults to this checkout)")
    parser.add_argument("--self-test", action="store_true",
                        help="run deterministic pass/regression/manual-promotion fixtures")
    parser.add_argument("--json", action="store_true", help="emit machine-readable live result")
    return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        if args.self_test:
            self_test()
            print("V1 hands-on readiness validator self-test passed.")
            return 0

        root = args.root.resolve()
        checks = analyze_repository(root)
        snapshot = live_snapshot(checks)
        if args.json:
            print(json.dumps({
                "schemaVersion": 1,
                "checks": [
                    {"id": check.signal, "state": "PASS" if check.passed else "FAIL",
                     "detail": check.detail}
                    for check in checks
                ],
                "evidence": snapshot["evidence"],
            }, ensure_ascii=False, indent=2, sort_keys=True))
        else:
            print(render(checks, snapshot))

        failed = [check for check in checks if not check.passed]
        return 0 if not failed else 1
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
        print(f"V1 hands-on readiness validator error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
