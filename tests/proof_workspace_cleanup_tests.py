#!/usr/bin/env python3
"""Positive and deliberate-defect tests for disposable proof workspace cleanup."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import time
import unittest
from unittest import mock


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
        cleanup_prefix = workflow[max(0, cleanup_index - 900):cleanup_index]
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

    def test_discovers_only_exact_postgresql_disposable_namespaces(self) -> None:
        expected = (
            self.root / "laplace-postgres-test.Abc123",
            self.root / "lp-pg.XyZ789",
        )
        for target in expected:
            target.mkdir()
        (self.root / "laplace-output").mkdir()
        (self.root / "lp-pg-invalid").mkdir()

        self.assertEqual(
            CLEANUP.discover_names(
                self.root, ["laplace-postgres-test.", "lp-pg."]
            ),
            ["laplace-postgres-test.Abc123", "lp-pg.XyZ789"],
        )

    def test_deliberate_broad_discovery_prefix_is_rejected(self) -> None:
        with self.assertRaisesRegex(CLEANUP.CleanupError, "unsafe.*discovery prefix"):
            CLEANUP.discover_names(self.root, ["laplace-"])

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

    def test_discovered_inaccessible_stale_residue_is_reported_not_promoted(self) -> None:
        target = self.root / "lp-pg.OldResidue"
        target.mkdir()
        old = time.time() - 600
        os.utime(target, (old, old))
        denied = PermissionError(13, "Permission denied", str(target))
        with mock.patch.object(CLEANUP, "_remove_entry", side_effect=denied):
            receipt = CLEANUP.cleanup(
                self.root,
                [target.name],
                minimum_age_seconds=300,
                proc_root=self.root / "no-proc",
                report_inaccessible_names=frozenset((target.name,)),
            )
        result = receipt["results"][0]
        self.assertEqual(result["state"], "inaccessible")
        self.assertEqual(result["reason"], "permission-denied")
        self.assertEqual(result["removed_entries"], 0)
        self.assertTrue(target.exists())

    def test_explicit_inaccessible_workspace_still_fails_cleanup(self) -> None:
        target = self.root / "laplace-output"
        target.mkdir()
        denied = PermissionError(13, "Permission denied", str(target))
        with mock.patch.object(CLEANUP, "_remove_entry", side_effect=denied):
            with self.assertRaisesRegex(
                CLEANUP.CleanupError, "cannot remove explicitly named"
            ):
                CLEANUP.cleanup(
                    self.root,
                    [target.name],
                    proc_root=self.root / "no-proc",
                )
        self.assertTrue(target.exists())

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
        self.assertGreaterEqual(
            custom.count("--discover-prefix laplace-postgres-test."), 2
        )
        self.assertGreaterEqual(custom.count("--discover-prefix lp-pg."), 2)

        postgres_names = ("laplace-postgresql-product-proof",)
        postgres = POSTGRESQL_PRODUCT.read_text(encoding="utf-8")
        self.assert_stale_sweep(postgres, postgres_names)
        self.assert_cleanup_step(postgres, postgres_names)
        self.assertGreaterEqual(
            postgres.count("--discover-prefix laplace-postgres-test."), 2
        )
        self.assertGreaterEqual(postgres.count("--discover-prefix lp-pg."), 2)

        package_names = ("laplace-package-product-proof",)
        package = PACKAGE_PRODUCT.read_text(encoding="utf-8")
        self.assert_stale_sweep(package, package_names)
        self.assert_cleanup_step(package, package_names)

    def test_custom_stack_cleans_both_native_namespaces_in_the_selected_scratch_root(self) -> None:
        workflow = CUSTOM_STACK.read_text(encoding="utf-8")
        self.assert_native_scratch_cleanup(workflow)

    def test_deliberate_native_scratch_root_omission_is_detected(self) -> None:
        workflow = CUSTOM_STACK.read_text(encoding="utf-8")
        mutant = workflow.replace('${TMPDIR:-/build/laplace/work}', '$RUNNER_TEMP')
        with self.assertRaises(AssertionError):
            self.assert_native_scratch_cleanup(mutant)

    def assert_native_scratch_cleanup(self, workflow: str) -> None:
        for step, stale in (("Sweep stale interrupted custom-stack residue", True),
                            ("Clean disposable custom-stack workspaces", False)):
            block = workflow.split(f"      - name: {step}\n", 1)[1].split("      - name:", 1)[0]
            self.assertIn("bash tools/host/run-exclusive.sh", block)
            if not stale:
                self.assertIn("if: always()", block)
            script = textwrap.dedent(block.split("        run: |\n", 1)[1])
            for selected in (None, str(self.root / "operator scratch")):
                calls_path = self.root / "cleanup-invocations"
                calls_path.write_bytes(b"")
                environment = dict(os.environ, RUNNER_TEMP=str(self.root / "runner-temp"),
                                   CLEANUP_CALLS=str(calls_path))
                if selected is None:
                    environment.pop("TMPDIR", None)
                else:
                    environment["TMPDIR"] = selected
                # Observe the actual shell argument expansion without deleting
                # any host workspace or invoking the process terminator.
                spy = 'python3() { printf "%s\\0" "$@" >> "$CLEANUP_CALLS"; printf "\\n" >> "$CLEANUP_CALLS"; }\n'
                subprocess.run(["bash", "-c", spy + script], env=environment,
                               check=True, capture_output=True, text=True)
                calls = [line.rstrip(b"\0").decode().split("\0")
                         for line in calls_path.read_bytes().splitlines()]
                root = selected or "/build/laplace/work"
                matching = [call for call in calls if call[0] == "tools/delivery/proof_workspace_cleanup.py"
                            and call[call.index("--runner-temp") + 1] == root]
                self.assertEqual(len(matching), 1)
                call = matching[0]
                prefixes = [call[index + 1] for index, item in enumerate(call) if item == "--discover-prefix"]
                self.assertEqual(set(prefixes), {"laplace-postgres-test.", "lp-pg."})
                self.assertNotIn("--name", call)
                if stale:
                    self.assertEqual(call[call.index("--minimum-age-seconds") + 1], "300")
                else:
                    self.assertNotIn("--minimum-age-seconds", call)

    def test_deliberate_workflow_cleanup_omission_is_detected(self) -> None:
        workflow = POSTGRESQL_PRODUCT.read_text(encoding="utf-8")
        marker = "      - name: Clean disposable PostgreSQL-product workspace\n"
        self.assertIn(marker, workflow)
        mutant = workflow[: workflow.index(marker)]
        with self.assertRaises(AssertionError):
            self.assert_cleanup_step(mutant, ("laplace-postgresql-product-proof",))

    def test_deliberate_stale_sweep_omission_is_detected(self) -> None:
        workflow = PACKAGE_PRODUCT.read_text(encoding="utf-8")
        # Locate the operation rather than its editable presentation label.
        # Renaming a step cannot disable this deliberate-omission check.
        for source in (workflow, workflow.replace("name: ", "name: renamed ")):
            with self.subTest(renamed=source != workflow):
                self.assert_stale_sweep(source, ("laplace-package-product-proof",))
                steps = source.split("\n      - ")
                sweeps = [index for index, step in enumerate(steps)
                          if "tools/delivery/proof_workspace_cleanup.py" in step
                          and "--minimum-age-seconds 300" in step
                          and "--name laplace-package-product-proof" in step]
                self.assertEqual(len(sweeps), 1)
                del steps[sweeps[0]]
                mutant = "\n      - ".join(steps)
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
