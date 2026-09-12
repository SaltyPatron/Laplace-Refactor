#!/usr/bin/env python3
"""Acceptance and deliberate-defect tests for persistent receipt convergence."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/receipt_estate.py"
SPEC = importlib.util.spec_from_file_location("laplace_receipt_estate", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
ESTATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ESTATE)


class ReceiptEstateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-receipt-estate-")
        self.root = Path(self.temporary.name)
        self.receipts = self.root / "opt/laplace/receipts"
        self.instance = self.receipts / "postgresql/refactor"
        self.receipts.mkdir(parents=True)
        self.instance.mkdir(parents=True)
        self.contract = {
            "instance": {
                "receipt_directory": "/opt/laplace/receipts/postgresql/refactor"
            }
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_json(self, path: Path, value: dict) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")

    def installation(self, package_id: str) -> dict:
        return {
            "schema": ESTATE.INSTALLATION_SCHEMA,
            "phase": "installed",
            "package_id": package_id,
            "installation_receipt_sha256": "a" * 64,
        }

    def test_legacy_package_plan_and_product_receipts_converge_to_one_package_tree(self) -> None:
        package_id = "1" * 64
        self.write_json(
            self.receipts / "packages" / f"{package_id}.json",
            self.installation(package_id),
        )
        self.write_json(
            self.receipts / "packages" / f"{package_id}.replay.json",
            self.installation(package_id),
        )
        self.write_json(
            self.receipts / "plans" / package_id / "host-inventory.json",
            {"schema": "inventory"},
        )
        self.write_json(
            self.receipts / "plans" / package_id / "host-selection.json",
            {"schema": "selection"},
        )
        self.write_json(
            self.receipts / "plans" / package_id / "resource-observation.json",
            {"schema": "laplace.execution-resource-observation/v2", "package_id": package_id},
        )
        self.write_json(
            self.instance / "release-capacity" / package_id / "capacity.json",
            {"schema": "laplace.product-release-capacity/v1", "successor_package_id": package_id},
        )
        self.write_json(
            self.instance / "product-cognition" / package_id / "installed-proof.json",
            {"schema": "laplace.installed-product-cognition-proof/v1", "package_id": package_id},
        )
        self.write_json(
            self.instance / "unicode-product-activation.json",
            {"schema": "laplace.unicode-product-activation-receipt/v1", "package_id": package_id},
        )
        self.write_json(
            self.instance / "highway-product-activation.json",
            {"schema": "laplace.highway-product-activation-receipt/v1", "package_id": package_id},
        )
        (self.receipts / "deployments").mkdir()

        result = ESTATE.converge(self.root, self.contract)
        canonical = self.instance / "cluster-activation" / package_id
        expected = {
            "package-installation.json",
            "package-installation-replay.json",
            "host-inventory.json",
            "host-selection.json",
            "resource-observation.json",
            "release-capacity.json",
            "installed-cognition-proof.json",
            "unicode-product-activation.json",
            "highway-product-activation.json",
        }
        self.assertEqual({path.name for path in canonical.iterdir()}, expected)
        self.assertFalse((self.receipts / "packages").exists())
        self.assertFalse((self.receipts / "plans").exists())
        self.assertFalse((self.instance / "release-capacity").exists())
        self.assertFalse((self.instance / "product-cognition").exists())
        self.assertFalse((self.instance / "unicode-product-activation.json").exists())
        self.assertFalse((self.instance / "highway-product-activation.json").exists())
        self.assertFalse((self.receipts / "deployments").exists())
        self.assertEqual(result["migration_count"], len(expected))
        self.assertEqual(result["preserved_unknown_or_conflicting"], [])

    def test_instance_plan_resource_observation_moves_into_activation_generation(self) -> None:
        package_id = "2" * 64
        source = self.instance / "plan" / package_id / "resource-observation.json"
        self.write_json(
            source,
            {"schema": "laplace.execution-resource-observation/v2", "package_id": package_id},
        )
        ESTATE.converge(self.root, self.contract)
        target = self.instance / "cluster-activation" / package_id / "resource-observation.json"
        self.assertTrue(target.is_file())
        self.assertFalse((self.instance / "plan").exists())

    def test_equal_existing_canonical_bytes_are_deduplicated(self) -> None:
        package_id = "3" * 64
        source = self.receipts / "packages" / f"{package_id}.json"
        target = self.instance / "cluster-activation" / package_id / "package-installation.json"
        document = self.installation(package_id)
        self.write_json(source, document)
        self.write_json(target, document)
        result = ESTATE.converge(self.root, self.contract)
        self.assertFalse(source.exists())
        self.assertEqual(result["migrated"][0]["action"], "deduplicated")

    def test_conflicting_canonical_bytes_fail_closed_without_deleting_legacy(self) -> None:
        package_id = "4" * 64
        source = self.receipts / "packages" / f"{package_id}.json"
        target = self.instance / "cluster-activation" / package_id / "package-installation.json"
        self.write_json(source, self.installation(package_id))
        changed = self.installation(package_id)
        changed["installation_receipt_sha256"] = "b" * 64
        self.write_json(target, changed)
        with self.assertRaisesRegex(ESTATE.ReceiptEstateError, "conflicts with canonical"):
            ESTATE.converge(self.root, self.contract)
        self.assertTrue(source.is_file())
        self.assertTrue(target.is_file())

    def test_mismatched_package_identity_is_not_readdressed(self) -> None:
        addressed = "5" * 64
        actual = "6" * 64
        source = self.receipts / "packages" / f"{addressed}.json"
        self.write_json(source, self.installation(actual))
        with self.assertRaisesRegex(ESTATE.ReceiptEstateError, "differs from its legacy address"):
            ESTATE.converge(self.root, self.contract)
        self.assertTrue(source.is_file())

    def test_unknown_legacy_entries_are_preserved(self) -> None:
        unknown = self.receipts / "plans" / "not-a-package"
        unknown.mkdir(parents=True)
        (unknown / "mystery").write_text("preserve\n", encoding="utf-8")
        result = ESTATE.converge(self.root, self.contract)
        self.assertTrue(unknown.is_dir())
        self.assertIn(str(unknown), result["preserved_unknown_or_conflicting"])

    def test_nonempty_deployments_is_preserved(self) -> None:
        deployments = self.receipts / "deployments"
        deployments.mkdir()
        (deployments / "unknown.json").write_text("{}\n", encoding="utf-8")
        result = ESTATE.converge(self.root, self.contract)
        self.assertTrue(deployments.is_dir())
        self.assertIn(str(deployments), result["preserved_unknown_or_conflicting"])


if __name__ == "__main__":
    unittest.main()
