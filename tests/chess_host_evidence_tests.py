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


class HostEvidence(unittest.TestCase):
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
