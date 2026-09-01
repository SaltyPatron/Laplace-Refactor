#!/usr/bin/env python3
"""Exact and deliberate-defect tests for runner-owned orphan cleanup."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/runner_orphan_cleanup.py"
SPEC = importlib.util.spec_from_file_location("laplace_runner_orphan_cleanup", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
CLEANUP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CLEANUP)

CUSTOM_STACK = REPOSITORY / ".github/workflows/custom-stack.yml"
POSTGRESQL_PRODUCT = REPOSITORY / ".github/workflows/postgresql-product.yml"


class RunnerOrphanCleanupTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.proc = Path(self.temporary.name)
        self.caller_pid = 900
        self.uid = 994
        self.cgroup = "0::/system.slice/laplace-refactor-runner.service\n"
        caller = self.proc / str(self.caller_pid)
        caller.mkdir()
        (caller / "cgroup").write_text(self.cgroup, encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def process(
        self,
        pid: int,
        *,
        name: str = "postgres",
        ppid: int = 1,
        uid: int | None = None,
        cgroup: str | None = None,
    ) -> None:
        root = self.proc / str(pid)
        root.mkdir()
        actual_uid = self.uid if uid is None else uid
        (root / "status").write_text(
            f"Name:\t{name}\nPPid:\t{ppid}\nUid:\t{actual_uid}\t{actual_uid}\t{actual_uid}\t{actual_uid}\n",
            encoding="utf-8",
        )
        (root / "cgroup").write_text(
            self.cgroup if cgroup is None else cgroup, encoding="utf-8"
        )

    def discover(self) -> list[dict[str, object]]:
        with mock.patch.object(CLEANUP, "_process_age_seconds", return_value=600.0):
            return CLEANUP.discover(
                self.proc,
                caller_pid=self.caller_pid,
                caller_uid=self.uid,
                minimum_age_seconds=300,
            )

    def test_selects_only_same_owner_init_orphan_in_exact_runner_cgroup(self) -> None:
        self.process(101)
        self.process(102, ppid=77)
        self.process(103, uid=1000)
        self.process(104, cgroup="0::/system.slice/postgresql.service\n")
        self.process(105, name="dotnet")

        self.assertEqual(self.discover(), [{"pid": 101, "age_seconds": 600.0}])

    def test_deliberate_cross_service_candidate_is_rejected(self) -> None:
        self.process(201, cgroup="0::/system.slice/postgresql.service\n")
        self.assertEqual(self.discover(), [])

    def test_deliberate_live_job_descendant_is_rejected(self) -> None:
        self.process(202, ppid=self.caller_pid)
        self.assertEqual(self.discover(), [])

    def test_deliberate_other_owner_candidate_is_rejected(self) -> None:
        self.process(203, uid=self.uid + 1)
        self.assertEqual(self.discover(), [])

    def test_workflows_cleanup_before_proof_workspaces_are_used(self) -> None:
        for path in (CUSTOM_STACK, POSTGRESQL_PRODUCT):
            workflow = path.read_text(encoding="utf-8")
            cleanup = workflow.index("tools/delivery/runner_orphan_cleanup.py")
            workspace = workflow.index("tools/delivery/proof_workspace_cleanup.py")
            self.assertLess(cleanup, workspace)
            invocation = workflow[cleanup : cleanup + 220]
            self.assertIn("--minimum-age-seconds 300", invocation)
            self.assertIn("--terminate", invocation)

    def test_deliberate_workflow_omission_is_detected(self) -> None:
        workflow = CUSTOM_STACK.read_text(encoding="utf-8")
        mutant = workflow.replace("tools/delivery/runner_orphan_cleanup.py", "omitted")
        with self.assertRaises(ValueError):
            mutant.index("tools/delivery/runner_orphan_cleanup.py")


if __name__ == "__main__":
    unittest.main()
