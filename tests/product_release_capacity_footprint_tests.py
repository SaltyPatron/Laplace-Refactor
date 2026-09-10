#!/usr/bin/env python3
"""Regression tests for exact product-release copy footprint accounting."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "tools/delivery/product_release_capacity.py"


def load():
    spec = importlib.util.spec_from_file_location("laplace_release_footprint", MODULE)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {MODULE}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CAPACITY = load()


class ProductReleaseFootprintTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-release-footprint-")
        self.root = Path(self.temporary.name).resolve()
        self.release_root = self.root / "opt/laplace/releases"
        self.release_root.mkdir(parents=True)
        self.receipt_root = self.root / "opt/laplace/receipts/postgresql/refactor"
        self.receipt_root.mkdir(parents=True)
        self.active = self.root / "opt/laplace/current"
        self.active.parent.mkdir(parents=True, exist_ok=True)
        self.proc_root = self.root / "proc"
        self.proc_root.mkdir()
        self.package_id = "f" * 64
        logical_root = f"/opt/laplace/releases/{self.package_id}"
        self.source_prefix = self.root / "source"
        self.source_release = self.source_prefix / logical_root.lstrip("/")
        self.source_release.mkdir(parents=True)
        payload = self.source_release / "bin/laplace"
        payload.parent.mkdir(parents=True)
        payload.write_bytes(b"x" * (1024 * 1024 + 17))
        self.payload_bytes = payload.stat().st_size
        self.manifest = {
            "package_id": self.package_id,
            "root": logical_root,
            "files": [
                {
                    "path": "bin/laplace",
                    "kind": "file",
                    "sha256": "0" * 64,
                    "mode": 0o755,
                }
            ],
        }
        self.product_receipt = {
            "package_id": self.package_id,
            "physical_root": str(self.source_release),
        }
        self.contract = {
            "schema": "laplace.postgresql-cluster-contract/v1",
            "package": {
                "release_root": str(self.release_root),
                "active_link": str(self.active),
            },
            "instance": {"receipt_directory": str(self.receipt_root)},
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_copy_capacity_is_exact_target_footprint_not_fixed_512mib_reserve(self) -> None:
        with mock.patch.object(CAPACITY, "_allocation_unit", return_value=4096):
            footprint = CAPACITY._target_copy_footprint(
                self.product_receipt, self.manifest, self.release_root
            )
        required = footprint["required_allocation_bytes"]
        self.assertGreaterEqual(required, self.payload_bytes)
        self.assertLess(
            required,
            self.payload_bytes + CAPACITY.MIN_HEADROOM_BYTES,
            "the retired fixed 512 MiB reserve must not be execution capacity",
        )

        with mock.patch.object(CAPACITY, "_allocation_unit", return_value=4096), \
             mock.patch.object(CAPACITY, "_disk_free", return_value=required), \
             mock.patch.object(CAPACITY, "_disk_free_inodes", return_value=10_000):
            receipt = CAPACITY.reconcile_capacity(
                self.contract,
                self.product_receipt,
                self.manifest,
                proc_root=self.proc_root,
            )

        self.assertTrue(receipt["capacity_satisfied"])
        self.assertEqual(receipt["required_available_bytes"], required)
        self.assertEqual(receipt["required_copy_bytes"], self.payload_bytes)
        self.assertEqual(receipt["allocation_unit_bytes"], 4096)
        self.assertEqual(receipt["removed_releases"], [])

    def test_existing_successor_requires_no_second_copy_reserve(self) -> None:
        (self.release_root / self.package_id).mkdir()
        with mock.patch.object(CAPACITY, "_allocation_unit", return_value=4096), \
             mock.patch.object(CAPACITY, "_disk_free", return_value=0), \
             mock.patch.object(CAPACITY, "_disk_free_inodes", return_value=0):
            receipt = CAPACITY.reconcile_capacity(
                self.contract,
                self.product_receipt,
                self.manifest,
                proc_root=self.proc_root,
            )

        self.assertFalse(receipt["copy_required"])
        self.assertEqual(receipt["required_available_bytes"], 0)
        self.assertEqual(receipt["required_available_inodes"], 0)
        self.assertTrue(receipt["capacity_satisfied"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
