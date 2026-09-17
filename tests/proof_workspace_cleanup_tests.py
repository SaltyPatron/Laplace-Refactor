#!/usr/bin/env python3
"""Positive and deliberate-defect tests for disposable proof workspace cleanup."""

from __future__ import annotations

import contextlib
import importlib.util
import json
import os
import select
from pathlib import Path
import subprocess
import sys
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
            custom.count("--discover-prefix laplace-postgres-test."), 1
        )
        self.assertGreaterEqual(custom.count("--discover-prefix lp-pg."), 1)

        postgres_names = ("laplace-postgresql-product-proof",)
        postgres = POSTGRESQL_PRODUCT.read_text(encoding="utf-8")
        self.assert_stale_sweep(postgres, postgres_names)
        self.assert_cleanup_step(postgres, postgres_names)
        self.assertGreaterEqual(
            postgres.count("--discover-prefix laplace-postgres-test."), 1
        )
        self.assertGreaterEqual(postgres.count("--discover-prefix lp-pg."), 1)

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
        for step, stale in (("Sweep stale interrupted custom-stack residue", True),):
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


    @staticmethod
    def cleanup_cases() -> tuple[tuple[Path, str, tuple[str, ...]], ...]:
        return (
            (CUSTOM_STACK, "Clean disposable custom-stack workspaces",
             ("laplace-sources", "laplace-output", "laplace-custom-stack-qa",
              "laplace-custom-stack-qa-result-replay.json",
              "laplace-custom-stack-changed.status", "laplace-custom-stack-qa-plan.json")),
            (POSTGRESQL_PRODUCT, "Clean disposable PostgreSQL-product workspace",
             ("laplace-postgresql-product-proof",)),
            (PACKAGE_PRODUCT, "Clean isolated installation workspace",
             ("laplace-package-product-proof",)),
        )

    @staticmethod
    def step_block(workflow: str, name: str) -> str:
        return workflow.split(f"      - name: {name}\n", 1)[1].split("      - name:", 1)[0]

    def assert_private_cleanup(self, workflow: str, name: str, names: tuple[str, ...]) -> str:
        block = self.step_block(workflow, name)
        self.assertIn("if: always()", block)
        self.assertIn("        shell: bash\n", block)
        self.assertNotIn("run-exclusive.sh", block)
        self.assertNotIn("runner_orphan_cleanup.py", block)
        self.assertNotIn("--discover-prefix", block)
        self.assertNotIn("TMPDIR", block)
        self.assertNotIn("--runner-temp /tmp", block)
        script = textwrap.dedent(block.split("        run: |\n", 1)[1])
        calls_path = self.root / "private-cleanup-calls"
        calls_path.write_bytes(b"")
        environment = dict(os.environ, RUNNER_TEMP=str(self.root / "runner temp"),
                           GITHUB_STEP_SUMMARY=str(self.root / "summary"),
                           CLEANUP_CALLS=str(calls_path), TMPDIR=str(self.root / "shared scratch"))
        spy = 'python3() { printf "%s\\0" "$@" >> "$CLEANUP_CALLS"; printf "\\n" >> "$CLEANUP_CALLS"; }\n'
        subprocess.run(["bash", "-c", spy + script], cwd=REPOSITORY, env=environment,
                       check=True, capture_output=True, text=True, timeout=10)
        calls = [line.rstrip(b"\0").decode().split("\0")
                 for line in calls_path.read_bytes().splitlines()]
        expected = ["tools/delivery/proof_workspace_cleanup.py", "--runner-temp",
                    environment["RUNNER_TEMP"]]
        for item in names:
            expected.extend(("--name", item))
        self.assertEqual(calls, [expected])
        return script

    def test_terminal_cleanup_is_explicit_private_and_does_not_scan_or_signal(self) -> None:
        for path, name, names in self.cleanup_cases():
            with self.subTest(workflow=path.name):
                self.assert_private_cleanup(path.read_text(encoding="utf-8"), name, names)

    def test_deliberate_terminal_lock_or_shared_discovery_is_rejected(self) -> None:
        path, name, names = self.cleanup_cases()[1]
        source = path.read_text(encoding="utf-8")
        block = self.step_block(source, name)
        for mutant in (
            block.replace("shell: bash\n", "shell: bash tools/host/run-exclusive.sh bash {0}\n"),
            block.replace("--name laplace-postgresql-product-proof",
                          "--discover-prefix laplace-postgres-test."),
            block.replace("tools/delivery/proof_workspace_cleanup.py",
                          "tools/delivery/runner_orphan_cleanup.py"),
        ):
            with self.subTest(mutant=mutant):
                with self.assertRaises(AssertionError):
                    self.assert_private_cleanup(source.replace(block, mutant), name, names)

    def test_shared_recovery_stays_reserved_and_private_cleanup_keeps_ownership_guard(self) -> None:
        for path, prepare_id in ((CUSTOM_STACK, "prepare_custom_stack"),
                                 (POSTGRESQL_PRODUCT, "prepare_postgresql_product")):
            workflow = path.read_text(encoding="utf-8")
            preparation = self.step_block(workflow, "Terminate stale database workers from interrupted jobs")
            self.assertIn(f"id: {prepare_id}", preparation)
            self.assertIn("shell: bash tools/host/run-exclusive.sh", preparation)
            self.assertIn("printf 'started=true\\n'", preparation)
            self.assertIn("--minimum-age-seconds 300", preparation)
            self.assertIn("--terminate", preparation)
            blocks = workflow.split("\n      - name: ")[1:]
            for block in blocks:
                if "--discover-prefix" in block:
                    self.assertIn("shell: bash tools/host/run-exclusive.sh", block)
                    self.assertIn("--minimum-age-seconds 300", block)
            name = next(name for case, name, _ in self.cleanup_cases() if case == path)
            terminal = self.step_block(workflow, name)
            self.assertIn(f"always() && steps.{prepare_id}.outputs.started == 'true'", terminal)
            self.assertIn("Shared interrupted residue recovery: deferred to the next reserved preparation pass.",
                          terminal)

    def assert_evidence_before_cleanup(self, workflow: str, cleanup_name: str,
                                      evidence_names: tuple[str, ...]) -> None:
        cleanup_at = workflow.index(f"      - name: {cleanup_name}\n")
        for evidence in evidence_names:
            self.assertLess(workflow.index(f"      - name: {evidence}\n"), cleanup_at)

    def test_evidence_and_failure_diagnostics_precede_private_deletion(self) -> None:
        custom = CUSTOM_STACK.read_text(encoding="utf-8")
        evidence = ("Retain exact custom-stack terminal result",
                    "Upload custom-stack QA evidence copy",
                    "Upload exact C++ provider build evidence",
                    "Upload exact PGN provider build evidence",
                    "Persist custom-stack terminal result by BLAKE3",
                    "Print retained PostgreSQL evidence")
        self.assert_evidence_before_cleanup(custom, self.cleanup_cases()[0][1], evidence)
        postgres = POSTGRESQL_PRODUCT.read_text(encoding="utf-8")
        self.assert_evidence_before_cleanup(
            postgres, self.cleanup_cases()[1][1],
            ("Prove exact current-change PostgreSQL 18.6 package runtime",
             "Print bounded PostgreSQL-product failure diagnostics"))
        package = PACKAGE_PRODUCT.read_text(encoding="utf-8")
        self.assert_evidence_before_cleanup(
            package, self.cleanup_cases()[2][1],
            ("Build or reuse package and verify isolated installation",
             "Build and verify local installer"))
        # Deliberately moving deletion ahead of evidence must fail the same check.
        early = "      - name: Clean disposable custom-stack workspaces\n" + custom
        with self.assertRaises(AssertionError):
            self.assert_evidence_before_cleanup(early, self.cleanup_cases()[0][1], evidence)

    @contextlib.contextmanager
    def held_host_lock(self, lock: Path, cwd: Path):
        # A real independent process owns the lock and an active cwd reference.
        holder = subprocess.Popen(
            [sys.executable, "-c",
             "import fcntl,sys\n"
             "with open(sys.argv[1], 'a') as lock:\n"
             " fcntl.flock(lock, fcntl.LOCK_EX)\n"
             " print('locked', flush=True)\n"
             " sys.stdin.read()\n", str(lock)],
            cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True)
        try:
            self.assertTrue(select.select([holder.stdout], [], [], 10)[0],
                            "lock holder did not become ready")
            self.assertEqual(holder.stdout.readline().strip(), "locked")
            yield holder
        finally:
            try:
                holder.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                holder.kill()
                holder.communicate(timeout=10)

    @unittest.skipUnless(sys.platform == "linux", "requires Linux flock and proc references")
    def test_private_cleanup_finishes_while_host_lock_and_other_workspace_are_active(self) -> None:
        for index, (path, name, names) in enumerate(self.cleanup_cases()):
            with self.subTest(workflow=path.name):
                workflow = path.read_text(encoding="utf-8")
                script = self.assert_private_cleanup(workflow, name, names)
                root = self.root / f"case-{index}"
                root.mkdir()
                for target_name in names:
                    target = root / target_name
                    target.mkdir()
                    (target / "private-output").write_text("disposable\n", encoding="utf-8")
                sibling = root / "laplace-postgres-test.Active"
                sibling.mkdir()
                marker = sibling / "keep"
                marker.write_text("active other proof\n", encoding="utf-8")
                # Receipts outside the deleted tree remain available to later jobs.
                evidence = root / "laplace-postgresql-product-proof.json"
                evidence.write_text("retained result\n", encoding="utf-8")
                lock = self.root / f"host-resource-{index}.lock"
                environment = dict(os.environ, RUNNER_TEMP=str(root),
                                   GITHUB_STEP_SUMMARY=str(root / "summary"),
                                   LAPLACE_HOST_RESOURCE_LOCK=str(lock),
                                   TMPDIR=str(sibling))
                with self.held_host_lock(lock, sibling) as holder:
                    locked = subprocess.run(["flock", "--nonblock", str(lock), "true"],
                                            capture_output=True, timeout=10)
                    self.assertEqual(locked.returncode, 1, "global lock was not held")
                    completed = subprocess.run(["bash", "-c", script], cwd=REPOSITORY,
                                               env=environment, capture_output=True,
                                               text=True, timeout=10)
                    self.assertEqual(completed.returncode, 0, completed.stderr)
                    self.assertIsNone(holder.poll())
                    receipt = json.loads(completed.stdout)
                    self.assertEqual({row["name"] for row in receipt["results"]}, set(names))
                    self.assertTrue(all(row["state"] == "removed" for row in receipt["results"]))
                    self.assertTrue(all(not (root / item).exists() for item in names))
                    self.assertEqual(marker.read_text(encoding="utf-8"), "active other proof\n")
                    self.assertEqual(evidence.read_text(encoding="utf-8"), "retained result\n")
                    self.assertEqual(subprocess.run(
                        ["flock", "--nonblock", str(lock), "true"],
                        capture_output=True, timeout=10).returncode, 1)

    @unittest.skipUnless(sys.platform == "linux", "requires Linux flock and proc references")
    def test_private_cleanup_refuses_real_live_target_without_waiting_for_host_lock(self) -> None:
        path, name, names = self.cleanup_cases()[1]
        script = self.assert_private_cleanup(path.read_text(encoding="utf-8"), name, names)
        root = self.root / "live-target-case"
        target = root / names[0]
        target.mkdir(parents=True)
        marker = target / "keep"
        marker.write_text("live target\n", encoding="utf-8")
        lock = self.root / "live-target-host.lock"
        environment = dict(os.environ, RUNNER_TEMP=str(root),
                           GITHUB_STEP_SUMMARY=str(root / "summary"),
                           LAPLACE_HOST_RESOURCE_LOCK=str(lock))
        with self.held_host_lock(lock, target) as holder:
            completed = subprocess.run(["bash", "-c", script], cwd=REPOSITORY,
                                       env=environment, capture_output=True,
                                       text=True, timeout=10)
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("refusing active workspace", completed.stderr)
            self.assertIsNone(holder.poll())
            self.assertEqual(marker.read_text(encoding="utf-8"), "live target\n")


if __name__ == "__main__":
    unittest.main()
