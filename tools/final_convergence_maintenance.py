#!/usr/bin/env python3
"""Apply fail-closed repairs discovered by final convergence acceptance.

This script exists only for the final convergence branch. Every edit is guarded by
an exact precondition so it cannot silently rewrite a changed file shape.
"""

from __future__ import annotations

import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]


def repair_tabular_wrapper() -> None:
    path = ROOT / "engine/src/tabular_source.cpp"
    text = path.read_text(encoding="utf-8")
    old = """        return recursive_admission::BuildRecursive(\n            input, providers, provider_count, plan);\n"""
    new = """        return recursive_admission::BuildRecursiveWithProviders(\n            input, providers, provider_count, plan);\n"""
    if text.count(old) != 1:
        raise RuntimeError(
            "expected exactly one stale generic source-decomposition wrapper call"
        )
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def repair_source_discovery_evidence() -> None:
    path = ROOT / "contracts/operation-model.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    stages = document.get("stages")
    if not isinstance(stages, list):
        raise RuntimeError("operation model lost stages array")

    matches = [
        stage for stage in stages
        if isinstance(stage, dict)
        and stage.get("id") == "world.discover-qualify-sources"
    ]
    if len(matches) != 1:
        raise RuntimeError(
            "expected exactly one world.discover-qualify-sources stage"
        )
    stage = matches[0]
    implementation = stage.get("implementation")
    if not isinstance(implementation, dict):
        raise RuntimeError("source-discovery stage lost implementation object")
    evidence = implementation.get("evidence")
    if not isinstance(evidence, list) or not all(
        isinstance(item, str) for item in evidence
    ):
        raise RuntimeError("source-discovery implementation evidence is invalid")

    retired = ".github/workflows/source-catalog.yml"
    if evidence.count(retired) != 1:
        raise RuntimeError(
            "expected exactly one retired source-catalog workflow evidence pointer"
        )
    evidence.remove(retired)

    # The reusable implementation and its direct positive/deliberate-defect tests
    # are the durable evidence. The deleted workflow was only an orchestration
    # wrapper around these surfaces.
    durable = [
        "contracts/source-discovery-catalog.json",
        "tools/sources/catalog-source-root.py",
        "tests/source_discovery_catalog_tests.py",
    ]
    for item in durable:
        if item not in evidence:
            evidence.append(item)

    path.write_text(
        json.dumps(document, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    try:
        repair_tabular_wrapper()
        repair_source_discovery_evidence()
    except Exception as error:
        print(f"final-convergence-maintenance: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
