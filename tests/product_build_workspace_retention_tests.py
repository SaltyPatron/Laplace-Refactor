#!/usr/bin/env python3
"""Positive and deliberate-defect tests for product build workspace retention."""

from __future__ import annotations

import importlib.util
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/product/build_workspace_retention.py"
SPEC = importlib.util.spec_from_file_location("laplace_product_build_retention", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
RETENTION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RETENTION)


class ProductBuildWorkspaceRetentionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-build-retention-")
        self.root = Path(self.temporary.name)
        self.product = self.root / "build/laplace/runner/product"
        self.build_root = self.product / "build"
        self.stage_root = self.product / "stage"
        self.receipt_root = self.product / "retention"
        self.build_root.mkdir(parents=True)
        self.stage_root.mkdir(parents=True)
        self.receipt_root.mkdir(parents=True)
        self.contract = {
            "build": {
                "root": str(self.build_root),
                "stage_root": str(self.stage_root),
            }
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def make_complete(self, plan_digit: str, package_digit: str, timestamp: int = 100) -> tuple[str, Path, Path]:
        plan_id = plan_digit * 64
        package_id = package_digit * 64
        build = self.build_root / plan_id
        stage = self.stage_root / plan_id
        build.mkdir()
        physical = stage / "root/opt/laplace/releases" / package_id
        physical.mkdir(parents=True)
        (physical / "payload").write_bytes(b"rebuildable package payload\n")
        manifest = {
            "package_id": package_id,
            "root": f"/opt/laplace/releases/{package_id}",
        }
        manifest_path = build / "package-manifest.json"
        manifest_path.write_bytes(RETENTION.canonical_bytes(manifest))
        receipt = {
            "schema": RETENTION.PACKAGE_RECEIPT_SCHEMA,
            "plan_sha256": "e" * 64,
            "package_id": package_id,
            "manifest": str(manifest_path),
            "manifest_sha256": RETENTION.sha256_file(manifest_path),
            "physical_root": str(physical),
            "activation_eligible": True,
            "build_input_closure_complete": True,
            "product_activated": False,
        }
        (build / "package-receipt.json").write_bytes(RETENTION.canonical_bytes(receipt))
        for path in (physical / "payload", physical, stage / "root/opt/laplace/releases", stage, manifest_path, build / "package-receipt.json", build):
            os.utime(path, (timestamp, timestamp), follow_symlinks=False)
        return plan_id, build, stage

    def test_completed_obsolete_generation_moves_metadata_and_reclaims_payload(self) -> None:
        plan_id, build, stage = self.make_complete("1", "a")
        receipt = RETENTION.reconcile(
            self.contract,
            receipt_root=self.receipt_root,
            preserve=set(),
            minimum_age_seconds=10,
            now=1000,
        )
        self.assertFalse(build.exists())
        self.assertFalse(stage.exists())
        retained = self.receipt_root / plan_id
        self.assertTrue((retained / "package-receipt.json").is_file())
        self.assertTrue((retained / "package-manifest.json").is_file())
        metadata = json.loads((retained / "retention.json").read_text(encoding="utf-8"))
        self.assertTrue(metadata["payload_rebuildable"])
        self.assertTrue(metadata["payload_reclaimed"])
        self.assertEqual(receipt["retention_root"], str(self.receipt_root))
        self.assertGreater(receipt["allocated_bytes_reclaimed"], 0)
        self.assertEqual(receipt["removed"][0]["reason"], "completed-plan-payload-rebuildable")

    def test_selected_generation_is_never_reclaimed(self) -> None:
        plan_id, build, stage = self.make_complete("2", "b")
        receipt = RETENTION.reconcile(
            self.contract,
            receipt_root=self.receipt_root,
            preserve={plan_id},
            minimum_age_seconds=0,
            now=1000,
        )
        self.assertTrue(build.is_dir())
        self.assertTrue(stage.is_dir())
        self.assertEqual(receipt["removed"], [])
        self.assertEqual(receipt["preserved"][0]["reason"], "selected-plan")

    def test_stale_interrupted_generation_is_reclaimed_without_fabricated_package_receipt(self) -> None:
        plan_id = "3" * 64
        build = self.build_root / plan_id
        stage = self.stage_root / plan_id
        build.mkdir()
        stage.mkdir()
        (build / "partial.o").write_bytes(b"interrupted\n")
        for path in (build / "partial.o", build, stage):
            os.utime(path, (100, 100), follow_symlinks=False)
        receipt = RETENTION.reconcile(
            self.contract,
            receipt_root=self.receipt_root,
            preserve=set(),
            minimum_age_seconds=10,
            now=1000,
        )
        self.assertFalse(build.exists())
        self.assertFalse(stage.exists())
        self.assertFalse((self.receipt_root / plan_id).exists())
        self.assertEqual(receipt["removed"][0]["reason"], "stale-incomplete-plan-workspace")

    def test_busy_plan_lock_preserves_generation(self) -> None:
        plan_id, build, stage = self.make_complete("4", "c")
        with mock.patch.object(RETENTION.fcntl, "flock", side_effect=BlockingIOError):
            receipt = RETENTION.reconcile(
                self.contract,
                receipt_root=self.receipt_root,
                preserve=set(),
                minimum_age_seconds=0,
                now=1000,
            )
        self.assertTrue(build.is_dir())
        self.assertTrue(stage.is_dir())
        self.assertEqual(receipt["preserved"][0]["reason"], "plan-lock-busy")

    def test_symlinked_canonical_plan_is_preserved_fail_closed(self) -> None:
        plan_id = "5" * 64
        outside = self.root / "outside"
        outside.mkdir()
        (self.build_root / plan_id).symlink_to(outside, target_is_directory=True)
        receipt = RETENTION.reconcile(
            self.contract,
            receipt_root=self.receipt_root,
            preserve=set(),
            minimum_age_seconds=0,
            now=1000,
        )
        self.assertTrue((self.build_root / plan_id).is_symlink())
        self.assertTrue(outside.is_dir())
        self.assertEqual(receipt["preserved"][0]["reason"], "unsafe-plan-node")

    def test_corrupt_completion_metadata_blocks_deletion(self) -> None:
        _plan_id, build, stage = self.make_complete("6", "d")
        receipt_path = build / "package-receipt.json"
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        receipt["manifest_sha256"] = "0" * 64
        receipt_path.write_bytes(RETENTION.canonical_bytes(receipt))
        with self.assertRaisesRegex(RETENTION.RetentionError, "completed exact build"):
            RETENTION.reconcile(
                self.contract,
                receipt_root=self.receipt_root,
                preserve=set(),
                minimum_age_seconds=0,
                now=1000,
            )
        self.assertTrue(build.is_dir())
        self.assertTrue(stage.is_dir())

    def test_execution_retention_cannot_escape_product_build_estate(self) -> None:
        outside = self.root / "opt/laplace/receipts/not-build-state"
        with self.assertRaisesRegex(RETENTION.RetentionError, "inside the product build estate"):
            RETENTION.reconcile(
                self.contract,
                receipt_root=outside,
                preserve=set(),
                minimum_age_seconds=0,
                now=1000,
            )


if __name__ == "__main__":
    unittest.main()
