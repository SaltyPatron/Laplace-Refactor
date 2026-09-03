#!/usr/bin/env python3
"""Protect inventor-authored original Laplace product law from summary substitution."""

from __future__ import annotations

import copy
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUTHORITY_PATH = ROOT / "contracts" / "authority-stack.json"
LINEAGE_PATH = ROOT / "contracts" / "original-laplace-invention-lineage.json"


def load(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path} is not a JSON object")
    return value


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def contains_all(value: object, terms: tuple[str, ...], message: str) -> None:
    text = json.dumps(value, sort_keys=True).lower()
    missing = [term for term in terms if term.lower() not in text]
    require(not missing, f"{message}: {', '.join(missing)}")


def validate(authority: dict, lineage: dict) -> None:
    require(
        lineage.get("schema") == "laplace.original-invention-lineage/v1",
        "original invention lineage schema drift",
    )
    require(lineage.get("source_repository") == "SaltyPatron/Laplace", "original repository drift")
    require(
        lineage.get("pinned_commit") == "d2f5a9bfeda117c65a82cea2d02bc769d97de8cc",
        "original Laplace commit is no longer pinned",
    )
    contains_all(
        lineage.get("authority", {}),
        (
            "inventor-authored",
            "remain product-law evidence",
            "historical implementation code",
            "not automatically clean-refactor implementation authority",
            "derived Refactor summary",
            "cannot silently narrow",
            "must be classified",
            "absence of reconciliation is an implementation defect",
            "Laplace is the integrated invention",
        ),
        "original invention authority was weakened",
    )

    expected = {
        "docs/INVENTIONS.md": "8649af6a653e7194b2eacf4634183ef381a33c77",
        "docs/specs/36_Laplace_Forward_Pass.md": "a9e13653cdca6c09fdeadd968100eb0ef1e364a4",
        "web/src/explore/browse/BrowseHome.tsx": "0e70fb8fcbbf24a4874dfd7decd7cdfd44c2d16c",
        "web/src/explore/entity/EntityDetail.tsx": "8f42fbc3f293bfe64c063f15e2db9f5c580b12f6",
    }
    actual = {item.get("path"): item.get("blob_sha") for item in lineage.get("artifacts", [])}
    require(actual == expected, "pinned original invention artifact set drift")
    contains_all(
        lineage.get("artifacts", []),
        (
            "Unicode/DUCET universal text floor",
            "S3 geometry",
            "Glicko epistemology",
            "dynamic-frontier generation",
            "consumer-role separation",
            "RESOLVE ORIENT ROUTE SCAN COMPOSE PROPOSE STEER SELECT REALIZE WITNESS",
            "each emitted constituent updates the next frontier",
            "2D and 3D neighborhood exploration",
            "Overview Graph Glome Structure Links Provenance and Export",
        ),
        "original invention or product-surface semantics were narrowed",
    )

    classes = authority.get("authority_classes", {})
    contains_all(
        classes.get("historical_inventor_product_law", ""),
        ("inventor-authored", "remain product-law evidence", "explicitly superseded"),
        "original inventor product law class was demoted",
    )
    contains_all(
        classes.get("historical_counterexample", ""),
        ("implementation", "runtime evidence", "cannot override inventor-authored product law", "ABI", "schema"),
        "historical implementation and inventor product law were reconflated",
    )

    order = authority.get("required_load_order", [])
    paths = [item.get("path") for item in order]
    require(
        paths[:4] == [
            "docs/audits/DIRECT_REQUIREMENT_EVIDENCE.md",
            "docs/product/CONSTITUTION.md",
            "docs/product/INVENTION_MODEL.md",
            "contracts/original-laplace-invention-lineage.json",
        ],
        "original invention lineage is not loaded before later derived synthesis",
    )
    lineage_entry = order[3]
    require(
        lineage_entry.get("class") == "historical_inventor_product_law",
        "original invention lineage has the wrong authority class",
    )
    contains_all(
        authority.get("work_selection_gate", []),
        ("pinned original Laplace invention lineage", "derived Refactor interpretation"),
        "work selection can ignore the original invention",
    )
    contains_all(
        authority.get("forbidden_substitutions", []),
        ("derived Refactor summary", "unreconciled inventor-authored original Laplace product law"),
        "derived summaries can still substitute for the invention",
    )

    controls = set(lineage.get("required_negative_controls", []))
    require(
        {
            "original-inventor-product-law-demoted-to-counterexample-only",
            "derived-refactor-summary-overrides-unreconciled-original-law",
            "historical-implementation-abi-copied-as-clean-authority",
            "flat-admin-table-substitutes-for-navigable-entity-world",
            "component-green-state-substitutes-for-whole-laplace",
        }.issubset(controls),
        "original invention negative controls are incomplete",
    )


class OriginalInventionAuthorityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.authority = load(AUTHORITY_PATH)
        self.lineage = load(LINEAGE_PATH)

    def test_original_invention_remains_load_bearing(self) -> None:
        validate(self.authority, self.lineage)

    def test_mutation_demoting_original_law_to_counterexample_is_rejected(self) -> None:
        mutant = copy.deepcopy(self.authority)
        mutant["authority_classes"]["historical_inventor_product_law"] = (
            "old repository is counterexample-only and cannot define product behavior"
        )
        with self.assertRaisesRegex(ValueError, "demoted"):
            validate(mutant, self.lineage)

    def test_mutation_removing_original_lineage_from_load_order_is_rejected(self) -> None:
        mutant = copy.deepcopy(self.authority)
        mutant["required_load_order"] = [
            item
            for item in mutant["required_load_order"]
            if item.get("path") != "contracts/original-laplace-invention-lineage.json"
        ]
        with self.assertRaisesRegex(ValueError, "not loaded"):
            validate(mutant, self.lineage)

    def test_mutation_flat_admin_table_can_replace_explore_is_rejected(self) -> None:
        mutant = copy.deepcopy(self.lineage)
        mutant["required_negative_controls"].remove(
            "flat-admin-table-substitutes-for-navigable-entity-world"
        )
        with self.assertRaisesRegex(ValueError, "negative controls"):
            validate(self.authority, mutant)


if __name__ == "__main__":
    unittest.main()
