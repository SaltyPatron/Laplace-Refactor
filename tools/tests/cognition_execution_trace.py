#!/usr/bin/env python3
"""Verify the consolidated Laplace cognition execution contract and projections."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


class ContractError(RuntimeError):
    """Raised when cognition execution authority becomes incomplete or contradictory."""


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


def text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        raise ContractError(f"cannot read {path}: {error}") from error


def normalized(value: object) -> str:
    rendered = json.dumps(value, sort_keys=True) if not isinstance(value, str) else value
    return " ".join(rendered.lower().split())


def contains_all(value: object, terms: tuple[str, ...], message: str) -> None:
    rendered = normalized(value)
    missing = [term for term in terms if normalized(term) not in rendered]
    require(not missing, f"{message}: {', '.join(missing)}")


def validate(repo_root: Path) -> None:
    contract_path = repo_root / "contracts" / "cognition-execution.json"
    authority_path = repo_root / "contracts" / "authority-stack.json"
    stable_path = repo_root / "docs" / "architecture" / "COGNITION_EXECUTION.md"

    contract = load_object(contract_path)
    authority = load_object(authority_path)

    require(
        contract.get("schema") == "laplace.cognition-execution/v1",
        "cognition execution schema drift",
    )

    contract_authority = contract.get("authority", {})
    contains_all(
        contract_authority,
        (
            "docs/architecture/COGNITION_EXECUTION.md",
            "LP-COGNITION-004",
            "LP-QUERY-001",
            "LP-SEARCH-001",
            "LP-CONNECTION-001",
            "LP-LIMITS-001",
            "LP-BULK-001",
        ),
        "cognition execution authority lost required joins",
    )

    contains_all(
        contract.get("terms", {}),
        (
            "finite query-relative native cognition execution",
            "typed filtered indexed search",
            "typed filtered indexed A*",
            "g+h",
            "declared bounded/best-first search",
        ),
        "cognition execution terminology drift",
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
            "repeat while unresolved obligations and finite resources permit",
            "typed why-not",
            "realize",
            "witness",
        ),
        "cognition execution spine was narrowed",
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
        "cognition state classes were flattened",
    )

    contains_all(
        contract.get("provider_law", {}),
        (
            "set-wise bounded typed working set",
            "compiled transition law",
            "push hard predicates",
            "never become truth",
            "exact native predicates",
            "miss is non-authoritative",
            "trajectory",
            "semantic relation",
            "evidence epoch",
            "dependence root",
            "typed standing lane",
            "GiST",
            "perfcache",
        ),
        "indexed provider/filter law was weakened",
    )

    contains_all(
        contract.get("astar", {}),
        (
            "f(s) = g(s) + h(s)",
            "nonnegative declared transition costs",
            "admissible heuristic",
            "reopen semantics",
            "complete typed state identity",
            "deterministic tie",
            "requested path multiplicity",
            "finite evidence world authority resource and search boundary",
            "terminal completion certificate",
            "known upper bound",
            "partial",
            "unknown",
            "exhausted",
        ),
        "A-star proof/why-not law was weakened",
    )

    contains_all(
        contract.get("sql_native_boundary", {}),
        (
            "C/C++ and PostgreSQL server engine owns semantic calculation search and cognition",
            "durable state transactions constraints partitions statistics indexes",
            "typed set-oriented orchestration",
            "prepared parameterized reusable set-oriented",
        ),
        "native/PostgreSQL/SQL ownership drift",
    )

    forbidden = contract.get("forbidden_primary_path_mechanisms", [])
    contains_all(
        forbidden,
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
        "forbidden cognition hot-path substitutes are incomplete",
    )

    contains_all(
        contract.get("allowed_drain", ""),
        ("epoch-pinned readers", "retired immutable perfcache generation", "not semantic maintenance"),
        "legitimate perfcache reader drain was lost or conflated with semantic drains",
    )
    contains_all(
        contract.get("allowed_dynamic_construction", ""),
        ("administrative", "DDL", "nonsemantic boundary", "may not become cognition"),
        "dynamic construction boundary is ambiguous",
    )

    contains_all(
        contract.get("edit_dispositions", {}),
        (
            "canonical content unchanged",
            "occurrence or physicality state",
            "canonical endpoints unchanged",
            "new canonical content identity",
            "new exact composition",
            "exact declared content",
            "probabilistic regeneration",
        ),
        "exact rise/edit/descent law was narrowed",
    )

    contains_all(
        contract.get("required_execution_receipt", []),
        (
            "compiled program and query identity",
            "world evidence source time context and authority boundary",
            "transition-provider families",
            "index partition and perfcache generation",
            "frontier states in and out",
            "candidate rows generated",
            "rejected by hard filters",
            "exact candidate states accepted",
            "rows examined",
            "heap or table fetches",
            "server SPI and native crossings",
            "g and h evaluations",
            "dominance prunes",
            "dependence and contradiction prunes",
            "reopen events",
            "path count",
            "peak frontier width",
            "peak working memory",
            "CPU time",
            "I/O time",
            "elapsed time",
            "optimality or upper-bound disposition",
            "continuation condition",
        ),
        "cognition performance/receipt observability was narrowed",
    )

    contains_all(
        contract.get("postgresql_evidence", []),
        (
            "EXPLAIN ANALYZE BUFFERS WAL SETTINGS TIMING",
            "pg_stat_statements",
            "index and partition plan identity",
            "logical-result parity",
            "measured performance delta",
        ),
        "PostgreSQL plan evidence was weakened",
    )

    contains_all(
        contract.get("deliberate_defects", []),
        (
            "raw-hop or fixed-fanout",
            "endpoint-only",
            "g-only priority",
            "known path reported as shortest",
            "universal adjacency",
            "per-frontier-state SQL or SPI",
            "recursive SQL",
            "dynamic per-candidate SQL",
            "scalar or RBAR fallback",
            "dropped source context time evidence or authority filter",
            "dependent copies",
            "accelerator miss",
            "maintenance or repair drain",
            "unbounded adjacency load or full-corpus scan",
            "result parity accepted without",
        ),
        "cognition deliberate-defect coverage was narrowed",
    )

    order = authority.get("required_load_order", [])
    require(isinstance(order, list) and order, "authority stack has no required load order")
    paths = [entry.get("path") for entry in order if isinstance(entry, dict)]
    require(
        "docs/architecture/COGNITION_EXECUTION.md" in paths,
        "stable cognition execution law is missing from authority load order",
    )
    require(
        "contracts/cognition-execution.json" in paths,
        "cognition execution contract is missing from authority load order",
    )
    require(
        paths.index("docs/product/INVENTION_MODEL.md")
        < paths.index("docs/architecture/COGNITION_EXECUTION.md")
        < paths.index("AGENTS.md"),
        "stable cognition execution law is not loaded after invention and before agent projection",
    )
    require(
        paths.index("requirements/product.yaml")
        < paths.index("contracts/cognition-execution.json")
        < paths.index("contracts/operation-model.json"),
        "cognition execution contract load order bypasses product requirements or operation model",
    )

    stable = text(stable_path)
    contains_all(
        stable,
        (
            "single operational description",
            "not autoregressive token-by-token",
            "typed filtered indexed search",
            "planner does not first load a large generic neighborhood",
            "frontier is a typed working set",
            "bubble up, change the typed state, bubble back down",
            "recursive SQL/CTEs",
            "dynamic SQL strings generated per candidate",
            "semantic repair",
            "Epoch-pinned readers",
            "EXPLAIN (ANALYZE, BUFFERS, WAL",
            "result parity without the required plan/crossing/frontier/performance receipt",
        ),
        "stable cognition execution document lost a required boundary",
    )

    projections = {
        "README.md": (
            "native cognition is a finite query-relative execution",
            "typed filtered indexed A*",
            "recursive sql walk",
            "is not a second cognition engine",
        ),
        "AGENTS.md": (
            "cognition execution and database hot paths",
            "typed filtered indexed search",
            "silent scalar fallback",
            "complexity is an acceptance contract",
        ),
        "docs/architecture/ENGINEERING_STANDARDS.md": (
            "typed filtered indexed cognition/search",
            "dynamic sql generated per candidate",
            "semantic repair",
            "logically correct result reached by a forbidden physical plan fails",
        ),
        "requirements/features/query_neighborhood.feature": (
            "typed filtered indexed state search",
            "filter-first",
            "recursive sql or cte search",
            "same logical path",
        ),
    }
    for relative, terms in projections.items():
        contains_all(text(repo_root / relative), terms, f"cognition execution projection drift in {relative}")


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
    print("verified cognition execution authority, projections, hot-path prohibitions, and receipts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
