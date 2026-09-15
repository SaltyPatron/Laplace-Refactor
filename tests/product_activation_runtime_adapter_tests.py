#!/usr/bin/env python3
"""Prove the installed gateway accepts the identities emitted by real controllers."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

import highway_committed_revalidation_tests as recovery_fixture


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


def producer_revalidation(test: unittest.TestCase) -> dict:
    fixture = recovery_fixture.CommittedRevalidationTests()
    fixture.setUp()
    test.addCleanup(fixture.doCleanups)
    # Consumers receive serialized evidence, which has no Python object aliases.
    return json.loads(json.dumps(fixture.execute()))


def resign_revalidation(receipt: dict) -> None:
    receipt["request_sha256"] = original.sha256_bytes(original.canonical_bytes(receipt["revalidation_request"]) + b"\n")
    receipt["receipt_sha256"] = adapter.producer_document_identity(receipt, "receipt_sha256")


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

    def test_current_native_revalidation_traverses_gateway_runner_and_distinct_aggregate(self) -> None:
        receipt = producer_revalidation(self)
        package = receipt["package_id"]
        original.validate_highway_success(self.contract, receipt, package)
        adapter.validate_highway_success(self.contract, receipt, package)
        runner = load(REPOSITORY / "tools/delivery/product_activation_runner.py", "laplace_revalidation_runner_tests")
        runner.validate_highway_result(receipt, package)
        aggregate = original.highway_aggregate_fields(receipt)
        original.validate_product_terminal_result(aggregate)
        self.assertEqual(aggregate["phase"], original.HIGHWAY_REVALIDATION_PHASE)
        self.assertNotIn("highway_activation_receipt_sha256", aggregate)
        self.assertFalse(aggregate["highway_activation_performed"])
        for name in ("stored_working_set_receipt", "stored_producer_receipt",
                     "historical_composition_receipt_present", "historical_intermediate_receipts_verified"):
            self.assertEqual(aggregate["highway_" + name], receipt["revalidation"][name])
        self.assertNotEqual(receipt["revalidation"]["stored_isa_receipt"], receipt["revalidation"]["current_isa_receipt"])
        self.assertNotEqual(receipt["readback"]["isa_receipt"], receipt["revalidation"]["stored_isa_receipt"])

    def test_revalidation_rejects_resigned_false_history_native_input_and_readback(self) -> None:
        receipt = producer_revalidation(self)
        mutations = {
            "historical request invented": lambda r: r.update(historical_request_present=True),
            "activation invented": lambda r: r.update(activation_performed=True),
            "activation alias": lambda r: r.update(activation=r["revalidation"]),
            "native proof omitted": lambda r: r.pop("revalidation"),
            "cold native proof omitted": lambda r: r.pop("cold_revalidation"),
            "cold native identity changed": lambda r: r["cold_revalidation"].update(stored_generation_fingerprint="f8" * 32),
            "cold native status type changed": lambda r: r["cold_revalidation"].update(status=False),
            "cold native SQL changed": lambda r: next(c for c in r["command_receipts"] if c["label"] == "cold-revalidate-committed-highway-registry").update(stdin_sha256="f9" * 32),
            "zero native receipt": lambda r: r["revalidation"].update(verification_receipt="00" * 32),
            "wrong physicality width": lambda r: r["revalidation"].update(root_physicality_id="ab" * 16),
            "native activation": lambda r: r["revalidation"].update(activation_performed=True),
            "historical verification invented": lambda r: r["revalidation"].update(historical_intermediate_receipts_verified=True),
            "historical verification mistyped": lambda r: r["revalidation"].update(historical_intermediate_receipts_verified=0),
            "composition presence mistyped": lambda r: r["revalidation"].update(historical_composition_receipt_present=0),
            "composition presence omitted": lambda r: r["revalidation"].pop("historical_composition_receipt_present"),
            "historical verification omitted": lambda r: r["revalidation"].pop("historical_intermediate_receipts_verified"),
            "stored working set omitted": lambda r: r["revalidation"].pop("stored_working_set_receipt"),
            "stored producer omitted": lambda r: r["revalidation"].pop("stored_producer_receipt"),
            "stored working set zero": lambda r: r["revalidation"].update(stored_working_set_receipt="00" * 32),
            "stored producer width differs": lambda r: r["revalidation"].update(stored_producer_receipt="ab" * 16),
            "cold native stored working set changed": lambda r: r["cold_revalidation"].update(stored_working_set_receipt="f7" * 32),
            "cold native stored producer changed": lambda r: r["cold_revalidation"].update(stored_producer_receipt="f7" * 32),
            "cold native composition presence changed": lambda r: r["cold_revalidation"].update(historical_composition_receipt_present=True),
            "cold native historical verification changed": lambda r: r["cold_revalidation"].update(historical_intermediate_receipts_verified=True),
            "empty reconstruction": lambda r: r["revalidation"].update(canonical_entity_count=0),
            "changed current numeric epoch": lambda r: r["revalidation_request"]["context_epochs"].update(numeric_epoch="f1" * 32),
            "changed execution context": lambda r: r["revalidation_request"]["execution_context"].update(cpu_slots=100),
            "unbound native SQL": lambda r: r["revalidation_request"].update(native_sql_sha256="f2" * 32),
            "wrong restart observation": lambda r: r["revalidation_request"].update(loaded_before_observation_sha256="f3" * 32),
            "changed stored readback": lambda r: r["readback"].update(activation_receipt="f4" * 32),
            "empty read ISA": lambda r: r["readback"].update(isa_receipt="00" * 32),
            "missing projection": lambda r: r["readback"]["kind_ids"].pop(),
            "restart not proven": lambda r: r.update(restart_proven=False),
            "cold readback not proven": lambda r: r.update(cold_application_readback_proven=False),
            "failed native execution": lambda r: next(c for c in r["command_receipts"] if c["label"] == "revalidate-committed-highway-registry").update(exit_code=1),
            "missing restart command": lambda r: r.update(command_receipts=[c for c in r["command_receipts"] if c["label"] != "restart-after-highway-revalidation"]),
            "wrong selected system": lambda r: r.update(system_identifier="12345"),
        }
        for name, mutate in mutations.items():
            with self.subTest(defect=name):
                invalid = copy.deepcopy(receipt)
                mutate(invalid)
                if ("revalidation" in invalid and invalid["revalidation"] != receipt["revalidation"]
                        and not name.startswith("cold native")):
                    invalid["cold_revalidation"] = copy.deepcopy(invalid["revalidation"])
                resign_revalidation(invalid)
                with self.assertRaises(adapter.activation.ActivationGatewayError):
                    adapter.validate_highway_success(self.contract, invalid, invalid["package_id"])

    def test_composition_presence_is_supported_without_promoting_historical_coverage(self) -> None:
        receipt = producer_revalidation(self)
        for present in (False, True):
            with self.subTest(present=present):
                for field in ("revalidation", "cold_revalidation"):
                    receipt[field]["historical_composition_receipt_present"] = present
                resign_revalidation(receipt)
                adapter.validate_highway_success(self.contract, receipt, receipt["package_id"])
                aggregate = original.highway_aggregate_fields(receipt)
                original.validate_product_terminal_result(aggregate)
                self.assertIs(aggregate["highway_historical_composition_receipt_present"], present)
                self.assertIs(aggregate["highway_historical_intermediate_receipts_verified"], False)
                for name, value in (("highway_historical_composition_receipt_present", 0),
                                    ("highway_historical_intermediate_receipts_verified", True),
                                    ("highway_historical_intermediate_receipts_verified", 0),
                                    ("highway_stored_working_set_receipt", "00" * 32),
                                    ("highway_stored_producer_receipt", "ab" * 16)):
                    with self.subTest(field=name, value=value), self.assertRaises(original.ActivationGatewayError):
                        original.validate_product_terminal_result({**aggregate, name: value})
                for name in ("highway_stored_working_set_receipt", "highway_stored_producer_receipt",
                             "highway_historical_composition_receipt_present", "highway_historical_intermediate_receipts_verified"):
                    invalid = dict(aggregate)
                    invalid.pop(name)
                    with self.subTest(missing=name), self.assertRaises(original.ActivationGatewayError):
                        original.validate_product_terminal_result(invalid)

    def test_revalidation_requires_explicit_pinned_contract_and_distinct_aggregate(self) -> None:
        receipt = producer_revalidation(self)
        for policy in (None, {"contract_sha256": "ab" * 32}):
            invalid = copy.deepcopy(self.contract)
            if policy is None:
                invalid["operation"].pop("highway_revalidation")
            else:
                invalid["operation"]["highway_revalidation"] = policy
            with self.assertRaisesRegex(adapter.activation.ActivationGatewayError, "explicitly selected"):
                adapter.validate_highway_success(invalid, receipt, receipt["package_id"])
        aggregate = original.highway_aggregate_fields(receipt)
        aggregate["highway_activation_receipt_sha256"] = receipt["receipt_sha256"]
        with self.assertRaisesRegex(original.ActivationGatewayError, "mislabeled"):
            original.validate_product_terminal_result(aggregate)
        aggregate.pop("highway_activation_receipt_sha256")
        aggregate["phase"] = "product-unicode-and-highway-activated"
        with self.assertRaises(original.ActivationGatewayError):
            original.validate_product_terminal_result(aggregate)

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
