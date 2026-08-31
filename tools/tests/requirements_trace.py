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
OPERATION_SCHEMA = "laplace.operation-model/v1"
AUTHORITY_STACK_SCHEMA = "laplace.authority-stack/v1"
PRODUCT_ID = re.compile(r"^LP-[A-Z0-9-]+$")
EVIDENCE_ID = re.compile(r"^LP-TEST-[A-Z0-9-]+$")
ALIGNMENT_ID = re.compile(r"^LAP-ALIGN-[A-Z0-9-]+$")
DIRECT_ID = re.compile(r"^LAP-[A-Z0-9-]+$")
OPERATION_STAGE_ID = re.compile(r"^[a-z][a-z0-9-]*(?:\.[a-z][a-z0-9-]*)+$")
OPERATION_STATES = {"unimplemented", "partial", "staged", "implemented"}
PROGRAM_PHASES = set(range(9))


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
    operation_stage_count: int
    operationally_mapped_product_count: int
    required_authority_contract_count: int
    required_contract_scenario_count: int


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


def parse_operation_model(path: Path, repo_root: Path) -> dict[str, dict[str, object]]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise TraceError(f"cannot read {path}: {error}") from error
    if document.get("schema") != OPERATION_SCHEMA:
        raise TraceError(f"{path} must use {OPERATION_SCHEMA}")
    completion_rule = document.get("completion_rule")
    if not isinstance(completion_rule, str) or not completion_rule:
        raise TraceError(f"{path} must declare a completion_rule")
    tracking = document.get("tracking")
    if not isinstance(tracking, dict):
        raise TraceError(f"{path} must declare program tracking")
    repository = tracking.get("repository")
    if repository != "SaltyPatron/Laplace-Refactor":
        raise TraceError(f"{path} has invalid tracking repository")
    issue = tracking.get("parent_issue")
    if not isinstance(issue, int) or isinstance(issue, bool) or issue <= 0:
        raise TraceError(f"{path} has invalid parent_issue")
    if "session_audit_issue" in tracking:
        raise TraceError(f"{path} cannot make a historical session audit product authority")
    tracked_issues = tracking.get("tracked_issues")
    if (
        not isinstance(tracked_issues, list)
        or not tracked_issues
        or any(
            not isinstance(issue, int) or isinstance(issue, bool) or issue <= 0
            for issue in tracked_issues
        )
        or len(tracked_issues) != len(set(tracked_issues))
    ):
        raise TraceError(f"{path} has invalid tracked_issues")
    milestones = tracking.get("phase_milestones")
    if (
        not isinstance(milestones, dict)
        or set(milestones) != {str(phase) for phase in PROGRAM_PHASES}
        or any(not isinstance(title, str) or not title for title in milestones.values())
    ):
        raise TraceError(f"{path} has invalid phase_milestones")
    entries = document.get("stages")
    if not isinstance(entries, list) or not entries:
        raise TraceError(f"{path} must contain a non-empty stages array")

    stages: dict[str, dict[str, object]] = {}
    list_fields = (
        "consumes",
        "produces",
        "persistent_authority",
        "acceleration",
        "receipts",
        "product_requirements",
    )
    for entry in entries:
        if not isinstance(entry, dict):
            raise TraceError(f"{path} contains a non-object operation stage")
        identifier = entry.get("id")
        if not isinstance(identifier, str) or not OPERATION_STAGE_ID.fullmatch(identifier):
            raise TraceError(f"invalid operation stage identifier: {identifier}")
        if identifier in stages:
            raise TraceError(f"duplicate operation stage: {identifier}")
        program_phases = entry.get("program_phases")
        if (
            not isinstance(program_phases, list)
            or not program_phases
            or any(
                not isinstance(phase, int)
                or isinstance(phase, bool)
                or phase not in PROGRAM_PHASES
                for phase in program_phases
            )
            or len(program_phases) != len(set(program_phases))
        ):
            raise TraceError(f"operation stage has invalid program phases: {identifier}")
        github_issues = entry.get("github_issues")
        if (
            not isinstance(github_issues, list)
            or not github_issues
            or any(
                not isinstance(issue, int) or isinstance(issue, bool) or issue <= 0
                for issue in github_issues
            )
            or len(github_issues) != len(set(github_issues))
        ):
            raise TraceError(f"operation stage has invalid GitHub issues: {identifier}")
        owner = entry.get("owner")
        if not isinstance(owner, str) or not owner:
            raise TraceError(f"operation stage has no owner: {identifier}")
        dependencies = entry.get("depends_on")
        if (
            not isinstance(dependencies, list)
            or any(not isinstance(item, str) or not item for item in dependencies)
            or len(dependencies) != len(set(dependencies))
        ):
            raise TraceError(f"operation stage has invalid dependencies: {identifier}")
        for field in list_fields:
            values = entry.get(field)
            if (
                not isinstance(values, list)
                or not values
                or any(not isinstance(item, str) or not item for item in values)
                or len(values) != len(set(values))
            ):
                raise TraceError(
                    f"operation stage has invalid {field}: {identifier}"
                )
        requirements = entry["product_requirements"]
        for requirement in requirements:
            if not PRODUCT_ID.fullmatch(requirement):
                raise TraceError(
                    f"operation stage has invalid product requirement: {identifier}"
                )
        implementation = entry.get("implementation")
        if not isinstance(implementation, dict):
            raise TraceError(f"operation stage has no implementation disposition: {identifier}")
        state = implementation.get("state")
        if state not in OPERATION_STATES:
            raise TraceError(f"operation stage has invalid implementation state: {identifier}")
        evidence = implementation.get("evidence")
        if (
            not isinstance(evidence, list)
            or not evidence
            or any(not isinstance(item, str) or not item for item in evidence)
            or len(evidence) != len(set(evidence))
        ):
            raise TraceError(f"operation stage has invalid implementation evidence: {identifier}")
        for relative in evidence:
            evidence_path = Path(relative)
            if evidence_path.is_absolute() or ".." in evidence_path.parts:
                raise TraceError(
                    f"operation stage has unsafe implementation evidence path: {identifier}"
                )
            if not (repo_root / evidence_path).is_file():
                raise TraceError(
                    f"operation stage implementation evidence is missing: {identifier}: {relative}"
                )
        stages[identifier] = entry

    known_stages = set(stages)
    for identifier, entry in stages.items():
        dependencies = set(entry["depends_on"])
        unknown = sorted(dependencies - known_stages)
        if unknown:
            raise TraceError(
                f"operation stage references unknown dependencies: {identifier}: "
                + ", ".join(unknown)
            )
        if identifier in dependencies:
            raise TraceError(f"operation stage depends on itself: {identifier}")

    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(identifier: str) -> None:
        if identifier in visiting:
            raise TraceError(f"operation model contains a dependency cycle at: {identifier}")
        if identifier in visited:
            return
        visiting.add(identifier)
        for dependency in stages[identifier]["depends_on"]:
            visit(dependency)
        visiting.remove(identifier)
        visited.add(identifier)

    for identifier in stages:
        visit(identifier)

    foundation = "foundation.acquire-build"
    framework = "framework.execution"
    activation = "bootstrap.dependencies"
    if not {foundation, framework, activation}.issubset(stages):
        raise TraceError("operation model is missing the physical bootstrap stages")
    if stages[foundation]["depends_on"]:
        raise TraceError("minimal trusted acquisition cannot depend on product execution")
    if foundation not in stages[framework]["depends_on"]:
        raise TraceError("framework execution must depend on acquired build inputs")
    activation_dependencies = set(stages[activation]["depends_on"])
    if not {foundation, framework}.issubset(activation_dependencies):
        raise TraceError(
            "dependency activation must follow both acquisition and framework execution"
        )

    stage_issues = {
        issue
        for stage in stages.values()
        for issue in stage["github_issues"]
    }
    missing_stage_issues = sorted(set(tracked_issues) - stage_issues)
    untracked_stage_issues = sorted(stage_issues - set(tracked_issues))
    if missing_stage_issues:
        raise TraceError(
            "tracked GitHub issues have no operational stage: "
            + ", ".join(str(issue) for issue in missing_stage_issues)
        )
    if untracked_stage_issues:
        raise TraceError(
            "operation stages reference untracked GitHub issues: "
            + ", ".join(str(issue) for issue in untracked_stage_issues)
        )
    return stages


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


def required_feature_scenario_tags(
    path: Path,
    known_evidence: set[str],
) -> list[set[str]]:
    lines = _read_lines(path)
    scenarios: list[set[str]] = []
    pending_tags: set[str] = set()
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("@"):
            pending_tags = {
                tag
                for tag in re.findall(r"@([A-Z0-9-]+)", stripped)
                if tag.startswith("LP-TEST-")
            }
            unknown = sorted(pending_tags - known_evidence)
            if unknown:
                raise TraceError(
                    f"required feature references unknown evidence targets: {path}: "
                    + ", ".join(unknown)
                )
            continue
        if re.fullmatch(r"Scenario(?: Outline)?: .+", stripped):
            if not pending_tags:
                raise TraceError(f"required feature has an untagged scenario: {path}: {stripped}")
            scenarios.append(set(pending_tags))
            pending_tags.clear()
            continue
        if stripped and not stripped.startswith("#") and not stripped.startswith("Feature:"):
            pending_tags.clear()
    if not scenarios:
        raise TraceError(f"required feature has no tagged scenarios: {path}")
    return scenarios


def validate_required_authority_contracts(
    repo_root: Path,
    product: dict[str, ProductRequirement],
    operation_stages: dict[str, dict[str, object]],
) -> tuple[int, int]:
    authority_path = repo_root / "contracts/authority-stack.json"
    try:
        authority = json.loads(authority_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise TraceError(f"cannot read {authority_path}: {error}") from error
    if authority.get("schema") != AUTHORITY_STACK_SCHEMA:
        raise TraceError(f"{authority_path} must use {AUTHORITY_STACK_SCHEMA}")
    load_order = authority.get("required_load_order")
    if not isinstance(load_order, list) or not load_order:
        raise TraceError(f"{authority_path} has no required_load_order")
    loaded_contracts = {
        entry.get("path")
        for entry in load_order
        if isinstance(entry, dict) and entry.get("class") == "executable_contract"
    }

    evidence_owners: dict[str, list[str]] = {}
    for requirement in product.values():
        for evidence in requirement.evidence:
            evidence_owners.setdefault(evidence, []).append(requirement.identifier)
    stage_issues = {
        issue
        for stage in operation_stages.values()
        for issue in stage["github_issues"]
    }

    required_count = 0
    scenario_count = 0
    for path in sorted((repo_root / "contracts").glob("*.json")):
        try:
            raw_contract = path.read_text(encoding="utf-8")
        except (OSError, json.JSONDecodeError) as error:
            raise TraceError(f"cannot read {path}: {error}") from error
        if '"authority_stack_required"' not in raw_contract:
            continue
        try:
            contract = json.loads(raw_contract)
        except json.JSONDecodeError as error:
            raise TraceError(f"cannot read required contract {path}: {error}") from error
        traceability = contract.get("traceability")
        if not isinstance(traceability, dict) or traceability.get("authority_stack_required") is not True:
            continue
        required_count += 1
        relative = path.relative_to(repo_root).as_posix()
        if relative not in loaded_contracts:
            raise TraceError(f"required contract is absent from authority load order: {relative}")

        contract_requirements = traceability.get("product_requirements")
        if (
            not isinstance(contract_requirements, list)
            or not contract_requirements
            or any(item not in product for item in contract_requirements)
            or len(contract_requirements) != len(set(contract_requirements))
        ):
            raise TraceError(f"required contract has invalid product joins: {relative}")
        contract_requirement_set = set(contract_requirements)

        contract_issues = traceability.get("github_issues")
        if (
            not isinstance(contract_issues, list)
            or not contract_issues
            or any(not isinstance(item, int) or isinstance(item, bool) or item <= 0 for item in contract_issues)
            or len(contract_issues) != len(set(contract_issues))
        ):
            raise TraceError(f"required contract has invalid issue joins: {relative}")
        missing_issues = sorted(set(contract_issues) - stage_issues)
        if missing_issues:
            raise TraceError(
                f"required contract issues are absent from operation graph: {relative}: "
                + ", ".join(str(issue) for issue in missing_issues)
            )

        feature_files = traceability.get("feature_files")
        if (
            not isinstance(feature_files, list)
            or not feature_files
            or any(not isinstance(item, str) or not item for item in feature_files)
            or len(feature_files) != len(set(feature_files))
        ):
            raise TraceError(f"required contract has invalid feature joins: {relative}")
        for feature_relative in feature_files:
            feature_path = repo_root / feature_relative
            if feature_path.is_absolute() and not feature_path.is_relative_to(repo_root):
                raise TraceError(f"required contract has unsafe feature join: {relative}")
            if not feature_path.is_file():
                raise TraceError(f"required contract feature is missing: {feature_relative}")
            tagged_scenarios = required_feature_scenario_tags(feature_path, set(evidence_owners))
            scenario_count += len(tagged_scenarios)
            for tags in tagged_scenarios:
                for tag in tags:
                    owners = evidence_owners.get(tag, [])
                    if len(owners) != 1:
                        raise TraceError(
                            f"required scenario evidence target lacks exactly one product owner: {tag}"
                        )
                    if owners[0] not in contract_requirement_set:
                        raise TraceError(
                            f"required scenario evidence target is outside contract product joins: {relative}: {tag}"
                        )
    if required_count == 0:
        raise TraceError("no authority-stack-required contracts were declared")
    return required_count, scenario_count


def _flatten(values: Iterable[Iterable[str]]) -> set[str]:
    return {item for group in values for item in group}


def validate(repo_root: Path) -> TraceReport:
    product = parse_product(repo_root / "requirements/product.yaml")
    alignment = parse_alignment(repo_root / "requirements/alignment.yaml")
    registry = parse_registry(repo_root / "tests/registry.json")
    operation_stages = parse_operation_model(
        repo_root / "contracts/operation-model.json", repo_root
    )

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

    operational_product_ids = {
        identifier
        for stage in operation_stages.values()
        for identifier in stage["product_requirements"]
    }
    unknown_operational_product_ids = sorted(operational_product_ids - product_ids)
    missing_operational_product_ids = sorted(product_ids - operational_product_ids)
    if unknown_operational_product_ids:
        raise TraceError(
            "operation model references unknown product requirements: "
            + ", ".join(unknown_operational_product_ids)
        )
    if missing_operational_product_ids:
        raise TraceError(
            "product requirements have no operational stage: "
            + ", ".join(missing_operational_product_ids)
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
    required_contract_count, required_contract_scenario_count = (
        validate_required_authority_contracts(repo_root, product, operation_stages)
    )
    return TraceReport(
        product_requirement_count=len(product),
        evidence_target_count=len(evidence_ids),
        alignment_domain_count=len(alignment),
        direct_requirement_count=len(direct_ids),
        feature_scenario_count=scenario_count,
        registered_test_count=len(registry),
        implemented_evidence_count=len(implemented_evidence),
        operation_stage_count=len(operation_stages),
        operationally_mapped_product_count=len(operational_product_ids),
        required_authority_contract_count=required_contract_count,
        required_contract_scenario_count=required_contract_scenario_count,
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
        f"{report.implemented_evidence_count} implemented evidence targets, "
        f"{report.operation_stage_count} operation stages, "
        f"{report.operationally_mapped_product_count} operationally mapped product requirements, "
        f"{report.required_authority_contract_count} authority-stack-required contracts, "
        f"{report.required_contract_scenario_count} required contract scenarios"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
