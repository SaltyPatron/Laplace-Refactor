#!/usr/bin/env python3
"""Verify the consolidated native cognition execution law and its live projections."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


class ContractError(RuntimeError):
    """Raised when cognition execution authority is incomplete or contradictory."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ContractError(message)


def load_object(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ContractError(f"cannot read {path}: {error}") from error
    require(isinstance(value, dict), f"{path} is not a JSON object")
    return value


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        raise ContractError(f"cannot read {path}: {error}") from error


def normalized(value: object) -> str:
    rendered = json.dumps(value, sort_keys=True) if not isinstance(value, str) else value
    return " ".join(rendered.lower().split())


def contains_all(value: object, terms: tuple[str, ...], label: str) -> None:
    rendered = normalized(value)
    missing = [term for term in terms if normalized(term) not in rendered]
    require(not missing, f"{label} lost: {', '.join(missing)}")


def validate(repo_root: Path) -> None:
    contract_path = repo_root / "contracts/cognition-execution.json"
    authority_path = repo_root / "contracts/authority-stack.json"
    architecture_path = repo_root / "docs/architecture/COGNITION_EXECUTION.md"
    query_feature_path = repo_root / "requirements/features/query_neighborhood.feature"

    contract = load_object(contract_path)
    authority = load_object(authority_path)
    architecture = read_text(architecture_path)
    query_feature = read_text(query_feature_path)

    require(
        contract.get("schema") == "laplace.cognition-execution/v1",
        "cognition execution schema drift",
    )

    contains_all(
        contract.get("authority", {}),
        (
            "docs/architecture/COGNITION_EXECUTION.md",
            "LP-COGNITION-004",
            "LP-QUERY-001",
            "LP-SEARCH-001",
            "LP-CONNECTION-001",
            "LP-LIMITS-001",
            "LP-BULK-001",
        ),
        "cognition execution authority",
    )
    contains_all(
        contract.get("logical_spine", []),
        (
            "resolve canonical identities",
            "orient",
            "compile finite typed guidance",
            "admissible transition-provider families",
            "indexed physicality",
            "typed facts and testimony",
            "push semantically safe hard filters",
            "bounded typed frontier",
            "typed filtered indexed A*",
            "fold only program-declared",
            "update bindings trajectory deficits and completion obligations",
            "typed why-not",
            "realize",
            "witness",
        ),
        "cognition logical spine",
    )
    contains_all(
        contract.get("state_separation", []),
        (
            "canonical content identity",
            "physicality calculation",
            "occurrence and observation",
            "testimony and seeded fact",
            "dependence and provenance",
            "standing and adjudication epoch",
            "query-relative operator state",
            "realized or target artifact",
        ),
        "cognition state separation",
    )
    contains_all(
        contract.get("provider_law", {}),
        (
            "set-wise bounded typed working set",
            "push hard predicates",
            "never become truth",
            "miss is non-authoritative",
            "trajectory",
            "semantic relation",
            "evidence epoch",
            "typed standing lane",
            "GiST",
            "perfcache",
        ),
        "indexed provider law",
    )
    contains_all(
        contract.get("astar", {}),
        (
            "f(s) = g(s) + h(s)",
            "nonnegative declared transition costs",
            "admissible heuristic",
            "reopen semantics",
            "complete typed state identity",
            "deterministic tie law",
            "terminal completion certificate",
            "known upper bound",
            "partial",
            "unknown",
            "exhausted",
        ),
        "A-star proof law",
    )
    contains_all(
        contract.get("sql_native_boundary", {}),
        (
            "C/C++ and PostgreSQL server engine owns semantic calculation search and cognition",
            "typed set-oriented orchestration",
            "prepared parameterized reusable set-oriented",
        ),
        "native SQL boundary",
    )
    contains_all(
        contract.get("forbidden_primary_path_mechanisms", []),
        (
            "cursor-driven cognition",
            "RBAR",
            "one SQL or SPI query per frontier state",
            "recursive SQL",
            "dynamic SQL",
            "silent scalar-query fallback",
            "unbounded whole-corpus scan",
            "giant universal adjacency",
            "unbounded in-memory edge loading",
            "per-row cache maintenance",
            "accelerator miss treated as authoritative absence",
            "semantic repair",
            "maintenance drain",
            "raw graph hop fixed fanout nearest-neighbor lookup",
        ),
        "forbidden cognition physical plans",
    )
    contains_all(
        contract.get("allowed_drain", ""),
        (
            "epoch-pinned readers",
            "retired immutable perfcache generation",
            "not semantic maintenance",
        ),
        "perfcache reader-drain distinction",
    )
    contains_all(
        contract.get("edit_dispositions", {}),
        (
            "canonical content unchanged",
            "canonical endpoints unchanged",
            "new canonical content identity",
            "new exact composition",
            "exact declared content",
            "probabilistic regeneration",
        ),
        "exact edit/recomposition law",
    )
    contains_all(
        contract.get("required_execution_receipt", []),
        (
            "compiled program and query identity",
            "transition-provider families",
            "frontier states in and out",
            "candidate rows generated",
            "candidate rows rejected by hard filters",
            "rows examined",
            "server SPI and native crossings",
            "g and h evaluations",
            "dominance prunes",
            "reopen events",
            "peak frontier width",
            "peak working memory",
            "CPU time",
            "I/O time",
            "elapsed time",
            "completion optimality or upper-bound disposition",
            "continuation condition",
        ),
        "cognition execution receipts",
    )

    order = authority.get("required_load_order", [])
    require(isinstance(order, list) and order, "authority stack has no required load order")
    paths = [entry.get("path") for entry in order if isinstance(entry, dict)]
    for required in (
        "docs/product/INVENTION_MODEL.md",
        "docs/architecture/COGNITION_EXECUTION.md",
        "AGENTS.md",
        "requirements/product.yaml",
        "contracts/cognition-execution.json",
        "contracts/operation-model.json",
    ):
        require(required in paths, f"authority stack does not load {required}")
    require(
        paths.index("docs/product/INVENTION_MODEL.md")
        < paths.index("docs/architecture/COGNITION_EXECUTION.md")
        < paths.index("AGENTS.md"),
        "cognition architecture must load after invention and before agent projection",
    )
    require(
        paths.index("requirements/product.yaml")
        < paths.index("contracts/cognition-execution.json")
        < paths.index("contracts/operation-model.json"),
        "cognition executable contract must load after requirements and before the operation model",
    )

    contains_all(
        architecture,
        (
            "not autoregressive token-by-token dense attention",
            "typed filtered indexed search",
            "planner does not first load a large generic neighborhood",
            "frontier is a typed working set",
            "bubble up, change the typed state, bubble back down",
            "recursive SQL/CTEs",
            "dynamic SQL strings generated per candidate",
            "Epoch-pinned readers",
            "EXPLAIN (ANALYZE, BUFFERS, WAL",
            "result parity without the required plan/crossing/frontier/performance receipt",
        ),
        "cognition architecture projection",
    )
    contains_all(
        query_feature,
        (
            "cognition query compiles an indexed typed state search",
            "native and PostgreSQL indexes generate each eligible frontier family in bounded bulk batches",
            "Frontier expansion is set-oriented",
            "one database call per state",
            "Metric nearest retrieval is only a frontier generator",
            "partial work fluent output and best-found candidates cannot impersonate completion",
        ),
        "query/search acceptance projection",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "repo_root",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = parser.parse_args()
    try:
        validate(args.repo_root.resolve())
    except ContractError as error:
        parser.error(str(error))
    print("verified native cognition execution authority, physical-plan boundaries, and receipts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
