#!/usr/bin/env python3
"""Exact and deliberate-defect tests for runner-owned orphan cleanup."""

from __future__ import annotations

import importlib.util
import os
import signal
import select
import shutil
import subprocess
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
        self.root = Path(self.temporary.name)
        self.proc = self.root / "proc"
        self.proc.mkdir()
        self.parent = self.root / "proofs"
        self.parent.mkdir()
        self.executable = self.root / "bin/postgres"
        self.executable.parent.mkdir()
        self.executable.write_bytes(b"fixture executable identity; never executed")
        self.executable.chmod(0o755)
        self.caller_pid = 900
        self.uid = os.geteuid()
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
        data: Path | None = None,
        start_ticks: int = 100,
    ) -> Path:
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
        fields = ["S", str(ppid)] + ["0"] * 17 + [str(start_ticks)]
        (root / "stat").write_text(f"{pid} (postgres) " + " ".join(fields))
        actual_data = data or self.parent / f"laplace-postgres-test.P{pid}" / "data"
        actual_data.mkdir(parents=True)
        (actual_data / "PG_VERSION").write_text("18\n")
        (actual_data / "postmaster.pid").write_text(f"{pid}\n{actual_data}\n123456\n")
        (root / "cmdline").write_bytes(
            os.fsencode(self.executable) + b"\0-D\0" + os.fsencode(actual_data) + b"\0"
        )
        (root / "exe").symlink_to(self.executable)
        return actual_data

    def discover(self) -> list[dict[str, object]]:
        with mock.patch.object(CLEANUP, "_process_age_seconds", return_value=600.0):
            return CLEANUP.discover(
                self.proc,
                caller_pid=self.caller_pid,
                caller_uid=self.uid,
                minimum_age_seconds=300,
                parents=(self.parent,),
            )

    def test_selects_only_same_owner_init_orphan_in_exact_runner_cgroup(self) -> None:
        self.process(101)
        self.process(102, ppid=77)
        self.process(103, uid=self.uid + 1)
        self.process(104, cgroup="0::/system.slice/postgresql.service\n")
        self.process(105, name="dotnet")

        selected = self.discover()
        self.assertEqual([item["pid"] for item in selected], [101])
        self.assertEqual(selected[0]["age_seconds"], 600.0)
        self.assertEqual(selected[0]["start_ticks"], 100)
        self.assertEqual(selected[0]["data_directory"],
                         str(self.parent / "laplace-postgres-test.P101/data"))

    def test_deliberate_cross_service_candidate_is_rejected(self) -> None:
        self.process(201, cgroup="0::/system.slice/postgresql.service\n")
        self.assertEqual(self.discover(), [])

    def test_deliberate_live_job_descendant_is_rejected(self) -> None:
        self.process(202, ppid=self.caller_pid)
        self.assertEqual(self.discover(), [])

    def test_deliberate_other_owner_candidate_is_rejected(self) -> None:
        self.process(203, uid=self.uid + 1)
        self.assertEqual(self.discover(), [])


    def test_persistent_and_unrelated_same_cgroup_postmasters_are_not_orphans(self) -> None:
        self.process(301, data=self.root / "persistent/refactor/data")
        self.process(302, data=self.parent / "unrelated/data")
        self.process(303)
        # The first two satisfy every old name/UID/PPid/cgroup predicate.
        self.assertEqual([item["pid"] for item in self.discover()], [303])

    def test_every_declared_proof_namespace_is_eligible_but_another_parent_is_not(self) -> None:
        expected = []
        for pid, prefix in enumerate(sorted(CLEANUP.RUNNER_WORKSPACE_PREFIXES), 310):
            self.process(pid, data=self.parent / (prefix + "ABC123") / "data")
            expected.append(pid)
        self.process(399, data=self.root / "unselected/laplace-postgres-test.ABC123/data")
        self.assertEqual([item["pid"] for item in self.discover()], expected)

    def test_missing_or_mismatched_postmaster_identity_is_not_selected(self) -> None:
        for pid, mutation in enumerate(("pid", "directory", "version", "missing", "argv"), 410):
            data = self.process(pid)
            if mutation == "pid":
                (data / "postmaster.pid").write_text(f"999\n{data}\n")
            elif mutation == "directory":
                (data / "postmaster.pid").write_text(f"{pid}\n/wrong/data\n")
            elif mutation == "version":
                (data / "PG_VERSION").write_text("not PostgreSQL")
            elif mutation == "missing":
                (data / "postmaster.pid").unlink()
            else:
                (self.proc / str(pid) / "cmdline").write_bytes(b"postgres: checkpointer\0")
        self.assertEqual(self.discover(), [])

    def test_linked_data_or_linked_proof_parent_is_not_selected(self) -> None:
        data = self.process(420)
        original = data.with_name("original")
        data.rename(original)
        data.symlink_to(original, target_is_directory=True)
        self.process(421)
        linked = self.root / "linked-parent"
        linked.symlink_to(self.parent, target_is_directory=True)
        linked_data = linked / "laplace-postgres-test.P421/data"
        (self.proc / "421/cmdline").write_bytes(
            os.fsencode(self.executable) + b"\0-D\0" + os.fsencode(linked_data) + b"\0")
        with mock.patch.object(CLEANUP, "_process_age_seconds", return_value=600.0):
            selected = CLEANUP.discover(self.proc, caller_pid=self.caller_pid,
                caller_uid=self.uid, parents=(self.parent, linked))
        self.assertEqual(selected, [])

    def test_foreign_owned_or_wrong_executable_data_is_not_selected(self) -> None:
        self.process(430)
        original = CLEANUP._physical_directory
        def foreign(path, uid=None):
            if path.name == "data" and uid is not None:
                return None
            return original(path, uid)
        with mock.patch.object(CLEANUP, "_physical_directory", side_effect=foreign):
            self.assertEqual(self.discover(), [])
        (self.proc / "430/exe").unlink()
        wrong = self.executable.with_name("other")
        wrong.write_bytes(b"unrelated executable")
        (self.proc / "430/exe").symlink_to(wrong)
        self.assertEqual(self.discover(), [])

    def terminate(self, candidates, send):
        with mock.patch.object(CLEANUP.os, "pidfd_open", return_value=700) as opened, \
                mock.patch.object(CLEANUP.signal, "pidfd_send_signal", side_effect=send), \
                mock.patch.object(CLEANUP.os, "close") as closed:
            result = CLEANUP.terminate(candidates, 0, proc_root=self.proc,
                caller_pid=self.caller_pid, caller_uid=self.uid, parents=(self.parent,))
        return result, opened, closed

    def test_eligible_orphan_uses_pinned_handle_for_term_and_kill(self) -> None:
        self.process(440)
        signals = []
        def send(fd, number, info, flags):
            signals.append((fd, number, info, flags))
        forced, opened, closed = self.terminate(self.discover(), send)
        self.assertEqual(forced, [440])
        opened.assert_called_once_with(440, 0)
        self.assertEqual(signals, [(700, signal.SIGTERM, None, 0),
                                   (700, signal.SIGKILL, None, 0)])
        closed.assert_called_once_with(700)

    def test_changed_birth_uid_cgroup_or_data_identity_prevents_any_signal(self) -> None:
        for mutation in ("birth", "uid", "cgroup", "data"):
            with self.subTest(mutation=mutation):
                pid = 450
                if (self.proc / str(pid)).exists():
                    shutil.rmtree(self.proc / str(pid))
                    shutil.rmtree(self.parent / f"laplace-postgres-test.P{pid}")
                data = self.process(pid)
                candidate = self.discover()
                if mutation == "birth":
                    path = self.proc / str(pid) / "stat"
                    path.write_text(path.read_text().rsplit(" ", 1)[0] + " 200")
                elif mutation == "uid":
                    path = self.proc / str(pid) / "status"
                    path.write_text(path.read_text().replace(f"Uid:\t{self.uid}", f"Uid:\t{self.uid + 1}"))
                elif mutation == "cgroup":
                    (self.proc / str(pid) / "cgroup").write_text("0::/different")
                else:
                    (data / "postmaster.pid").write_text(f"{pid}\n/wrong/data\n")
                sent = mock.Mock()
                forced, opened, _ = self.terminate(candidate, sent)
                self.assertEqual(forced, [])
                opened.assert_not_called()
                sent.assert_not_called()

    def test_replacement_between_discovery_and_open_or_between_signals_is_not_signalled(self) -> None:
        self.process(460)
        candidate = self.discover()
        path = self.proc / "460/stat"
        original = path.read_text()
        def replace_pid(*unused):
            path.write_text(original.rsplit(" ", 1)[0] + " 200")
            return 700
        with mock.patch.object(CLEANUP.os, "pidfd_open", side_effect=replace_pid), \
                mock.patch.object(CLEANUP.signal, "pidfd_send_signal") as sent, \
                mock.patch.object(CLEANUP.os, "close"):
            self.assertEqual(CLEANUP.terminate(candidate, 0, proc_root=self.proc,
                caller_pid=self.caller_pid, caller_uid=self.uid, parents=(self.parent,)), [])
            sent.assert_not_called()
        path.write_text(original)
        signals = []
        def replace_after_term(fd, number, info, flags):
            signals.append(number)
            replace_pid()
        forced, _, closed = self.terminate(candidate, replace_after_term)
        self.assertEqual(signals, [signal.SIGTERM])
        self.assertEqual(forced, [])
        closed.assert_called_once_with(700)

    def test_caller_scope_change_and_pidfile_retirement_prevent_escalation(self) -> None:
        data = self.process(470)
        candidate = self.discover()
        signals = []
        def complete_shutdown(fd, number, info, flags):
            signals.append(number)
            (data / "postmaster.pid").unlink()
        forced, _, _ = self.terminate(candidate, complete_shutdown)
        self.assertEqual(signals, [signal.SIGTERM])
        self.assertEqual(forced, [])
        (data / "postmaster.pid").write_text(f"470\n{data}\n123456\n")
        candidate = self.discover()
        self.assertEqual([entry["pid"] for entry in candidate], [470])
        (self.proc / str(self.caller_pid) / "cgroup").write_text("0::/other-caller")
        sent = mock.Mock()
        forced, opened, _ = self.terminate(candidate, sent)
        self.assertEqual(forced, [])
        opened.assert_not_called()
        sent.assert_not_called()

    @unittest.skipUnless(shutil.which("cc") and hasattr(os, "pidfd_open")
                         and hasattr(signal, "pidfd_send_signal"),
                         "Linux pidfd and a real C compiler are required")
    def test_real_owned_process_cleanup_preserves_a_persistent_neighbor(self) -> None:
        # This is a genuine Linux process/signalling control, not a PostgreSQL server.
        # The tiny executable implements only the identity protocol consumed by cleanup.
        source = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc != 3 || strcmp(argv[1], "-D")) return 2;
    pid_t pid = fork();
    if (pid < 0) return 3;
    if (pid > 0) { printf("%ld\n", (long)pid); fflush(stdout); return 0; }
    if (setsid() < 0 || chdir(argv[2]) != 0) _exit(4);
    FILE *out = fopen("postmaster.pid", "w");
    if (!out) _exit(5);
    fprintf(out, "%ld\n%s\n", (long)getpid(), argv[2]);
    if (fclose(out) != 0) _exit(6);
    close(0); close(1); close(2);
    for (;;) pause();
}
"""
        subprocess.run([shutil.which("cc"), "-x", "c", "-", "-o", str(self.executable)],
                       input=source, text=True, check=True, timeout=30,
                       capture_output=True)
        handles = {}
        try:
            for name, data in (
                    ("proof", self.parent / "laplace-postgres-test.Real01/data"),
                    ("persistent", self.root / "persistent/refactor/data")):
                data.mkdir(parents=True)
                (data / "PG_VERSION").write_text("18\n")
                child = subprocess.run([str(self.executable), "-D", str(data)],
                    check=True, capture_output=True, text=True, timeout=5)
                pid = int(child.stdout.strip())
                descriptor = os.pidfd_open(pid, 0)
                handles[name] = (pid, descriptor)
                poll = select.poll()
                poll.register(descriptor, select.POLLIN)
                self.assertEqual(poll.poll(0), [], "owned process exited before cleanup")
            candidates = CLEANUP.discover(minimum_age_seconds=0, parents=(self.parent,))
            self.assertEqual([entry["pid"] for entry in candidates], [handles["proof"][0]])
            self.assertEqual(CLEANUP.terminate(candidates, 1, parents=(self.parent,)), [])
            proof = select.poll()
            proof.register(handles["proof"][1], select.POLLIN)
            self.assertTrue(proof.poll(1000), "eligible owned process did not exit")
            persistent = select.poll()
            persistent.register(handles["persistent"][1], select.POLLIN)
            self.assertEqual(persistent.poll(0), [], "persistent neighbor was signalled")
        finally:
            for _, descriptor in handles.values():
                try:
                    signal.pidfd_send_signal(descriptor, signal.SIGKILL, None, 0)
                except ProcessLookupError:
                    pass
                os.close(descriptor)

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
