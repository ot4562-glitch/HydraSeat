#!/usr/bin/env python3
"""Fail-closed source/contract gate for HydraSeat V1 first-use Play authority."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Iterable, Mapping

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURE_PATH = ROOT / "tools" / "testdata" / "v1_play_authority" / "cases.json"

CHECK_IDS = (
    "release_local_evidence_writer",
    "release_runtime_authority_writer",
    "production_physical_only",
    "evidence_origin_boundary",
    "fail_closed_authority",
    "local_check_runner_boundary",
    "production_input_authority_boundary",
    "games_user_integration",
    "host_independent_reresolution",
    "evidence_class_separation",
)

EVIDENCE_CLASSES = (
    "Automated",
    "Controlled",
    "ComputerUse",
    "Physical",
    "RealGame",
    "CleanMachine",
    "Signing",
)

MODULES = {
    "evidence": (
        "include/hydra/local_compatibility_evidence.hpp",
        "src/local_compatibility_evidence.cpp",
    ),
    "authority": (
        "include/hydra/runtime_requirement_authority.hpp",
        "src/runtime_requirement_authority.cpp",
    ),
    "runner": (
        "include/hydra/local_compatibility_runner.hpp",
        "src/local_compatibility_runner.cpp",
    ),
    "input_authority": (
        "include/hydra/production_input_authority.hpp",
        "src/production_input_authority.cpp",
    ),
}


@dataclass(frozen=True)
class Check:
    check_id: str
    passed: bool
    detail: str


class RepositoryView:
    def __init__(self, root: pathlib.Path | None = None, files: Mapping[str, str] | None = None):
        self._root = root
        self._files = dict(files) if files is not None else None

    def text(self, relative: str) -> str:
        if self._files is not None:
            return self._files.get(relative, "")
        assert self._root is not None
        try:
            return (self._root / relative).read_text(encoding="utf-8")
        except (OSError, UnicodeError):
            return ""

    def exists(self, relative: str) -> bool:
        if self._files is not None:
            return relative in self._files
        assert self._root is not None
        return (self._root / relative).is_file()


def contains_all(text: str, tokens: Iterable[str]) -> bool:
    return all(item in text for item in tokens)


def extract_braced_function(text: str, marker: str) -> str:
    start = text.find(marker)
    if start < 0:
        return ""
    brace = text.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    in_string = False
    quote = ""
    escaped = False
    line_comment = False
    block_comment = False
    index = brace
    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""
        if line_comment:
            if char == "\n":
                line_comment = False
        elif block_comment:
            if char == "*" and next_char == "/":
                block_comment = False
                index += 1
        elif in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                in_string = False
        else:
            if char == "/" and next_char == "/":
                line_comment = True
                index += 1
            elif char == "/" and next_char == "*":
                block_comment = True
                index += 1
            elif char in {'"', "'"}:
                in_string = True
                quote = char
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return text[start:index + 1]
        index += 1
    return ""


def cmake_blocks(text: str, command: str) -> list[str]:
    """Extract bounded command(...) blocks without pretending to parse all CMake."""
    pattern = re.compile(rf"\b{re.escape(command)}\s*\(", re.IGNORECASE)
    blocks: list[str] = []
    for match in pattern.finditer(text):
        start = match.start()
        index = match.end()
        depth = 1
        quoted = False
        escaped = False
        while index < len(text) and depth:
            char = text[index]
            if quoted:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == '"':
                    quoted = False
            else:
                if char == '"':
                    quoted = True
                elif char == "(":
                    depth += 1
                elif char == ")":
                    depth -= 1
            index += 1
        if depth == 0:
            blocks.append(text[start:index])
    return blocks


def cmake_release_target_for_source(cmake: str, source: str) -> tuple[bool, str]:
    normalized = source.replace("\\", "/")
    for block in cmake_blocks(cmake, "add_executable") + cmake_blocks(cmake, "target_sources"):
        if re.search(r"\(\s*HydraSeat\b", block, re.IGNORECASE) and normalized in block.replace("\\", "/"):
            return True, "HydraSeat"

    library_target = ""
    for block in cmake_blocks(cmake, "add_library"):
        if normalized not in block.replace("\\", "/"):
            continue
        match = re.search(r"add_library\s*\(\s*([A-Za-z0-9_.+-]+)", block, re.IGNORECASE)
        if match:
            library_target = match.group(1)
            break
    if not library_target:
        return False, ""

    for block in cmake_blocks(cmake, "target_link_libraries"):
        if re.search(r"\(\s*HydraSeat\b", block, re.IGNORECASE) and re.search(
            rf"\b{re.escape(library_target)}\b", block
        ):
            return True, library_target
    return False, library_target


def public_module_symbols(header: str) -> set[str]:
    symbols = set(re.findall(r"\b(?:class|struct)\s+([A-Za-z_]\w*)", header))
    for name in re.findall(
        r"\b([A-Za-z_]\w*)\s*\([^;{}\n]*\)\s*(?:const\s*)?(?:noexcept\s*)?;",
        header,
    ):
        if name not in {"if", "for", "while", "switch", "return", "operator", "succeeded", "found", "path"}:
            symbols.add(name)
    preferred = {
        symbol
        for symbol in symbols
        if len(symbol) >= 6
        and any(
            keyword in symbol.lower()
            for keyword in (
                "compat", "evidence", "requirement", "authority", "runner", "check",
                "publish", "persist", "write", "review", "result",
            )
        )
    }
    return preferred or {symbol for symbol in symbols if len(symbol) >= 6}


def source_uses_module(source: str, header: str, header_path: str) -> bool:
    include_name = pathlib.PurePosixPath(header_path).name
    if include_name not in source:
        return False
    body = "\n".join(
        line for line in source.splitlines() if not line.lstrip().startswith("#include")
    )
    return any(re.search(rf"\b{re.escape(symbol)}\b", body) for symbol in public_module_symbols(header))


def suspicious_physical_promotion(text: str) -> bool:
    return bool(
        re.search(
            r"(?:\.origin|\borigin)\s*=\s*(?:[A-Za-z_:]+::)?ResultOrigin::Physical\b",
            text,
        )
        or re.search(
            r"(?:\.origin|\borigin)\s*=\s*(?:[A-Za-z_:]+::)?Physical\b",
            text,
        )
    )


def parse_evidence_map(document: str) -> dict[str, str]:
    begin = "<!-- V1_PLAY_AUTHORITY_EVIDENCE_CLASSES"
    end = "V1_PLAY_AUTHORITY_EVIDENCE_CLASSES -->"
    start = document.find(begin)
    finish = document.find(end, start + len(begin)) if start >= 0 else -1
    if start < 0 or finish < 0:
        return {}
    body = document[start + len(begin):finish]
    result: dict[str, str] = {}
    for raw in body.splitlines():
        line = raw.strip()
        if not line or "=" not in line:
            continue
        key, value = (part.strip() for part in line.split("=", 1))
        result[key] = value
    return result


def analyze(view: RepositoryView) -> list[Check]:
    cmake = view.text("CMakeLists.txt")
    evidence_header = view.text(MODULES["evidence"][0])
    evidence_source = view.text(MODULES["evidence"][1])
    authority_header = view.text(MODULES["authority"][0])
    authority_source = view.text(MODULES["authority"][1])
    runner_header = view.text(MODULES["runner"][0])
    runner_source = view.text(MODULES["runner"][1])
    input_authority_header = view.text(MODULES["input_authority"][0])
    input_authority_source = view.text(MODULES["input_authority"][1])
    activation_bridges_source = view.text("src/production_activation_bridges.cpp")
    resolver_header = view.text("include/hydra/game_runtime_requirement_resolver.hpp")
    resolver_source = view.text("src/game_runtime_requirement_resolver.cpp")
    host_main = view.text("src/host_main.cpp")
    production_runtime = view.text("src/production_launch_runtime.cpp")
    gui_source = view.text("src/gui_win32.cpp")
    launcher_source = view.text("src/launcher_win32.cpp")
    qa_ledger = view.text("docs/qa/V1_PLAY_AUTHORITY_QA.md")

    evidence_release, evidence_target = cmake_release_target_for_source(
        cmake, MODULES["evidence"][1]
    )
    evidence_store_contract = contains_all(
        evidence_source,
        (
            "CompatibilityLocalStore",
            "defaultCompatibilityLocalStorePath",
            ".load(",
            ".save(",
        ),
    )
    evidence_writer_ok = (
        bool(evidence_header)
        and bool(evidence_source)
        and evidence_release
        and evidence_store_contract
        and "tests/" not in evidence_source
    )

    authority_release, authority_target = cmake_release_target_for_source(
        cmake, MODULES["authority"][1]
    )
    direct_authority_io_tokens = (
        "runtime-" + "requirements.json",
        "std::of" + "stream",
        "std::f" + "stream",
        "fo" + "pen(",
        "Create" + "FileW(",
        "Write" + "File(",
    )
    direct_authority_io = any(item in authority_source for item in direct_authority_io_tokens)
    authority_store_contract = contains_all(
        authority_source,
        (
            "GameRuntimeRequirementStore",
            "defaultGameRuntimeRequirementStorePath",
            "LocalRequirementEvidenceRecord",
            ".load(",
            ".save(",
        ),
    )
    authority_writer_ok = (
        bool(authority_header)
        and bool(authority_source)
        and authority_release
        and authority_store_contract
        and not direct_authority_io
    )

    physical_only_ok = (
        "trust{LocalEvidenceTrust::PhysicalOnly}" in resolver_header
        and "candidate.trust = LocalEvidenceTrust::PhysicalOnly;" in resolver_source
        and contains_all(
            resolver_source,
            (
                "origin == compat::ResultOrigin::ImportedCommunity",
                "origin == compat::ResultOrigin::Synthetic",
                "trust == LocalEvidenceTrust::PhysicalOnly",
                "return origin == compat::ResultOrigin::Physical;",
            ),
        )
    )

    promotion_sources = evidence_source + "\n" + authority_source + "\n" + runner_source
    origin_function = extract_braced_function(resolver_source, "bool originAllowed(")
    origin_boundary_ok = (
        physical_only_ok
        and not suspicious_physical_promotion(promotion_sources)
        and "return origin == compat::ResultOrigin::ControlledProcess ||" in resolver_source
        and "return false;" in origin_function
    )

    store_resolve = extract_braced_function(
        resolver_source, "StoreBackedTrustedRequirementSource::resolveCurrent("
    )
    fail_closed_store = (
        bool(store_resolve)
        and "!loaded.succeeded() || !loaded.found()" in store_resolve
        and "RequirementSnapshotCode::InvalidStore" in store_resolve
        and "RequirementSnapshotCode::Success" not in store_resolve
    )
    fail_closed_evidence = contains_all(
        resolver_source,
        (
            "RequirementResolveCode::InvalidLocalEvidence",
            "RequirementResolveCode::StaleLocalEvidence",
            "RequirementResolveCode::MissingLocalEvidence",
            "continue;",
        ),
    )
    fail_closed_ok = fail_closed_store and fail_closed_evidence

    runner_release, runner_target = cmake_release_target_for_source(cmake, MODULES["runner"][1])
    forbidden_runner_tokens = (
        "GameRuntimeRequirementStore",
        "LocalRequirementEvidenceRecord",
        "runtime-" + "requirements.json",
        "RuntimeRequirementAuthority",
        "hydra_window_" + "test_app",
        "window_" + "test_app.exe",
        "Shell" + "Execute",
        "sys" + "tem(",
        "po" + "pen(",
    )
    runner_escape = any(item.lower() in runner_source.lower() for item in forbidden_runner_tokens)
    shell_block_guard = contains_all(
        runner_source,
        (
            "blockedIndirectionTarget",
            "request.launch.executablePath",
            "cmd.exe",
            "powershell.exe",
            "pwsh.exe",
        ),
    )
    runner_shape = (
        "ProcessLaunchSpec" in (runner_header + runner_source)
        and ("ProcessLauncher" in runner_source or "ProcessGroup" in runner_source)
        and ("timeout" in runner_source.lower() or "deadline" in runner_source.lower())
        and "cancel" in (runner_header + runner_source).lower()
        and shell_block_guard
    )
    runner_ok = (
        bool(runner_header)
        and bool(runner_source)
        and runner_release
        and runner_shape
        and not runner_escape
    )

    input_authority_release, input_authority_target = cmake_release_target_for_source(
        cmake, MODULES["input_authority"][1]
    )
    input_snapshot_body = extract_braced_function(
        input_authority_source, "loadDefaultProductionInputAuthoritySnapshot("
    )
    default_bridge_body = extract_braced_function(
        activation_bridges_source, "makeDefaultProductionActivationResourceBridges("
    )
    typed_physical_selection = (
        bool(input_authority_header)
        and bool(input_authority_source)
        and input_authority_release
        and "loadPhase3HardwareAcceptanceEvidence" in input_authority_source
        and "saveDefaultProductionPhysicalEvidenceSelection" in input_authority_source
        and "trustedProductionGateCProfiles" in input_authority_source
        and "physicalSelection.code == PhysicalEvidenceSelectionCode::Success" in input_snapshot_body
        and "evidence.has_value()" in input_snapshot_body
        and "inputEvidenceClass = ProductionInputEvidenceClass::Physical" in input_snapshot_body
        and "physicalAcceptanceEvidence = std::move(evidence)" in input_snapshot_body
    )
    host_fresh_physical_load = (
        bool(default_bridge_body)
        and "loadDefaultProductionInputAuthoritySnapshot()" in default_bridge_body
        and "config.gateCProfiles = std::move(inputAuthority.gateCProfiles)" in default_bridge_body
        and "config.inputEvidenceClass = inputAuthority.inputEvidenceClass" in default_bridge_body
        and "config.physicalAcceptanceEvidence" in default_bridge_body
    )
    games_typed_physical_selection = (
        "saveDefaultProductionPhysicalEvidenceSelection" in launcher_source
        and "checkDefaultProductionInputAuthorityPrerequisites" in launcher_source
        and "phase3-hardware-manifest.json" in launcher_source
        and "ProductionInputEvidenceClass::Physical" not in launcher_source
        and "gateCProfiles" not in launcher_source
        and "ProductionGateCProfile" not in launcher_source
    )
    input_authority_ok = (
        typed_physical_selection
        and host_fresh_physical_load
        and games_typed_physical_selection
    )

    games_sources = gui_source + "\n" + launcher_source
    direct_standard_risk = bool(
        re.search(
            r"\bcheck\.targetRisk\s*=\s*(?:[A-Za-z_]\w*::)*LocalCompatibilityTargetRisk::Standard\b",
            launcher_source,
        )
    )
    reviewed_risk_ok = (
        "CompatibilityRiskReviewPrompt" in launcher_source
        and "MB_YESNOCANCEL" in launcher_source
        and "LocalCompatibilityTargetRisk::ProtectedOrExperimental" in launcher_source
        and "LocalCompatibilityTargetRisk::Unknown" in launcher_source
        and "check.targetRisk = targetRisk;" in launcher_source
        and not direct_standard_risk
    )
    games_integration_ok = (
        evidence_writer_ok
        and authority_writer_ok
        and runner_ok
        and source_uses_module(games_sources, evidence_header, MODULES["evidence"][0])
        and source_uses_module(games_sources, authority_header, MODULES["authority"][0])
        and source_uses_module(games_sources, runner_header, MODULES["runner"][0])
        and "resolveCurrentRequirementProjection" in games_sources
        and "openGameLibrary" in games_sources
        and reviewed_risk_ok
    )

    install_body = extract_braced_function(
        production_runtime, "HostProviderPlanRegistry::install("
    )
    activation_body = extract_braced_function(
        production_runtime, "HostProviderPlanRegistry::createForBinding("
    )
    host_ok = (
        "makeDefaultProductionTrustedRequirementSource" in host_main
        and "HostControlServer" in host_main
        and "trustedRequirements_->resolveCurrent(trustedSnapshot)" in install_body
        and "validateProviderAwareLaunchPlanAgainstTrustedRequirements" in install_body
        and "trustedRequirements_->resolveCurrent(trustedSnapshot)" in activation_body
    )

    evidence_map = parse_evidence_map(qa_ledger)
    evidence_classes_ok = (
        set(evidence_map) == set(EVIDENCE_CLASSES)
        and all(evidence_map.get(name) == name for name in EVIDENCE_CLASSES)
    )

    return [
        Check(
            "release_local_evidence_writer",
            evidence_writer_ok,
            f"release target={evidence_target or 'none'}; validated local store={'yes' if evidence_store_contract else 'no'}",
        ),
        Check(
            "release_runtime_authority_writer",
            authority_writer_ok,
            f"release target={authority_target or 'none'}; validated requirement store={'yes' if authority_store_contract else 'no'}; direct file IO={'yes' if direct_authority_io else 'no'}",
        ),
        Check(
            "production_physical_only",
            physical_only_ok,
            "production context and resolver must keep LocalEvidenceTrust::PhysicalOnly",
        ),
        Check(
            "evidence_origin_boundary",
            origin_boundary_ok,
            "Synthetic/ImportedCommunity stay rejected and writer/runner modules cannot manufacture Physical origin",
        ),
        Check(
            "fail_closed_authority",
            fail_closed_ok,
            "missing/corrupt/stale authority must leave normal Play without a trusted requirement",
        ),
        Check(
            "local_check_runner_boundary",
            runner_ok,
            f"release target={runner_target or 'none'}; bounded runner={'yes' if runner_shape else 'no'}; authority/shell/test-target escape={'yes' if runner_escape else 'no'}",
        ),
        Check(
            "production_input_authority_boundary",
            input_authority_ok,
            f"release target={input_authority_target or 'none'}; typed P3-HW selection={'yes' if typed_physical_selection else 'no'}; Host fresh load={'yes' if host_fresh_physical_load else 'no'}; Games profile injection={'no' if games_typed_physical_selection else 'possible'}",
        ),
        Check(
            "games_user_integration",
            games_integration_ok,
            "Games must use all three release modules, explicitly review target risk without hard-coded Standard, and refresh the trusted requirement projection",
        ),
        Check(
            "host_independent_reresolution",
            host_ok,
            "Host must own a production trusted source and re-resolve at plan install and Seat activation",
        ),
        Check(
            "evidence_class_separation",
            evidence_classes_ok,
            "Automated/Controlled/ComputerUse/Physical/RealGame/CleanMachine/Signing mappings must remain one-to-one",
        ),
    ]


def blockers(checks: Iterable[Check]) -> list[str]:
    return [check.check_id for check in checks if not check.passed]


def print_checks(checks: list[Check], as_json: bool) -> None:
    failed = blockers(checks)
    if as_json:
        print(
            json.dumps(
                {
                    "schema_version": 1,
                    "result": "PASS" if not failed else "BLOCKER",
                    "checks": [
                        {
                            "id": check.check_id,
                            "result": "PASS" if check.passed else "BLOCKER",
                            "detail": check.detail,
                        }
                        for check in checks
                    ],
                    "blockers": failed,
                    "evidence_scope": "Automated source/contract only",
                },
                indent=2,
                sort_keys=True,
            )
        )
        return
    for check in checks:
        print(f"{'PASS' if check.passed else 'BLOCKER':7} {check.check_id}: {check.detail}")
    print(f"RESULT: {'PASS' if not failed else 'BLOCKER'} ({len(checks) - len(failed)}/{len(checks)} checks)")
    if failed:
        print("BLOCKERS: " + ", ".join(failed))
    print("Evidence: Automated source/contract only; no manual evidence class is promoted.")


def apply_fixture_case(base_files: Mapping[str, str], case: Mapping[str, object]) -> dict[str, str]:
    files = copy.deepcopy(dict(base_files))
    for path in case.get("remove", []):
        files.pop(str(path), None)
    for path, text in dict(case.get("override", {})).items():
        files[str(path)] = str(text)
    for path, text in dict(case.get("append", {})).items():
        files[str(path)] = files.get(str(path), "") + str(text)
    replacements = case.get("replace", [])
    if not isinstance(replacements, list):
        raise ValueError("fixture replace must be a list")
    for replacement in replacements:
        if not isinstance(replacement, dict):
            raise ValueError("fixture replacement must be an object")
        path = str(replacement["path"])
        old = str(replacement["old"])
        new = str(replacement["new"])
        if path not in files or old not in files[path]:
            raise ValueError(f"fixture replacement text missing: {path}: {old!r}")
        files[path] = files[path].replace(old, new)
    return files


def run_self_test(as_json: bool) -> int:
    try:
        payload = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        print(f"SELF-TEST ERROR: cannot read {FIXTURE_PATH}: {exc}", file=sys.stderr)
        return 2
    if payload.get("schema_version") != 1 or not isinstance(payload.get("base_files"), dict):
        print("SELF-TEST ERROR: unsupported fixture schema", file=sys.stderr)
        return 2

    results: list[dict[str, object]] = []
    failure_count = 0
    base_files = payload["base_files"]
    cases = payload.get("cases", [])
    if not isinstance(cases, list):
        print("SELF-TEST ERROR: cases must be a list", file=sys.stderr)
        return 2

    for case in cases:
        if not isinstance(case, dict):
            results.append({"name": "<invalid>", "passed": False, "error": "case is not an object"})
            failure_count += 1
            continue
        try:
            files = apply_fixture_case(base_files, case)
            checks = analyze(RepositoryView(files=files))
        except (KeyError, TypeError, ValueError) as exc:
            results.append({"name": case.get("name", "<unnamed>"), "passed": False, "error": str(exc)})
            failure_count += 1
            continue
        actual = set(blockers(checks))
        expected = set(str(item) for item in case.get("expect_blockers", []))
        expect_pass = bool(case.get("expect_pass", False))
        case_pass = (not actual) if expect_pass else expected.issubset(actual)
        results.append(
            {
                "name": case.get("name", "<unnamed>"),
                "passed": case_pass,
                "expected_blockers": sorted(expected),
                "actual_blockers": sorted(actual),
            }
        )
        if not case_pass:
            failure_count += 1

    if as_json:
        print(
            json.dumps(
                {"schema_version": 1, "failed": failure_count, "cases": results},
                indent=2,
                sort_keys=True,
            )
        )
    else:
        for result in results:
            status = "PASS" if result["passed"] else "FAIL"
            print(f"{status:4} {result['name']}")
            if not result["passed"]:
                print(f"     expected blockers: {result.get('expected_blockers', [])}")
                print(f"     actual blockers:   {result.get('actual_blockers', [])}")
                if "error" in result:
                    print(f"     error: {result['error']}")
        print(
            f"SELF-TEST: {'PASS' if failure_count == 0 else 'FAIL'} "
            f"({len(results) - failure_count}/{len(results)} cases)"
        )
    return 0 if failure_count == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, default=ROOT, help="repository root to validate")
    parser.add_argument("--self-test", action="store_true", help="run deterministic fixture suite")
    parser.add_argument("--json", action="store_true", help="emit deterministic JSON")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test(args.json)

    checks = analyze(RepositoryView(root=args.root.resolve()))
    print_checks(checks, args.json)
    return 0 if not blockers(checks) else 2


if __name__ == "__main__":
    raise SystemExit(main())
