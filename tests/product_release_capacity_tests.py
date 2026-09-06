#!/usr/bin/env python3
"""Positive and deliberate-defect tests for product release capacity recovery."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "tools/delivery/product_release_capacity.py"
SPEC = importlib.util.spec_from_file_location("laplace_product_release_capacity", MODULE)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE}")
CAPACITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAPACITY)


class ProductReleaseCapacityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-release-capacity-")
        self.root = Path(self.temporary.name).resolve()
        self.release_root = self.root / "opt/laplace/releases"
        self.release_root.mkdir(parents=True)
        self.receipt_root = self.root / "opt/laplace/receipts/postgresql/refactor"
        self.receipt_root.mkdir(parents=True)
        self.proc_root = self.root / "proc"
        self.proc_root.mkdir()
        self.active = self.root / "opt/laplace/current"
        self.active.parent.mkdir(parents=True, exist_ok=True)
        self.successor = "f" * 64
        self.contract = {
            "schema": "laplace.postgresql-cluster-contract/v1",
            "package": {
                "release_root": str(self.release_root),
                "active_link": str(self.active),
            },
            "instance": {"receipt_directory": str(self.receipt_root)},
        }
        self.source_prefix = self.root / "source"
        logical_root = f"/opt/laplace/releases/{self.successor}"
        self.source_release = self.source_prefix / logical_root.lstrip("/")
        self.source_release.mkdir(parents=True)
        self.payload = self.source_release / "bin/product"
        self.payload.parent.mkdir(parents=True)
        self.payload.write_bytes(b"product-bytes")
        self.manifest = {
            "package_id": self.successor,
            "root": logical_root,
            "files": [
                {
                    "path": "bin/product",
                    "kind": "file",
                    "sha256": "0" * 64,
                    "mode": 0o755,
                }
            ],
        }
        self.product_receipt = {
            "package_id": self.successor,
            "physical_root": str(self.source_release),
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def make_release(self, package_id: str, payload: bytes = b"old-release") -> Path:
        release = self.release_root / package_id
        release.mkdir()
        (release / "payload").write_bytes(payload)
        return release

    def write_installation_receipt(self, package_id: str, release: Path) -> Path:
        evidence = self.receipt_root / "cluster-activation" / package_id
        evidence.mkdir(parents=True, exist_ok=True)
        receipt = {
            "schema": CAPACITY.INSTALLATION_SCHEMA,
            "phase": "installed",
            "package_id": package_id,
            "installed_release": str(release),
            "installed_package_verified": True,
            "source_package_verified": True,
            "overwrite_performed": False,
            "total_file_bytes": sum(
                path.stat().st_size for path in release.rglob("*") if path.is_file()
            ),
        }
        receipt["installation_receipt_sha256"] = CAPACITY.document_identity(
            receipt, "installation_receipt_sha256"
        )
        path = evidence / "package-installation.json"
        path.write_text(__import__("json").dumps(receipt), encoding="utf-8")
        return evidence

    def test_verified_never_activated_release_is_reclaimed_for_successor(self) -> None:
        old_id = "1" * 64
        old = self.make_release(old_id)
        self.write_installation_receipt(old_id, old)
        required = len(b"product-bytes") + CAPACITY.MIN_HEADROOM_BYTES

        def free(_path: Path) -> int:
            return required + 1 if not old.exists() else 0

        with mock.patch.object(CAPACITY, "_disk_free", side_effect=free):
            receipt = CAPACITY.reconcile_capacity(
                self.contract,
                self.product_receipt,
                self.manifest,
                proc_root=self.proc_root,
            )

        self.assertFalse(old.exists())
        self.assertTrue(receipt["capacity_satisfied"])
        self.assertEqual(receipt["removed_releases"][0]["package_id"], old_id)
        self.assertEqual(
            receipt["receipt_sha256"],
            CAPACITY.document_identity(receipt, "receipt_sha256"),
        )

    def test_active_release_is_never_reclaimed(self) -> None:
        active_id = "2" * 64
        active_release = self.make_release(active_id)
        self.write_installation_receipt(active_id, active_release)
        self.active.symlink_to(Path("releases") / active_id, target_is_directory=True)
        with mock.patch.object(CAPACITY, "_disk_free", return_value=0):
            with self.assertRaisesRegex(CAPACITY.CapacityError, "capacity remains insufficient"):
                CAPACITY.reconcile_capacity(
                    self.contract,
                    self.product_receipt,
                    self.manifest,
                    proc_root=self.proc_root,
                )
        self.assertTrue(active_release.is_dir())

    def test_activated_release_is_never_reclaimed(self) -> None:
        old_id = "3" * 64
        old = self.make_release(old_id)
        evidence = self.write_installation_receipt(old_id, old)
        (evidence / "activation-complete.json").write_text("{}\n", encoding="utf-8")
        with mock.patch.object(CAPACITY, "_disk_free", return_value=0):
            with self.assertRaisesRegex(CAPACITY.CapacityError, "capacity remains insufficient"):
                CAPACITY.reconcile_capacity(
                    self.contract,
                    self.product_receipt,
                    self.manifest,
                    proc_root=self.proc_root,
                )
        self.assertTrue(old.is_dir())

    def test_unknown_release_without_installation_receipt_is_preserved(self) -> None:
        unknown = self.make_release("4" * 64)
        with mock.patch.object(CAPACITY, "_disk_free", return_value=0):
            with self.assertRaisesRegex(CAPACITY.CapacityError, "capacity remains insufficient"):
                CAPACITY.reconcile_capacity(
                    self.contract,
                    self.product_receipt,
                    self.manifest,
                    proc_root=self.proc_root,
                )
        self.assertTrue(unknown.is_dir())

    def test_bad_installation_receipt_blocks_cleanup(self) -> None:
        old_id = "5" * 64
        old = self.make_release(old_id)
        evidence = self.write_installation_receipt(old_id, old)
        path = evidence / "package-installation.json"
        value = __import__("json").loads(path.read_text(encoding="utf-8"))
        value["installation_receipt_sha256"] = "0" * 64
        path.write_text(__import__("json").dumps(value), encoding="utf-8")
        with self.assertRaisesRegex(CAPACITY.CapacityError, "does not prove"):
            CAPACITY.reconcile_capacity(
                self.contract,
                self.product_receipt,
                self.manifest,
                proc_root=self.proc_root,
            )
        self.assertTrue(old.is_dir())

    def test_live_runner_reference_preserves_never_activated_release(self) -> None:
        old_id = "6" * 64
        old = self.make_release(old_id)
        self.write_installation_receipt(old_id, old)
        with mock.patch.object(CAPACITY, "_disk_free", return_value=0), mock.patch.object(
            CAPACITY, "_runner_process_references", return_value=["123:maps"]
        ):
            with self.assertRaisesRegex(CAPACITY.CapacityError, "capacity remains insufficient"):
                CAPACITY.reconcile_capacity(
                    self.contract,
                    self.product_receipt,
                    self.manifest,
                    proc_root=self.proc_root,
                )
        self.assertTrue(old.is_dir())

    def test_old_unpublished_install_staging_can_be_removed(self) -> None:
        old_id = "7" * 64
        staging = self.release_root / f".{old_id}.install.interrupted"
        staging.mkdir()
        (staging / "partial").write_bytes(b"partial")
        old = time.time() - 600
        os.utime(staging, (old, old))
        required = len(b"product-bytes") + CAPACITY.MIN_HEADROOM_BYTES

        def free(_path: Path) -> int:
            return required + 1 if not staging.exists() else 0

        with mock.patch.object(CAPACITY, "_disk_free", side_effect=free):
            receipt = CAPACITY.reconcile_capacity(
                self.contract,
                self.product_receipt,
                self.manifest,
                proc_root=self.proc_root,
                now=time.time(),
            )
        self.assertFalse(staging.exists())
        self.assertEqual(len(receipt["removed_temporaries"]), 1)
        self.assertTrue(receipt["capacity_satisfied"])

    def test_recent_install_staging_is_not_removed(self) -> None:
        old_id = "8" * 64
        staging = self.release_root / f".{old_id}.install.current"
        staging.mkdir()
        with mock.patch.object(CAPACITY, "_disk_free", return_value=0):
            with self.assertRaisesRegex(CAPACITY.CapacityError, "capacity remains insufficient"):
                CAPACITY.reconcile_capacity(
                    self.contract,
                    self.product_receipt,
                    self.manifest,
                    proc_root=self.proc_root,
                    now=time.time(),
                )
        self.assertTrue(staging.is_dir())

    def test_product_pointer_outside_release_root_fails_closed(self) -> None:
        outside = self.root / "outside"
        outside.mkdir()
        self.active.symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(CAPACITY.CapacityError, "escapes release root"):
            CAPACITY.reconcile_capacity(
                self.contract,
                self.product_receipt,
                self.manifest,
                proc_root=self.proc_root,
            )

    def test_source_package_bytes_are_measured_from_exact_physical_files(self) -> None:
        self.assertEqual(
            CAPACITY._source_package_bytes(self.product_receipt, self.manifest),
            len(b"product-bytes"),
        )
        self.payload.unlink()
        with self.assertRaisesRegex(CAPACITY.CapacityError, "source package file is absent"):
            CAPACITY._source_package_bytes(self.product_receipt, self.manifest)


if __name__ == "__main__":
    unittest.main(verbosity=2)
