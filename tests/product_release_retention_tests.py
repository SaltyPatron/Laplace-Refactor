#!/usr/bin/env python3
"""Regression tests for bounded successful product-release retention."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "tools/delivery/product_release_capacity.py"


def load_module():
    spec = importlib.util.spec_from_file_location("laplace_product_release_retention", MODULE)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {MODULE}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CAPACITY = load_module()


class ProductReleaseRetentionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-release-retention-")
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
        logical_root = f"/opt/laplace/releases/{self.successor}"
        self.source_release = self.root / "source" / logical_root.lstrip("/")
        self.source_release.mkdir(parents=True)
        payload = self.source_release / "bin/product"
        payload.parent.mkdir(parents=True)
        payload.write_bytes(b"successor")
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

    def activated_release(self, digit: str, timestamp: int) -> tuple[str, Path]:
        package_id = digit * 64
        release = self.release_root / package_id
        release.mkdir()
        (release / "payload").write_bytes(f"release-{digit}".encode())
        evidence = self.receipt_root / "cluster-activation" / package_id
        evidence.mkdir(parents=True)
        installation = {
            "schema": CAPACITY.INSTALLATION_SCHEMA,
            "phase": "installed",
            "package_id": package_id,
            "installed_release": str(release),
            "installed_package_verified": True,
            "source_package_verified": True,
            "overwrite_performed": False,
            "total_file_bytes": (release / "payload").stat().st_size,
        }
        installation["installation_receipt_sha256"] = CAPACITY.document_identity(
            installation, "installation_receipt_sha256"
        )
        (evidence / "package-installation.json").write_text(
            json.dumps(installation), encoding="utf-8"
        )
        activation = evidence / "activation-complete.json"
        activation.write_text("{}\n", encoding="utf-8")
        os.utime(activation, (timestamp, timestamp))
        return package_id, release

    def reconcile(self, rollback_releases: int = 2):
        with mock.patch.object(CAPACITY, "_disk_free", return_value=10**12), mock.patch.object(
            CAPACITY, "_disk_free_inodes", return_value=10**9
        ):
            return CAPACITY.reconcile_capacity(
                self.contract,
                self.product_receipt,
                self.manifest,
                proc_root=self.proc_root,
                rollback_releases=rollback_releases,
            )

    def test_only_newest_activated_rollback_generations_are_retained(self) -> None:
        oldest_id, oldest = self.activated_release("1", 100)
        second_id, second = self.activated_release("2", 200)
        third_id, third = self.activated_release("3", 300)
        newest_id, newest = self.activated_release("4", 400)

        receipt = self.reconcile(rollback_releases=2)

        self.assertFalse(oldest.exists())
        self.assertFalse(second.exists())
        self.assertTrue(third.is_dir())
        self.assertTrue(newest.is_dir())
        self.assertEqual(
            {item["package_id"] for item in receipt["removed_releases"]},
            {oldest_id, second_id},
        )
        self.assertTrue(
            all(
                item["reason"] == "verified-activated-release-outside-rollback-window"
                for item in receipt["removed_releases"]
            )
        )
        retained = {
            item["package_id"]
            for item in receipt["preserved_releases"]
            if item["reason"] == "rollback-window-retained"
        }
        self.assertEqual(retained, {third_id, newest_id})
        self.assertEqual(receipt["rollback_release_count"], 2)

    def test_current_pointer_is_preserved_outside_rollback_window(self) -> None:
        active_id, active = self.activated_release("1", 100)
        self.active.symlink_to(Path("releases") / active_id, target_is_directory=True)
        _second_id, second = self.activated_release("2", 200)
        _third_id, third = self.activated_release("3", 300)
        _newest_id, newest = self.activated_release("4", 400)

        receipt = self.reconcile(rollback_releases=1)

        self.assertTrue(active.is_dir())
        self.assertFalse(second.exists())
        self.assertFalse(third.exists())
        self.assertTrue(newest.is_dir())
        self.assertTrue(
            any(
                item["package_id"] == active_id
                and item["reason"] == "selected-product-pointer"
                for item in receipt["preserved_releases"]
            )
        )

    def test_zero_rollback_window_reclaims_all_unselected_activated_history(self) -> None:
        first_id, first = self.activated_release("1", 100)
        second_id, second = self.activated_release("2", 200)

        receipt = self.reconcile(rollback_releases=0)

        self.assertFalse(first.exists())
        self.assertFalse(second.exists())
        self.assertEqual(
            {item["package_id"] for item in receipt["removed_releases"]},
            {first_id, second_id},
        )


if __name__ == "__main__":
    unittest.main()
