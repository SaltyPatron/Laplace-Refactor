#!/usr/bin/env python3
"""Prove the installed gateway accepts the identities emitted by real controllers."""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
ORIGINAL_PATH = REPOSITORY / "tools/delivery/product_activation.py"
ADAPTER_PATH = REPOSITORY / "tools/delivery/product_activation_gateway.py"


def load(path: Path, name: str):
    specification = importlib.util.spec_from_file_location(name, path)
    if specification is None or specification.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(specification)
    sys.modules[name] = module
    specification.loader.exec_module(module)
    return module


original = load(ORIGINAL_PATH, "laplace_product_activation_original_tests")
adapter = load(ADAPTER_PATH, "laplace_product_activation_adapter_tests")
PACKAGE_ID = "42" * 32
MANIFEST_SHA = "43" * 32


class ProductActivationRuntimeAdapterTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = original.load_json(
            REPOSITORY / "contracts/product-activation-gateway.json"
        )

    def producer_receipt(self, document: dict, field: str) -> dict:
        result = copy.deepcopy(document)
        result[field] = adapter.producer_document_identity(result, field)
        return result

    def test_runtime_patches_all_four_cross_controller_receipt_boundaries(self) -> None:
        self.assertIs(
            adapter.activation.validate_package_installation,
            adapter.validate_package_installation,
        )
        self.assertIs(
            adapter.activation.validate_cluster_success,
            adapter.validate_cluster_success,
        )
        self.assertIs(
            adapter.activation.validate_unicode_success,
            adapter.validate_unicode_success,
        )
        self.assertIs(
            adapter.activation.validate_highway_success,
            adapter.validate_highway_success,
        )

    def test_real_package_installation_identity_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-runtime-adapter-package-") as temporary:
            source_root = Path(temporary) / "stage"
            source_root.mkdir()
            payload = {
                "package": {
                    "id": PACKAGE_ID,
                    "manifest_sha256": MANIFEST_SHA,
                    "source_root": str(source_root),
                }
            }
            release = f"{self.contract['product']['package_release_root']}/{PACKAGE_ID}"
            receipt = self.producer_receipt(
                {
                    "schema": self.contract["product"][
                        "package_installation_schema"
                    ],
                    "phase": "installed",
                    "package_id": PACKAGE_ID,
                    "package_manifest_sha256": MANIFEST_SHA,
                    "package_root": release,
                    "installation_root": "/",
                    "installed_release": release,
                    "source_physical_root": str(source_root.resolve()),
                    "source_package_verified": True,
                    "installed_package_verified": True,
                    "overwrite_performed": False,
                },
                "installation_receipt_sha256",
            )
            adapter.validate_package_installation(self.contract, receipt, payload)
            with self.assertRaises(original.ActivationGatewayError):
                original.validate_package_installation(self.contract, receipt, payload)

    def test_real_cluster_unicode_and_highway_identities_are_accepted(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-runtime-adapter-receipts-") as temporary:
            contract = copy.deepcopy(self.contract)
            evidence_root = Path(temporary) / "cluster-activation"
            contract["product"]["cluster_activation_root"] = str(evidence_root)
            plan = evidence_root / PACKAGE_ID / "cluster-plan.json"
            plan.parent.mkdir(parents=True)
            plan.write_text("{}\n", encoding="utf-8")
            cluster = self.producer_receipt(
                {
                    "schema": contract["operation"]["cluster_success_schema"],
                    "phase": contract["operation"]["cluster_success_phase"],
                    "package_id": PACKAGE_ID,
                    "restart_proven": True,
                    "active_target": f"releases/{PACKAGE_ID}",
                    "cluster_plan_path": str(plan),
                },
                "activation_receipt_sha256",
            )
            self.assertEqual(
                adapter.validate_cluster_success(contract, cluster, PACKAGE_ID), plan
            )
            with self.assertRaises(original.ActivationGatewayError):
                original.validate_cluster_success(contract, cluster, PACKAGE_ID)

            unicode = self.producer_receipt(
                {
                    "schema": contract["operation"]["unicode_success_schema"],
                    "phase": "product-activated",
                    "package_id": PACKAGE_ID,
                    "restart_proven": True,
                    "cold_public_readback_proven": True,
                    "reverse_inversion_proven": True,
                },
                "receipt_sha256",
            )
            adapter.validate_unicode_success(contract, unicode, PACKAGE_ID)
            with self.assertRaises(original.ActivationGatewayError):
                original.validate_unicode_success(contract, unicode, PACKAGE_ID)

            highway = self.producer_receipt(
                {
                    "schema": contract["operation"]["highway_success_schema"],
                    "phase": "product-activated",
                    "package_id": PACKAGE_ID,
                    "restart_proven": True,
                    "cold_application_readback_proven": True,
                },
                "receipt_sha256",
            )
            adapter.validate_highway_success(contract, highway, PACKAGE_ID)
            with self.assertRaises(original.ActivationGatewayError):
                original.validate_highway_success(contract, highway, PACKAGE_ID)

    def test_non_newline_identity_mutants_remain_rejected(self) -> None:
        unicode = {
            "schema": self.contract["operation"]["unicode_success_schema"],
            "phase": "product-activated",
            "package_id": PACKAGE_ID,
            "restart_proven": True,
            "cold_public_readback_proven": True,
            "reverse_inversion_proven": True,
        }
        unicode["receipt_sha256"] = original.document_identity(
            unicode, "receipt_sha256"
        )
        with self.assertRaisesRegex(
            adapter.activation.ActivationGatewayError, "not exact and complete"
        ):
            adapter.validate_unicode_success(self.contract, unicode, PACKAGE_ID)


if __name__ == "__main__":
    unittest.main(verbosity=2)
