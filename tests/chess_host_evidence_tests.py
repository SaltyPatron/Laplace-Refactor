#!/usr/bin/env python3
"""Counterexamples for shared host exclusion and retained admission diagnostics."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('highway_diagnostic', ROOT / 'tools/delivery/highway_receipt_diagnostic.py')
D = importlib.util.module_from_spec(spec)
spec.loader.exec_module(D)
request_spec = importlib.util.spec_from_file_location('chess_request', ROOT / 'tools/dependencies/chess_calibration_request.py')
REQUEST = importlib.util.module_from_spec(request_spec)
request_spec.loader.exec_module(REQUEST)


class HostEvidence(unittest.TestCase):
    def policy(self):
        return json.loads((ROOT / 'contracts/chess-benchmark.json').read_text())['deployment_calibration']

    def test_unrelated_changes_do_not_request_chess_calibration(self):
        before = {'dependencies': {'stockfish': {'version': '19'}, 'eigen': {'version': 'old'}}}
        after = {'dependencies': {'stockfish': {'version': '19'}, 'eigen': {'version': 'new'}}}
        self.assertEqual(REQUEST.select(['dependencies/lock.json', 'docs/example.md'], self.policy(), lambda _: before, lambda _: after), [])

    def test_chess_source_network_or_tool_change_requests_calibration(self):
        policy = self.policy()
        source = REQUEST.select(['dependencies/lock.json'], policy, lambda _: {'dependencies': {'stockfish': {'version': '18'}}}, lambda _: {'dependencies': {'stockfish': {'version': '19'}}})
        self.assertEqual(source, ['dependencies/lock.json:stockfish'])
        network = REQUEST.select(['dependencies/artifact-lock.json'], policy, lambda _: {'artifacts': {'stockfish-nnue': {'sha': 'old'}}}, lambda _: {'artifacts': {'stockfish-nnue': {'sha': 'new'}}})
        self.assertEqual(network, ['dependencies/artifact-lock.json:stockfish-nnue'])
        self.assertEqual(REQUEST.select(['tools/dependencies/chess_tools.py'], policy, lambda _: {}, lambda _: {}), ['tools/dependencies/chess_tools.py'])

    def test_deployment_calibration_is_gated_and_core_benchmark_policy_is_preserved(self):
        workflow = (ROOT / '.github/workflows/product-path.yml').read_text()
        block = workflow.split('  deployed-chess-calibration:\n', 1)[1].split('\n  dev-bat-live-substrate:', 1)[0]
        for gate in ("github.event_name == 'push'", "github.ref == 'refs/heads/main'", "needs.dev-bat-deployment.result == 'success'", "needs.classify.outputs.requires_chess_calibration == 'true'"):
            self.assertIn(gate, block)
        self.assertIn('uses: ./.github/workflows/chess-calibration.yml', block)
        core = json.loads((ROOT / 'contracts/benchmark-suite.json').read_text())
        self.assertEqual(core['dispatch']['source_triggers_forbidden'], ['push', 'pull_request', 'merge'])
        self.assertEqual(core['dispatch']['long_running_default'], 'explicit-only')

    def test_missing_admission_is_reported_without_creating_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = D.inspect(root / 'receipts', root / 'diagnostic', [])
            self.assertEqual(report['retained_admission_directory']['kind'], 'absent')
            self.assertFalse((root / 'receipts').exists())
            self.assertEqual(report['candidate_count'], 0)

    def test_canonical_request_bytes_are_retained_without_reconstruction(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            admission = root / 'receipts/highway' / ('a' * 64)
            admission.mkdir(parents=True)
            raw = b'{"schema":"laplace.highway-product-activation-request/v1","system_identifier":"witness"}\n'
            (admission / 'request.json').write_bytes(raw)
            (admission / 'other.json').write_text('{"not":"a receipt"}')
            (admission / 'receipt.json').write_text('{"schema":"laplace.highway-unrelated/v1","private":"must not export"}')
            result = D.inspect(root / 'receipts', root / 'diagnostic', [])
            self.assertEqual(result['candidate_count'], 1)
            self.assertEqual((root / 'diagnostic' / result['candidates'][0]['capture']).read_bytes(), raw)
            self.assertEqual((admission / 'request.json').read_bytes(), raw)
            self.assertIn('requires-native-identity-validation', result['recovery_status'])

    def test_symlink_admission_cannot_export_target_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'receipts').mkdir()
            (root / 'elsewhere').mkdir()
            (root / 'elsewhere/request.json').write_text('{"schema":"laplace.highway-secret/v1"}')
            (root / 'receipts/highway').symlink_to(root / 'elsewhere', target_is_directory=True)
            report = D.inspect(root / 'receipts', root / 'diagnostic', [])
            self.assertEqual(report['retained_admission_directory']['kind'], 'symlink')
            self.assertEqual(report['candidate_count'], 0)
            self.assertEqual(len(report['symlinks_not_followed']), 1)

    def test_evidence_scan_respects_entry_budget(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'receipts').mkdir()
            for number in range(4):
                (root / f'receipts/{number}.json').write_text('{}')
            with patch.object(D, 'MAX_ENTRIES', 2):
                result = D.inspect(root / 'receipts', root / 'diagnostic', [])
            self.assertEqual(result['examined_entries'], 2)
            self.assertTrue(result['truncated'])

    def test_large_archive_cannot_starve_later_explicit_recovery_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            busy = root / 'busy'
            recovery = root / 'recovery'
            busy.mkdir()
            recovery.mkdir()
            for number in range(4):
                (busy / f'{number}.json').write_text('{}')
            raw = b'{"schema":"laplace.highway-product-activation-request/v1"}\n'
            (recovery / 'historical-admission.json').write_bytes(raw)
            with patch.object(D, 'MAX_ENTRIES', 2):
                result = D.inspect(busy, root / 'diagnostic', [recovery])
            self.assertTrue(result['truncated'])
            self.assertEqual(result['examined_entries'], 3)
            self.assertEqual(result['candidate_count'], 1)
            self.assertEqual((root / 'diagnostic' / result['candidates'][0]['capture']).read_bytes(), raw)

    def test_shared_host_lock_serializes_distinct_callers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            environment = {**os.environ, 'LAPLACE_HOST_RESOURCE_LOCK': str(root / 'host.lock')}
            script = 'import pathlib,sys,time; p=pathlib.Path(sys.argv[1]);p.write_text("started");\nwhile not pathlib.Path(sys.argv[2]).exists(): time.sleep(.01)'
            first = subprocess.Popen(['bash', str(ROOT / 'tools/host/run-exclusive.sh'), sys.executable, '-c', script, str(root / 'first'), str(root / 'release')], env=environment)
            second = None
            try:
                deadline = time.monotonic() + 3
                while not (root / 'first').exists() and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertTrue((root / 'first').exists())
                second = subprocess.Popen(['bash', str(ROOT / 'tools/host/run-exclusive.sh'), sys.executable, '-c', 'import pathlib,sys;pathlib.Path(sys.argv[1]).write_text("second")', str(root / 'second')], env=environment)
                time.sleep(.05)
                self.assertFalse((root / 'second').exists())
                (root / 'release').touch()
                self.assertEqual(first.wait(timeout=3), 0)
                self.assertEqual(second.wait(timeout=3), 0)
                self.assertTrue((root / 'second').exists())
            finally:
                (root / 'release').touch()
                first.wait(timeout=3)
                if second is not None:
                    second.wait(timeout=3)

    def test_deployed_command_does_not_inherit_host_lock_descriptor(self):
        with tempfile.TemporaryDirectory() as temporary:
            environment = {**os.environ, 'LAPLACE_HOST_RESOURCE_LOCK': str(Path(temporary) / 'host.lock')}
            result = subprocess.run(['bash', str(ROOT / 'tools/host/run-exclusive.sh'), 'bash', '-c', 'if : >&9 2>/dev/null; then exit 1; fi'], env=environment, capture_output=True)
            self.assertEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
