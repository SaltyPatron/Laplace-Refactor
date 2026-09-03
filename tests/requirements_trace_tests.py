#!/usr/bin/env python3
"""Mutation checks for the requirement graph verifier."""

from __future__ import annotations

import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools/tests/requirements_trace.py"
SPEC = importlib.util.spec_from_file_location("requirements_trace", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
TRACE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = TRACE
SPEC.loader.exec_module(TRACE)


class RequirementTraceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "requirements/features").mkdir(parents=True)
        (self.root / "tests").mkdir()
        (self.root / "contracts").mkdir()
        shutil.copy2(REPO_ROOT / "requirements/product.yaml", self.root / "requirements/product.yaml")
        shutil.copy2(REPO_ROOT / "requirements/alignment.yaml", self.root / "requirements/alignment.yaml")
        shutil.copytree(
            REPO_ROOT / "requirements/features",
            self.root / "requirements/features",
            dirs_exist_ok=True,
        )
        shutil.copy2(REPO_ROOT / "tests/registry.json", self.root / "tests/registry.json")
        operation_model = REPO_ROOT / "contracts/operation-model.json"
        shutil.copy2(operation_model, self.root / "contracts/operation-model.json")
        for contract in (
            "authority-stack.json",
            "trust-matchup-realization.json",
            "source-recipe-preparation.json",
        ):
            shutil.copy2(REPO_ROOT / "contracts" / contract, self.root / "contracts" / contract)
        operation_document = json.loads(operation_model.read_text(encoding="utf-8"))
        for stage in operation_document["stages"]:
            for relative in stage["implementation"]["evidence"]:
                target = self.root / relative
                if not target.exists():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.touch()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def replace(self, relative: str, before: str, after: str) -> None:
        path = self.root / relative
        content = path.read_text(encoding="utf-8")
        self.assertIn(before, content)
        path.write_text(content.replace(before, after, 1), encoding="utf-8")

    def set_required_feature_files(self, feature_files: list[str]) -> None:
        path = self.root / "contracts/trust-matchup-realization.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        document["traceability"]["feature_files"] = feature_files
        path.write_text(json.dumps(document), encoding="utf-8")

    def test_current_graph_is_valid(self) -> None:
        report = TRACE.validate(self.root)
        self.assertGreater(report.direct_requirement_count, 0)
        self.assertGreater(report.registered_test_count, 0)
        self.assertGreater(report.operation_stage_count, 0)
        self.assertEqual(
            report.operationally_mapped_product_count,
            report.product_requirement_count,
        )
        self.assertEqual(report.required_authority_contract_count, 2)
        self.assertGreater(report.required_contract_scenario_count, 0)

    def test_required_contract_missing_from_authority_stack_is_rejected(self) -> None:
        path = self.root / "contracts/authority-stack.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        document["required_load_order"] = [
            entry
            for entry in document["required_load_order"]
            if entry["path"] != "contracts/trust-matchup-realization.json"
        ]
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "absent from authority load order"):
            TRACE.validate(self.root)

    def test_untagged_required_contract_scenario_is_rejected(self) -> None:
        path = self.root / "requirements/features/trust_matchup_realization.feature"
        content = path.read_text(encoding="utf-8")
        content = content.replace("  @LP-TEST-UNICODE-MACHINE-LANGUAGE\n", "", 1)
        path.write_text(content, encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "untagged scenario"):
            TRACE.validate(self.root)

    def test_required_contract_issue_missing_from_operation_graph_is_rejected(self) -> None:
        path = self.root / "contracts/trust-matchup-realization.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        document["traceability"]["github_issues"].append(999999)
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "absent from operation graph"):
            TRACE.validate(self.root)

    def test_required_feature_parent_traversal_is_rejected_even_when_target_exists(self) -> None:
        outside = self.root.parent / f"{self.root.name}-outside.feature"
        shutil.copy2(
            self.root / "requirements/features/trust_matchup_realization.feature",
            outside,
        )
        try:
            self.set_required_feature_files([f"../{outside.name}"])
            with self.assertRaisesRegex(TRACE.TraceError, "unsafe feature join"):
                TRACE.validate(self.root)
        finally:
            outside.unlink(missing_ok=True)

    def test_required_feature_symlink_escape_is_rejected(self) -> None:
        outside = self.root.parent / f"{self.root.name}-outside-symlink.feature"
        shutil.copy2(
            self.root / "requirements/features/trust_matchup_realization.feature",
            outside,
        )
        linked = self.root / "requirements/features/escaped.feature"
        linked.symlink_to(outside)
        try:
            self.set_required_feature_files(["requirements/features/escaped.feature"])
            with self.assertRaisesRegex(TRACE.TraceError, "unsafe feature join"):
                TRACE.validate(self.root)
        finally:
            outside.unlink(missing_ok=True)

    def test_unknown_product_join_is_rejected(self) -> None:
        self.replace(
            "requirements/alignment.yaml",
            "      - LP-PRODUCT-001\n",
            "      - LP-NOT-DECLARED-001\n",
        )
        with self.assertRaisesRegex(TRACE.TraceError, "unknown product requirements"):
            TRACE.validate(self.root)

    def test_unjoined_product_requirement_is_rejected(self) -> None:
        alignment = self.root / "requirements/alignment.yaml"
        content = alignment.read_text(encoding="utf-8")
        content = content.replace("      - LP-UNIVERSAL-001\n", "")
        alignment.write_text(content, encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "no alignment domain"):
            TRACE.validate(self.root)

    def test_unknown_registry_evidence_is_rejected(self) -> None:
        path = self.root / "tests/registry.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        document["tests"][0]["evidence_targets"] = ["LP-TEST-NOT-DECLARED"]
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "unknown evidence targets"):
            TRACE.validate(self.root)

    def test_unknown_feature_evidence_is_rejected(self) -> None:
        feature = next((self.root / "requirements/features").glob("*.feature"))
        feature.write_text(
            "@LP-TEST-NOT-DECLARED\n" + feature.read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(TRACE.TraceError, "unknown evidence target"):
            TRACE.validate(self.root)

    def operation_document(self) -> tuple[Path, dict[str, object]]:
        path = self.root / "contracts/operation-model.json"
        return path, json.loads(path.read_text(encoding="utf-8"))

    def test_product_requirement_without_operational_stage_is_rejected(self) -> None:
        path, document = self.operation_document()
        target = "LP-PRODUCT-001"
        for stage in document["stages"]:
            stage["product_requirements"] = [
                identifier
                for identifier in stage["product_requirements"]
                if identifier != target
            ]
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "no operational stage"):
            TRACE.validate(self.root)

    def test_unknown_operation_dependency_is_rejected(self) -> None:
        path, document = self.operation_document()
        document["stages"][0]["depends_on"] = ["missing.stage"]
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "unknown dependencies"):
            TRACE.validate(self.root)

    def test_operation_dependency_cycle_is_rejected(self) -> None:
        path, document = self.operation_document()
        document["stages"][0]["depends_on"] = [document["stages"][1]["id"]]
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "dependency cycle"):
            TRACE.validate(self.root)

    def test_framework_before_acquisition_is_rejected(self) -> None:
        path, document = self.operation_document()
        framework = next(
            stage for stage in document["stages"] if stage["id"] == "framework.execution"
        )
        framework["depends_on"] = []
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "acquired build inputs"):
            TRACE.validate(self.root)

    def test_operation_stage_without_program_tracking_is_rejected(self) -> None:
        path, document = self.operation_document()
        document["stages"][0].pop("github_issues")
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "invalid GitHub issues"):
            TRACE.validate(self.root)

    def test_unowned_tracked_issue_is_rejected(self) -> None:
        path, document = self.operation_document()
        document["tracking"]["tracked_issues"].append(999999)
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "no operational stage"):
            TRACE.validate(self.root)

    def test_historical_session_audit_cannot_be_product_tracking_authority(self) -> None:
        path, document = self.operation_document()
        document["tracking"]["session_audit_issue"] = 24
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "historical session audit"):
            TRACE.validate(self.root)


if __name__ == "__main__":
    unittest.main()
