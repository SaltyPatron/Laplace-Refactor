#!/usr/bin/env python3
"""Counterexamples for shared host exclusion and retained admission diagnostics."""
import importlib.util
import hashlib
import json
import os
import signal
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
    def database_fixture(self, root, behavior):
        contract = json.loads((ROOT / 'contracts/postgresql-cluster.json').read_text())
        release = root / 'releases' / ('a' * 64)
        executable = release / 'pgsql-18/bin/psql'
        executable.parent.mkdir(parents=True)
        active = root / 'current'
        active.symlink_to(release, target_is_directory=True)
        contract['package'].update(release_root=str(release.parent), active_link=str(active))
        output = root / 'evidence'
        output.mkdir()
        executable.write_text(f'#!{sys.executable}\n' + textwrap.dedent(behavior))
        executable.chmod(0o750)
        return contract, output

    def observe_database_fixture(self, contract, output):
        runner = D.load_runner()
        with patch.object(D, 'load_runner', return_value=runner), patch.object(runner, 'require_runner'):
            return D.observe_committed_state(contract, output)

    def test_database_snapshot_uses_actual_shared_psql_transport_and_retains_output(self):
        behavior = '''
            import json, os, sys
            sql = sys.stdin.read()
            assert sql.startswith('BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY;')
            assert sql.endswith('ROLLBACK;\\n')
            assert "SET LOCAL statement_timeout = '20s'" in sql
            assert "SET LOCAL lock_timeout = '2s'" in sql
            assert '--no-psqlrc' in sys.argv and 'ON_ERROR_STOP=1' in sys.argv
            assert set(os.environ) <= {'LANG', 'LC_ALL', 'PATH'}
            print(json.dumps({'schema':'laplace.highway-committed-state-observation/v1',
                'transaction_read_only':'on', 'transaction_isolation':'repeatable read',
                'control': {'highway_present': True}}))
            sys.stderr.write('observed diagnostic message\\n')
        '''
        with tempfile.TemporaryDirectory() as temporary:
            contract, output = self.database_fixture(Path(temporary), behavior)
            result = self.observe_database_fixture(contract, output)
            self.assertEqual(result['status'], 'observed', result)
            self.assertEqual(result['process']['exit_code'], 0)
            self.assertEqual(result['sql_sha256'], hashlib.sha256((output / 'committed-state.sql').read_bytes()).hexdigest())
            self.assertEqual((output / 'committed-state.stderr').read_bytes(), b'observed diagnostic message\n')
            self.assertEqual(result['process']['stdout_sha256'], result['outputs']['stdout']['observed_sha256'])
            self.assertEqual(result['transport']['selected_release_directory_name'], 'a' * 64)
            self.assertFalse(result['transport']['loaded_server_package_verified'])
            self.assertEqual(json.loads((output / 'committed-state.json').read_text()), result)

    def test_native_failure_is_retained_before_exception_preview_and_collected_separately(self):
        body = 'LAPLACE_HIGHWAY_REVALIDATION_DIAGNOSTIC {"candidate":"exact"}\n' + 'x' * 5000
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            contract, _ = self.database_fixture(root, f'import sys\nsys.stderr.write({body!r})\nsys.exit(3)\n')
            runner = D.load_runner()
            transport = {'package_root': str(Path(contract['package']['active_link']).resolve()),
                'postgresql_major': 18, 'instance': contract['instance']}
            failures = root / 'receipts/highway/failures'
            with patch.object(runner, 'require_runner'):
                with self.assertRaises(runner.RunnerActivationError) as raised:
                    runner.runner_sql(transport, contract, 'SELECT 1;', 'native-failure',
                        'laplace-runner', 'laplace_admin', 3,
                        completed_observer=lambda process, receipt: runner.retain_failed_sql(process, receipt, directory=failures))
            self.assertNotIn('LAPLACE_HIGHWAY_REVALIDATION_DIAGNOSTIC', str(raised.exception))
            files = list(failures.glob('sql-failure-*.json'))
            self.assertEqual(len(files), 1)
            evidence = json.loads(files[0].read_text())
            self.assertEqual(bytes.fromhex(evidence['outputs']['stderr']['retained_hex']), body.encode())
            self.assertFalse(evidence['outputs']['stderr']['truncated'])
            self.assertEqual(evidence['command']['exit_code'], 3)
            self.assertEqual(evidence['command']['stderr_sha256'], hashlib.sha256(body.encode()).hexdigest())
            report = D.inspect(root / 'receipts', root / 'captured', [])
            self.assertEqual(report['candidate_count'], 0)
            self.assertEqual(len(report['failure_evidence']), 1)
            self.assertEqual((root / 'captured' / report['failure_evidence'][0]['capture']).read_bytes(), files[0].read_bytes())

    def test_failure_retention_bounds_exact_bytes_without_recording_success_as_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runner = D.load_runner()
            success = subprocess.CompletedProcess(['psql'], 0, 'ok', '')
            runner.retain_failed_sql(success, {'exit_code': 0}, directory=root / 'success')
            self.assertFalse((root / 'success').exists())
            content = 'é' * 40
            failed = subprocess.CompletedProcess(['psql'], 1, '', content)
            with patch.object(runner, 'MAX_FAILURE_OUTPUT_BYTES', 7):
                runner.retain_failed_sql(failed, {'exit_code': 1}, directory=root / 'failure')
                runner.retain_failed_sql(failed, {'exit_code': 1}, directory=root / 'failure')
            files = list((root / 'failure').glob('*.json'))
            self.assertEqual(len(files), 1)
            captured = json.loads(files[0].read_text())['outputs']['stderr']
            self.assertTrue(captured['truncated'])
            self.assertEqual(bytes.fromhex(captured['retained_hex']), content.encode()[:7])
            self.assertEqual(captured['observed_bytes'], len(content.encode()))

    def test_database_process_failure_preserves_both_outputs_and_nonzero_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            contract, output = self.database_fixture(Path(temporary), "import sys\nsys.stdout.write('partial row\\n')\nsys.stderr.write('exact failure\\n')\nsys.exit(7)\n")
            result = self.observe_database_fixture(contract, output)
            self.assertEqual(result['status'], 'failed')
            self.assertEqual(result['process']['exit_code'], 7)
            self.assertEqual((output / 'committed-state.stdout').read_bytes(), b'partial row\n')
            self.assertEqual((output / 'committed-state.stderr').read_bytes(), b'exact failure\n')
            self.assertNotIn('observation', result)

    def test_database_timeout_preserves_actual_partial_subprocess_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            contract, output = self.database_fixture(Path(temporary), "import sys,time\nprint('partial timeout',flush=True)\nsys.stderr.write('waiting\\n')\nsys.stderr.flush()\ntime.sleep(2)\n")
            with patch.object(D, 'DATABASE_TIMEOUT_SECONDS', 0.2):
                result = self.observe_database_fixture(contract, output)
            self.assertEqual(result['status'], 'timed-out')
            self.assertTrue(result['partial_output'])
            self.assertEqual((output / 'committed-state.stdout').read_bytes(), b'partial timeout\n')
            self.assertEqual((output / 'committed-state.stderr').read_bytes(), b'waiting\n')
            self.assertIsNone(result['process'])

    def test_database_invalid_json_and_snapshot_boundary_are_retained_failures(self):
        for body in ('not json', '{}', '{"schema":"laplace.highway-committed-state-observation/v1","transaction_read_only":"off","transaction_isolation":"repeatable read"}'):
            with self.subTest(body=body), tempfile.TemporaryDirectory() as temporary:
                contract, output = self.database_fixture(Path(temporary), f'print({body!r})\n')
                result = self.observe_database_fixture(contract, output)
                self.assertEqual(result['status'], 'failed')
                self.assertEqual((output / 'committed-state.stdout').read_text(), body + '\n')
                self.assertNotIn('observation', result)

    def test_database_output_cap_is_explicit_and_never_reports_complete_observation(self):
        body = json.dumps({'schema': 'laplace.highway-committed-state-observation/v1',
            'transaction_read_only': 'on', 'transaction_isolation': 'repeatable read', 'extra': 'x' * 2048})
        with tempfile.TemporaryDirectory() as temporary:
            contract, output = self.database_fixture(Path(temporary), f'print({body!r})\n')
            with patch.object(D, 'MAX_DATABASE_OUTPUT_BYTES', 256):
                result = self.observe_database_fixture(contract, output)
            self.assertEqual(result['status'], 'failed')
            self.assertTrue(result['outputs']['stdout']['truncated'])
            self.assertEqual((output / 'committed-state.stdout').stat().st_size, 256)
            self.assertEqual(result['outputs']['stdout']['observed_sha256'], hashlib.sha256((body + '\n').encode()).hexdigest())
            self.assertNotIn('observation', result)

    def test_database_missing_active_package_is_unavailable_without_process(self):
        with tempfile.TemporaryDirectory() as temporary:
            contract, output = self.database_fixture(Path(temporary), 'raise AssertionError("must not execute")\n')
            Path(contract['package']['active_link']).unlink()
            result = self.observe_database_fixture(contract, output)
            self.assertEqual(result['status'], 'unavailable')
            self.assertIsNone(result['process'])
            self.assertIn('error', result['outputs'])

    def test_database_selection_cannot_escape_the_declared_release_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            contract, output = self.database_fixture(root, 'raise AssertionError("must not execute")\n')
            active = Path(contract['package']['active_link'])
            active.unlink()
            active.symlink_to(root)
            result = self.observe_database_fixture(contract, output)
            self.assertEqual(result['status'], 'unavailable')
            self.assertIsNone(result['process'])

    def test_filesystem_diagnostic_does_not_select_database_execution_by_default(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            contract = root / 'contract.json'
            contract.write_text(json.dumps({'instance': {'receipt_directory': str(root / 'receipts')}}))
            with patch.object(sys, 'argv', ['diagnostic', '--contract', str(contract), '--output', str(root / 'output')]), patch.object(D, 'load_runner', side_effect=AssertionError('database was selected')):
                D.main()
            self.assertFalse((root / 'output/committed-state.json').exists())

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
        self.assertEqual(REQUEST.select(['tools/dependencies/chess_pgn.py'], policy, lambda _: {}, lambda _: {}), ['tools/dependencies/chess_pgn.py'])
        provider = REQUEST.select(['dependencies/artifact-lock.json'], policy,
            lambda _: {'artifacts': {'chess-pgn-validator': {'sha256': 'old'}}},
            lambda _: {'artifacts': {'chess-pgn-validator': {'sha256': 'new'}}})
        self.assertEqual(provider, ['dependencies/artifact-lock.json:chess-pgn-validator'])

    def test_actual_calibration_command_requires_complete_games_and_finite_budgets(self):
        workflow = (ROOT / '.github/workflows/chess-calibration.yml').read_text()
        block = workflow.split('      - name: Measure the actual runner with bounded resource admission\n', 1)[1].split('      - name:', 1)[0]
        command = textwrap.dedent(block.split('        run: |\n', 1)[1])

        def captured(script):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                executable = root / 'python3'
                executable.write_text(f'#!{sys.executable}\nimport json,os,sys\nopen(os.environ["CAPTURE"], "w").write(json.dumps(sys.argv[1:]))\n')
                executable.chmod(0o750)
                receipt = root / 'arguments.json'
                environment = os.environ | {'PATH': str(root) + os.pathsep + os.environ['PATH'],
                    'CAPTURE': str(receipt), 'RUNNER_TEMP': str(root), 'GITHUB_RUN_ID': '19',
                    'GITHUB_RUN_ATTEMPT': '2', 'CPU_BUDGET': '4', 'MEMORY_MIB': '1024'}
                subprocess.run(['bash', '-eu', '-c', script], env=environment, check=True, capture_output=True)
                return json.loads(receipt.read_text())

        def assert_full_profile(arguments):
            self.assertEqual(arguments[:2], ['tools/dependencies/chess_benchmark.py', 'run'])
            self.assertNotIn('--max-moves', arguments)
            self.assertNotIn('--diagnostic', arguments)
            for option, value in (('--game-depth', '8'), ('--game-time-control', '60'), ('--timeout', '600'),
                                  ('--overall-timeout', '1800'), ('--cpu-budget', '4'), ('--memory-mib', '1024')):
                self.assertEqual(arguments[arguments.index(option) + 1], value)

        assert_full_profile(captured(command))
        # Prove this check rejects the former capped workflow, not just new prose.
        with self.assertRaises(AssertionError):
            assert_full_profile(captured(command.replace('--games 16', '--diagnostic --max-moves 12 --games 16')))

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

    def wait_file(self, path, *, timeout=5):
        deadline = time.monotonic() + timeout
        while not path.is_file() and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertTrue(path.is_file(), str(path))

    def lock_available(self, path):
        result = subprocess.run(['flock', '--exclusive', '--nonblock', str(path), 'true'],
                                capture_output=True, timeout=3)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        return result.returncode == 0

    def test_command_owns_exact_lock_and_preserves_normal_exit_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock = root / 'host.lock'
            environment = os.environ | {'LAPLACE_HOST_RESOURCE_LOCK': str(lock)}
            script = ('import os,sys; held=os.fstat(9); expected=os.stat(sys.argv[1]); '
                      'assert (held.st_dev,held.st_ino)==(expected.st_dev,expected.st_ino); '
                      'sys.exit(int(sys.argv[2]))')
            for status in (0, 7, 23):
                with self.subTest(status=status):
                    result = subprocess.run(['bash', str(ROOT / 'tools/host/run-exclusive.sh'),
                        sys.executable, '-c', script, str(lock), str(status)],
                        env=environment, capture_output=True, timeout=5)
                    self.assertEqual(result.returncode, status, result.stderr)
                    self.assertTrue(self.lock_available(lock))

    def test_real_detached_service_subprocess_does_not_retain_the_host_lock(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock = root / 'host.lock'
            environment = os.environ | {'LAPLACE_HOST_RESOURCE_LOCK': str(lock)}
            service = textwrap.dedent("""
                import json,os,sys,time
                from pathlib import Path
                root=Path(sys.argv[1])
                try:
                    os.fstat(9)
                except OSError:
                    inherited=False
                else:
                    inherited=True
                (root/'service.json').write_text(json.dumps({'pid':os.getpid(),'lock_inherited':inherited}))
                while not (root/'release-service').exists(): time.sleep(.01)
                (root/'service-finished').touch()
            """)
            launcher = textwrap.dedent("""
                import os,subprocess,sys,time
                from pathlib import Path
                os.fstat(9)
                root=Path(sys.argv[1])
                # Actual Python service boundary: default close_fds=True.
                subprocess.Popen([sys.executable,'-c',sys.argv[2],str(root)],
                    start_new_session=True,stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                deadline=time.monotonic()+3
                while not (root/'service.json').is_file() and time.monotonic()<deadline: time.sleep(.01)
                assert (root/'service.json').is_file()
            """)
            try:
                result = subprocess.run(['bash', str(ROOT / 'tools/host/run-exclusive.sh'),
                    sys.executable, '-c', launcher, str(root), service],
                    env=environment, capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                observation = json.loads((root/'service.json').read_text())
                self.assertFalse(observation['lock_inherited'])
                os.kill(observation['pid'], 0)
                self.assertFalse((root/'service-finished').exists())
                self.assertTrue(self.lock_available(lock))
            finally:
                (root/'release-service').touch()
                if (root/'service.json').exists():
                    self.wait_file(root/'service-finished')

    def cancellation_fixture(self):
        # Real wrapper/timeout/Python/signal/FD behavior; the PostgreSQL and
        # systemd calls below use the existing explicit lifecycle protocol fixture.
        return textwrap.dedent("""
            import importlib.util,json,os,signal,sys,time
            from pathlib import Path
            from unittest import mock
            spec=importlib.util.spec_from_file_location('lock_service_fixture',sys.argv[1])
            tests=importlib.util.module_from_spec(spec)
            spec.loader.exec_module(tests)
            case=tests.PersistentPostgreSQLServiceOwnerTests()
            case.setUp()
            try:
                service=case.service
                acceptance,owner,model,target,historical=case.convergence_fixture()
                root=Path(sys.argv[2])
                held=os.fstat(9)
                expected=(root/'host.lock').stat()
                assert (held.st_dev,held.st_ino)==(expected.st_dev,expected.st_ino)
                prior_term=signal.getsignal(signal.SIGTERM)
                prior_int=signal.getsignal(signal.SIGINT)
                original_converge=service.converge
                original_action=owner.action.side_effect
                original_execute=acceptance.runner.clusterctl.execute_activation_command.side_effect
                def pause(plan,label,kind,timeout):
                    if label=='start-system-postmaster':
                        assert model['pid']==0 and not model['managed']
                        (root/'ready.json').write_text(json.dumps({
                            'helper':os.getpid(),'timeout':os.getppid(),
                            'timeout_group':os.getpgid(os.getppid())}))
                        signal.pause()
                        raise AssertionError('signal did not enter actual CLI rollback')
                    return original_action(plan,label,kind,timeout)
                def rollback(label,command,timeout):
                    if label=='stop-failed-system-handoff':
                        (root/'rollback-started').touch()
                        while not (root/'release-rollback').exists(): time.sleep(.01)
                    return original_execute(label,command,timeout)
                owner.action.side_effect=pause
                acceptance.runner.clusterctl.execute_activation_command.side_effect=rollback
                with mock.patch.object(service,'Owner',return_value=owner), \\
                     mock.patch.object(service,'selection_path',return_value=target), \\
                     mock.patch.object(service,'converge',side_effect=lambda sha,path:
                                       original_converge(sha,path,acceptance=acceptance)), \\
                     mock.patch.object(sys,'argv',['service_lifecycle.py','converge',
                         '--expected-sha','4'*40,'--output-directory',str(root/'convergence')]):
                    try: service.main()
                    except (InterruptedError,KeyboardInterrupt): pass
                    else: raise AssertionError('cancelled operation reported success')
                report=json.loads((root/'convergence/result.json').read_text())
                assert report['status']=='failed' and report['previous_owner_restored'] is True
                assert report['successful_selection_published'] is False and not target.exists()
                assert signal.getsignal(signal.SIGTERM)==prior_term
                assert signal.getsignal(signal.SIGINT)==prior_int
                (root/'finished.json').write_text(json.dumps({
                    'restored':True,'handler_restored':True,'historical':historical['lifecycle_provider']}))
            finally:
                case.doCleanups()
        """)

    def run_lock_cancellation(self, *, nested, signum):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock = root/'host.lock'
            environment = os.environ | {'LAPLACE_HOST_RESOURCE_LOCK':str(lock)}
            command = ['timeout','--signal=TERM','--kill-after=10s','20s',
                sys.executable,'-c',self.cancellation_fixture(),
                str(ROOT/'tests/postgresql_cluster_tests.py'),str(root)]
            if nested:
                # Keep a real intermediate Bash; do not optimize it into timeout.
                command = ['bash','-c','"$@"; status=$?; exit "$status"','fixture',*command]
            process = subprocess.Popen(['bash',str(ROOT/'tools/host/run-exclusive.sh'),*command],
                env=environment,start_new_session=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            identities = None
            try:
                self.wait_file(root/'ready.json')
                identities = json.loads((root/'ready.json').read_text())
                self.assertFalse(self.lock_available(lock))
                # Signal timeout once; it forwards to the real Python CLI.
                os.kill(identities['timeout'],signum)
                self.wait_file(root/'rollback-started')
                if nested:
                    process.terminate()
                    self.assertEqual(process.wait(timeout=3), -signal.SIGTERM)
                # Actual runner cancellation follows INT with TERM; its process
                # tree pass may also repeat an initial TERM during rollback.
                os.kill(identities['helper'],signal.SIGTERM)
                time.sleep(.05)
                self.assertFalse(self.lock_available(lock))
                self.assertFalse((root/'finished.json').exists())
                (root/'release-rollback').touch()
                self.wait_file(root/'finished.json')
                if not nested:
                    self.assertNotEqual(process.wait(timeout=3),0)
                deadline = time.monotonic()+3
                while not self.lock_available(lock) and time.monotonic()<deadline: time.sleep(.01)
                self.assertTrue(self.lock_available(lock))
                completion=json.loads((root/'finished.json').read_text())
                self.assertTrue(completion['restored'])
                self.assertTrue(completion['handler_restored'])
                self.assertEqual(completion['historical'],'pg_ctl')
            finally:
                (root/'release-rollback').touch()
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=3)
                if identities is not None and not (root/'finished.json').exists():
                    try: os.killpg(identities['timeout_group'],signal.SIGKILL)
                    except ProcessLookupError: pass

    def test_nested_timeout_rollback_retains_lock_after_bash_exit_and_repeated_term(self):
        self.run_lock_cancellation(nested=True,signum=signal.SIGTERM)

    def test_direct_timeout_int_then_term_retains_lock_through_actual_cli_rollback(self):
        self.run_lock_cancellation(nested=False,signum=signal.SIGINT)


if __name__ == '__main__':
    unittest.main()
