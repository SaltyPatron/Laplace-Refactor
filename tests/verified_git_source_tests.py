#!/usr/bin/env python3
"""Exercise source provenance against real temporary Git objects and file changes."""
import hashlib
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/sources'))
import verified_git as V
import qualify_grammar as Q
import admit_source as A


class VerifiedGitSourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.checkout = self.root / 'checkout'
        self.checkout.mkdir()
        self.upstream = 'https://github.com/example/source'
        self.command('init', '-q')
        self.command('config', 'user.email', 'fixture@example.invalid')
        self.command('config', 'user.name', 'Source test fixture')
        self.command('remote', 'add', 'origin', self.upstream + '.git')
        (self.checkout / 'src').mkdir()
        (self.checkout / 'src/main.cpp').write_bytes(b'#include "main.h"\nint main() { return fixture; }\n')
        (self.checkout / 'src/main.h').write_bytes(b'constexpr int fixture = 0;\n')
        (self.checkout / 'README.md').write_bytes('Exact UTF-8 source observation: \u03bb\n'.encode())
        self.commit()

    def command(self, *arguments):
        return subprocess.run(['git', '-C', str(self.checkout), *arguments],
                              capture_output=True, check=True).stdout.decode().strip()

    def test_local_import_requires_exact_locked_archive_and_preserves_origin(self):
        revision = self.command('rev-parse', 'HEAD')
        archive = hashlib.sha256(V.git(self.checkout, 'archive', '--format=tar', revision)).hexdigest()
        self.command('remote', 'set-url', 'origin', str(self.root / 'verified-import'))
        with self.assertRaisesRegex(V.GitCorpusError, 'local-import lineage'):
            V.observe(self.checkout, self.upstream, revision)
        with self.assertRaisesRegex(V.GitCorpusError, 'archive'):
            V.observe(self.checkout, self.upstream, revision, expected_archive_sha256='00'*32)
        manifest, _ = V.observe(self.checkout, self.upstream, revision, expected_archive_sha256=archive)
        self.assertEqual(manifest['acquisition'], 'verified-local-import')
        self.assertEqual(manifest['checkout_origin'], str(self.root / 'verified-import'))
        self.assertEqual(self.command('remote', 'get-url', 'origin'), manifest['checkout_origin'])

    def commit(self):
        self.command('add', '-A')
        self.command('commit', '-qm', 'Fixture source')
        self.head = self.command('rev-parse', 'HEAD')

    def observe(self, **kwargs):
        return V.observe(self.checkout, self.upstream, self.head, **kwargs)

    def source_lock(self):
        return {'upstream': self.upstream, 'revision': self.head,
                'git_archive_sha256': V.digest(V.git(self.checkout, 'archive', '--format=tar', 'HEAD')),
                'license_files': [{'path': 'README.md', 'sha256': V.digest((self.checkout / 'README.md').read_bytes())}]}

    def test_exact_git_bytes_include_cpp_headers_and_plain_text_without_semantic_claims(self):
        manifest, files = self.observe()
        self.assertEqual(manifest['file_count'], 3)
        self.assertEqual(manifest['cpp_file_count'], 2)
        self.assertEqual(manifest['semantic_testimony_count'], 0)
        self.assertFalse(manifest['canonical_admission_completed'])
        self.assertFalse(manifest['executable_semantics_verified'])
        self.assertEqual(manifest['byte_count'], sum(map(len, files.values())))
        for artifact in manifest['artifacts']:
            self.assertEqual(artifact['sha256'], hashlib.sha256(files[artifact['path']]).hexdigest())
            self.assertEqual(files[artifact['path']], (self.checkout / artifact['path']).read_bytes())

    def test_untracked_build_outputs_do_not_change_selected_tree(self):
        before = self.observe()
        (self.checkout / 'src/stockfish').write_bytes(b'untracked build output\0')
        self.assertEqual(self.observe(), before)

    def test_dirty_tracked_bytes_reject(self):
        (self.checkout / 'src/main.cpp').write_text('changed\n')
        with self.assertRaises(V.GitCorpusError):
            self.observe()

    def test_assume_unchanged_does_not_hide_changed_corpus_bytes(self):
        self.command('update-index', '--assume-unchanged', 'src/main.cpp')
        (self.checkout / 'src/main.cpp').write_text('changed\n')
        self.assertEqual(self.command('diff', '--name-only', 'HEAD'), '')
        with self.assertRaises(V.GitCorpusError):
            self.observe()

    def test_replacement_commit_cannot_redefine_selected_source_bytes(self):
        selected = self.head
        (self.checkout / 'src/main.cpp').write_text('int substituted_source = 7;\n')
        self.commit()
        replacement = self.head
        self.command('reset', '--hard', selected)
        self.command('replace', selected, replacement)
        self.command('reset', '--hard', selected)
        self.head = selected
        self.assertEqual(self.command('rev-parse', 'HEAD'), selected)
        self.assertEqual(self.command('diff', '--name-only', 'HEAD'), '')
        with self.assertRaises(V.GitCorpusError):
            self.observe()

    def test_selected_commit_and_origin_are_authoritative(self):
        for upstream, head in ((self.upstream, '0' * 40), ('https://github.com/example/wrong', self.head)):
            with self.subTest(upstream=upstream, head=head), self.assertRaises(V.GitCorpusError):
                V.observe(self.checkout, upstream, head)

    def test_private_remote_credentials_cannot_enter_public_manifest(self):
        self.upstream = 'https://user:secret@github.com/example/source'
        self.command('remote', 'set-url', 'origin', self.upstream)
        with self.assertRaises(V.GitCorpusError):
            self.observe()

    def test_committed_symlink_and_replaced_regular_file_reject(self):
        target = self.checkout / 'src/main.cpp'
        target.unlink()
        target.symlink_to('main.h')
        with self.assertRaises(V.GitCorpusError):
            self.observe()
        self.commit()
        with self.assertRaises(V.GitCorpusError):
            self.observe()

    def test_nontext_and_empty_artifacts_have_explicit_rejection(self):
        target = self.checkout / 'payload.txt'
        for body in (b'', b'contains\0binary', b'\xff'):
            with self.subTest(body=body):
                target.write_bytes(body)
                self.commit()
                with self.assertRaises(V.GitCorpusError):
                    self.observe()

    def test_each_resource_limit_rejects_incomplete_snapshot(self):
        for bounds in ({'maximum_files': 2}, {'maximum_bytes': 1}, {'maximum_file_bytes': 1}):
            with self.subTest(bounds=bounds), self.assertRaises(V.GitCorpusError):
                self.observe(**bounds)

    def test_frozen_bytes_and_manifest_preserve_observation_and_refuse_overwrite(self):
        manifest, files = self.observe()
        destination = self.root / 'frozen'
        manifest_path = V.freeze(manifest, files, destination)
        self.assertEqual(json.loads(manifest_path.read_bytes()), manifest)
        for name, body in files.items():
            self.assertEqual((destination / 'files' / name).read_bytes(), body)
        with self.assertRaises(V.GitCorpusError):
            V.freeze(manifest, files, destination)

    def test_grammar_source_checks_archive_revision_origin_and_license(self):
        locked = self.source_lock()
        self.assertEqual(Q.verify_source(self.checkout, locked)['revision'], self.head)
        for key in ('git_archive_sha256', 'revision', 'upstream'):
            with self.subTest(key=key), self.assertRaises(V.GitCorpusError):
                Q.verify_source(self.checkout, locked | {key: 'wrong'})
        wrong_license = locked | {'license_files': [{'path': 'README.md', 'sha256': '0' * 64}]}
        with self.assertRaises(V.GitCorpusError):
            Q.verify_source(self.checkout, wrong_license)

    def test_grammar_header_bytes_must_match_git_even_when_index_ignores_changes(self):
        locked = self.source_lock()
        self.command('update-index', '--assume-unchanged', 'src/main.h')
        (self.checkout / 'src/main.h').write_text('constexpr int fixture = 9;\n')
        self.assertEqual(self.command('diff', '--name-only', 'HEAD'), '')
        with self.assertRaises(V.GitCorpusError):
            Q.verify_source(self.checkout, locked)

    def test_grammar_build_rejects_wrong_generated_parser_before_creating_output(self):
        grammar = self.source_lock() | {'name': 'fixture',
            'generated_parsers': [{'path': 'src/main.cpp', 'sha256': '0' * 64}],
            'external_scanners': []}
        grammar_lock = self.root / 'grammar.json'
        runtime_lock = self.root / 'runtime.json'
        grammar_lock.write_text(json.dumps({'repositories': [grammar]}))
        runtime_lock.write_text(json.dumps({'dependencies': {'tree-sitter': self.source_lock()}}))
        output = self.root / 'provider'
        with self.assertRaisesRegex(V.GitCorpusError, 'parser/scanner'):
            Q.build(self.checkout, self.checkout, grammar_lock, runtime_lock, 'fixture', output)
        self.assertFalse(output.exists())

    def build_fixture(self, source, output):
        parser = self.checkout / 'src/parser.c'
        parser.write_bytes(source)
        self.commit()
        grammar = self.source_lock() | {'name': 'fixture',
            'generated_parsers': [{'path': 'src/parser.c', 'sha256': V.digest(source)}],
            'external_scanners': []}
        grammar_lock = self.root / 'grammar.json'
        runtime_lock = self.root / 'runtime.json'
        grammar_lock.write_text(json.dumps({'repositories': [grammar]}))
        runtime_lock.write_text(json.dumps({'dependencies': {'tree-sitter': self.source_lock()}}))
        return lambda: Q.build(self.checkout, self.checkout, grammar_lock, runtime_lock, 'fixture', output)

    @unittest.skipUnless(shutil.which('cc'), 'actual C compiler unavailable')
    def test_real_compile_uses_frozen_tracked_headers_and_ignores_ambient_includes(self):
        output = self.root / 'provider'
        build = self.build_fixture(b'#include <stdint.h>\nint fixture_value(void) { return sizeof(uint32_t); }\n', output)
        # Either untracked header or CPATH would break a build from the checkout.
        (self.checkout / 'src/stdint.h').write_text('#error untracked header selected\n')
        ambient = self.root / 'ambient'
        ambient.mkdir()
        (ambient / 'stdint.h').write_text('#error ambient header selected\n')
        with patch.dict(os.environ, {'CPATH': str(ambient), 'C_INCLUDE_PATH': str(ambient)}):
            receipt_path = build()
        receipt = json.loads(receipt_path.read_bytes())
        self.assertFalse((output / 'source/src/stdint.h').exists())
        self.assertEqual(receipt['exit_code'], 0)
        library = Path(receipt['library']['path'])
        self.assertEqual(receipt['library']['sha256'], V.digest(library.read_bytes()))
        self.assertEqual(ctypes.CDLL(str(library)).fixture_value(), 4)
        self.assertFalse(receipt['grammar_runtime_execution_completed'])
        self.assertFalse(receipt['canonical_corpus_admission_completed'])

    @unittest.skipUnless(shutil.which('cc'), 'actual C compiler unavailable')
    def test_real_compile_failure_retains_diagnostics_without_success_receipt(self):
        output = self.root / 'failed-provider'
        build = self.build_fixture(b'#error deliberate source fixture rejection\n', output)
        with self.assertRaisesRegex(V.GitCorpusError, 'build failed'):
            build()
        self.assertIn(b'deliberate source fixture rejection', (output / 'build.stderr.log').read_bytes())
        self.assertFalse((output / 'build-receipt.json').exists())


class CommittedSourceReceiptTests(unittest.TestCase):
    def execute(self, committed):
        initial = {'schema':'laplace.admit-source/v1','persisted_profile':None,
                   'admission':{'profile_id':'\\x'+'12'*32,'source_profile_receipt_id':'\\x'+'34'*32}}
        outputs = [subprocess.CompletedProcess([],0,json.dumps(initial),'') ,
                   subprocess.CompletedProcess([],0,json.dumps(committed),'')]
        with patch.object(A.subprocess,'run',side_effect=outputs) as run:
            result = A.run_sql(['psql'],'BEGIN; SELECT actual_native_function(); COMMIT;')
            self.assertEqual(run.call_count,2)
            self.assertIn('laplace.source_profile',run.call_args.kwargs['input'])
            return result

    def test_first_admission_reads_committed_profile_after_empty_statement_snapshot(self):
        result=self.execute({'persisted_profile':{'profile_id':'\\x'+'12'*32},
                             'source_profile_receipt_bound':True,
                             'entity_count':23,'physicality_count':23,'attestation_count':7})
        self.assertEqual(result['entity_count'],23)
        self.assertEqual(result['persisted_profile']['profile_id'],result['admission']['profile_id'])

    def test_missing_or_different_committed_profile_cannot_be_reported_as_admitted(self):
        for profile in (None,{'profile_id':'\\x'+'56'*32}):
            with self.assertRaisesRegex(A.AdmissionError,'exact native source profile'):
                self.execute({'persisted_profile':profile})

    def test_unbound_native_profile_receipt_cannot_be_reported_as_admitted(self):
        with self.assertRaisesRegex(A.AdmissionError,'exact native source profile'):
            self.execute({'persisted_profile':{'profile_id':'\\x'+'12'*32},
                          'source_profile_receipt_bound':False})


if __name__ == '__main__':
    unittest.main()
