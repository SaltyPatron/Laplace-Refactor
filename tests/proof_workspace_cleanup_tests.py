#!/usr/bin/env python3
"""Positive and deliberate-defect tests for disposable proof workspace cleanup."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import tempfile
import time
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/proof_workspace_cleanup.py"
SPEC = importlib.util.spec_from_file_location("laplace_proof_workspace_cleanup", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
CLEANUP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CLEANUP)

CUSTOM_STACK = REPOSITORY / ".github/workflows/custom-stack.yml"
POSTGRESQL_PRODUCT = REPOSITORY / ".github/workflows/postgresql-product.yml"
PACKAGE_PRODUCT = REPOSITORY / ".github/workflows/package-product.yml"


class ProofWorkspaceCleanupTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def assert_cleanup_step(self, workflow: str, names: tuple[str, ...]) -> None:
        self.assertGreaterEqual(workflow.count("tools/delivery/proof_workspace_cleanup.py"), 2)
        cleanup_index = workflow.rindex("tools/delivery/proof_workspace_cleanup.py")
        cleanup_prefix = workflow[max(0, cleanup_index - 240):cleanup_index]
        self.assertIn("if: always()", cleanup_prefix)
        for name in names:
            self.assertIn(f"--name {name}", workflow[cleanup_index:])

    def assert_stale_sweep(self, workflow: str, names: tuple[str, ...]) -> None:
        first = workflow.index("tools/delivery/proof_workspace_cleanup.py")
        last = workflow.rindex("tools/delivery/proof_workspace_cleanup.py")
        self.assertLess(first, last)
        stale = workflow[first:last]
        self.assertIn("--minimum-age-seconds 300", stale)
        self.assertNotIn("if: always()", workflow[max(0, first - 240):first])
        for name in names:
            self.assertIn(f"--name {name}", stale)

    def test_removes_only_named_disposable_direct_children(self) -> None:
        target = self.root / "laplace-postgresql-product-proof"
        nested = target / "runtime/data"
        nested.mkdir(parents=True)
        (nested / "postgres.log").write_text("diagnostic\n", encoding="utf-8")
        sibling = self.root / "laplace-package-product-proof"
        sibling.mkdir()
        (sibling / "keep.txt").write_text("keep\n", encoding="utf-8")

        receipt = CLEANUP.cleanup(self.root, [target.name], proc_root=self.root / "no-proc")

        self.assertFalse(target.exists())
        self.assertTrue((sibling / "keep.txt").is_file())
        self.assertEqual(receipt["schema"], "laplace.proof-workspace-cleanup/v1")
        self.assertEqual(receipt["results"][0]["state"], "removed")
        self.assertGreaterEqual(receipt["results"][0]["removed_entries"], 4)

    def test_absent_workspace_is_idempotent(self) -> None:
        first = CLEANUP.cleanup(self.root, ["laplace-custom-stack-qa"])
        second = CLEANUP.cleanup(self.root, ["laplace-custom-stack-qa"])
        self.assertEqual(first["results"][0]["state"], "absent")
        self.assertEqual(second["results"][0]["state"], "absent")

    def test_stale_workspace_can_be_recovered_after_minimum_age(self) -> None:
        target = self.root / "laplace-output"
        target.mkdir()
        old = time.time() - 600
        os.utime(target, (old, old))
        receipt = CLEANUP.cleanup(
            self.root,
            [target.name],
            minimum_age_seconds=300,
            proc_root=self.root / "no-proc",
        )
        self.assertEqual(receipt["minimum_age_seconds"], 300)
        self.assertEqual(receipt["results"][0]["state"], "removed")
        self.assertFalse(target.exists())

    def test_workflows_sweep_interrupted_residue_and_cleanup_terminal_workspaces(self) -> None:
        custom_names = (
            "laplace-sources",
            "laplace-output",
            "laplace-custom-stack-qa",
            "laplace-custom-stack-qa-result-replay.json",
            "laplace-custom-stack-changed.status",
            "laplace-custom-stack-qa-plan.json",
        )
        custom = CUSTOM_STACK.read_text(encoding="utf-8")
        self.assert_stale_sweep(custom, custom_names)
        self.assert_cleanup_step(custom, custom_names)

        postgres_names = ("laplace-postgresql-product-proof",)
        postgres = POSTGRESQL_PRODUCT.read_text(encoding="utf-8")
        self.assert_stale_sweep(postgres, postgres_names)
        self.assert_cleanup_step(postgres, postgres_names)

        package_names = ("laplace-package-product-proof",)
        package = PACKAGE_PRODUCT.read_text(encoding="utf-8")
        self.assert_stale_sweep(package, package_names)
        self.assert_cleanup_step(package, package_names)

    def test_deliberate_workflow_cleanup_omission_is_detected(self) -> None:
        workflow = POSTGRESQL_PRODUCT.read_text(encoding="utf-8")
        marker = "      - name: Clean disposable PostgreSQL-product workspace\n"
        self.assertIn(marker, workflow)
        mutant = workflow[: workflow.index(marker)]
        with self.assertRaises(AssertionError):
            self.assert_cleanup_step(mutant, ("laplace-postgresql-product-proof",))

    def test_deliberate_stale_sweep_omission_is_detected(self) -> None:
        workflow = PACKAGE_PRODUCT.read_text(encoding="utf-8")
        start = workflow.index("      - name: Sweep stale interrupted package-product residue\n")
        end = workflow.index("      - name: Verify package-proof authority", start)
        mutant = workflow[:start] + workflow[end:]
        with self.assertRaises((AssertionError, ValueError)):
            self.assert_stale_sweep(mutant, ("laplace-package-product-proof",))

    def test_deliberate_path_escape_is_rejected(self) -> None:
        outside = self.root.parent / "outside-proof-do-not-delete"
        outside.write_text("outside\n", encoding="utf-8")
        self.addCleanup(lambda: outside.unlink(missing_ok=True))
        with self.assertRaisesRegex(CLEANUP.CleanupError, "unsafe disposable workspace name"):
            CLEANUP.cleanup(self.root, ["../outside-proof-do-not-delete"])
        self.assertEqual(outside.read_text(encoding="utf-8"), "outside\n")

    def test_deliberate_non_laplace_target_is_rejected(self) -> None:
        target = self.root / "postgresql-product-proof"
        target.mkdir()
        with self.assertRaisesRegex(CLEANUP.CleanupError, "unsafe disposable workspace name"):
            CLEANUP.cleanup(self.root, [target.name])
        self.assertTrue(target.is_dir())

    def test_deliberate_symlink_workspace_root_is_rejected(self) -> None:
        outside = self.root / "outside"
        outside.mkdir()
        marker = outside / "marker"
        marker.write_text("keep\n", encoding="utf-8")
        target = self.root / "laplace-custom-stack-qa"
        target.symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(CLEANUP.CleanupError, "symlink workspace root"):
            CLEANUP.cleanup(self.root, [target.name])
        self.assertEqual(marker.read_text(encoding="utf-8"), "keep\n")

    def test_deliberate_recent_workspace_is_rejected_by_stale_sweeper(self) -> None:
        target = self.root / "laplace-output"
        target.mkdir()
        with self.assertRaisesRegex(CLEANUP.CleanupError, "refusing recent workspace"):
            CLEANUP.cleanup(
                self.root,
                [target.name],
                minimum_age_seconds=300,
                proc_root=self.root / "no-proc",
            )
        self.assertTrue(target.is_dir())

    def test_deliberate_active_process_reference_is_rejected(self) -> None:
        target = self.root / "laplace-package-product-proof"
        target.mkdir()
        marker = target / "marker"
        marker.write_text("keep\n", encoding="utf-8")
        proc_root = self.root / "proc"
        fd_root = proc_root / "1234/fd"
        fd_root.mkdir(parents=True)
        (fd_root / "7").symlink_to(marker)
        with self.assertRaisesRegex(CLEANUP.CleanupError, "refusing active workspace"):
            CLEANUP.cleanup(self.root, [target.name], proc_root=proc_root)
        self.assertEqual(marker.read_text(encoding="utf-8"), "keep\n")

    def test_deleted_inode_reference_cannot_claim_recreated_workspace(self) -> None:
        target = self.root / "laplace-output"
        target.mkdir()
        marker = target / "current"
        marker.write_text("current generation\n", encoding="utf-8")
        proc_root = self.root / "proc"
        fd_root = proc_root / "1234/fd"
        fd_root.mkdir(parents=True)
        (fd_root / "7").symlink_to(f"{marker} (deleted)")

        receipt = CLEANUP.cleanup(self.root, [target.name], proc_root=proc_root)

        self.assertEqual(receipt["results"][0]["state"], "removed")
        self.assertFalse(target.exists())

    def test_deliberate_cross_device_entry_is_rejected_before_deletion(self) -> None:
        target = self.root / "laplace-package-product-proof"
        target.mkdir()
        marker = target / "marker"
        marker.write_text("keep\n", encoding="utf-8")
        wrong_device = marker.lstat().st_dev + 1
        with self.assertRaisesRegex(CLEANUP.CleanupError, "cross-device cleanup"):
            CLEANUP._remove_entry(marker, wrong_device)
        self.assertEqual(marker.read_text(encoding="utf-8"), "keep\n")

    def test_duplicate_target_is_rejected(self) -> None:
        with self.assertRaisesRegex(CLEANUP.CleanupError, "duplicate disposable workspace name"):
            CLEANUP.cleanup(
                self.root,
                ["laplace-custom-stack-qa", "laplace-custom-stack-qa"],
            )


if __name__ == "__main__":
    unittest.main()
