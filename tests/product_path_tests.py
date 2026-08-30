#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import sys
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/product_path.py"
SPEC = importlib.util.spec_from_file_location("laplace_product_path_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product-path module")
product_path = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = product_path
SPEC.loader.exec_module(product_path)


class ProductPathTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = product_path.load_json(REPOSITORY / "contracts/product-path.json")
        product_path.validate_contract(self.contract)

    def classify(self, *paths: str) -> dict:
        return product_path.classify(self.contract, list(paths))

    def test_docs_only_never_requires_physical_runner(self) -> None:
        result = self.classify("README.md", "docs/product/ROADMAP.md")
        self.assertTrue(result["hosted_only"])
        self.assertEqual(result["required_evidence"], ["hosted"])
        self.assertFalse(result["requires_custom_stack"])
        self.assertFalse(result["blocked"])

    def test_native_change_requires_custom_stack(self) -> None:
        result = self.classify("engine/src/composition.cpp")
        self.assertFalse(result["hosted_only"])
        self.assertIn("native", result["classes"])
        self.assertEqual(result["required_evidence"], ["custom-stack", "hosted"])
        self.assertTrue(result["requires_custom_stack"])
        self.assertFalse(result["blocked"])

    def test_postgresql_change_cannot_green_without_exact_product_provider(self) -> None:
        result = self.classify("postgres/extension/src/composition_pg.c")
        self.assertIn("postgresql", result["classes"])
        self.assertTrue(result["requires_custom_stack"])
        self.assertTrue(result["requires_postgresql_product"])
        self.assertIn("postgresql-product", result["unimplemented_evidence"])
        self.assertTrue(result["blocked"])

    def test_package_change_cannot_green_without_exact_package_provider(self) -> None:
        result = self.classify("tools/product/build-package.py")
        self.assertIn("package", result["classes"])
        self.assertTrue(result["requires_package_product"])
        self.assertIn("package-product", result["unimplemented_evidence"])
        self.assertTrue(result["blocked"])

    def test_mixed_docs_and_semantic_change_is_semantic(self) -> None:
        result = self.classify("docs/product/ROADMAP.md", "managed/Laplace.Managed/Isa.cs")
        self.assertFalse(result["hosted_only"])
        self.assertIn("managed", result["classes"])
        self.assertTrue(result["requires_custom_stack"])

    def test_contract_and_workflow_changes_require_custom_stack(self) -> None:
        result = self.classify(
            "contracts/product-path.json", ".github/workflows/product-path.yml"
        )
        self.assertEqual(result["classes"], ["ci-test", "contract"])
        self.assertTrue(result["requires_custom_stack"])
        self.assertFalse(result["blocked"])

    def test_unknown_path_fails_closed_into_product_semantics(self) -> None:
        result = self.classify("new-runtime-surface.bin")
        self.assertEqual(result["classes"], ["product-semantic"])
        self.assertEqual(result["unmatched_semantic_paths"], ["new-runtime-surface.bin"])
        self.assertTrue(result["requires_custom_stack"])
        self.assertFalse(result["blocked"])

    def test_noncanonical_paths_are_rejected(self) -> None:
        for path in ("../engine/src/composition.cpp", "/tmp/file", "engine/../README.md"):
            with self.subTest(path=path), self.assertRaises(product_path.ProductPathError):
                self.classify(path)

    def test_missing_provider_reference_is_rejected(self) -> None:
        broken = copy.deepcopy(self.contract)
        broken["class_rules"][0]["evidence"].append("invented-provider")
        with self.assertRaisesRegex(product_path.ProductPathError, "unknown evidence"):
            product_path.validate_contract(broken)

    def test_unimplemented_provider_cannot_be_mutated_into_success(self) -> None:
        mutated = copy.deepcopy(self.contract)
        provider = next(
            row for row in mutated["evidence"] if row["id"] == "postgresql-product"
        )
        provider.pop("implemented")
        with self.assertRaisesRegex(product_path.ProductPathError, "implementation state"):
            product_path.validate_contract(mutated)


if __name__ == "__main__":
    unittest.main()
