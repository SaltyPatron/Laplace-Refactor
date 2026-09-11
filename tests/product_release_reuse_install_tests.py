#!/usr/bin/env python3
"""Regression tests for link-aware immutable product release installation."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import tempfile
import types
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "tools/delivery/product_activation_reconcile.py"


def load():
    spec = importlib.util.spec_from_file_location("laplace_release_reuse_install", MODULE)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {MODULE}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


RECONCILE = load()


class ProductReleaseReuseInstallTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-release-reuse-")
        self.root = Path(self.temporary.name).resolve()
        self.install_root = self.root / "installed"
        self.install_root.mkdir()
        self.source_root = self.root / "source"
        self.source_root.mkdir()
        self.package_id = "e" * 64
        self.logical_release = f"/opt/laplace/releases/{self.package_id}"
        self.source_release = RECONCILE.runner.clusterctl.prefixed(
            self.source_root, self.logical_release
        )
        self.source_release.mkdir(parents=True)
        self.payload = self.source_release / "bin/laplace"
        self.payload.parent.mkdir(parents=True)
        self.payload.write_bytes((b"laplace\0" * 131073) + b"tail")
        self.payload.chmod(0o755)
        self.entry = {
            "path": "bin/laplace",
            "kind": "file",
            "sha256": RECONCILE.runner.clusterctl.sha256_file(self.payload),
            "mode": 0o755,
            "runpath": [],
        }
        self.manifest = {
            "package_id": self.package_id,
            "root": self.logical_release,
            "files": [self.entry],
        }
        self.contract = {
            "package": {
                "release_root": "/opt/laplace/releases",
                "active_link": "/opt/laplace/current",
            },
            "instance": {
                "receipt_directory": "/opt/laplace/receipts/postgresql/refactor"
            },
        }
        self.status = types.SimpleNamespace(
            verified=True,
            reason="fixture verified",
            files={self.entry["path"]: self.entry},
            manifest_sha256="a" * 64,
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_same_filesystem_source_is_reused_without_payload_copy_capacity(self) -> None:
        with mock.patch.object(RECONCILE.runner.clusterctl, "validate_contract"), \
             mock.patch.object(
                 RECONCILE.runner.clusterctl,
                 "verify_package",
                 return_value=self.status,
             ):
            plan = RECONCILE._reuse_install_plan(
                self.manifest,
                self.contract,
                self.source_root,
                self.install_root,
            )

        receipt = plan["capacity_receipt"]
        self.assertEqual(plan["entries"][0]["method"], "hardlink-source")
        self.assertEqual(receipt["copied_file_count"], 0)
        self.assertEqual(receipt["copied_file_bytes"], 0)
        self.assertEqual(receipt["hardlinked_file_count"], 1)
        self.assertEqual(receipt["hardlinked_source_bytes"], self.payload.stat().st_size)
        self.assertLess(receipt["required_available_bytes"], self.payload.stat().st_size)
        self.assertTrue(receipt["capacity_satisfied"])

    def test_reused_tree_is_verified_and_published_atomically(self) -> None:
        installation_receipt = {
            "schema": "laplace.product-package-installation-receipt/v1",
            "phase": "installed",
            "package_id": self.package_id,
        }
        with mock.patch.object(RECONCILE.runner.clusterctl, "validate_contract"), \
             mock.patch.object(
                 RECONCILE.runner.clusterctl,
                 "verify_package",
                 return_value=self.status,
             ), \
             mock.patch.object(
                 RECONCILE.runner.clusterctl,
                 "package_installation_receipt",
                 return_value=installation_receipt,
             ) as receipt_call:
            plan = RECONCILE._reuse_install_plan(
                self.manifest,
                self.contract,
                self.source_root,
                self.install_root,
            )
            result = RECONCILE._execute_reuse_install(
                plan,
                self.manifest,
                self.contract,
                self.source_root,
                self.install_root,
            )

        installed = RECONCILE.runner.clusterctl.prefixed(
            self.install_root, self.logical_release
        ) / self.entry["path"]
        self.assertEqual(result, installation_receipt)
        self.assertTrue(installed.is_file())
        self.assertFalse(installed.is_symlink())
        self.assertEqual(installed.read_bytes(), self.payload.read_bytes())
        self.assertEqual(installed.stat().st_ino, self.payload.stat().st_ino)
        self.assertEqual(installed.stat().st_dev, self.payload.stat().st_dev)
        receipt_call.assert_called_once()
        staging = list(installed.parents[2].glob(f".{self.package_id}.install.*"))
        self.assertEqual(staging, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
