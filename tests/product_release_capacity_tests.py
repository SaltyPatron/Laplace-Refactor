#!/usr/bin/env python3
"""Positive and deliberate-defect tests for product release capacity recovery."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "tools/delivery/product_release_capacity.py"
RECONCILER_MODULE = ROOT / "tools/delivery/product_activation_reconcile.py"


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CAPACITY = load("laplace_product_release_capacity", MODULE)
RECONCILE = load("laplace_product_release_capacity_reconciler", RECONCILER_MODULE)


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
        path.write_text(json.dumps(receipt), encoding="utf-8")
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
            with self.assertRaisesRegex(
                CAPACITY.CapacityError, "capacity remains insufficient"
            ):
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
            with self.assertRaisesRegex(
                CAPACITY.CapacityError, "capacity remains insufficient"
            ):
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
            with self.assertRaisesRegex(
                CAPACITY.CapacityError, "capacity remains insufficient"
            ):
                CAPACITY.reconcile_capacity(
                    self.contract,
                    self.product_receipt,
                    self.manifest,
                    proc_root=self.proc_root,
                )
        self.assertTrue(unknown.is_dir())

    def test_invalid_installation_receipt_is_preserved_while_proven_residue_can_clear(self) -> None:
        invalid_id = "5" * 64
        invalid = self.make_release(invalid_id)
        invalid_evidence = self.write_installation_receipt(invalid_id, invalid)
        invalid_path = invalid_evidence / "package-installation.json"
        value = json.loads(invalid_path.read_text(encoding="utf-8"))
        value["installation_receipt_sha256"] = "0" * 64
        invalid_path.write_text(json.dumps(value), encoding="utf-8")

        safe_id = "9" * 64
        safe = self.make_release(safe_id)
        self.write_installation_receipt(safe_id, safe)
        required = len(b"product-bytes") + CAPACITY.MIN_HEADROOM_BYTES

        def free(_path: Path) -> int:
            return required + 1 if not safe.exists() else 0

        with mock.patch.object(CAPACITY, "_disk_free", side_effect=free):
            receipt = CAPACITY.reconcile_capacity(
                self.contract,
                self.product_receipt,
                self.manifest,
                proc_root=self.proc_root,
            )
        self.assertTrue(invalid.is_dir())
        self.assertFalse(safe.exists())
        self.assertTrue(
            any(
                item["package_id"] == invalid_id
                and item["reason"] == "invalid-installation-receipt"
                for item in receipt["preserved_releases"]
            )
        )

    def test_live_runner_reference_preserves_never_activated_release(self) -> None:
        old_id = "6" * 64
        old = self.make_release(old_id)
        self.write_installation_receipt(old_id, old)
        with mock.patch.object(CAPACITY, "_disk_free", return_value=0), mock.patch.object(
            CAPACITY, "_runner_process_references", return_value=["123:maps"]
        ):
            with self.assertRaisesRegex(
                CAPACITY.CapacityError, "capacity remains insufficient"
            ):
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
            with self.assertRaisesRegex(
                CAPACITY.CapacityError, "capacity remains insufficient"
            ):
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

    def test_successor_release_symlink_is_rejected(self) -> None:
        outside = self.root / "successor-outside"
        outside.mkdir()
        successor = self.release_root / self.successor
        successor.symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(CAPACITY.CapacityError, "successor release path is a symlink"):
            CAPACITY.reconcile_capacity(
                self.contract,
                self.product_receipt,
                self.manifest,
                proc_root=self.proc_root,
            )
        self.assertTrue(successor.is_symlink())
        self.assertTrue(outside.is_dir())

    def test_source_package_bytes_are_measured_from_exact_physical_files(self) -> None:
        self.assertEqual(
            CAPACITY._source_package_bytes(self.product_receipt, self.manifest),
            len(b"product-bytes"),
        )
        self.payload.unlink()
        with self.assertRaisesRegex(
            CAPACITY.CapacityError, "source package file is absent"
        ):
            CAPACITY._source_package_bytes(self.product_receipt, self.manifest)

    def test_reconciler_proves_capacity_before_package_installation(self) -> None:
        events: list[str] = []
        capacity_receipt = {
            "schema": CAPACITY.SCHEMA,
            "capacity_satisfied": True,
            "receipt_sha256": "a" * 64,
        }

        def capacity(*_args, **_kwargs):
            events.append("capacity")
            return capacity_receipt

        def install(*_args, **_kwargs):
            events.append("install")
            return {"schema": "installed"}

        with mock.patch.object(
            RECONCILE.release_capacity, "reconcile_capacity", side_effect=capacity
        ) as capacity_call, mock.patch.object(
            RECONCILE, "fresh_install_package", side_effect=install
        ) as install_call:
            result = RECONCILE.install_package_with_capacity(
                self.manifest,
                self.contract,
                self.source_prefix,
                Path("/"),
                True,
            )

        self.assertEqual(result, {"schema": "installed"})
        self.assertEqual(events, ["capacity", "install"])
        capacity_call.assert_called_once()
        install_call.assert_called_once()
        receipt_path = (
            self.receipt_root
            / "release-capacity"
            / self.successor
            / "capacity.json"
        )
        self.assertEqual(json.loads(receipt_path.read_text(encoding="utf-8")), capacity_receipt)

    def test_capacity_failure_prevents_package_installation(self) -> None:
        with mock.patch.object(
            RECONCILE.release_capacity,
            "reconcile_capacity",
            side_effect=RECONCILE.release_capacity.CapacityError("insufficient"),
        ), mock.patch.object(RECONCILE, "fresh_install_package") as install_call:
            with self.assertRaisesRegex(
                RECONCILE.release_capacity.CapacityError, "insufficient"
            ):
                RECONCILE.install_package_with_capacity(
                    self.manifest,
                    self.contract,
                    self.source_prefix,
                    Path("/"),
                    True,
                )
        install_call.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)
