#!/usr/bin/env python3
"""Counterexamples for shared host exclusion and retained admission diagnostics."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
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
        self.assertEqual(REQUEST.select(['tools/dependencies/git_checkout.py'], policy, lambda _: {}, lambda _: {}), ['tools/dependencies/git_checkout.py'])

    def test_deployment_calibration_is_gated_and_core_benchmark_policy_is_preserved(self):
        workflow = (ROOT / '.github/workflows/product-path.yml').read_text()
        block = workflow.split('  deployed-chess-calibration:\n', 1)[1].split('\n  dev-bat-live-substrate:', 1)[0]
        for gate in ("github.event_name == 'push'", "github.ref == 'refs/heads/main'", "needs.dev-bat-deployment.result == 'success'", "needs.classify.outputs.requires_chess_calibration == 'true'"):
            self.assertIn(gate, block)
        self.assertIn('uses: ./.github/workflows/chess-calibration.yml', block)
        core = json.loads((ROOT / 'contracts/benchmark-suite.json').read_text())
        self.assertEqual(core['dispatch']['source_triggers_forbidden'], ['push', 'pull_request', 'merge'])
        self.assertEqual(core['dispatch']['long_running_default'], 'explicit-only')

    def test_candidate_calibration_requires_same_repository_chess_and_hosted_proof(self):
        workflow = (ROOT / '.github/workflows/product-path.yml').read_text()
        block = workflow.split('  candidate-chess-calibration:\n', 1)[1].split('\n  postgresql-product-proof:', 1)[0]
        for gate in ("github.event_name == 'pull_request'", "github.event.pull_request.head.repo.full_name == github.repository", "needs.classify.outputs.requires_chess_calibration == 'true'", "needs.hosted-proof.result == 'success'"):
            self.assertIn(gate, block)
        self.assertIn('needs: [classify, hosted-proof]', block)
        self.assertNotIn('custom-stack-proof', block)
        self.assertIn('uses: ./.github/workflows/chess-calibration.yml', block)
        self.assertNotIn('product_activation', block)
        calibration = (ROOT / '.github/workflows/chess-calibration.yml').read_text()
        self.assertIn('candidate-chess-dependencies', calibration)
        self.assertIn('execution-context.json', calibration)
        self.assertIn('checked_out_sha', calibration)
        self.assertIn('tools/host/run-exclusive.sh', calibration)

    def test_actual_candidate_condition_admits_measurement_despite_custom_proof_failure(self):
        workflow = (ROOT / '.github/workflows/product-path.yml').read_text()
        block = workflow.split('  candidate-chess-calibration:\n', 1)[1].split('\n  postgresql-product-proof:', 1)[0]
        expression = ' '.join(block.split('    if: >-\n', 1)[1].split('    uses:', 1)[0].split())
        baseline = {
            'github.event_name': 'pull_request',
            'github.event.pull_request.head.repo.full_name': 'owner/repo',
            'github.repository': 'owner/repo',
            'needs.classify.outputs.requires_chess_calibration': 'true',
            'needs.hosted-proof.result': 'success',
            'needs.custom-stack-proof.result': 'failure',
        }

        def admitted(overrides):
            actual = expression.replace('!cancelled()', 'True').replace('always()', 'True').replace('&&', 'and')
            for key, value in (baseline | overrides).items():
                actual = actual.replace(key, repr(value))
            # Execute the actual checked-in condition with no available builtins.
            return eval(actual, {'__builtins__': {}}, {})

        for result in ('success', 'failure', 'cancelled', 'skipped', ''):
            with self.subTest(custom=result):
                self.assertTrue(admitted({'needs.custom-stack-proof.result': result}))
        for overrides in (
            {'github.event_name': 'push'},
            {'github.event.pull_request.head.repo.full_name': 'fork/repo'},
            {'needs.classify.outputs.requires_chess_calibration': 'false'},
            *({'needs.hosted-proof.result': result} for result in ('failure', 'cancelled', 'skipped', '')),
        ):
            with self.subTest(overrides=overrides):
                self.assertFalse(admitted(overrides))

    def candidate_gate(self):
        workflow = (ROOT / '.github/workflows/product-path.yml').read_text()
        block = workflow.split('  product-path:\n', 1)[1].split('\n  dev-bat-deployment:', 1)[0]
        self.assertIn('      - candidate-chess-calibration\n', block)
        self.assertIn('CHESS_RESULT: ${{ needs.candidate-chess-calibration.result }}', block)
        self.assertIn("REQUIRES_CANDIDATE_CHESS: ${{ github.event_name == 'pull_request' && github.event.pull_request.head.repo.full_name == github.repository && needs.classify.outputs.requires_chess_calibration == 'true' }}", block)
        return textwrap.dedent(block.split('        run: |\n', 1)[1])

    def run_candidate_gate(self, script, required, result, custom_result='success'):
        with tempfile.TemporaryDirectory() as temporary:
            environment = os.environ | {
                'CLASSIFY_RESULT': 'success', 'HOSTED_RESULT': 'success',
                'PUBLICATION_RECOVERY_RESULT': 'success', 'CUSTOM_RESULT': custom_result,
                'POSTGRESQL_RESULT': 'success', 'PACKAGE_RESULT': 'success',
                'REQUIRES_CUSTOM': 'true', 'REQUIRES_POSTGRESQL': 'true',
                'REQUIRES_PACKAGE': 'true', 'REQUIRES_CANDIDATE_CHESS': required,
                'CHESS_RESULT': result, 'BLOCKED': 'false',
                'REQUIRED_EVIDENCE': '[]', 'UNIMPLEMENTED_EVIDENCE': '[]',
                'GITHUB_STEP_SUMMARY': str(Path(temporary) / 'summary.md'),
            }
            return subprocess.run(['bash', '-c', script], env=environment, capture_output=True).returncode

    def test_selected_candidate_failure_or_missing_proof_blocks_actual_aggregate_shell(self):
        script = self.candidate_gate()
        for result in ('success', 'failure', 'cancelled', 'skipped', ''):
            with self.subTest(result=result):
                self.assertEqual(self.run_candidate_gate(script, 'true', result) == 0, result == 'success')
        for result in ('success', 'failure', 'skipped'):
            with self.subTest(unselected=result):
                self.assertEqual(self.run_candidate_gate(script, 'false', result) == 0, result == 'skipped')

    def test_removed_candidate_check_would_admit_failed_measurement(self):
        script = self.candidate_gate()
        conditional = 'if [[ "$REQUIRES_CANDIDATE_CHESS" == true ]]; then\n  test "$CHESS_RESULT" = success\nelse\n  test "$CHESS_RESULT" = skipped\nfi\n'
        self.assertIn(conditional, script)
        self.assertNotEqual(self.run_candidate_gate(script, 'true', 'failure'), 0)
        self.assertEqual(self.run_candidate_gate(script.replace(conditional, ''), 'true', 'failure'), 0)

    def test_passing_calibration_cannot_mask_failed_or_missing_custom_proof(self):
        script = self.candidate_gate()
        for result in ('success', 'failure', 'cancelled', 'skipped', ''):
            with self.subTest(custom=result):
                self.assertEqual(self.run_candidate_gate(script, 'true', 'success', result) == 0, result == 'success')

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

    def test_diagnostic_directory_index_is_bounded_and_receipts_are_prioritized(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / 'archive'
            archive.mkdir()
            noise = archive / 'build'
            noise.mkdir()
            for number in range(20):
                (noise / f'output-{number}.json').write_text('{}')
            highway = archive / 'highway-receipts'
            highway.mkdir()
            raw = b'{"schema":"laplace.highway-product-activation-request/v1"}\n'
            (highway / 'request.json').write_bytes(raw)
            with patch.object(D, 'MAX_ENTRIES', 5), patch.object(D, 'MAX_DIRECTORY_INDEX', 1):
                result = D.inspect(archive, root / 'diagnostic', [])
            self.assertEqual(result['candidate_count'], 1)
            self.assertEqual(len(result['directory_index']), 1)
            self.assertTrue(result['directory_index_truncated'])
            self.assertTrue(result['truncated'])

    def test_related_receipts_require_exact_selected_package_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / 'receipts'
            archive.mkdir()
            wanted = 'a' * 64
            for index, schema in enumerate(D.RELATED_SCHEMAS):
                (archive / f'{index}.json').write_text(json.dumps({'schema': schema, 'package_id': wanted, 'request_fingerprint': 'original-context'}))
            (archive / 'unrelated.json').write_text(json.dumps({'schema': 'laplace.unicode-product-activation-receipt/v1', 'package_id': 'b' * 64}))
            (archive / 'malformed-list-schema.json').write_text(json.dumps({'schema': ['not-a-schema'], 'package_id': wanted}))
            (archive / 'malformed-object-schema.json').write_text(json.dumps({'schema': {'not': 'a-schema'}, 'package_id': wanted}))
            report = D.inspect(archive, root / 'diagnostic', [], package_id=wanted)
            self.assertEqual(report['candidate_count'], 3)
            self.assertTrue(all(item['package_id'] == wanted for item in report['candidates']))
            self.assertTrue(all(item['capture_reason'] == 'exact related package identity' for item in report['candidates']))
            unselected = D.inspect(archive, root / 'unselected', [])
            self.assertEqual(unselected['candidate_count'], 0)

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
