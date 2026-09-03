#!/usr/bin/env python3
"""Mutation checks for the consolidated native cognition execution authority."""

from __future__ import annotations

import importlib.util
import json
import shutil
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools/tests/cognition_execution_trace.py"
SPEC = importlib.util.spec_from_file_location("cognition_execution_trace", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

ContractError = MODULE.ContractError
validate = MODULE.validate

REQUIRED_FILES = (
    "contracts/cognition-execution.json",
    "contracts/authority-stack.json",
    "docs/architecture/COGNITION_EXECUTION.md",
    "requirements/features/query_neighborhood.feature",
)


def copy_fixture(root: Path) -> None:
    for relative in REQUIRED_FILES:
        source = REPO_ROOT / relative
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def load_json(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise AssertionError(f"fixture is not a JSON object: {path}")
    return value


def write_json(path: Path, value: dict[str, object]) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def require_rejected(root: Path, label: str) -> None:
    try:
        validate(root)
    except ContractError:
        return
    raise AssertionError(f"cognition execution validator accepted mutant: {label}")


def mutate_remove_recursive_sql(root: Path) -> None:
    path = root / "contracts/cognition-execution.json"
    value = load_json(path)
    forbidden = value.get("forbidden_primary_path_mechanisms")
    if not isinstance(forbidden, list):
        raise AssertionError("fixture has no forbidden_primary_path_mechanisms")
    value["forbidden_primary_path_mechanisms"] = [
        item for item in forbidden if "recursive SQL" not in str(item)
    ]
    write_json(path, value)


def mutate_conflate_reader_drain(root: Path) -> None:
    path = root / "contracts/cognition-execution.json"
    value = load_json(path)
    value["allowed_drain"] = "all drains are equivalent and forbidden"
    write_json(path, value)


def mutate_remove_exact_edit(root: Path) -> None:
    path = root / "contracts/cognition-execution.json"
    value = load_json(path)
    edits = value.get("edit_dispositions")
    if not isinstance(edits, dict):
        raise AssertionError("fixture has no edit_dispositions")
    edits.pop("canonical_constituent_or_value", None)
    write_json(path, value)


def mutate_unload_contract(root: Path) -> None:
    path = root / "contracts/authority-stack.json"
    value = load_json(path)
    order = value.get("required_load_order")
    if not isinstance(order, list):
        raise AssertionError("fixture has no required_load_order")
    value["required_load_order"] = [
        entry
        for entry in order
        if not (
            isinstance(entry, dict)
            and entry.get("path") == "contracts/cognition-execution.json"
        )
    ]
    write_json(path, value)


def mutate_g_only_priority(root: Path) -> None:
    path = root / "contracts/cognition-execution.json"
    value = load_json(path)
    astar = value.get("astar")
    if not isinstance(astar, dict):
        raise AssertionError("fixture has no astar contract")
    astar["priority"] = "f(s) = g(s)"
    write_json(path, value)


def mutate_remove_setwise_acceptance(root: Path) -> None:
    path = root / "requirements/features/query_neighborhood.feature"
    content = path.read_text(encoding="utf-8")
    needle = "Frontier expansion is set-oriented"
    if needle not in content:
        raise AssertionError("query fixture lost set-oriented scenario")
    path.write_text(content.replace(needle, "Frontier expansion is row-driven"), encoding="utf-8")


def main() -> int:
    validate(REPO_ROOT)
    mutants = (
        ("recursive SQL allowed", mutate_remove_recursive_sql),
        ("reader drain conflated with semantic maintenance", mutate_conflate_reader_drain),
        ("canonical edit disposition removed", mutate_remove_exact_edit),
        ("cognition contract removed from authority order", mutate_unload_contract),
        ("A-star reduced to g-only priority", mutate_g_only_priority),
        ("set-wise frontier acceptance weakened", mutate_remove_setwise_acceptance),
    )

    for label, mutate in mutants:
        with tempfile.TemporaryDirectory(prefix="laplace-cognition-contract-") as directory:
            root = Path(directory)
            copy_fixture(root)
            mutate(root)
            require_rejected(root, label)

    print(f"verified cognition execution validator rejects {len(mutants)} deliberate defects")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
