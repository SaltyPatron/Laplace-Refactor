#!/usr/bin/env python3
"""Failure controls for resource admission and measured chess receipts."""
import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import sys
import tarfile
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
    @classmethod
    def setUpClass(cls):
        cache = Path(os.environ.get("LAPLACE_CHESS_VALIDATION_CACHE", "/build/laplace/work/chess-pgn-provider-test-cache"))
        cls.provider = BENCH.chess_pgn.load_provider(cache)
        # Real checkmate corpus from the same verified upstream archive. Only
        # player labels are changed to match the benchmark's two engine slots.
        with tarfile.open(cls.provider[2]["archive"]["path"], "r:gz") as archive:
            fixture = archive.extractfile(f'chess-{cls.provider[2]["version"]}/data/pgn/molinari-bordais-1979.pgn')
            assert fixture is not None
            cls.upstream_pgn = fixture.read().decode("utf-8")

    def validate(self, text, games=2, **kwargs):
        return BENCH.validate_pgn(text, games, provider=self.provider, **kwargs)

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
        return '\n'.join(re.sub(r'^\[Black "[^"\n]+"\]$', f'[Black "{black}"]',
                                re.sub(r'^\[White "[^"\n]+"\]$', f'[White "{white}"]', self.upstream_pgn, flags=re.M), flags=re.M)
                         for white, black in [("Source-A", "Source-B"), ("Source-B", "Source-A")])

    def capped_pgn(self):
        return '\n'.join(f'[Event "Diagnostic"]\n[White "{white}"]\n[Black "{black}"]\n[Result "1/2-1/2"]\n'
                         '[Termination "adjudication"]\n[PlyCount "4"]\n\n'
                         '1. e4 e5 2. Nf3 Nc6 {Draw by adjudication: maximal game length} 1/2-1/2\n'
                         for white, black in [("Source-A", "Source-B"), ("Source-B", "Source-A")])

    def test_pgn_counts_plies_and_rejects_unbalanced_colors(self):
        result = self.validate(self.pgn())
        self.assertEqual(result["plies"], 20)
        self.assertEqual(result["normal_completed_games"], 2)
        self.assertEqual(result["capped_diagnostic_games"], 0)
        self.assertTrue(result["legal_moves_validated"])
        self.assertEqual({record["board_outcome"] for record in result["records"]}, {"CHECKMATE"})
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "pairing"):
            self.validate(self.pgn().replace('[White "Source-B"]', '[White "Source-A"]'))

    def test_crashed_and_unfinished_games_cannot_pass(self):
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "unfinished"):
            self.validate(self.pgn().replace('[Result "0-1"]', '[Result "*"]'))
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "failure"):
            self.validate(self.pgn() + "{Engine disconnected}")

    def test_declared_result_without_legal_terminal_position_is_refused(self):
        unfinished = self.capped_pgn().replace('[Termination "adjudication"]\n', '').replace('{Draw by adjudication: maximal game length}', '')
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "terminal outcome"):
            self.validate(unfinished)
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "terminal outcome"):
            self.validate(self.pgn().replace('0-1', '1-0'))

    def test_legal_draw_claim_is_validated_from_complete_history(self):
        text = '\n'.join(f'[Event "Repetition"]\n[White "{white}"]\n[Black "{black}"]\n[Result "1/2-1/2"]\n\n'
                         '1. Nf3 Nf6 2. Ng1 Ng8 3. Nf3 Nf6 4. Ng1 Ng8 1/2-1/2\n'
                         for white, black in [("Source-A", "Source-B"), ("Source-B", "Source-A")])
        result = self.validate(text)
        self.assertEqual(result["normal_completed_games"], 2)
        self.assertEqual({record["board_outcome"] for record in result["records"]}, {"THREEFOLD_REPETITION"})
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "terminal outcome"):
            self.validate(text.replace('3. Nf3 Nf6 4. Ng1 Ng8 ', ''))

    def test_illegal_null_and_unaccounted_moves_cannot_pass(self):
        for old, new in [('1. e4', '1. e5'), ('1. e4', '1. --'), ('1. e4', '1. e4garbage'), ('1. e4', '1. e4 garbage')]:
            with self.subTest(new=new), self.assertRaises(BENCH.tools.ChessToolError):
                self.validate(self.pgn().replace(old, new))
        with self.assertRaises(BENCH.tools.ChessToolError):
            self.validate(self.pgn().replace('Nd3# 0-1', 'Nd3# 6. Kg2 0-1'))

    def test_result_marker_and_exact_ply_count_are_bound(self):
        for changed in [self.pgn().replace('Nd3# 0-1', 'Nd3# 1-0'), self.pgn().replace('Nd3# 0-1', 'Nd3#'),
                        self.pgn().replace('[PlyCount "10"]', '[PlyCount "9"]'),
                        self.pgn().replace('[Result "0-1"]', '[Result "0-1"]\n[Result "0-1"]')]:
            with self.subTest(changed=changed[:100]), self.assertRaises(BENCH.tools.ChessToolError):
                self.validate(changed)

    def test_terminal_fen_does_not_impersonate_standard_start_game(self):
        text = '[Event "Truncated"]\n[White "Source-A"]\n[Black "Source-B"]\n[Result "0-1"]\n[SetUp "1"]\n[FEN "8/8/8/8/8/5k2/6q1/7K w - - 0 1"]\n\n0-1\n'
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "standard initial position"):
            self.validate(text)

    def test_cap_is_accepted_only_as_explicit_exact_diagnostic(self):
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "non-normal"):
            self.validate(self.capped_pgn())
        result = self.validate(self.capped_pgn(), diagnostic=True, max_moves=2)
        self.assertEqual((result["normal_completed_games"], result["capped_diagnostic_games"], result["plies"]), (0, 2, 8))
        for changed, cap in [(self.capped_pgn(), 3), (self.capped_pgn().replace('maximal game length', 'evaluation'), 2),
                             (self.capped_pgn().replace('1. e4', '1. e5'), 2)]:
            with self.subTest(cap=cap), self.assertRaises(BENCH.tools.ChessToolError):
                self.validate(changed, diagnostic=True, max_moves=cap)

    def test_normal_command_is_uncapped_and_retains_search_and_time_controls(self):
        arguments = BENCH.argument_parser().parse_args(['run', '--output', '/unused'])
        configuration = {"threads": 1, "hash_mib": 16, "concurrency": 1}
        command = BENCH.cutechess_command(arguments, configuration, '/cutechess', '/stockfish', Path('/game.pgn'))
        self.assertIsNone(arguments.max_moves)
        self.assertNotIn('-maxmoves', command)
        self.assertNotIn('-resign', command)
        self.assertNotIn('-draw', command)
        self.assertIn(f'depth={arguments.game_depth}', command)
        self.assertIn('tc=60', command)
        arguments.max_moves = 2
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, '--diagnostic'):
            BENCH.cutechess_command(arguments, configuration, '/cutechess', '/stockfish', Path('/game.pgn'))
        arguments.diagnostic = True
        command = BENCH.cutechess_command(arguments, configuration, '/cutechess', '/stockfish', Path('/game.pgn'))
        self.assertEqual(command[command.index('-maxmoves') + 1], '2')

    def test_diagnostic_rates_never_supply_full_game_recommendation(self):
        arguments = BENCH.argument_parser().parse_args(['run', '--output', '/unused', '--diagnostic', '--max-moves', '2'])
        validation = self.validate(self.capped_pgn(), diagnostic=True, max_moves=2)
        rates = BENCH.game_rates(validation, 2, True)
        self.assertEqual(rates, {"diagnostic_episodes_per_second": 1, "plies_per_second": 4})
        with self.assertRaisesRegex(BENCH.tools.ChessToolError, "full-game throughput"):
            BENCH.game_rates(validation, 2, False)
        recommendation = BENCH.recommendations([{"wall_seconds_median": 1}], [{"median_diagnostic_episodes_per_second": 99}], BENCH.game_workload(arguments))
        self.assertIsNone(recommendation['aggregate_game_throughput'])
        self.assertFalse(recommendation['database_recording_capacity_measured'])
        self.assertIsNone(recommendation['game_workload']['database_recording']['recorded_games'])
        full = BENCH.game_rates(self.validate(self.pgn()), 2, False)
        self.assertEqual(full, {"normal_completed_games_per_second": 1, "plies_per_second": 10})

    def test_overall_budget_limits_each_case_and_refuses_another_after_expiry(self):
        with patch.object(BENCH.time, 'monotonic', return_value=90):
            self.assertEqual(BENCH.remaining_timeout(100, 600), 10)
            self.assertEqual(BENCH.remaining_timeout(100, 5), 5)
        with patch.object(BENCH.time, 'monotonic', return_value=100), self.assertRaisesRegex(BENCH.tools.ChessToolError, 'overall benchmark timeout'):
            BENCH.remaining_timeout(100, 600)

    def test_actual_timed_out_child_retains_failed_case_metrics_and_no_recommendation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'src').mkdir()
            (root / 'src/benchmark.cpp').write_text('failure-control fixture\n')
            installation = {'tools': {'stockfish': {'executable': sys.executable, 'source': str(root)},
                                      'cutechess': {'executable': sys.executable}}}
            output = root / 'result'
            argv = ['chess_benchmark.py', 'run', '--output', str(output), '--samples', '2', '--warmups', '0',
                    '--games', '2', '--threads', '1', '--hash-mib', '16', '--concurrency', '1',
                    '--cpu-budget', '1', '--memory-mib', '1024', '--timeout', '0.15', '--overall-timeout', '5']
            actual_measurement = BENCH.measured_process
            def timed_out_child(command, transcript, host, timeout, memory, *args, **kwargs):
                return actual_measurement([sys.executable, '-c', 'import time; time.sleep(10)'],
                                          transcript, host, timeout, memory)
            with contextlib.ExitStack() as stack:
                stack.enter_context(patch.object(sys, 'argv', argv))
                stack.enter_context(patch.object(BENCH, 'host_observation', return_value=self.host()))
                stack.enter_context(patch.object(BENCH.tools, 'verify_installation', return_value=installation))
                stack.enter_context(patch.object(BENCH.tools, 'execute', return_value='option name Threads type spin default 1 min 1 max 8\noption name Hash type spin default 16 min 1 max 1024\nuciok\nreadyok\n'))
                stack.enter_context(patch.object(BENCH.chess_pgn, 'load_provider', return_value=self.provider))
                stack.enter_context(patch.object(BENCH, 'measured_process', side_effect=timed_out_child))
                stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
                stack.enter_context(contextlib.redirect_stderr(io.StringIO()))
                self.assertEqual(BENCH.main(), 1)
            receipt = json.loads((output / 'receipt.json').read_text())
            self.assertIsNone(receipt['recommendations'])
            self.assertEqual(receipt['failed_case']['profile'], 'stockfish')
            self.assertEqual(receipt['failed_case']['measurement']['completion'], 'timeout')
            self.assertGreater(receipt['failed_case']['measurement']['wall_seconds'], 0)
            self.assertNotEqual(receipt['failed_case']['measurement']['exit_code'], 0)
            self.assertTrue(Path(receipt['failed_case']['measurement']['transcript']).is_file())
            self.assertEqual(receipt['stockfish_samples'], [])

    def test_warmups_do_not_affect_summary(self):
        sample = {"configuration": {"threads": 1}, "sampled_peak": {"rss_bytes": 100}, "user_cpu_seconds": 1, "system_cpu_seconds": 0, "wall_seconds": 1,
                  "games": 2, "normal_completed_games": 2, "capped_diagnostic_games": 0, "plies": 20}
        values = [{**sample, "warmup": True, "rate": 1000000}, {**sample, "warmup": False, "rate": 2}, {**sample, "warmup": False, "rate": 4}]
        result = BENCH.aggregates(values, "median_rate", "rate")[0]
        self.assertEqual((result["samples"], result["median_rate"]), (2, 3))
        self.assertEqual((result["total_games"], result["total_normal_completed_games"], result["total_capped_diagnostic_games"], result["total_plies"], result["total_wall_seconds"]), (4, 4, 0, 40, 2))

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
