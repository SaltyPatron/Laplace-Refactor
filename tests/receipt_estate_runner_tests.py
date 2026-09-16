#!/usr/bin/env python3
"""Acceptance tests for recurring runner-owned receipt convergence."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import pwd
import tempfile
import unittest
from unittest import mock

import product_activation_runtime_adapter_tests as recovery_fixture


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE = REPOSITORY / "tools/delivery/receipt_estate_runner.py"
SPEC = importlib.util.spec_from_file_location("laplace_receipt_estate_runner", MODULE)
assert SPEC is not None and SPEC.loader is not None
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class RunnerReceiptEstateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-runner-receipts-")
        self.root = Path(self.temporary.name)
        self.receipts = self.root / "receipts"
        self.instance = self.receipts / "postgresql/refactor"
        self.instance.mkdir(parents=True)
        self.contract = {"instance": {"receipt_directory": str(self.instance)}}
        self.account = pwd.getpwuid(os.geteuid())

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write(self, path: Path, value: dict) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value) + "\n", encoding="utf-8")

    def test_runner_converges_accessible_legacy_and_instance_package_evidence(self) -> None:
        package_id = "a" * 64
        self.write(
            self.receipts / "packages" / f"{package_id}.json",
            {
                "schema": RUNNER.estate.INSTALLATION_SCHEMA,
                "phase": "installed",
                "package_id": package_id,
                "installation_receipt_sha256": "1" * 64,
            },
        )
        self.write(
            self.receipts / "plans" / package_id / "host-selection.json",
            {"schema": "host-selection"},
        )
        (self.receipts / "deployments").mkdir()
        self.write(
            self.instance / "release-capacity" / package_id / "capacity.json",
            {"schema": "laplace.product-release-capacity/v1", "successor_package_id": package_id},
        )
        self.write(
            self.instance / "unicode-product-activation.json",
            {"schema": "unicode", "package_id": package_id},
        )
        self.write(
            self.instance / "highway-product-activation.json",
            {"schema": "highway", "package_id": package_id},
        )
        self.write(
            self.instance / "product-cognition" / package_id / "installed-proof.json",
            {"schema": "proof", "package_id": package_id},
        )
        with mock.patch.object(RUNNER, "RUNNER_USER", self.account.pw_name):
            result = RUNNER.converge(self.contract)
        generation = self.instance / "cluster-activation" / package_id
        self.assertTrue((generation / "package-installation.json").is_file())
        self.assertTrue((generation / "host-selection.json").is_file())
        self.assertTrue((generation / "release-capacity.json").is_file())
        self.assertTrue((generation / "unicode-product-activation.json").is_file())
        self.assertTrue((generation / "highway-product-activation.json").is_file())
        self.assertTrue((generation / "installed-cognition-proof.json").is_file())
        self.assertFalse((self.receipts / "packages").exists())
        self.assertFalse((self.receipts / "plans").exists())
        self.assertFalse((self.receipts / "deployments").exists())
        self.assertEqual(result["migration_count"], 6)
        self.assertEqual(result["preserved_unknown_or_conflicting"], [])

    def test_nonempty_deployments_is_preserved(self) -> None:
        deployments = self.receipts / "deployments"
        deployments.mkdir()
        (deployments / "unknown").write_text("preserve\n", encoding="utf-8")
        with mock.patch.object(RUNNER, "RUNNER_USER", self.account.pw_name):
            result = RUNNER.converge(self.contract)
        self.assertTrue(deployments.is_dir())
        self.assertIn(str(deployments), result["preserved_unknown_or_conflicting"])

    def test_wrong_execution_identity_is_rejected(self) -> None:
        fake = type("Fake", (), {"pw_uid": os.geteuid() + 1, "pw_gid": os.getegid()})()
        with mock.patch.object(RUNNER.pwd, "getpwnam", return_value=fake), mock.patch.object(
            RUNNER.pwd, "getpwuid", return_value=self.account
        ):
            with self.assertRaisesRegex(
                RUNNER.RunnerReceiptEstateError, "requires laplace-runner"
            ):
                RUNNER.converge(self.contract)


    def test_selected_package_preserves_conflicting_and_malformed_historical_evidence(self) -> None:
        selected, historical = "a" * 64, "b" * 64
        generation = self.instance / "cluster-activation"
        legacy = [
            (self.receipts / "packages" / f"{selected}.json",
             generation / selected / "package-installation.json",
             {"schema": RUNNER.estate.INSTALLATION_SCHEMA, "package_id": selected}),
            (self.receipts / "plans" / selected / "host-selection.json",
             generation / selected / "host-selection.json", {"selection": selected}),
            (self.instance / "plan" / selected / "resource-observation.json",
             generation / selected / "resource-observation.json", {"package_id": selected}),
            (self.instance / "release-capacity" / selected / "capacity.json",
             generation / selected / "release-capacity.json", {"successor_package_id": selected}),
            (self.instance / "product-cognition" / selected / "installed-proof.json",
             generation / selected / "installed-cognition-proof.json", {"package_id": selected}),
            (self.instance / "unicode-product-activation.json",
             generation / selected / "unicode-product-activation.json",
             {"schema": "unicode", "package_id": selected}),
            (self.instance / "highway-product-activation.json",
             generation / selected / "highway-product-activation.json",
             {"schema": "highway", "package_id": selected}),
        ]
        expected = {}
        for source, destination, document in legacy:
            self.write(source, document)
            expected[destination] = source.read_bytes()
        old = [
            self.receipts / "packages" / f"{historical}.json",
            self.receipts / "plans" / historical / "host-selection.json",
            self.instance / "plan" / historical / "resource-observation.json",
            self.instance / "release-capacity" / historical / "capacity.json",
            generation / historical / "release-capacity.json",
            self.instance / "product-cognition" / historical / "installed-proof.json",
        ]
        for index, path in enumerate(old):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(f"historical malformed conflicting bytes {index}\n".encode())
        old_bytes = {path: path.read_bytes() for path in old}
        deployments = self.receipts / "deployments"
        deployments.mkdir()
        with mock.patch.object(RUNNER, "RUNNER_USER", self.account.pw_name):
            result = RUNNER.converge(self.contract, package_id=selected)
            repeat = RUNNER.converge(self.contract, package_id=selected)
        self.assertEqual(result["scope"], "selected-package")
        self.assertEqual(result["selected_package_id"], selected)
        self.assertEqual(result["migration_count"], len(legacy))
        self.assertEqual(repeat["migration_count"], 0)
        self.assertEqual({path: path.read_bytes() for path in old}, old_bytes)
        self.assertEqual({path: path.read_bytes() for path in expected}, expected)
        self.assertTrue(all(not source.exists() for source, _, _ in legacy))
        self.assertTrue(deployments.is_dir())
        for path in old:
            if generation not in path.parents:
                self.assertTrue(any(str(path) == preserved or
                                    str(path).startswith(preserved + "/")
                                    for preserved in result["preserved_unknown_or_conflicting"]),
                                str(path))

    def test_selected_package_conflict_still_fails_without_overwriting_or_deleting(self) -> None:
        package_id = "a" * 64
        source = self.instance / "release-capacity" / package_id / "capacity.json"
        destination = self.instance / "cluster-activation" / package_id / "release-capacity.json"
        self.write(source, {"same_package": package_id, "value": 1})
        self.write(destination, {"same_package": package_id, "value": 2})
        before = (source.read_bytes(), destination.read_bytes())
        with mock.patch.object(RUNNER, "RUNNER_USER", self.account.pw_name):
            with self.assertRaisesRegex(RUNNER.estate.ReceiptEstateError, "conflicts"):
                RUNNER.converge(self.contract, package_id=package_id)
        self.assertEqual((source.read_bytes(), destination.read_bytes()), before)

    def test_selected_package_rejects_wrong_singleton_identity(self) -> None:
        source = self.instance / "unicode-product-activation.json"
        self.write(source, {"schema": "unicode", "package_id": "b" * 64})
        before = source.read_bytes()
        with mock.patch.object(RUNNER, "RUNNER_USER", self.account.pw_name):
            with self.assertRaisesRegex(RUNNER.estate.ReceiptEstateError, "identity differs"):
                RUNNER.converge(self.contract, package_id="a" * 64)
        self.assertEqual(source.read_bytes(), before)
        self.assertFalse((self.instance / "cluster-activation" / ("b" * 64)).exists())

    def test_selected_package_identity_is_validated_before_filesystem_changes(self) -> None:
        for package_id in ("", "../other", "A" * 64, "a" * 63, 42, True):
            with self.subTest(package_id=package_id):
                with self.assertRaisesRegex(RUNNER.RunnerReceiptEstateError, "selected package identity"):
                    RUNNER.converge(self.contract, package_id=package_id)
                self.assertFalse((self.instance / "cluster-activation").exists())

    def test_cli_selection_reaches_the_real_migration_owner(self) -> None:
        selected, historical = "a" * 64, "b" * 64
        contract_path, output = self.root / "contract.json", self.root / "result.json"
        self.write(contract_path, self.contract)
        for package_id in (selected, historical):
            self.write(self.instance / "release-capacity" / package_id / "capacity.json",
                       {"successor_package_id": package_id})
        with mock.patch.object(RUNNER, "RUNNER_USER", self.account.pw_name):
            self.assertEqual(RUNNER.main([
                "--cluster-contract", str(contract_path), "--package-id", selected,
                "--output", str(output)]), 0)
        result = json.loads(output.read_text())
        self.assertEqual(result["selected_package_id"], selected)
        self.assertEqual(result["migration_count"], 1)
        self.assertTrue((self.instance / "release-capacity" / historical / "capacity.json").is_file())
        self.assertFalse((self.instance / "release-capacity" / selected / "capacity.json").exists())


    def test_selected_revalidation_keeps_original_activation_and_exact_proof_bytes(self) -> None:
        receipt = recovery_fixture.producer_revalidation(self)
        package_id = receipt["package_id"]
        generation = self.instance / "cluster-activation" / package_id
        original = generation / "highway-product-activation.json"
        source = self.instance / "highway-committed-revalidation.json"
        self.write(original, {"schema": "laplace.highway-product-activation-receipt/v1",
                              "package_id": package_id})
        self.write(source, receipt)
        original_bytes, proof_bytes = original.read_bytes(), source.read_bytes()
        with mock.patch.object(RUNNER, "RUNNER_USER", self.account.pw_name):
            result = RUNNER.converge(self.contract, package_id=package_id)
        self.assertEqual(original.read_bytes(), original_bytes)
        self.assertEqual((generation / source.name).read_bytes(), proof_bytes)
        self.assertFalse(source.exists())
        self.assertEqual(result["migration_count"], 1)

    def test_selected_revalidation_rejects_empty_native_proof_without_moving_bytes(self) -> None:
        receipt = recovery_fixture.producer_revalidation(self)
        receipt["revalidation"]["verification_receipt"] = "00" * 32
        receipt["cold_revalidation"]["verification_receipt"] = "00" * 32
        recovery_fixture.resign_revalidation(receipt)
        source = self.instance / "highway-committed-revalidation.json"
        self.write(source, receipt)
        before = source.read_bytes()
        with mock.patch.object(RUNNER, "RUNNER_USER", self.account.pw_name):
            with self.assertRaisesRegex(RUNNER.estate.ReceiptEstateError, "native proof is empty"):
                RUNNER.converge(self.contract, package_id=receipt["package_id"])
        self.assertEqual(source.read_bytes(), before)
        self.assertFalse((self.instance / "cluster-activation" / receipt["package_id"] / source.name).exists())


if __name__ == "__main__":
    unittest.main()
