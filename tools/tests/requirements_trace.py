#!/usr/bin/env python3
"""Verify the repository requirement, alignment, scenario, and test joins."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


PRODUCT_SCHEMA = "laplace.requirements/v1"
ALIGNMENT_SCHEMA = "laplace.alignment/v1"
PRODUCT_ID = re.compile(r"^LP-[A-Z0-9-]+$")
EVIDENCE_ID = re.compile(r"^LP-TEST-[A-Z0-9-]+$")
ALIGNMENT_ID = re.compile(r"^LAP-ALIGN-[A-Z0-9-]+$")
DIRECT_ID = re.compile(r"^LAP-[A-Z0-9-]+$")


class TraceError(RuntimeError):
    """Raised when the requirement graph is internally inconsistent."""


@dataclass(frozen=True)
class ProductRequirement:
    identifier: str
    evidence: tuple[str, ...]


@dataclass(frozen=True)
class AlignmentDomain:
    identifier: str
    product_requirements: tuple[str, ...]
    direct_requirements: tuple[str, ...]


@dataclass(frozen=True)
class TraceReport:
    product_requirement_count: int
    evidence_target_count: int
    alignment_domain_count: int
    direct_requirement_count: int
    feature_scenario_count: int
    registered_test_count: int
    implemented_evidence_count: int


def _read_lines(path: Path) -> list[str]:
    try:
        return path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise TraceError(f"cannot read {path}: {error}") from error


def _schema(lines: list[str], path: Path) -> str:
    for line in lines:
        if line.startswith("schema: "):
            return line.removeprefix("schema: ").strip()
    raise TraceError(f"{path} has no schema")


def parse_product(path: Path) -> dict[str, ProductRequirement]:
    lines = _read_lines(path)
    if _schema(lines, path) != PRODUCT_SCHEMA:
        raise TraceError(f"{path} must use {PRODUCT_SCHEMA}")

    requirements: dict[str, ProductRequirement] = {}
    current_id: str | None = None
    current_evidence: list[str] = []
    reading_evidence = False

    def publish() -> None:
        nonlocal current_id, current_evidence
        if current_id is None:
            return
        if current_id in requirements:
            raise TraceError(f"duplicate product requirement: {current_id}")
        if not current_evidence:
            raise TraceError(f"product requirement has no evidence targets: {current_id}")
        if len(current_evidence) != len(set(current_evidence)):
            raise TraceError(f"product requirement repeats an evidence target: {current_id}")
        requirements[current_id] = ProductRequirement(current_id, tuple(current_evidence))

    for line in lines:
        match = re.fullmatch(r"  - id: (LP-[A-Z0-9-]+)", line)
        if match:
            publish()
            current_id = match.group(1)
            current_evidence = []
            reading_evidence = False
            if not PRODUCT_ID.fullmatch(current_id):
                raise TraceError(f"invalid product requirement identifier: {current_id}")
            continue
        if current_id is None:
            continue
        if line == "    evidence:":
            reading_evidence = True
            continue
        if reading_evidence:
            evidence_match = re.fullmatch(r"      - (LP-TEST-[A-Z0-9-]+)", line)
            if evidence_match:
                evidence_id = evidence_match.group(1)
                if not EVIDENCE_ID.fullmatch(evidence_id):
                    raise TraceError(f"invalid evidence identifier: {evidence_id}")
                current_evidence.append(evidence_id)
                continue
            if line and not line.startswith("      "):
                reading_evidence = False
    publish()
    if not requirements:
        raise TraceError(f"{path} defines no product requirements")
    return requirements


def parse_alignment(path: Path) -> dict[str, AlignmentDomain]:
    lines = _read_lines(path)
    if _schema(lines, path) != ALIGNMENT_SCHEMA:
        raise TraceError(f"{path} must use {ALIGNMENT_SCHEMA}")

    domains: dict[str, AlignmentDomain] = {}
    current_id: str | None = None
    product_ids: list[str] = []
    direct_ids: list[str] = []
    section: str | None = None

    def publish() -> None:
        nonlocal current_id, product_ids, direct_ids
        if current_id is None:
            return
        if current_id in domains:
            raise TraceError(f"duplicate alignment domain: {current_id}")
        if not product_ids:
            raise TraceError(f"alignment domain has no product join: {current_id}")
        if not direct_ids:
            raise TraceError(f"alignment domain has no direct requirements: {current_id}")
        if len(product_ids) != len(set(product_ids)):
            raise TraceError(f"alignment domain repeats a product join: {current_id}")
        if len(direct_ids) != len(set(direct_ids)):
            raise TraceError(f"alignment domain repeats a direct requirement: {current_id}")
        domains[current_id] = AlignmentDomain(
            current_id, tuple(product_ids), tuple(direct_ids)
        )

    for line in lines:
        match = re.fullmatch(r"  - id: (LAP-ALIGN-[A-Z0-9-]+)", line)
        if match:
            publish()
            current_id = match.group(1)
            product_ids = []
            direct_ids = []
            section = None
            if not ALIGNMENT_ID.fullmatch(current_id):
                raise TraceError(f"invalid alignment domain identifier: {current_id}")
            continue
        if current_id is None:
            continue
        if line == "    product_requirements:":
            section = "product"
            continue
        if line == "    requirements:":
            section = "direct"
            continue
        item_match = re.fullmatch(r"      - ([A-Z0-9-]+)", line)
        if not item_match:
            continue
        identifier = item_match.group(1)
        if section == "product":
            if not PRODUCT_ID.fullmatch(identifier):
                raise TraceError(f"invalid product join in {current_id}: {identifier}")
            product_ids.append(identifier)
        elif section == "direct":
            if not DIRECT_ID.fullmatch(identifier) or ALIGNMENT_ID.fullmatch(identifier):
                raise TraceError(f"invalid direct requirement in {current_id}: {identifier}")
            direct_ids.append(identifier)
    publish()
    if not domains:
        raise TraceError(f"{path} defines no alignment domains")
    return domains


def parse_registry(path: Path) -> list[dict[str, object]]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise TraceError(f"cannot read {path}: {error}") from error
    tests = document.get("tests")
    if not isinstance(tests, list) or not tests:
        raise TraceError(f"{path} must contain a non-empty tests array")
    names: set[str] = set()
    for entry in tests:
        if not isinstance(entry, dict):
            raise TraceError(f"{path} contains a non-object test entry")
        name = entry.get("ctest_name")
        targets = entry.get("evidence_targets")
        profiles = entry.get("profiles")
        if not isinstance(name, str) or not name:
            raise TraceError(f"{path} contains a test without ctest_name")
        if name in names:
            raise TraceError(f"duplicate registered test: {name}")
        names.add(name)
        if not isinstance(targets, list) or not targets:
            raise TraceError(f"registered test has no evidence targets: {name}")
        for target in targets:
            if not isinstance(target, str) or not EVIDENCE_ID.fullmatch(target):
                raise TraceError(f"registered test has invalid evidence target: {name}")
        if profiles is not None:
            if (
                not isinstance(profiles, list)
                or not profiles
                or any(not isinstance(profile, str) or not profile for profile in profiles)
                or len(profiles) != len(set(profiles))
            ):
                raise TraceError(f"registered test has invalid profiles: {name}")
    return tests


def feature_scenarios(feature_root: Path, known_evidence: set[str]) -> int:
    count = 0
    for path in sorted(feature_root.glob("*.feature")):
        lines = _read_lines(path)
        if not any(line.startswith("Feature: ") for line in lines):
            raise TraceError(f"feature file has no Feature declaration: {path}")
        path_count = sum(
            1
            for line in lines
            if re.match(r"^  Scenario(?: Outline)?: ", line)
        )
        if path_count == 0:
            raise TraceError(f"feature file has no scenarios: {path}")
        count += path_count
        for line in lines:
            for tag in re.findall(r"@([A-Z0-9-]+)", line):
                if tag.startswith("LP-TEST-") and tag not in known_evidence:
                    raise TraceError(f"feature file references unknown evidence target: {tag}")
    if count == 0:
        raise TraceError(f"{feature_root} contains no scenarios")
    return count


def _flatten(values: Iterable[Iterable[str]]) -> set[str]:
    return {item for group in values for item in group}


def validate(repo_root: Path) -> TraceReport:
    product = parse_product(repo_root / "requirements/product.yaml")
    alignment = parse_alignment(repo_root / "requirements/alignment.yaml")
    registry = parse_registry(repo_root / "tests/registry.json")

    product_ids = set(product)
    evidence_ids = _flatten(item.evidence for item in product.values())
    joined_product_ids = _flatten(
        domain.product_requirements for domain in alignment.values()
    )
    unknown_product_ids = sorted(joined_product_ids - product_ids)
    missing_product_ids = sorted(product_ids - joined_product_ids)
    if unknown_product_ids:
        raise TraceError(
            "alignment references unknown product requirements: "
            + ", ".join(unknown_product_ids)
        )
    if missing_product_ids:
        raise TraceError(
            "product requirements have no alignment domain: "
            + ", ".join(missing_product_ids)
        )

    direct_ids = [
        identifier
        for domain in alignment.values()
        for identifier in domain.direct_requirements
    ]
    repeated_direct_ids = sorted(
        identifier for identifier in set(direct_ids) if direct_ids.count(identifier) > 1
    )
    if repeated_direct_ids:
        raise TraceError(
            "direct requirements occur in multiple alignment domains: "
            + ", ".join(repeated_direct_ids)
        )

    implemented_evidence = {
        target
        for entry in registry
        for target in entry["evidence_targets"]
        if isinstance(target, str)
    }
    unknown_evidence = sorted(implemented_evidence - evidence_ids)
    if unknown_evidence:
        raise TraceError(
            "test registry references unknown evidence targets: "
            + ", ".join(unknown_evidence)
        )

    scenario_count = feature_scenarios(
        repo_root / "requirements/features", evidence_ids
    )
    return TraceReport(
        product_requirement_count=len(product),
        evidence_target_count=len(evidence_ids),
        alignment_domain_count=len(alignment),
        direct_requirement_count=len(direct_ids),
        feature_scenario_count=scenario_count,
        registered_test_count=len(registry),
        implemented_evidence_count=len(implemented_evidence),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "repo_root",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    arguments = parser.parse_args()
    try:
        report = validate(arguments.repo_root.resolve())
    except TraceError as error:
        parser.error(str(error))
    print(
        "verified requirement graph: "
        f"{report.product_requirement_count} product requirements, "
        f"{report.alignment_domain_count} alignment domains, "
        f"{report.direct_requirement_count} direct requirements, "
        f"{report.evidence_target_count} declared evidence targets, "
        f"{report.feature_scenario_count} scenarios, "
        f"{report.registered_test_count} registered tests, "
        f"{report.implemented_evidence_count} implemented evidence targets"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
