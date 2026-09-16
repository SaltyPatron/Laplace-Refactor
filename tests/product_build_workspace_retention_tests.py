#!/usr/bin/env python3
"""Positive and deliberate-defect tests for product build workspace retention."""

from __future__ import annotations

import importlib.util
import json
import os
import shutil
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
        retained = Path(receipt["removed"][0]["retained_metadata"])
        self.assertEqual(retained.parent, self.receipt_root / plan_id)
        self.assertEqual(retained.name, RETENTION.sha256_file(retained / "package-receipt.json"))
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


    def reclaim(self) -> dict:
        return RETENTION.reconcile(
            self.contract, receipt_root=self.receipt_root, preserve=set(),
            minimum_age_seconds=10, now=1000,
        )

    @staticmethod
    def metadata_bytes(directory: Path) -> dict[str, bytes]:
        return {name: (directory / name).read_bytes() for name in (
            "package-receipt.json", "package-manifest.json", "retention.json"
        )}

    def legacy_metadata(self, plan_id: str, build: Path, stage: Path) -> Path:
        metadata = RETENTION._completed_metadata(plan_id, build, stage)
        assert metadata is not None
        receipt_bytes, manifest_bytes, summary = metadata
        destination = self.receipt_root / plan_id
        destination.mkdir()
        for name, payload in {
            "package-receipt.json": receipt_bytes,
            "package-manifest.json": manifest_bytes,
            "retention.json": RETENTION.canonical_bytes({"schema": RETENTION.SCHEMA, **summary}),
        }.items():
            (destination / name).write_bytes(payload)
        return destination

    def change_execution_log(self, build: Path) -> bytes:
        path = build / "package-receipt.json"
        receipt = json.loads(path.read_text(encoding="utf-8"))
        receipt["build_log_sha256"] = "9" * 64
        payload = RETENTION.canonical_bytes(receipt)
        path.write_bytes(payload)
        return payload

    def test_identical_rebuild_reuses_exact_receipt_metadata(self) -> None:
        plan_id, _build, _stage = self.make_complete("7", "a")
        first = self.reclaim()
        retained = Path(first["removed"][0]["retained_metadata"])
        original = self.metadata_bytes(retained)
        self.make_complete("7", "a")
        second = self.reclaim()
        self.assertEqual(second["removed"][0]["retained_metadata"], str(retained))
        self.assertEqual(self.metadata_bytes(retained), original)
        self.assertEqual(list((self.receipt_root / plan_id).iterdir()), [retained])

    def test_same_plan_and_package_retain_distinct_execution_receipts(self) -> None:
        plan_id, _build, _stage = self.make_complete("8", "b")
        first = self.reclaim()
        earlier = Path(first["removed"][0]["retained_metadata"])
        original = self.metadata_bytes(earlier)
        _same_plan, build, stage = self.make_complete("8", "b")
        rebuilt = self.change_execution_log(build)
        second = self.reclaim()
        later = Path(second["removed"][0]["retained_metadata"])
        self.assertNotEqual(earlier, later)
        self.assertEqual(later, self.receipt_root / plan_id / RETENTION.sha256_bytes(rebuilt))
        self.assertEqual(self.metadata_bytes(earlier), original)
        self.assertEqual((later / "package-receipt.json").read_bytes(), rebuilt)
        self.assertEqual((later / "package-manifest.json").read_bytes(),
                         original["package-manifest.json"])
        self.assertFalse(build.exists())
        self.assertFalse(stage.exists())

    def test_identical_legacy_metadata_is_reused_without_a_second_copy(self) -> None:
        plan_id, build, stage = self.make_complete("9", "c")
        legacy = self.legacy_metadata(plan_id, build, stage)
        original = self.metadata_bytes(legacy)
        result = self.reclaim()
        self.assertEqual(result["removed"][0]["retained_metadata"], str(legacy))
        self.assertEqual(self.metadata_bytes(legacy), original)
        self.assertEqual({path.name for path in legacy.iterdir()}, set(original))

    def test_different_execution_preserves_flat_legacy_metadata(self) -> None:
        plan_id, build, stage = self.make_complete("a", "d")
        legacy = self.legacy_metadata(plan_id, build, stage)
        original = self.metadata_bytes(legacy)
        rebuilt = self.change_execution_log(build)
        result = self.reclaim()
        retained = Path(result["removed"][0]["retained_metadata"])
        self.assertEqual(retained.parent, legacy)
        self.assertEqual(retained.name, RETENTION.sha256_bytes(rebuilt))
        self.assertEqual(self.metadata_bytes(legacy), original)
        self.assertEqual((retained / "package-receipt.json").read_bytes(), rebuilt)
        self.assertFalse(build.exists())
        self.assertFalse(stage.exists())

    def test_changed_bytes_at_same_receipt_identity_still_block_deletion(self) -> None:
        _plan_id, _build, _stage = self.make_complete("b", "e")
        result = self.reclaim()
        retained = Path(result["removed"][0]["retained_metadata"])
        (retained / "package-manifest.json").write_bytes(b"corrupt retained manifest\n")
        _same_plan, build, stage = self.make_complete("b", "e")
        with self.assertRaisesRegex(RETENTION.RetentionError, "metadata collision"):
            self.reclaim()
        self.assertTrue(build.is_dir())
        self.assertTrue(stage.is_dir())
        self.assertEqual((retained / "package-manifest.json").read_bytes(),
                         b"corrupt retained manifest\n")

    def test_symlinked_receipt_destination_blocks_deletion(self) -> None:
        plan_id, build, stage = self.make_complete("c", "f")
        receipt_id = RETENTION.sha256_file(build / "package-receipt.json")
        plan_destination = self.receipt_root / plan_id
        plan_destination.mkdir()
        outside = self.root / "outside-receipt"
        outside.mkdir()
        marker = outside / "untouched"
        marker.write_bytes(b"preserve\n")
        (plan_destination / receipt_id).symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(RETENTION.RetentionError, "destination is unsafe"):
            self.reclaim()
        self.assertTrue(build.is_dir())
        self.assertTrue(stage.is_dir())
        self.assertEqual(marker.read_bytes(), b"preserve\n")
        self.assertEqual(list(outside.iterdir()), [marker])


    def selected_metadata(self, plan_id: str, package_id: str, digest: str, **kwargs) -> dict:
        return RETENTION.resolve_package_metadata(
            self.build_root, self.stage_root, plan_id, package_id, digest, **kwargs)

    def test_reader_survives_actual_reclamation_without_rewriting_provenance(self) -> None:
        plan, build, stage = self.make_complete("1", "a")
        original = (build / "package-receipt.json").read_bytes()
        digest = RETENTION.sha256_file(build / "package-manifest.json")
        live = self.selected_metadata(plan, "a" * 64, digest)
        self.assertEqual(live["selection"], "build")
        self.reclaim()
        retained = self.selected_metadata(plan, "a" * 64, digest)
        self.assertEqual(retained["selection"], "retained")
        self.assertEqual(retained["manifest"], live["manifest"])
        self.assertEqual(retained["receipt"], live["receipt"])
        self.assertEqual(Path(retained["receipt_path"]).read_bytes(), original)
        self.assertEqual(retained["original_manifest_path"], str(build / "package-manifest.json"))
        explicit = RETENTION.resolve_product_receipt(
            Path(retained["receipt_path"]), self.build_root, self.stage_root, "a" * 64)
        self.assertEqual(explicit["receipt_sha256"], retained["receipt_sha256"])
        self.assertEqual(explicit["build_metadata"], {"status": "not-observed"})
        self.assertFalse(build.exists())
        self.assertFalse(stage.exists())

    def test_reader_supports_legacy_and_deterministic_distinct_execution_receipts(self) -> None:
        plan, build, stage = self.make_complete("2", "b")
        legacy = self.legacy_metadata(plan, build, stage)
        digest = RETENTION.sha256_file(build / "package-manifest.json")
        self.reclaim()
        self.assertEqual(Path(self.selected_metadata(plan, "b" * 64, digest)["receipt_path"]).parent, legacy)
        self.make_complete("2", "b")
        self.change_execution_log(build)
        later = Path(self.reclaim()["removed"][0]["retained_metadata"])
        chosen = self.selected_metadata(plan, "b" * 64, digest)
        self.assertEqual(Path(chosen["receipt_path"]).parent, legacy)
        explicit = self.selected_metadata(plan, "b" * 64, digest,
                                         selected_receipt=later / "package-receipt.json")
        self.assertEqual(Path(explicit["receipt_path"]).parent, later)
        self.assertEqual(explicit["manifest"], chosen["manifest"])
        self.assertNotEqual(explicit["receipt_sha256"], chosen["receipt_sha256"])
        # Removing the legacy layout leaves the version-addressed record usable.
        for name in RETENTION._METADATA_NAMES:
            (legacy / name).unlink()
        self.assertEqual(self.selected_metadata(plan, "b" * 64, digest)["receipt_sha256"],
                         explicit["receipt_sha256"])

    def test_reader_refuses_changed_retained_documents_links_and_addresses(self) -> None:
        cases = ("manifest", "receipt", "retention", "manifest-link", "directory-link",
                 "wrong-version", "partial", "oversize", "plan")
        for index, case in enumerate(cases):
            with self.subTest(case=case):
                digit = format(index + 3, "x")
                plan, build, _ = self.make_complete(digit, "c")
                digest = RETENTION.sha256_file(build / "package-manifest.json")
                retained = Path(self.reclaim()["removed"][0]["retained_metadata"])
                if case in ("manifest", "receipt", "retention"):
                    name = {"manifest": "package-manifest.json", "receipt": "package-receipt.json",
                            "retention": "retention.json"}[case]
                    path = retained / name
                    document = RETENTION.load_json(path)
                    document["altered"] = True
                    path.write_bytes(RETENTION.canonical_bytes(document))
                elif case == "manifest-link":
                    path = retained / "package-manifest.json"
                    other = self.root / ("copied-" + digit)
                    path.rename(other)
                    path.symlink_to(other)
                elif case == "directory-link":
                    other = self.root / ("copied-" + digit)
                    retained.rename(other)
                    retained.symlink_to(other, target_is_directory=True)
                elif case == "wrong-version":
                    retained.rename(retained.with_name("f" * 64))
                elif case == "partial":
                    (retained / "package-receipt.json").unlink()
                elif case == "oversize":
                    with mock.patch.object(RETENTION, "MAXIMUM_METADATA_BYTES", 16):
                        with self.assertRaises(RETENTION.RetentionError):
                            self.selected_metadata(plan, "c" * 64, digest)
                    continue
                else:
                    record = RETENTION.load_json(retained / "retention.json")
                    record["original_build_directory"] = str(self.build_root / ("f" * 64))
                    (retained / "retention.json").write_bytes(RETENTION.canonical_bytes(record))
                with self.assertRaises(RETENTION.RetentionError):
                    self.selected_metadata(plan, "c" * 64, digest)

    def test_reader_uses_retained_generation_during_unrelated_partial_rebuild(self) -> None:
        for digit, defect in (("c", "partial"), ("d", "invalid"), ("e", "different"), ("f", "link")):
            with self.subTest(defect=defect):
                plan, build, _ = self.make_complete(digit, "d")
                digest = RETENTION.sha256_file(build / "package-manifest.json")
                retained = Path(self.reclaim()["removed"][0]["retained_metadata"])
                self.make_complete(digit, "e" if defect == "different" else "d")
                if defect == "partial":
                    (build / "package-receipt.json").unlink()
                elif defect == "invalid":
                    (build / "package-manifest.json").write_bytes(b"corrupt")
                elif defect == "link":
                    (build / "package-manifest.json").unlink()
                    (build / "package-manifest.json").symlink_to(retained / "package-manifest.json")
                with mock.patch.object(RETENTION, "_metadata_at", wraps=RETENTION._metadata_at) as reads:
                    explicit = self.selected_metadata(plan, "d" * 64, digest,
                        selected_receipt=retained / "package-receipt.json")
                self.assertEqual([call.args[0] for call in reads.call_args_list], [retained])
                self.assertEqual(explicit["build_metadata"], {"status": "not-observed"})
                implicit = self.selected_metadata(plan, "d" * 64, digest)
                self.assertEqual(implicit["receipt_sha256"], explicit["receipt_sha256"])
                self.assertEqual(implicit["build_metadata"]["status"], "rejected")
                self.assertTrue(implicit["build_metadata"]["failure"])
                # A rejected live execution never licenses corrupt retained bytes.
                (retained / "package-manifest.json").write_bytes(b"corrupt retained")
                with self.assertRaises(RETENTION.RetentionError):
                    self.selected_metadata(plan, "d" * 64, digest)
                # Leave no new live generation for the next reclaim invocation.
                shutil.rmtree(build)
                shutil.rmtree(self.stage_root / plan)

    def test_reader_requires_exact_expected_manifest_package_and_bounded_inventory(self) -> None:
        plan, build, _ = self.make_complete("d", "e")
        digest = RETENTION.sha256_file(build / "package-manifest.json")
        self.reclaim()
        for package, manifest in (("f" * 64, digest), ("e" * 64, "0" * 64)):
            with self.subTest(package=package, manifest=manifest), \
                    self.assertRaisesRegex(RETENTION.RetentionError, "no retained metadata"):
                self.selected_metadata(plan, package, manifest)
        with mock.patch.object(RETENTION, "MAXIMUM_RETAINED_VERSIONS", 0), \
                self.assertRaisesRegex(RETENTION.RetentionError, "version boundary"):
            self.selected_metadata(plan, "e" * 64, digest)


if __name__ == "__main__":
    unittest.main()
