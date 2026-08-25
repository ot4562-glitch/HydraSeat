#!/usr/bin/env python3
"""Summarize P3-HW-01 physical-acceptance evidence without inventing PASS.

The runner records process evidence plus explicit human checks. This parser can
reject malformed/privacy/cross-Seat evidence automatically, but a final PASS is
possible only when the manifest contains an explicit manual PASS and every
required manual check is PASS (or explicitly allowed NOT_APPLICABLE).
"""

from __future__ import annotations

import argparse
import json
import tempfile
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1
MAX_TRACE_LINES = 2_000_000
MAX_TRACE_LINE_BYTES = 1_048_576
ISOLATION_GUARANTEE = "diagnostic_route_only_native_os_input_not_suppressed"
MANUAL_VALUES = {"PENDING", "PASS", "FAIL", "NOT_APPLICABLE"}

REQUIRED_MANUAL_CHECKS = {
    "gate_a": {
        "two_keyboards_distinct",
        "two_pointing_devices_distinct",
        "key_down_up_transitions",
        "composite_child_removal",
        "unplug_replug_identity",
        "soak_minimum_duration",
        "drop_counter_reviewed",
    },
    "gate_b": {
        "seat1_exclusive_routing",
        "seat2_exclusive_routing",
        "unassigned_fails_closed",
        "shared_ambiguous_fails_closed",
        "missing_target_explicit_failure",
        "trace_seat_target_reviewed",
    },
    "gate_c": {
        "two_controlled_targets_visible",
        "seat1_changes_only_target1",
        "seat2_changes_only_target2",
        "unassigned_shared_fail_closed",
        "normal_windows_input_not_claimed_suppressed",
        "cleanup_no_owned_child_left",
        "metrics_reviewed",
    },
}

OPTIONAL_NOT_APPLICABLE = {
    ("gate_a", "composite_child_removal"),
}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8-sig") as handle:
        return json.load(handle)


def validate_manifest(manifest: Any) -> list[str]:
    errors: list[str] = []
    if not isinstance(manifest, dict):
        return ["manifest root must be an object"]
    if manifest.get("schema_version") != SCHEMA_VERSION:
        errors.append("manifest schema_version must be 1")
    if not isinstance(manifest.get("session_id"), str) or not manifest.get("session_id"):
        errors.append("manifest session_id is required")
    privacy = manifest.get("privacy")
    if not isinstance(privacy, dict) or not isinstance(
        privacy.get("sensitive_key_ids_enabled"), bool
    ):
        errors.append("manifest privacy.sensitive_key_ids_enabled must be boolean")
    profile = manifest.get("profile")
    if not isinstance(profile, dict):
        errors.append("manifest profile object is required")
    else:
        if profile.get("schema_version") != 2:
            errors.append("profile.schema_version must be 2")
        ownership = profile.get("expected_ownership")
        if not isinstance(ownership, list):
            errors.append("profile.expected_ownership must be an array")
        elif len(ownership) > 64:
            errors.append("profile.expected_ownership exceeds 64 entries")
        shared_case = profile.get("shared_case")
        if not isinstance(shared_case, dict):
            errors.append("profile.shared_case object is required")
        else:
            if shared_case.get("category") not in {"keyboard", "mouse"}:
                errors.append("profile.shared_case.category must be keyboard or mouse")
            if not isinstance(shared_case.get("device_id"), str) or not shared_case.get("device_id"):
                errors.append("profile.shared_case.device_id is required")
    stages = manifest.get("stages")
    if not isinstance(stages, dict):
        errors.append("manifest stages object is required")
    else:
        for stage_name in REQUIRED_MANUAL_CHECKS:
            stage = stages.get(stage_name)
            if not isinstance(stage, dict):
                errors.append(f"missing stage object: {stage_name}")
                continue
            checks = stage.get("manual_checks")
            if not isinstance(checks, dict):
                errors.append(f"{stage_name}.manual_checks must be an object")
                continue
            for key, value in checks.items():
                if value not in MANUAL_VALUES:
                    errors.append(f"invalid manual check value {stage_name}.{key}: {value!r}")
    if manifest.get("manual_verdict") not in {"PENDING", "PASS", "FAIL"}:
        errors.append("manual_verdict must be PENDING, PASS, or FAIL")
    return errors


def expected_ownership(manifest: dict[str, Any]) -> tuple[dict[str, int], list[str]]:
    result: dict[str, int] = {}
    errors: list[str] = []
    for item in manifest.get("profile", {}).get("expected_ownership", []):
        if not isinstance(item, dict):
            errors.append("expected_ownership entry is not an object")
            continue
        device_id = item.get("device_id")
        seat_id = item.get("seat_id")
        category = item.get("category")
        if not isinstance(device_id, str) or not device_id:
            errors.append("expected ownership has empty device_id")
            continue
        if category not in {"keyboard", "mouse"}:
            errors.append(f"expected ownership for {device_id!r} has invalid category")
            continue
        if not isinstance(seat_id, int) or seat_id <= 0:
            errors.append(f"expected ownership for {device_id!r} has invalid seat_id")
            continue
        previous = result.get(device_id)
        if previous is not None and previous != seat_id:
            errors.append(
                f"device {device_id!r} has conflicting expected Seats {previous} and {seat_id}"
            )
            continue
        result[device_id] = seat_id
    return result, errors


def resolve_artifact(manifest_path: Path, value: Any) -> Path | None:
    if value is None:
        return None
    if not isinstance(value, str) or not value:
        return None
    candidate = Path(value)
    if not candidate.is_absolute():
        candidate = manifest_path.parent / candidate
    return candidate


def parse_trace(
    path: Path,
    *,
    sensitive_key_ids_enabled: bool,
    expected_seats: dict[str, int] | None,
) -> dict[str, Any]:
    summary: dict[str, Any] = {
        "path": str(path),
        "present": path.is_file(),
        "lines": 0,
        "input_events": 0,
        "device_changes": 0,
        "arrivals": 0,
        "removals": 0,
        "unique_devices": 0,
        "redacted_key_records": 0,
        "sensitive_key_records": 0,
        "route_counts": {},
        "device_route_counts": {},
        "routed_seat_counts": {},
        "errors": [],
        "warnings": [],
    }
    if not path.is_file():
        summary["errors"].append("trace file is missing")
        return summary

    devices: set[str] = set()
    routes: Counter[str] = Counter()
    device_routes: dict[str, Counter[str]] = {}
    routed_seats: Counter[int] = Counter()
    with path.open("rb") as handle:
        for line_number, raw_line in enumerate(handle, start=1):
            if line_number > MAX_TRACE_LINES:
                summary["errors"].append(
                    f"trace exceeds bounded line count {MAX_TRACE_LINES}"
                )
                break
            if len(raw_line) > MAX_TRACE_LINE_BYTES:
                summary["errors"].append(
                    f"line {line_number} exceeds {MAX_TRACE_LINE_BYTES} bytes"
                )
                break
            if not raw_line.strip():
                continue
            summary["lines"] += 1
            try:
                record = json.loads(raw_line.decode("utf-8-sig"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                summary["errors"].append(f"line {line_number} is malformed JSON/UTF-8: {exc}")
                continue
            if not isinstance(record, dict):
                summary["errors"].append(f"line {line_number} is not a JSON object")
                continue
            device_id = record.get("device_id")
            if isinstance(device_id, str) and device_id:
                devices.add(device_id)
            record_type = record.get("record")
            if record_type == "device_change":
                summary["device_changes"] += 1
                change = record.get("change")
                if change == "Arrival":
                    summary["arrivals"] += 1
                elif change == "Removal":
                    summary["removals"] += 1
                else:
                    summary["warnings"].append(
                        f"line {line_number} has unknown device change {change!r}"
                    )
                continue
            if record_type != "input":
                summary["warnings"].append(
                    f"line {line_number} has unknown record type {record_type!r}"
                )
                continue

            summary["input_events"] += 1
            route = record.get("route")
            if isinstance(route, str):
                routes[route] += 1
                if isinstance(device_id, str) and device_id:
                    device_routes.setdefault(device_id, Counter())[route] += 1
                if route == "Routed" and isinstance(record.get("seat_id"), int):
                    routed_seats[record["seat_id"]] += 1
            if record.get("isolation_guarantee") != ISOLATION_GUARANTEE:
                summary["errors"].append(
                    f"line {line_number} has missing/incorrect isolation guarantee"
                )
            if record.get("physical_suppression_requested") is True:
                summary["errors"].append(
                    f"line {line_number} unexpectedly requests physical suppression"
                )

            redacted = record.get("key_code_redacted")
            vkey = record.get("vkey")
            if redacted is True and vkey is None:
                summary["redacted_key_records"] += 1
            elif redacted is False and isinstance(vkey, int):
                summary["sensitive_key_records"] += 1
                if not sensitive_key_ids_enabled:
                    summary["errors"].append(
                        f"line {line_number} contains a key identifier without manifest opt-in"
                    )
            elif vkey is not None:
                summary["errors"].append(
                    f"line {line_number} has non-canonical key privacy metadata"
                )

            if expected_seats is not None and route == "Routed":
                if not isinstance(device_id, str) or device_id not in expected_seats:
                    summary["errors"].append(
                        f"line {line_number} routes unexpected/unassigned device {device_id!r}"
                    )
                else:
                    seat_id = record.get("seat_id")
                    if seat_id != expected_seats[device_id]:
                        summary["errors"].append(
                            f"line {line_number} routes {device_id!r} to Seat {seat_id!r}; "
                            f"expected Seat {expected_seats[device_id]}"
                        )

    summary["unique_devices"] = len(devices)
    summary["route_counts"] = dict(sorted(routes.items()))
    summary["device_route_counts"] = {
        device_id: dict(sorted(counts.items()))
        for device_id, counts in sorted(device_routes.items())
    }
    summary["routed_seat_counts"] = {
        str(seat_id): count for seat_id, count in sorted(routed_seats.items())
    }
    if summary["lines"] == 0:
        summary["errors"].append("trace contains no evidence records")
    return summary


def parse_metrics(path: Path | None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "path": str(path) if path else None,
        "present": bool(path and path.is_file()),
        "errors": [],
        "warnings": [],
    }
    if path is None or not path.is_file():
        result["errors"].append("Gate C metrics report is missing")
        return result
    try:
        report = load_json(path)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        result["errors"].append(f"metrics report is malformed: {exc}")
        return result
    if not isinstance(report, dict) or report.get("schema_version") != 1:
        result["errors"].append("metrics report schema_version must be 1")
        return result

    required_ints = [
        "unique_input_events",
        "receiver_verified_events",
        "missing_receiver_evidence_events",
        "cross_seat_events",
        "cross_process_events",
    ]
    for key in required_ints:
        if not isinstance(report.get(key), int) or report[key] < 0:
            result["errors"].append(f"metrics report {key} must be a non-negative integer")
    queue = report.get("queue")
    recorder = report.get("recorder")
    if not isinstance(queue, dict):
        result["errors"].append("metrics report queue object is missing")
        queue = {}
    if not isinstance(recorder, dict):
        result["errors"].append("metrics report recorder object is missing")
        recorder = {}

    result.update(
        {
            "unique_input_events": report.get("unique_input_events"),
            "receiver_verified_events": report.get("receiver_verified_events"),
            "missing_receiver_evidence_events": report.get(
                "missing_receiver_evidence_events"
            ),
            "cross_seat_events": report.get("cross_seat_events"),
            "cross_process_events": report.get("cross_process_events"),
            "queue_high_water": queue.get("high_water"),
            "queue_dropped_frames": queue.get("dropped_frames"),
            "recorder_rotation_drops": recorder.get("rotation_drops"),
            "recorder_contention_drops": recorder.get("contention_drops"),
            "recorder_invalid_samples": recorder.get("invalid_samples"),
            "latency": report.get("latency"),
            "resource": report.get("resource"),
        }
    )

    if report.get("unique_input_events") == 0:
        result["errors"].append("metrics report contains zero input events")
    if isinstance(report.get("cross_seat_events"), int) and report["cross_seat_events"] > 0:
        result["errors"].append("verified cross-Seat events are nonzero")
    if isinstance(report.get("cross_process_events"), int) and report["cross_process_events"] > 0:
        result["errors"].append("verified cross-process events are nonzero")
    if isinstance(queue.get("dropped_frames"), int) and queue["dropped_frames"] > 0:
        result["errors"].append("Gate C writer queue dropped frames")
    for key in ("rotation_drops", "contention_drops", "invalid_samples"):
        value = recorder.get(key)
        if isinstance(value, int) and value > 0:
            result["errors"].append(f"metrics recorder {key} is nonzero")
    missing_receiver = report.get("missing_receiver_evidence_events")
    if isinstance(missing_receiver, int) and missing_receiver > 0:
        result["warnings"].append(
            "some events lack target receiver identity; zero cross counters alone are not zero-bleed proof"
        )
    return result


def manual_status(manifest: dict[str, Any]) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    pending: list[str] = []
    stages = manifest.get("stages", {})
    for stage_name, required in REQUIRED_MANUAL_CHECKS.items():
        checks = stages.get(stage_name, {}).get("manual_checks", {})
        for check_name in sorted(required):
            value = checks.get(check_name, "PENDING")
            if value == "FAIL":
                errors.append(f"manual check failed: {stage_name}.{check_name}")
            elif value == "PASS":
                continue
            elif value == "NOT_APPLICABLE" and (stage_name, check_name) in OPTIONAL_NOT_APPLICABLE:
                continue
            else:
                pending.append(f"{stage_name}.{check_name}")
    return errors, pending


def summarize(manifest_path: Path) -> dict[str, Any]:
    try:
        manifest = load_json(manifest_path)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        return {
            "schema_version": SCHEMA_VERSION,
            "generated_utc": utc_now(),
            "final_verdict": "FAIL",
            "errors": [f"manifest could not be read: {exc}"],
            "warnings": [],
            "stages": {},
        }

    errors = validate_manifest(manifest)
    warnings: list[str] = []
    owner_map, ownership_errors = expected_ownership(manifest)
    errors.extend(ownership_errors)
    privacy = manifest.get("privacy", {})
    sensitive = bool(privacy.get("sensitive_key_ids_enabled", False))
    stages_out: dict[str, Any] = {}

    for stage_name in ("gate_a", "gate_b", "gate_c"):
        stage = manifest.get("stages", {}).get(stage_name, {})
        stage_errors: list[str] = []
        stage_warnings: list[str] = []
        status = stage.get("status")
        exit_code = stage.get("process_exit_code")
        if status == "FAILED":
            stage_errors.append("runner recorded stage failure")
        if status not in {"RECORDED", "FAILED"}:
            stage_warnings.append(f"stage status is {status!r}, not RECORDED")
        if exit_code is not None and exit_code != 0:
            stage_errors.append(f"process exit code is {exit_code}")

        duration = stage.get("duration_seconds", 0)
        if stage_name == "gate_a" and isinstance(duration, (int, float)) and duration < 600:
            stage_warnings.append("Gate A duration is below the documented 10-minute soak")

        trace_path = resolve_artifact(manifest_path, stage.get("trace"))
        trace_summary = None
        if trace_path is not None:
            trace_summary = parse_trace(
                trace_path,
                sensitive_key_ids_enabled=sensitive,
                expected_seats=None if stage_name == "gate_a" else owner_map,
            )
            stage_errors.extend(trace_summary["errors"])
            stage_warnings.extend(trace_summary["warnings"])
            if stage_name == "gate_a":
                if trace_summary["unique_devices"] < 4:
                    stage_errors.append(
                        "Gate A trace has fewer than four distinct physical input identities"
                    )
                if trace_summary["removals"] <= 0 or trace_summary["arrivals"] <= 0:
                    stage_errors.append(
                        "Gate A trace must contain both removal and arrival hot-plug evidence"
                    )
            elif stage_name in {"gate_b", "gate_c"}:
                expected_seat_ids = sorted(set(owner_map.values()))[:2]
                for seat_id in expected_seat_ids:
                    if trace_summary["routed_seat_counts"].get(str(seat_id), 0) <= 0:
                        stage_errors.append(
                            f"{stage_name} trace has no routed evidence for expected Seat {seat_id}"
                        )
                if len(expected_seat_ids) < 2:
                    stage_errors.append(
                        f"{stage_name} requires at least two expected Seat ownership groups"
                    )
        else:
            stage_errors.append("trace path is missing from stage manifest")

        auxiliary_summaries: list[dict[str, Any]] = []
        if stage_name == "gate_b":
            auxiliary_values = stage.get("auxiliary_traces", [])
            if not isinstance(auxiliary_values, list) or not auxiliary_values:
                stage_errors.append("Gate B shared-case auxiliary trace is missing")
            else:
                for auxiliary_value in auxiliary_values:
                    auxiliary_path = resolve_artifact(manifest_path, auxiliary_value)
                    if auxiliary_path is None:
                        stage_errors.append("Gate B auxiliary trace path is invalid")
                        continue
                    auxiliary = parse_trace(
                        auxiliary_path,
                        sensitive_key_ids_enabled=sensitive,
                        expected_seats=None,
                    )
                    auxiliary_summaries.append(auxiliary)
                    stage_errors.extend(auxiliary["errors"])
                    stage_warnings.extend(auxiliary["warnings"])
                shared_device = manifest.get("profile", {}).get("shared_case", {}).get("device_id")
                if auxiliary_summaries and isinstance(shared_device, str):
                    routes_for_shared = auxiliary_summaries[0].get("device_route_counts", {}).get(shared_device, {})
                    if routes_for_shared.get("AmbiguousSharedDevice", 0) <= 0:
                        stage_errors.append(
                            "shared-case device has no AmbiguousSharedDevice evidence"
                        )
                    if routes_for_shared.get("Routed", 0) > 0:
                        stage_errors.append(
                            "shared-case device was routed despite ambiguous ownership"
                        )

        metrics_summary = None
        if stage_name == "gate_c":
            metrics_path = resolve_artifact(manifest_path, stage.get("metrics_report"))
            metrics_summary = parse_metrics(metrics_path)
            stage_errors.extend(metrics_summary["errors"])
            stage_warnings.extend(metrics_summary["warnings"])

        stages_out[stage_name] = {
            "status": status,
            "process_exit_code": exit_code,
            "duration_seconds": duration,
            "trace": trace_summary,
            "auxiliary_traces": auxiliary_summaries,
            "metrics": metrics_summary,
            "errors": stage_errors,
            "warnings": stage_warnings,
        }
        errors.extend(f"{stage_name}: {item}" for item in stage_errors)
        warnings.extend(f"{stage_name}: {item}" for item in stage_warnings)

    manual_errors, pending_checks = manual_status(manifest)
    errors.extend(manual_errors)
    manual_verdict = manifest.get("manual_verdict", "PENDING")

    if errors or manual_verdict == "FAIL":
        final_verdict = "FAIL"
    elif manual_verdict == "PASS" and not pending_checks:
        final_verdict = "PASS"
    else:
        final_verdict = "PENDING"

    if final_verdict == "PENDING" and not errors:
        warnings.append(
            "automatic evidence is not a physical PASS; explicit manual checks and manual_verdict=PASS are required"
        )

    return {
        "schema_version": SCHEMA_VERSION,
        "session_id": manifest.get("session_id"),
        "generated_utc": utc_now(),
        "manifest_state": manifest.get("state"),
        "manual_verdict": manual_verdict,
        "final_verdict": final_verdict,
        "privacy": {
            "sensitive_key_ids_enabled": sensitive,
        },
        "pending_manual_checks": pending_checks,
        "errors": errors,
        "warnings": warnings,
        "stages": stages_out,
    }


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")
    temporary.replace(path)


def _stage(check_names: set[str], trace: str, metrics: str | None = None) -> dict[str, Any]:
    return {
        "status": "RECORDED",
        "started_utc": "2026-08-26T00:00:00Z",
        "ended_utc": "2026-08-26T00:11:00Z",
        "duration_seconds": 660,
        "process_exit_code": 0,
        "trace": trace,
        "metrics_report": metrics,
        "auxiliary_traces": [],
        "manual_checks": {name: "PENDING" for name in sorted(check_names)},
        "notes": "",
    }


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="hydraseat-p3-hw-") as directory:
        root = Path(directory)
        gate_a = root / "gate-a.jsonl"
        gate_b = root / "gate-b.jsonl"
        gate_b_shared = root / "gate-b-shared.jsonl"
        gate_c = root / "gate-c.jsonl"
        metrics = root / "gate-c-metrics.json"
        base_input = {
            "record": "input",
            "sequence": 1,
            "timestamp_us": 100,
            "device_id": "Keyboard:A",
            "device_path": "fixture",
            "raw_type": 1,
            "vkey": None,
            "key_code_redacted": True,
            "key_transition": "Down",
            "delta_x": 0,
            "delta_y": 0,
            "wheel_delta": 0,
            "touchpad": False,
            "route": "Routed",
            "seat_id": 1,
            "target_hwnd": 111,
            "physical_suppression_requested": False,
            "isolation_guarantee": ISOLATION_GUARANTEE,
        }
        seat2_input = dict(base_input)
        seat2_input.update(
            {
                "sequence": 2,
                "device_id": "Keyboard:B",
                "seat_id": 2,
                "target_hwnd": 222,
            }
        )
        unassigned = dict(base_input)
        unassigned.update({"route": "UnassignedDevice", "seat_id": None, "target_hwnd": 0})
        ambiguous = dict(base_input)
        ambiguous.update({"route": "AmbiguousSharedDevice", "seat_id": None, "target_hwnd": 0})
        gate_a_records = []
        for sequence, device_id, raw_type in (
            (10, "Keyboard:A", 1),
            (11, "Keyboard:B", 1),
            (12, "Mouse:A", 0),
            (13, "Mouse:B", 0),
        ):
            observed = dict(unassigned)
            observed.update(
                {
                    "sequence": sequence,
                    "device_id": device_id,
                    "raw_type": raw_type,
                }
            )
            gate_a_records.append(observed)
        gate_a_records.extend(
            [
                {
                    "record": "device_change",
                    "sequence": 20,
                    "timestamp_us": 200,
                    "change": "Removal",
                    "device_id": "Keyboard:A",
                    "device_path": "fixture",
                    "raw_type": 1,
                    "touchpad": False,
                    "online": False,
                },
                {
                    "record": "device_change",
                    "sequence": 21,
                    "timestamp_us": 210,
                    "change": "Arrival",
                    "device_id": "Keyboard:A",
                    "device_path": "fixture",
                    "raw_type": 1,
                    "touchpad": False,
                    "online": True,
                },
            ]
        )
        for path, records in (
            (gate_a, gate_a_records),
            (gate_b, [base_input, seat2_input]),
            (gate_b_shared, [ambiguous]),
            (gate_c, [base_input, seat2_input]),
        ):
            with path.open("w", encoding="utf-8", newline="\n") as handle:
                for record in records:
                    handle.write(json.dumps(record, separators=(",", ":")) + "\n")
        write_json(
            metrics,
            {
                "schema_version": 1,
                "trace_samples": 6,
                "unique_input_events": 1,
                "complete_input_events": 0,
                "missing_stage_events": 1,
                "receiver_verified_events": 0,
                "missing_receiver_evidence_events": 1,
                "cross_seat_events": 0,
                "cross_process_events": 0,
                "event_classes": {"key": 1, "button": 0, "movement": 0, "wheel": 0},
                "queue": {"high_water": 1, "dropped_frames": 0},
                "recorder": {"rotation_drops": 0, "contention_drops": 0, "invalid_samples": 0},
                "latency": {},
                "resource": {},
            },
        )
        manifest = {
            "schema_version": 1,
            "session_id": "fixture",
            "created_utc": "2026-08-26T00:00:00Z",
            "updated_utc": "2026-08-26T00:11:00Z",
            "state": "READY_FOR_REVIEW",
            "privacy": {"sensitive_key_ids_enabled": False, "notice_acknowledged": True},
            "environment": {"windows_version": "fixture", "windows_build": "fixture", "architecture": "x64", "hardware_notes": "fixture"},
            "profile": {
                "source_path": "fixture.json",
                "sha256": "0" * 64,
                "schema_version": 2,
                "expected_ownership": [
                    {"device_id": "Keyboard:A", "category": "keyboard", "seat_id": 1},
                    {"device_id": "Mouse:A", "category": "mouse", "seat_id": 1},
                    {"device_id": "Keyboard:B", "category": "keyboard", "seat_id": 2},
                    {"device_id": "Mouse:B", "category": "mouse", "seat_id": 2},
                ],
                "shareable_resources": [],
                "shared_case": {
                    "derived_profile": "shared-profile.json",
                    "sha256": "1" * 64,
                    "device_id": "Keyboard:A",
                    "category": "keyboard",
                },
            },
            "stages": {
                "gate_a": _stage(REQUIRED_MANUAL_CHECKS["gate_a"], gate_a.name),
                "gate_b": _stage(REQUIRED_MANUAL_CHECKS["gate_b"], gate_b.name),
                "gate_c": _stage(REQUIRED_MANUAL_CHECKS["gate_c"], gate_c.name, metrics.name),
            },
            "manual_verdict": "PENDING",
            "manual_verdict_note": "",
        }
        manifest["stages"]["gate_b"]["auxiliary_traces"] = [gate_b_shared.name]
        manifest_path = root / "manifest.json"
        write_json(manifest_path, manifest)

        pending = summarize(manifest_path)
        if pending["final_verdict"] != "PENDING" or pending["errors"]:
            raise AssertionError(f"process evidence alone must remain PENDING: {pending}")

        for stage_name, required in REQUIRED_MANUAL_CHECKS.items():
            for check_name in required:
                manifest["stages"][stage_name]["manual_checks"][check_name] = "PASS"
        manifest["manual_verdict"] = "PASS"
        write_json(manifest_path, manifest)
        passed = summarize(manifest_path)
        if passed["final_verdict"] != "PASS" or passed["errors"]:
            raise AssertionError(f"explicit clean manual evidence should PASS: {passed}")

        with gate_b.open("w", encoding="utf-8", newline="\n") as handle:
            leaked = dict(base_input)
            leaked.update({"vkey": 65, "key_code_redacted": False})
            handle.write(json.dumps(leaked) + "\n")
        privacy_fail = summarize(manifest_path)
        if privacy_fail["final_verdict"] != "FAIL" or not any(
            "without manifest opt-in" in item for item in privacy_fail["errors"]
        ):
            raise AssertionError("privacy violation must fail closed")

        with gate_b.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(base_input) + "\n")
        metrics_value = load_json(metrics)
        metrics_value["cross_seat_events"] = 1
        write_json(metrics, metrics_value)
        bleed_fail = summarize(manifest_path)
        if bleed_fail["final_verdict"] != "FAIL" or not any(
            "cross-Seat" in item for item in bleed_fail["errors"]
        ):
            raise AssertionError("verified cross-Seat evidence must fail closed")

    print("P3-HW-01 trace/report parser self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()
    if args.manifest is None:
        parser.error("--manifest is required unless --self-test is used")
    report = summarize(args.manifest)
    if args.output is not None:
        write_json(args.output, report)
    else:
        print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    return 0 if report["final_verdict"] != "FAIL" else 2


if __name__ == "__main__":
    raise SystemExit(main())
