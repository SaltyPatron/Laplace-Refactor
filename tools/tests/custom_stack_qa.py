#!/usr/bin/env python3
"""Plan and execute selective fail-fast custom-stack QA from repository authority."""

from __future__ import annotations

import argparse
from collections import Counter
import datetime as dt
import fnmatch
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from typing import Any, Iterable, Sequence


SCHEMA = "laplace.custom-stack-qa/v1"
PLAN_SCHEMA = "laplace.custom-stack-qa-plan/v1"
RESULT_SCHEMA = "laplace.custom-stack-qa-result/v1"
SELECTION_PROOF_EXIT = 86
_CTEST_ERE_META = frozenset(r"\.^$*+?()[]{}|")


class QaError(RuntimeError):
    pass


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise QaError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise QaError(f"{path} must contain an object")
    return value


def write_json_atomic(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def canonical_path(raw: str) -> str:
    if not isinstance(raw, str) or not raw or "\x00" in raw:
        raise QaError("changed path is empty or invalid")
    candidate = raw.replace("\\", "/")
    path = PurePosixPath(candidate)
    if path.is_absolute() or candidate.startswith("/"):
        raise QaError(f"changed path is absolute: {raw!r}")
    if any(part in ("", ".", "..") for part in path.parts):
        raise QaError(f"changed path is not canonical: {raw!r}")
    if path.as_posix() != candidate:
        raise QaError(f"changed path is not canonical: {raw!r}")
    return candidate


def read_git_name_status_z(path: Path) -> list[str]:
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise QaError(f"cannot read changed paths: {error}") from error
    if not payload or not payload.endswith(b"\0"):
        raise QaError("changed path status is empty or not NUL terminated")
    fields = payload[:-1].split(b"\0")
    paths: list[str] = []
    index = 0
    while index < len(fields):
        try:
            status = fields[index].decode("ascii")
        except UnicodeDecodeError as error:
            raise QaError("changed path status code is not ASCII") from error
        index += 1
        if not status or status[0] not in {"A", "M", "D", "R", "C", "T"}:
            raise QaError(f"unsupported changed path status: {status!r}")
        count = 2 if status[0] in {"R", "C"} else 1
        if index + count > len(fields):
            raise QaError(f"changed path status {status!r} is truncated")
        for field in fields[index : index + count]:
            try:
                paths.append(canonical_path(field.decode("utf-8")))
            except UnicodeDecodeError as error:
                raise QaError("changed repository path is not UTF-8") from error
        index += count
    return paths


def registry_entries(repo_root: Path) -> list[dict[str, Any]]:
    inputs = [repo_root / "tests/registry.json"]
    inputs.extend(sorted((repo_root / "tests/registry.d").glob("*.json")))
    entries: list[dict[str, Any]] = []
    names: set[str] = set()
    for path in inputs:
        document = read_json(path)
        rows = document.get("tests")
        if not isinstance(rows, list):
            raise QaError(f"{path} has no tests array")
        for row in rows:
            if not isinstance(row, dict):
                raise QaError(f"{path} contains a non-object registry row")
            name = row.get("ctest_name")
            if not isinstance(name, str) or not name:
                raise QaError(f"{path} contains a registry row without ctest_name")
            if name in names:
                raise QaError(f"duplicate registry test: {name}")
            names.add(name)
            entries.append(row)
    return entries


def eligible_registry_names(repo_root: Path, profile: str) -> list[str]:
    names: list[str] = []
    for row in registry_entries(repo_root):
        profiles = row.get("profiles")
        if profiles is None or (
            isinstance(profiles, list) and profile in profiles
        ):
            names.append(str(row["ctest_name"]))
    return names


def explicit_registry_names(repo_root: Path, profile: str) -> list[str]:
    """Return tests whose registry row explicitly requires this provider."""

    names: list[str] = []
    for row in registry_entries(repo_root):
        profiles = row.get("profiles")
        if isinstance(profiles, list) and profile in profiles:
            names.append(str(row["ctest_name"]))
    return names


def validate_contract(contract: dict[str, Any], repo_root: Path) -> None:
    if contract.get("schema") != SCHEMA:
        raise QaError("custom-stack QA schema differs")
    registry_profile = contract.get("registry_profile")
    if not isinstance(registry_profile, str) or not registry_profile:
        raise QaError("registry_profile is invalid")
    execution = contract.get("execution")
    if not isinstance(execution, dict):
        raise QaError("custom-stack QA execution policy is absent")
    for field in ("core_parallel_jobs", "selected_physical_parallel_jobs"):
        if (
            not isinstance(execution.get(field), int)
            or isinstance(execution[field], bool)
            or execution[field] <= 0
        ):
            raise QaError(f"custom-stack QA {field} must be a positive integer")
    eligible = set(eligible_registry_names(repo_root, registry_profile))
    isolated = contract.get("isolated_tests")
    if not isinstance(isolated, list) or not isolated:
        raise QaError("isolated_tests must be a non-empty array")
    isolated_names: set[str] = set()
    profiles: set[str] = set()
    orders: set[int] = set()
    for row in isolated:
        if not isinstance(row, dict):
            raise QaError("isolated test row is not an object")
        name = row.get("ctest_name")
        profile = row.get("profile")
        order = row.get("order")
        if not isinstance(name, str) or not name or name in isolated_names:
            raise QaError("isolated test name is absent or duplicated")
        if name not in eligible:
            raise QaError(
                f"isolated test is absent from custom-stack registry: {name}"
            )
        if not isinstance(profile, str) or not profile:
            raise QaError(f"isolated test has invalid profile: {name}")
        if (
            not isinstance(order, int)
            or isinstance(order, bool)
            or order < 0
            or order in orders
        ):
            raise QaError(f"isolated test has invalid or duplicate order: {name}")
        isolated_names.add(name)
        profiles.add(profile)
        orders.add(order)
    rules = contract.get("selection_rules")
    if not isinstance(rules, list) or not rules:
        raise QaError("selection_rules must be a non-empty array")
    rule_ids: set[str] = set()
    selected_profile_names: set[str] = set()
    for row in rules:
        if not isinstance(row, dict):
            raise QaError("selection rule is not an object")
        identifier = row.get("id")
        patterns = row.get("patterns")
        selected_profiles = row.get("profiles")
        if (
            not isinstance(identifier, str)
            or not identifier
            or identifier in rule_ids
        ):
            raise QaError("selection rule id is absent or duplicated")
        if (
            not isinstance(patterns, list)
            or not patterns
            or any(not isinstance(x, str) or not x for x in patterns)
        ):
            raise QaError(f"selection rule has invalid patterns: {identifier}")
        if (
            not isinstance(selected_profiles, list)
            or not selected_profiles
            or any(not isinstance(x, str) or not x for x in selected_profiles)
        ):
            raise QaError(
                f"selection rule has invalid profiles: {identifier}"
            )
        unknown = sorted(set(selected_profiles) - profiles)
        if unknown:
            raise QaError(
                f"selection rule names unknown profiles: {identifier}: {unknown}"
            )
        rule_ids.add(identifier)
        selected_profile_names.update(selected_profiles)
    missing_profiles = sorted(profiles - selected_profile_names)
    if missing_profiles:
        raise QaError(
            f"isolated QA profiles have no selection rule: {missing_profiles}"
        )


def matches(path: str, patterns: Iterable[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def build_plan(
    contract: dict[str, Any],
    repo_root: Path,
    changed_paths: Sequence[str],
    head_sha: str | None = None,
) -> dict[str, Any]:
    validate_contract(contract, repo_root)
    canonical = sorted({canonical_path(path) for path in changed_paths})
    if not canonical:
        raise QaError("cannot plan custom-stack QA for an empty change set")
    registry_profile = str(contract["registry_profile"])
    eligible = eligible_registry_names(repo_root, registry_profile)
    explicit = explicit_registry_names(repo_root, registry_profile)
    isolated_rows = list(contract["isolated_tests"])
    isolated_names = {str(row["ctest_name"]) for row in isolated_rows}
    core = [name for name in explicit if name not in isolated_names]
    selected_profiles: set[str] = set()
    selected_rules: list[str] = []
    for rule in contract["selection_rules"]:
        if any(matches(path, rule["patterns"]) for path in canonical):
            selected_rules.append(str(rule["id"]))
            selected_profiles.update(
                str(value) for value in rule["profiles"]
            )
    selected_rows = sorted(
        (
            row
            for row in isolated_rows
            if str(row["profile"]) in selected_profiles
        ),
        key=lambda row: int(row["order"]),
    )
    selected = [str(row["ctest_name"]) for row in selected_rows]
    return {
        "schema": PLAN_SCHEMA,
        "contract_schema": contract["schema"],
        "head_sha": head_sha,
        "changed_paths": canonical,
        "selected_rules": selected_rules,
        "selected_profiles": sorted(selected_profiles),
        "registry_profile": registry_profile,
        "hosted_antecedent_required": True,
        "core_parallel_jobs": int(contract["execution"]["core_parallel_jobs"]),
        "selected_physical_parallel_jobs": int(
            contract["execution"]["selected_physical_parallel_jobs"]
        ),
        "core_tests": core,
        "selected_physical_tests": selected,
        "isolated_test_count": len(isolated_names),
        "eligible_test_count": len(eligible),
        "core_test_count": len(core),
        "selected_physical_test_count": len(selected),
    }


def ctest_names(build_directory: Path, ctest: str) -> set[str]:
    completed = subprocess.run(
        [ctest, "--test-dir", str(build_directory), "--show-only=json-v1"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        raise QaError(
            f"cannot enumerate CTest tests: {completed.stderr.strip()}"
        )
    try:
        document = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise QaError(f"CTest inventory is not JSON: {error}") from error
    tests = document.get("tests")
    if not isinstance(tests, list):
        raise QaError("CTest inventory has no tests array")
    return {
        str(row["name"])
        for row in tests
        if isinstance(row, dict) and isinstance(row.get("name"), str)
    }


def ctest_ere_escape(name: str) -> str:
    """Escape one exact CTest test name for the POSIX ERE accepted by CTest 3.22."""

    if not isinstance(name, str) or not name:
        raise QaError("cannot encode an empty CTest test name")
    return "".join(
        ("\\" + character) if character in _CTEST_ERE_META else character
        for character in name
    )


def test_regex(names: Sequence[str]) -> str:
    if not names:
        raise QaError("cannot build a CTest selector for an empty test set")
    return "^(" + "|".join(ctest_ere_escape(name) for name in names) + ")$"


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")


def _junit_cases(report: Path) -> list[ET.Element]:
    if not report.is_file():
        raise QaError(f"CTest JUnit report was not produced: {report}")
    try:
        root = ET.parse(report).getroot()
    except (ET.ParseError, OSError) as error:
        raise QaError(f"CTest JUnit report is invalid: {report}: {error}") from error
    return list(root.iter("testcase"))


def _observed_junit_names(report: Path) -> list[str]:
    names: list[str] = []
    for case in _junit_cases(report):
        name = case.get("name")
        if not isinstance(name, str) or not name:
            raise QaError("CTest JUnit contains a testcase without an exact name")
        names.append(name)
    return names


def verify_success_execution(
    report: Path, planned_names: Sequence[str]
) -> list[str]:
    """Prove that a zero-exit CTest lane actually executed exactly its plan."""

    if not planned_names:
        raise QaError("successful CTest lane has an empty planned test set")
    cases = _junit_cases(report)
    observed: list[str] = []
    skipped: list[str] = []
    for case in cases:
        name = case.get("name")
        if not isinstance(name, str) or not name:
            raise QaError("CTest JUnit contains a testcase without an exact name")
        observed.append(name)
        if case.find("skipped") is not None:
            skipped.append(name)
        if case.find("failure") is not None or case.find("error") is not None:
            raise QaError(
                f"CTest exited zero but JUnit records a failure for {name}"
            )
    if skipped:
        raise QaError(
            "CTest exited zero but required tests were skipped: "
            + ", ".join(skipped[:16])
        )
    if not observed:
        raise QaError("CTest exited zero without executing any selected test")
    if Counter(observed) != Counter(planned_names):
        missing = sorted((Counter(planned_names) - Counter(observed)).elements())
        extra = sorted((Counter(observed) - Counter(planned_names)).elements())
        raise QaError(
            "CTest successful execution differs from the selected plan: "
            f"missing={missing[:16]} extra={extra[:16]} "
            f"planned_count={len(planned_names)} executed_count={len(observed)}"
        )
    return observed


def first_failure(
    report: Path, log: Path, status: int
) -> dict[str, Any] | None:
    if report.is_file():
        try:
            root = ET.parse(report).getroot()
            for case in root.iter("testcase"):
                failures = list(case.findall("failure")) + list(
                    case.findall("error")
                )
                if failures:
                    text = " ".join(
                        filter(
                            None,
                            [
                                (failures[0].get("message") or "").strip(),
                                (failures[0].text or "").strip(),
                            ],
                        )
                    )
                    lowered = text.lower()
                    return {
                        "test": case.get("name", "<unnamed>"),
                        "elapsed_seconds": case.get("time"),
                        "class": (
                            "timeout"
                            if "timeout" in lowered or "timed out" in lowered
                            else "test-failure"
                        ),
                        "detail": text[:4000],
                    }
        except (ET.ParseError, OSError):
            pass
    if status != 0:
        tail = ""
        try:
            lines = log.read_text(
                encoding="utf-8", errors="replace"
            ).splitlines()
            tail = "\n".join(lines[-80:])
        except OSError:
            pass
        lowered = tail.lower()
        return {
            "test": None,
            "elapsed_seconds": None,
            "class": (
                "timeout"
                if "timeout" in lowered or "timed out" in lowered
                else "ctest-failure"
            ),
            "detail": tail[-4000:],
        }
    return None


def run_ctest_lane(
    *,
    lane: str,
    names: Sequence[str],
    build_directory: Path,
    output_directory: Path,
    ctest: str,
    parallel_jobs: int,
) -> dict[str, Any]:
    if not names:
        raise QaError(f"custom-stack QA lane {lane!r} has no executable tests")
    report = output_directory / f"{lane}.junit.xml"
    log = output_directory / f"{lane}.log"
    command = [
        ctest,
        "--test-dir",
        str(build_directory),
        "--output-on-failure",
        "--stop-on-failure",
        "--parallel",
        str(parallel_jobs),
        "--output-junit",
        str(report),
        "-R",
        test_regex(names),
    ]
    started = utc_now()
    start = time.monotonic()
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            output.write(line)
            output.flush()
        process.stdout.close()
        ctest_status = process.wait()
    elapsed = time.monotonic() - start

    executed_names: list[str] = []
    try:
        if report.is_file():
            executed_names = _observed_junit_names(report)
    except QaError:
        executed_names = []

    effective_status = ctest_status
    failure = first_failure(report, log, ctest_status)
    selection_verified = False
    if ctest_status == 0:
        try:
            executed_names = verify_success_execution(report, names)
            selection_verified = True
        except QaError as error:
            effective_status = SELECTION_PROOF_EXIT
            failure = {
                "test": None,
                "elapsed_seconds": None,
                "class": "selection-proof-failure",
                "detail": str(error)[:4000],
            }

    return {
        "lane": lane,
        "planned_test_count": len(names),
        "test_count": len(names),
        "executed_test_count": len(executed_names),
        "executed_tests": executed_names,
        "selection_verified": selection_verified,
        "command": command,
        "started_at_utc": started,
        "ended_at_utc": utc_now(),
        "elapsed_seconds": round(elapsed, 6),
        "ctest_exit_code": ctest_status,
        "exit_code": effective_status,
        "result": "passed" if effective_status == 0 else "failed",
        "junit": str(report),
        "log": str(log),
        "primary_failure": failure,
    }


def execute_plan(
    plan: dict[str, Any],
    build_directory: Path,
    output_directory: Path,
    result_path: Path,
    ctest: str = "ctest",
) -> int:
    if plan.get("schema") != PLAN_SCHEMA:
        raise QaError("custom-stack QA plan schema differs")
    core = plan.get("core_tests")
    selected = plan.get("selected_physical_tests")
    if not isinstance(core, list) or any(
        not isinstance(x, str) or not x for x in core
    ):
        raise QaError("plan core_tests is invalid")
    if not isinstance(selected, list) or any(
        not isinstance(x, str) or not x for x in selected
    ):
        raise QaError("plan selected_physical_tests is invalid")
    available = ctest_names(build_directory, ctest)
    missing = sorted((set(core) | set(selected)) - available)
    if missing:
        raise QaError(
            f"selected custom-stack tests are absent from CTest: {missing}"
        )
    output_directory.mkdir(parents=True, exist_ok=True)
    result: dict[str, Any] = {
        "schema": RESULT_SCHEMA,
        "head_sha": plan.get("head_sha"),
        "selected_profiles": plan.get("selected_profiles", []),
        "started_at_utc": utc_now(),
        "lanes": [],
        "primary_terminal_result": None,
        "aggregate_required_qa": "running",
    }
    write_json_atomic(result_path, result)

    lanes: list[tuple[str, list[str], int]] = []
    if core:
        lanes.append(("core", core, int(plan.get("core_parallel_jobs", 1))))
    if selected:
        lanes.append((
            "selected-physical",
            selected,
            int(plan.get("selected_physical_parallel_jobs", 1)),
        ))
    if not lanes:
        result["aggregate_required_qa"] = "failed"
        result["primary_terminal_result"] = {
            "lane": None,
            "failure": {
                "test": None,
                "elapsed_seconds": None,
                "class": "selection-plan-empty",
                "detail": "custom-stack QA plan selects no executable tests",
            },
            "exit_code": SELECTION_PROOF_EXIT,
        }
        result["ended_at_utc"] = utc_now()
        write_json_atomic(result_path, result)
        return SELECTION_PROOF_EXIT

    for lane, names, parallel_jobs in lanes:
        lane_result = run_ctest_lane(
            lane=lane,
            names=names,
            build_directory=build_directory,
            output_directory=output_directory,
            ctest=ctest,
            parallel_jobs=parallel_jobs,
        )
        result["lanes"].append(lane_result)
        if lane_result["result"] != "passed":
            result["primary_terminal_result"] = {
                "lane": lane,
                "failure": lane_result["primary_failure"],
                "exit_code": lane_result["exit_code"],
                "ctest_exit_code": lane_result["ctest_exit_code"],
            }
            result["aggregate_required_qa"] = "failed"
            result["ended_at_utc"] = utc_now()
            write_json_atomic(result_path, result)
            return int(lane_result["exit_code"] or 1)
        write_json_atomic(result_path, result)

    result["aggregate_required_qa"] = "passed"
    result["ended_at_utc"] = utc_now()
    write_json_atomic(result_path, result)
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    select = subparsers.add_parser("select")
    select.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    select.add_argument(
        "--contract",
        type=Path,
        default=Path("contracts/custom-stack-qa.json"),
    )
    select.add_argument("--git-name-status-z", type=Path, required=True)
    select.add_argument("--head-sha")
    select.add_argument("--output", type=Path, required=True)
    execute = subparsers.add_parser("execute")
    execute.add_argument("--plan", type=Path, required=True)
    execute.add_argument("--build-directory", type=Path, required=True)
    execute.add_argument("--output-directory", type=Path, required=True)
    execute.add_argument("--result", type=Path, required=True)
    execute.add_argument("--ctest", default="ctest")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    if arguments.command == "select":
        repo_root = arguments.repo_root.resolve()
        contract_path = arguments.contract
        if not contract_path.is_absolute():
            contract_path = repo_root / contract_path
        plan = build_plan(
            read_json(contract_path),
            repo_root,
            read_git_name_status_z(arguments.git_name_status_z),
            arguments.head_sha,
        )
        write_json_atomic(arguments.output, plan)
        print(json.dumps(plan, indent=2, sort_keys=True))
        return 0
    plan = read_json(arguments.plan)
    return execute_plan(
        plan,
        arguments.build_directory,
        arguments.output_directory,
        arguments.result,
        arguments.ctest,
    )


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except QaError as error:
        print(f"custom-stack-qa: {error}", file=sys.stderr)
        raise SystemExit(1) from error
