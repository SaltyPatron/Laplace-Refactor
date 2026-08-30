#!/usr/bin/env python3
"""Classify changed repository paths into the exact Laplace proof surfaces they require."""

from __future__ import annotations

import argparse
import fnmatch
import json
from pathlib import Path, PurePosixPath
import sys
from typing import Any, Iterable, Sequence


SCHEMA = "laplace.product-path/v1"


class ProductPathError(RuntimeError):
    """The product-path contract or change set is not admissible."""


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProductPathError(f"cannot read product-path contract: {error}") from error
    if not isinstance(value, dict):
        raise ProductPathError("product-path contract must be an object")
    return value


def _strings(value: Any, field: str, *, allow_empty: bool = False) -> list[str]:
    if not isinstance(value, list) or (not value and not allow_empty):
        raise ProductPathError(
            f"{field} must be a{' possibly empty' if allow_empty else ' non-empty'} string array"
        )
    result: list[str] = []
    for item in value:
        if not isinstance(item, str) or not item:
            raise ProductPathError(f"{field} contains an invalid string")
        result.append(item)
    if len(result) != len(set(result)):
        raise ProductPathError(f"{field} contains duplicates")
    return result


def validate_contract(contract: dict[str, Any]) -> None:
    if contract.get("schema") != SCHEMA:
        raise ProductPathError("product-path schema differs")
    hosted = _strings(contract.get("hosted_only_patterns"), "hosted_only_patterns")
    default_class = contract.get("default_class")
    if not isinstance(default_class, str) or not default_class:
        raise ProductPathError("default_class is invalid")
    default_evidence = _strings(contract.get("default_evidence"), "default_evidence")
    evidence_rows = contract.get("evidence")
    if not isinstance(evidence_rows, list) or not evidence_rows:
        raise ProductPathError("evidence must be a non-empty object array")
    evidence_ids: set[str] = set()
    for row in evidence_rows:
        if not isinstance(row, dict):
            raise ProductPathError("evidence row is not an object")
        identifier = row.get("id")
        if not isinstance(identifier, str) or not identifier or identifier in evidence_ids:
            raise ProductPathError("evidence id is absent or duplicated")
        if not isinstance(row.get("implemented"), bool):
            raise ProductPathError(f"evidence {identifier} has no implementation state")
        check = row.get("check")
        if not isinstance(check, str) or not check:
            raise ProductPathError(f"evidence {identifier} has no check name")
        evidence_ids.add(identifier)
    unknown_default = sorted(set(default_evidence) - evidence_ids)
    if unknown_default:
        raise ProductPathError(f"default_evidence names unknown providers: {unknown_default}")
    rules = contract.get("class_rules")
    if not isinstance(rules, list) or not rules:
        raise ProductPathError("class_rules must be a non-empty object array")
    class_ids: set[str] = set()
    for row in rules:
        if not isinstance(row, dict):
            raise ProductPathError("class rule is not an object")
        identifier = row.get("id")
        if not isinstance(identifier, str) or not identifier or identifier in class_ids:
            raise ProductPathError("class rule id is absent or duplicated")
        patterns = _strings(row.get("patterns"), f"class_rules[{identifier}].patterns")
        excludes = _strings(
            row.get("exclude_patterns"),
            f"class_rules[{identifier}].exclude_patterns",
            allow_empty=True,
        )
        required = _strings(
            row.get("evidence"), f"class_rules[{identifier}].evidence", allow_empty=True
        )
        unknown = sorted(set(required) - evidence_ids)
        if unknown:
            raise ProductPathError(f"class {identifier} names unknown evidence: {unknown}")
        if any(pattern in hosted for pattern in patterns):
            raise ProductPathError(
                f"class {identifier} exactly duplicates a hosted-only pattern"
            )
        for excluded in excludes:
            if not any(pattern_covers(excluded, pattern) for pattern in patterns):
                raise ProductPathError(
                    f"class {identifier} exclusion is not covered by its patterns: {excluded}"
                )
        class_ids.add(identifier)


def normalize_path(raw: str) -> str:
    if not isinstance(raw, str) or not raw:
        raise ProductPathError("changed path is empty")
    if "\x00" in raw:
        raise ProductPathError("changed path contains NUL")
    candidate = raw.replace("\\", "/")
    path = PurePosixPath(candidate)
    if path.is_absolute() or candidate.startswith("/"):
        raise ProductPathError(f"changed path is absolute: {raw!r}")
    if any(part in ("", ".", "..") for part in path.parts):
        raise ProductPathError(f"changed path is not canonical: {raw!r}")
    canonical = path.as_posix()
    if canonical != candidate:
        raise ProductPathError(f"changed path is not canonical: {raw!r}")
    return canonical


def matches(path: str, patterns: Iterable[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def pattern_covers(path_pattern: str, broader_pattern: str) -> bool:
    """Conservatively prove that an explicit exclusion sits under a positive pattern."""
    if not any(token in path_pattern for token in "*?["):
        return fnmatch.fnmatchcase(path_pattern, broader_pattern)
    if path_pattern == broader_pattern:
        return True
    fixed_prefix = path_pattern.split("*", 1)[0].split("?", 1)[0].split("[", 1)[0]
    return bool(fixed_prefix) and fnmatch.fnmatchcase(fixed_prefix, broader_pattern)


def rule_matches(path: str, rule: dict[str, Any]) -> bool:
    return matches(path, rule["patterns"]) and not matches(
        path, rule["exclude_patterns"]
    )


def classify(contract: dict[str, Any], paths: Sequence[str]) -> dict[str, Any]:
    validate_contract(contract)
    normalized = sorted({normalize_path(path) for path in paths})
    if not normalized:
        raise ProductPathError("product-path cannot classify an empty change set")

    hosted_patterns = contract["hosted_only_patterns"]
    hosted_only = all(matches(path, hosted_patterns) for path in normalized)
    classes: set[str] = set()
    required_evidence: set[str] = {"hosted"}
    unmatched_semantic: list[str] = []

    if not hosted_only:
        required_evidence.update(contract["default_evidence"])
        for path in normalized:
            if matches(path, hosted_patterns):
                continue
            matched_class = False
            for rule in contract["class_rules"]:
                if rule_matches(path, rule):
                    matched_class = True
                    classes.add(rule["id"])
                    required_evidence.update(rule["evidence"])
            if not matched_class:
                unmatched_semantic.append(path)
        if unmatched_semantic:
            classes.add(contract["default_class"])

    evidence_by_id = {row["id"]: row for row in contract["evidence"]}
    unimplemented = sorted(
        identifier
        for identifier in required_evidence
        if not evidence_by_id[identifier]["implemented"]
    )
    return {
        "schema": "laplace.product-path-classification/v1",
        "paths": normalized,
        "hosted_only": hosted_only,
        "classes": sorted(classes),
        "required_evidence": sorted(required_evidence),
        "unimplemented_evidence": unimplemented,
        "unmatched_semantic_paths": unmatched_semantic,
        "requires_custom_stack": "custom-stack" in required_evidence,
        "requires_postgresql_product": "postgresql-product" in required_evidence,
        "requires_package_product": "package-product" in required_evidence,
        "blocked": bool(unimplemented),
    }


def read_paths(path: Path) -> list[str]:
    try:
        content = path.read_text(encoding="utf-8")
    except OSError as error:
        raise ProductPathError(f"cannot read changed paths: {error}") from error
    return [
        line[:-1] if line.endswith("\r") else line
        for line in content.splitlines()
        if line
    ]


def read_git_name_status_z(path: Path) -> list[str]:
    """Read a NUL-delimited git --name-status stream without losing rename sources."""
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise ProductPathError(f"cannot read changed path status: {error}") from error
    if not payload or not payload.endswith(b"\0"):
        raise ProductPathError("changed path status is empty or not NUL terminated")

    fields = payload[:-1].split(b"\0")
    paths: list[str] = []
    index = 0
    while index < len(fields):
        try:
            status = fields[index].decode("ascii")
        except UnicodeDecodeError as error:
            raise ProductPathError("changed path status code is not ASCII") from error
        index += 1
        if not status:
            raise ProductPathError("changed path status code is empty")
        kind = status[0]
        if kind not in {"A", "M", "D", "R"}:
            raise ProductPathError(f"unsupported changed path status: {status!r}")

        field_count = 2 if kind == "R" else 1
        if index + field_count > len(fields):
            raise ProductPathError(f"changed path status {status!r} is truncated")
        raw_paths = fields[index : index + field_count]
        index += field_count
        for raw_path in raw_paths:
            try:
                paths.append(raw_path.decode("utf-8"))
            except UnicodeDecodeError as error:
                raise ProductPathError("changed repository path is not UTF-8") from error
    return paths


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    classify_parser = subparsers.add_parser("classify")
    classify_parser.add_argument("--contract", default="contracts/product-path.json")
    path_input = classify_parser.add_mutually_exclusive_group()
    path_input.add_argument("--paths-file", type=Path)
    path_input.add_argument("--git-name-status-z", type=Path)
    classify_parser.add_argument("--path", action="append", default=[])
    classify_parser.add_argument("--output", default="-")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    if arguments.command != "classify":
        raise ProductPathError("unsupported product-path command")
    paths = list(arguments.path)
    if arguments.paths_file is not None:
        paths.extend(read_paths(arguments.paths_file))
    if arguments.git_name_status_z is not None:
        paths.extend(read_git_name_status_z(arguments.git_name_status_z))
    result = classify(load_json(Path(arguments.contract)), paths)
    payload = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output == "-":
        sys.stdout.write(payload)
    else:
        Path(arguments.output).write_text(payload, encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ProductPathError as error:
        print(f"product-path: {error}", file=sys.stderr)
        raise SystemExit(1) from error