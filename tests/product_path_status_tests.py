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

    def test_deliberate_defect_dropping_type_changes_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        mutant = workflow.replace("--diff-filter=ACMRTD", "--diff-filter=ACMRD")
        self.assertNotEqual(workflow, mutant)
        self.assertNotIn("--diff-filter=ACMRTD", mutant)


if __name__ == "__main__":
    unittest.main()
