#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/product_path.py"
WORKFLOW_PATH = REPOSITORY / ".github/workflows/product-path.yml"
CLEAN_ROOM_PATH = REPOSITORY / ".github/workflows/ci.yml"
CUSTOM_STACK_PATH = REPOSITORY / ".github/workflows/custom-stack.yml"
POSTGRESQL_PRODUCT_PATH = REPOSITORY / ".github/workflows/postgresql-product.yml"
PACKAGE_PRODUCT_PATH = REPOSITORY / ".github/workflows/package-product.yml"
PRODUCT_ACTIVATION_PATH = REPOSITORY / ".github/workflows/product-activation.yml"
SPEC = importlib.util.spec_from_file_location("laplace_product_path_status_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product-path module")
product_path = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = product_path
SPEC.loader.exec_module(product_path)


class ProductPathGitStatusTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = product_path.load_json(REPOSITORY / "contracts/product-path.json")

    def read_status(self, payload: bytes) -> list[str]:
        with tempfile.TemporaryDirectory() as temporary:
            status = Path(temporary) / "changed.status"
            status.write_bytes(payload)
            return product_path.read_git_name_status_z(status)

    def assert_physical_resource_serialization(self, workflows: dict[str, str]) -> None:
        for name, workflow in workflows.items():
            self.assertIn(
                "group: laplace-physical-product-proof",
                workflow,
                f"{name} lost shared physical-host resource ownership",
            )
            self.assertIn(
                "cancel-in-progress: false",
                workflow,
                f"{name} may cancel an in-flight physical proof",
            )

    def test_type_change_remains_semantic_and_requires_custom_stack(self) -> None:
        paths = self.read_status(b"T\0engine/src/composition.cpp\0")
        self.assertEqual(paths, ["engine/src/composition.cpp"])
        result = product_path.classify(self.contract, paths)
        self.assertIn("native", result["classes"])
        self.assertTrue(result["requires_custom_stack"])

    def test_copy_preserves_semantic_source_and_destination(self) -> None:
        paths = self.read_status(
            b"C100\0engine/src/composition.cpp\0docs/composition.md\0"
        )
        self.assertEqual(
            paths,
            ["engine/src/composition.cpp", "docs/composition.md"],
        )
        result = product_path.classify(self.contract, paths)
        self.assertIn("native", result["classes"])
        self.assertTrue(result["requires_custom_stack"])

    def test_unsupported_unmerged_status_fails_closed(self) -> None:
        with self.assertRaisesRegex(product_path.ProductPathError, "unsupported"):
            self.read_status(b"U\0engine/src/composition.cpp\0")

    def test_workflow_collects_type_changes(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("--diff-filter=ACMRTD", workflow)

    def test_product_path_is_only_verification_entrypoint(self) -> None:
        product_path_workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("  pull_request:\n", product_path_workflow)
        self.assertIn("  push:\n    branches:\n      - main\n", product_path_workflow)
        self.assertIn("github.event.before", product_path_workflow)
        self.assertIn("github.event.pull_request.base.sha", product_path_workflow)

        for path in (
            CLEAN_ROOM_PATH,
            CUSTOM_STACK_PATH,
            POSTGRESQL_PRODUCT_PATH,
            PACKAGE_PRODUCT_PATH,
        ):
            workflow = path.read_text(encoding="utf-8")
            self.assertIn("  workflow_call:\n", workflow)
            self.assertNotIn("  pull_request:\n", workflow)
            self.assertNotIn("  push:\n", workflow)

        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        self.assertIn("  workflow_dispatch:\n", activation)
        self.assertNotIn("  pull_request:\n", activation)
        self.assertNotIn("  push:\n", activation)

    def test_physical_product_proofs_are_serialized(self) -> None:
        workflows = {
            "custom-stack": CUSTOM_STACK_PATH.read_text(encoding="utf-8"),
            "postgresql-product": POSTGRESQL_PRODUCT_PATH.read_text(encoding="utf-8"),
            "package-product": PACKAGE_PRODUCT_PATH.read_text(encoding="utf-8"),
        }
        self.assert_physical_resource_serialization(workflows)

        orchestration = WORKFLOW_PATH.read_text(encoding="utf-8")
        custom = orchestration.index("  custom-stack-proof:")
        postgres = orchestration.index("  postgresql-product-proof:")
        package = orchestration.index("  package-product-proof:")
        self.assertLess(custom, postgres)
        self.assertLess(postgres, package)
        postgres_block = orchestration[postgres:package]
        package_block = orchestration[package:orchestration.index("  product-path:", package)]
        self.assertIn("      - custom-stack-proof", postgres_block)
        self.assertIn("      - custom-stack-proof", package_block)
        self.assertIn("      - postgresql-product-proof", package_block)

    def test_deliberate_defect_dropping_type_changes_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        mutant = workflow.replace("--diff-filter=ACMRTD", "--diff-filter=ACMRD")
        self.assertNotEqual(workflow, mutant)
        self.assertNotIn("--diff-filter=ACMRTD", mutant)

    def test_deliberate_parallel_physical_proof_defect_is_detected(self) -> None:
        workflows = {
            "custom-stack": CUSTOM_STACK_PATH.read_text(encoding="utf-8"),
            "postgresql-product": POSTGRESQL_PRODUCT_PATH.read_text(encoding="utf-8"),
            "package-product": PACKAGE_PRODUCT_PATH.read_text(encoding="utf-8"),
        }
        mutant = dict(workflows)
        mutant["postgresql-product"] = mutant["postgresql-product"].replace(
            "group: laplace-physical-product-proof",
            "group: postgresql-product-${{ github.ref }}",
            1,
        )
        with self.assertRaises(AssertionError):
            self.assert_physical_resource_serialization(mutant)

    def test_deliberate_duplicate_pr_trigger_defect_is_detected(self) -> None:
        clean_room = CLEAN_ROOM_PATH.read_text(encoding="utf-8")
        mutant = clean_room.replace(
            "on:\n  workflow_call:\n",
            "on:\n  workflow_call:\n  pull_request:\n",
            1,
        )
        self.assertIn("  pull_request:\n", mutant)
        self.assertNotIn("  pull_request:\n", clean_room)


if __name__ == "__main__":
    unittest.main()
