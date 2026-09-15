#!/usr/bin/env python3
"""Failure controls for resource admission and measured chess receipts."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/dependencies"))
spec = importlib.util.spec_from_file_location("chess_benchmark", ROOT / "tools/dependencies/chess_benchmark.py")
assert spec and spec.loader
BENCH = importlib.util.module_from_spec(spec)
spec.loader.exec_module(BENCH)


class ChessBenchmarks(unittest.TestCase):
    def test_measured_process_retains_selected_child_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            transcript = Path(temporary) / "runtime.log"
            environment = {**os.environ, "LAPLACE_TEST_QT_RUNTIME": "selected-sdk"}
            host = {"cgroups": [], "process_identity": {"consistent": False}}
            result = BENCH.measured_process([sys.executable, "-c", "import os; print(os.environ['LAPLACE_TEST_QT_RUNTIME'])"], transcript, host, 5, 32 * BENCH.MIB, environment=environment)
            self.assertEqual(result["completion"], "completed")
            self.assertEqual(transcript.read_text().strip(), "selected-sdk")
            self.assertNotIn("LAPLACE_TEST_QT_RUNTIME", os.environ)

    def test_parent_quota_overrides_unlimited_child(self):
        cpu, memory = BENCH.resource_bounds(168, 100 * BENCH.MIB, [{"cpu.max": "max 100000", "memory.max": "max"}, {"cpu.max": "800000 100000", "memory.max": str(20 * BENCH.MIB), "memory.current": str(15 * BENCH.MIB)}])
        self.assertEqual(cpu, 8)
        self.assertEqual(memory, 5 * BENCH.MIB)

    def test_fractional_quota_and_memory_headroom_remain_exact(self):
        cpu, memory = BENCH.resource_bounds(9, 100, [{"cpu.max": "750000 100000", "memory.max": "90", "memory.current": "89"}, {"cpu.max": "800000 100000", "memory.max": "100", "memory.current": "50"}])
        self.assertEqual((cpu, memory), (7.5, 1))

    def test_affinity_is_a_tighter_boundary_than_quota(self):
        self.assertEqual(BENCH.resource_bounds(2, 1000, [{"cpu.max": "800000 100000"}])[0], 2)

    def test_exhausted_memory_has_zero_headroom(self):
        self.assertEqual(BENCH.resource_bounds(8, 1000, [{"memory.max": "100", "memory.current": "101"}])[1], 0)

    def test_mount_root_resolves_visible_ancestry(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "self").mkdir()
            (root / "self/cgroup").write_text("0::/tenant/session/worker\n")
            (root / "self/mountinfo").write_text("28 1 0:25 /tenant /sys/fs/cgroup rw - cgroup2 cgroup rw\n")
            paths, visibility = BENCH.visible_cgroups(root)
            self.assertEqual(paths, [Path("/sys/fs/cgroup/session/worker"), Path("/sys/fs/cgroup/session"), Path("/sys/fs/cgroup")])
            self.assertIn("unobservable", visibility)

    def options(self):
        return BENCH.parse_options("option name Threads type spin default 1 min 1 max 1024\noption name Hash type spin default 16 min 1 max 33554432\noption name NumaPolicy type string default auto\nuciok\nreadyok\n")

    def arguments(self):
        return argparse.Namespace(cpu_budget=8, memory_mib=4096, reserve_memory_mib=512, engine_overhead_mib=256, threads="1,8,16", hash_mib="16", concurrency="1,8", game_threads=1, game_hash_mib=16, games=8)

    def host(self):
        return {"effective_cpu_equivalents": 8, "effective_memory_headroom_bytes": 8192 * BENCH.MIB, "cgroups": [{"path": "/test"}]}

    def test_plan_rejects_excess_cpu_and_two_engine_memory(self):
        result = BENCH.plan(self.arguments(), self.host(), self.options())
        self.assertEqual([item["threads"] for item in result["stockfish"]], [1, 8])
        self.assertEqual([item["concurrency"] for item in result["cutechess"]], [1])
        self.assertEqual(len(result["rejected"]), 2)

    def test_ponder_off_accounts_active_search_cpu_per_game(self):
        arguments = self.arguments()
        arguments.memory_mib = 8192
        result = BENCH.plan(arguments, self.host(), self.options())
        self.assertEqual([item["concurrency"] for item in result["cutechess"]], [1, 8])
        self.assertFalse(result["ponder"])

    def test_explicit_cpu_budget_cannot_escape_quota(self):
        arguments = self.arguments()
        arguments.cpu_budget = 9
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "CPU budget exceeds"):
            BENCH.plan(arguments, self.host(), self.options())

    def test_automatic_memory_grant_covers_sweep_not_all_host_headroom(self):
        arguments = self.arguments()
        arguments.memory_mib = None
        arguments.cpu_budget = 12
        arguments.threads = None
        arguments.concurrency = None
        arguments.hash_mib = "16,64,256"
        arguments.games = 16
        host = {**self.host(), "effective_cpu_equivalents": 12, "effective_memory_headroom_bytes": 80 * 1024 * BENCH.MIB}
        result = BENCH.plan(arguments, host, self.options())
        self.assertEqual(result["estimated_peak_resident_mib"], 6528)
        self.assertEqual(result["memory_mib"], 7040)
        self.assertEqual(result["memory_margin_mib"], 512)
        self.assertFalse(result["memory_estimate_is_measurement"])
        # Background growth consumes 8 GiB; the admitted experiment still fits.
        after = {**host, "effective_memory_headroom_bytes": 72 * 1024 * BENCH.MIB}
        self.assertEqual(BENCH.stability_failures(host, after, {"engine": "same"}, {"engine": "same"}, result), [])
        # Deliberate break: a 6 GiB ending headroom cannot cover its 7040 MiB grant.
        unsafe = {**host, "effective_memory_headroom_bytes": 6 * 1024 * BENCH.MIB}
        self.assertIn("ending memory headroom", BENCH.stability_failures(host, unsafe, {"engine": "same"}, {"engine": "same"}, result)[0])

    def test_automatic_sweep_includes_affinity_visible_physical_core_boundary(self):
        arguments = self.arguments()
        arguments.memory_mib = None
        arguments.cpu_budget = 12
        arguments.threads = None
        arguments.concurrency = None
        arguments.games = 16
        host = {**self.host(), "effective_cpu_equivalents": 12, "visible_physical_cores": 6}
        result = BENCH.plan(arguments, host, self.options())
        self.assertEqual(sorted({item["threads"] for item in result["stockfish"]}), [1, 2, 4, 6, 8, 12])
        self.assertEqual([item["concurrency"] for item in result["cutechess"]], [1, 2, 4, 6, 8, 12])
        arguments.game_threads = 2
        result = BENCH.plan(arguments, host, self.options())
        self.assertEqual([item["concurrency"] for item in result["cutechess"]], [1, 2, 3, 4, 6])
        self.assertTrue(all(item["concurrency"] * item["threads"] <= 12 for item in result["cutechess"]))

    def test_physical_boundary_cannot_override_explicit_grid_or_resource_grant(self):
        arguments = self.arguments()
        arguments.memory_mib = None
        arguments.cpu_budget = 4
        arguments.threads = None
        arguments.concurrency = None
        host = {**self.host(), "effective_cpu_equivalents": 12, "visible_physical_cores": 6}
        result = BENCH.plan(arguments, host, self.options())
        self.assertEqual(sorted({item["threads"] for item in result["stockfish"]}), [1, 2, 4])
        self.assertEqual([item["concurrency"] for item in result["cutechess"]], [1, 2, 4])
        arguments.cpu_budget = 12
        arguments.threads = "1,8"
        arguments.concurrency = "1,4"
        result = BENCH.plan(arguments, host, self.options())
        self.assertEqual(sorted({item["threads"] for item in result["stockfish"]}), [1, 8])
        self.assertEqual([item["concurrency"] for item in result["cutechess"]], [1, 4])
        self.assertEqual(BENCH.automatic_sweep_values(12, None), [1, 2, 4, 8, 12])

    def test_automatic_budget_does_not_fund_ineligible_or_unfittable_cases(self):
        arguments = self.arguments()
        arguments.memory_mib = None
        arguments.hash_mib = "16,999999"
        arguments.concurrency = "1,16"
        result = BENCH.plan(arguments, self.host(), self.options())
        self.assertEqual(result["memory_mib"], 1056)
        self.assertEqual([item["concurrency"] for item in result["cutechess"]], [1])
        self.assertTrue(all(item["hash_mib"] == 16 for item in result["stockfish"]))

    def test_automatic_budget_honors_host_reserve_and_explicit_zero_is_invalid(self):
        arguments = self.arguments()
        arguments.memory_mib = None
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "reserve leaves no"):
            BENCH.plan(arguments, {**self.host(), "effective_memory_headroom_bytes": 512 * BENCH.MIB}, self.options())
        arguments.memory_mib = 0
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "memory budget"):
            BENCH.plan(arguments, self.host(), self.options())

    def test_fractional_sub_cpu_capacity_is_not_rounded_up(self):
        host = {**self.host(), "effective_cpu_equivalents": 0.5}
        for budget in (None, 1):
            arguments = self.arguments()
            arguments.cpu_budget = budget
            with self.assertRaisesRegex(BENCH.tools.ChessToolError, "less than one full CPU"):
                BENCH.plan(arguments, host, self.options())

    def test_concurrency_cannot_exceed_available_games(self):
        arguments = self.arguments()
        arguments.memory_mib = 8192
        arguments.games = 2
        result = BENCH.plan(arguments, self.host(), self.options())
        self.assertEqual([item["concurrency"] for item in result["cutechess"]], [1])
        self.assertTrue(any("game count" in item["reason"] for item in result["rejected"]))

    def test_resource_or_binary_drift_prevents_recommendation(self):
        before = self.host()
        initial = {"stockfish": "first", "cutechess": "same"}
        plan = {"memory_mib": 4096}
        self.assertEqual(BENCH.stability_failures(before, before, initial, initial, plan), [])
        self.assertIn("executable", BENCH.stability_failures(before, before, initial, {**initial, "stockfish": "rebuilt"}, plan)[0])
        changed = {**before, "effective_cpu_equivalents": 4}
        self.assertTrue(BENCH.stability_failures(before, changed, initial, initial, plan))
        changed = {**before, "cgroups": [{"path": "/test", "memory.max": "100"}]}
        self.assertTrue(BENCH.stability_failures(before, changed, initial, initial, plan))
        changed = {**before, "effective_memory_headroom_bytes": 1024}
        self.assertTrue(BENCH.stability_failures(before, changed, initial, initial, plan))

    def test_unreadable_smaps_is_unknown_even_after_readable_sample(self):
        def fake_read(path):
            return {"stat": "123 (engine) S 1 77 1", "status": "VmRSS: 8 kB\nThreads: 1", "smaps_rollup": None}[path.name]
        with patch.object(BENCH.Path, "iterdir", return_value=[Path("/proc/123")]), patch.object(BENCH, "read", side_effect=fake_read):
            sample = BENCH.sample_group(77)
        self.assertEqual(sample["rss_bytes"], 8192)
        self.assertIsNone(sample["pss_bytes"])
        self.assertIsNone(sample["anon_huge_bytes"])
        peak = BENCH.merge_peak(dict.fromkeys(BENCH.PROCESS_METRICS, 0), sample)
        self.assertIsNone(BENCH.merge_peak(peak, dict.fromkeys(BENCH.PROCESS_METRICS, 1))["pss_bytes"])

    def test_cleanup_reaps_owned_group_even_when_leader_has_exited(self):
        process = Mock(pid=123)
        process.poll.return_value = 0
        with patch.object(BENCH.os, "killpg") as kill:
            BENCH.cleanup_group(process)
        kill.assert_called_once_with(123, BENCH.signal.SIGKILL)
        process.wait.assert_called_once()

    def test_uci_readback_requires_ready_and_preserves_limits(self):
        self.assertEqual(self.options()["Threads"]["max"], "1024")
        self.assertEqual(self.options()["NumaPolicy"]["default"], "auto")
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "incomplete"):
            BENCH.parse_options("option name Hash type spin default 16 min 1 max 1000\n")

    def test_bench_requires_complete_positions_and_summary(self):
        text = "Position: 1/2\nbestmove e2e4\nPosition: 2/2\nbestmove e7e5\nTotal time (ms) : 100\nNodes searched : 1000\nNodes/second : 10000\n"
        self.assertEqual(BENCH.parse_bench(text)["position_count"], 2)
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "exact position set"):
            BENCH.parse_bench(text.replace("Position: 1/2\n", ""))
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "summary missing"):
            BENCH.parse_bench(text.replace("Nodes searched : 1000\n", ""))

    def pgn(self):
        return '\n'.join(f'[Event "Probe"]\n[White "{white}"]\n[Black "{black}"]\n[Result "1/2-1/2"]\n\n1. e4 e5 2. Nf3 Nc6 1/2-1/2\n' for white, black in [("Source-A", "Source-B"), ("Source-B", "Source-A")])

    def test_pgn_counts_plies_and_rejects_unbalanced_colors(self):
        self.assertEqual(BENCH.validate_pgn(self.pgn(), 2)["plies"], 8)
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "pairing"):
            BENCH.validate_pgn(self.pgn().replace('[White "Source-B"]', '[White "Source-A"]'), 2)

    def test_crashed_and_unfinished_games_cannot_pass(self):
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "unfinished"):
            BENCH.validate_pgn(self.pgn().replace('[Result "1/2-1/2"]', '[Result "*"]'), 2)
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "failure"):
            BENCH.validate_pgn(self.pgn() + "{Engine disconnected}", 2)

    def test_warmups_do_not_affect_summary(self):
        sample = {"configuration": {"threads": 1}, "sampled_peak": {"rss_bytes": 100}, "user_cpu_seconds": 1, "system_cpu_seconds": 0, "wall_seconds": 1}
        values = [{**sample, "warmup": True, "rate": 1000000}, {**sample, "warmup": False, "rate": 2}, {**sample, "warmup": False, "rate": 4}]
        result = BENCH.aggregates(values, "median_rate", "rate")[0]
        self.assertEqual((result["samples"], result["median_rate"]), (2, 3))

    def test_timeout_kills_measured_process_group(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = BENCH.measured_process([sys.executable, "-c", "import time; time.sleep(10)"], Path(temporary) / "timeout.log", {"cgroups": []}, 0.15, 128 * BENCH.MIB)
            self.assertEqual(result["completion"], "timeout")
            self.assertLess(result["wall_seconds"], 3)
            self.assertNotEqual(result["exit_code"], 0)

    def test_namespace_mismatch_disables_proc_attribution(self):
        with patch.object(BENCH.os, "getpid", return_value=10), patch.object(BENCH, "read", return_value="20 (python) S 1 2"):
            self.assertFalse(BENCH.process_identity()["consistent"])
        with tempfile.TemporaryDirectory() as temporary, patch.object(BENCH, "sample_group", side_effect=AssertionError("must not read unrelated proc PIDs")):
            result = BENCH.measured_process([sys.executable, "-c", "pass"], Path(temporary) / "pid.log", {"cgroups": [], "process_identity": {"consistent": False}}, 2, 128 * BENCH.MIB)
            self.assertEqual(result["completion"], "completed")
            self.assertFalse(result["process_metrics_available"])
            self.assertIsNone(result["sampled_peak"]["rss_bytes"])
            self.assertIn("unavailable", result["memory_limit_enforcement"])


if __name__ == "__main__":
    unittest.main()
