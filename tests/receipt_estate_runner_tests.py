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


if __name__ == "__main__":
    unittest.main()
