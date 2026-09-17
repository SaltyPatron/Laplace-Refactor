#!/usr/bin/env python3
"""Exercise source provenance against real temporary Git objects and file changes."""
import hashlib
import ctypes
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/sources'))
import verified_git as V
import qualify_grammar as Q
import build_cpp_provider_execution_mutant as M
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

    def test_imported_runtime_requires_locked_archive_and_actual_tracked_bytes(self):
        locked = self.source_lock()
        imported_origin = str(self.root / 'verified-runtime-import')
        self.command('remote', 'set-url', 'origin', imported_origin)
        observed = Q.verify_source(self.checkout, locked)
        self.assertEqual(observed['checkout_origin'], imported_origin)
        self.assertEqual(observed['git_archive_sha256'], locked['git_archive_sha256'])
        with self.assertRaisesRegex(V.GitCorpusError, 'archive'):
            Q.verify_source(self.checkout, locked | {'git_archive_sha256':'00'*32})
        self.command('update-index', '--assume-unchanged', 'src/main.h')
        (self.checkout / 'src/main.h').write_text('constexpr int fixture = 99;\n')
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


    def path_sensitive_fixture(self, output):
        # This header is committed and authenticated by the locked Git archive,
        # just like the generated source. Inline functions retain header locations.
        (self.checkout / 'src/fixture_path.h').write_text(
            'static inline const char *header_file(void) { return __FILE__; }\n'
            'static inline const char *header_base_file(void) { return __BASE_FILE__; }\n'
            'static inline const char *header_builtin_file(void) { return __builtin_FILE(); }\n')
        source = (
            b'#include "fixture_path.h"\n#include <assert.h>\n'
            b'const char *fixture_file(void) { return __FILE__; }\n'
            b'const char *fixture_base_file(void) { return __BASE_FILE__; }\n'
            b'const char *fixture_builtin_file(void) { return __builtin_FILE(); }\n'
            b'const char *fixture_header_file(void) { return header_file(); }\n'
            b'const char *fixture_header_base_file(void) { return header_base_file(); }\n'
            b'const char *fixture_header_builtin_file(void) { return header_builtin_file(); }\n'
            b'int fixture_value(void) { return 17; }\n'
            b'int fixture_checked(int value) { assert(value == 17); return value; }\n')
        return source, self.build_fixture(source, output)

    def build_locked_fixture(self, output):
        return Q.build(self.checkout, self.checkout, self.root / 'grammar.json',
                       self.root / 'runtime.json', 'fixture', output)

    @unittest.skipUnless(shutil.which('cc') and os.name == 'posix',
                         'actual POSIX C compiler unavailable')
    def test_real_provider_build_is_reproducible_and_source_sensitive(self):
        first = self.root / 'provider-a'
        second = self.root / 'different-length-build-root' / 'provider-b'
        source, build = self.path_sensitive_fixture(first)
        receipts = [json.loads(build().read_bytes()),
                    json.loads(self.build_locked_fixture(second).read_bytes())]
        first_bytes = Path(receipts[0]['library']['path']).read_bytes()
        self.assertEqual(first_bytes, Path(receipts[1]['library']['path']).read_bytes())
        for field in ('compiler', 'grammar', 'runtime', 'generated_sources',
                      'grammar_lock_sha256', 'dependency_lock_sha256'):
            self.assertEqual(receipts[0][field], receipts[1][field])
        self.assertNotEqual(receipts[0]['command'], receipts[1]['command'])
        self.assertNotEqual(receipts[0]['library']['path'], receipts[1]['library']['path'])
        for output, receipt in zip((first, second), receipts):
            self.assertEqual(receipt['exit_code'], 0)
            self.assertEqual(receipt['library']['sha256'], V.digest(first_bytes))
            self.assertEqual(receipt['library']['byte_count'], len(first_bytes))
            self.assertEqual(receipt['compiler']['sha256'],
                             V.digest(Path(receipt['compiler']['path']).read_bytes()))
            self.assertIn(str(output / 'source/src/parser.c'), receipt['command'])
            self.assertIn(str(output / 'provider.so'), receipt['command'])
            self.assertNotIn(str(output).encode(), first_bytes)
            library = ctypes.CDLL(receipt['library']['path'])
            for name, expected in (
                ('fixture_file', b'./src/parser.c'),
                ('fixture_base_file', b'./src/parser.c'),
                ('fixture_builtin_file', b'./src/parser.c'),
                ('fixture_header_file', b'./src/fixture_path.h'),
                ('fixture_header_base_file', b'./src/parser.c'),
                ('fixture_header_builtin_file', b'./src/fixture_path.h'),
            ):
                with self.subTest(output=str(output), function=name):
                    function = getattr(library, name)
                    function.restype = ctypes.c_char_p
                    self.assertEqual(function(), expected)
            library.fixture_checked.argtypes = [ctypes.c_int]
            self.assertEqual(library.fixture_checked(17), 17)
            self.assertEqual(library.fixture_value(), 17)

            # Execute a real failing assert in a child, without creating a core.
            failed = subprocess.run(
                [sys.executable, '-c',
                 'import ctypes, resource, sys; '
                 'resource.setrlimit(resource.RLIMIT_CORE, (0, 0)); '
                 'library = ctypes.CDLL(sys.argv[1]); '
                 'library.fixture_checked.argtypes = [ctypes.c_int]; '
                 'library.fixture_checked(0)',
                 receipt['library']['path']],
                capture_output=True, check=False, timeout=30)
            self.assertEqual(failed.returncode, -signal.SIGABRT)
            self.assertIn(b'./src/parser.c', failed.stderr)
            self.assertIn(b'value == 17', failed.stderr)
            self.assertNotIn(str(output).encode(), failed.stderr)

        # A different authenticated semantic input must still change the output.
        changed_source = source.replace(b'return 17;', b'return 23;')
        changed = json.loads(self.build_fixture(
            changed_source, self.root / 'changed-source-provider')().read_bytes())
        self.assertNotEqual(changed['grammar']['revision'], receipts[0]['grammar']['revision'])
        self.assertNotEqual(changed['grammar']['git_archive_sha256'],
                            receipts[0]['grammar']['git_archive_sha256'])
        self.assertNotEqual(changed['library']['sha256'], receipts[0]['library']['sha256'])
        self.assertEqual(ctypes.CDLL(changed['library']['path']).fixture_value(), 23)

    @unittest.skipUnless(shutil.which('cc') and os.name == 'posix',
                         'actual POSIX C compiler unavailable')
    def test_real_provider_without_prefix_normalization_changes_with_build_root(self):
        first = self.root / 'unnormalized-a'
        second = self.root / 'longer-unnormalized-root' / 'provider-b'
        _, build = self.path_sensitive_fixture(first)
        real_run = subprocess.run
        actual_compile_commands = []

        def compile_without_prefix_map(command, *args, **kwargs):
            if isinstance(command, list) and '-shared' in command:
                # Mutation only: execute the actual compiler with the old argv.
                selected = [arg for arg in command if not arg.startswith('-ffile-prefix-map=')]
                self.assertEqual(len(command) - len(selected), 1)
                actual_compile_commands.append(selected)
                command = selected
            return real_run(command, *args, **kwargs)

        with patch.object(Q.subprocess, 'run', side_effect=compile_without_prefix_map):
            receipts = [json.loads(build().read_bytes()),
                        json.loads(self.build_locked_fixture(second).read_bytes())]
        self.assertEqual(len(actual_compile_commands), 2)
        self.assertNotEqual(Path(receipts[0]['library']['path']).read_bytes(),
                            Path(receipts[1]['library']['path']).read_bytes())
        for output, receipt, command in zip((first, second), receipts, actual_compile_commands):
            self.assertIn(str(output / 'source/src/parser.c'), command)
            library = ctypes.CDLL(receipt['library']['path'])
            library.fixture_file.restype = ctypes.c_char_p
            self.assertEqual(library.fixture_file(), str(output / 'source/src/parser.c').encode())


    @unittest.skipUnless(shutil.which('cc'), 'actual C compiler unavailable')
    def test_real_execution_mutant_authenticates_sources_and_reuses_exact_build(self):
        parent_alias = self.root / 'accepted-output-parent'
        parent_alias.symlink_to(self.root, target_is_directory=True)
        _, first_build = self.path_sensitive_fixture(parent_alias / 'normalized-provider')
        first_receipt = first_build()
        first_bytes = first_receipt.read_bytes()
        original = json.loads(first_bytes)
        output_root = self.root / 'execution-mutant'
        args = (first_receipt, V.digest(first_bytes), self.checkout,
                self.root / 'grammar.json', self.root / 'runtime.json', output_root, 'fixture')
        result_path = M.build(*args)
        result_bytes = result_path.read_bytes()
        result = json.loads(result_bytes)
        self.assertEqual(result['status'], 'completed')
        self.assertEqual(result['exit_code'], 0)
        self.assertIn('remove only -ffile-prefix-map', result['inputs']['mutation'])
        self.assertFalse(result['inputs']['production_provider_selected'])
        self.assertEqual(result['inputs']['first_receipt_sha256'], V.digest(first_bytes))
        self.assertEqual(result['inputs']['generated_sources'], original['generated_sources'])
        self.assertIn('src/fixture_path.h',
                      [item['path'] for item in result['inputs']['tracked_files']])
        self.assertEqual(result['inputs']['compiler'], original['compiler'])
        self.assertFalse(any(arg.startswith('-ffile-prefix-map=') for arg in result['command']))
        library = Path(result['library']['path'])
        self.assertNotEqual(result['library']['sha256'], original['library']['sha256'])
        self.assertEqual(result['library']['sha256'], V.digest(library.read_bytes()))
        loaded = ctypes.CDLL(str(library))
        loaded.fixture_file.restype = ctypes.c_char_p
        loaded.fixture_header_file.restype = ctypes.c_char_p
        self.assertEqual(loaded.fixture_file(), str(result_path.parent / 'source/src/parser.c').encode())
        self.assertEqual(loaded.fixture_header_file(),
                         str(result_path.parent / 'source/src/fixture_path.h').encode())
        self.assertEqual(loaded.fixture_value(), 17)
        before = (library.stat().st_mtime_ns, result_path.stat().st_mtime_ns)
        self.assertEqual(M.build(*args), result_path)
        self.assertEqual(result_path.read_bytes(), result_bytes)
        self.assertEqual((library.stat().st_mtime_ns, result_path.stat().st_mtime_ns), before)
        self.assertEqual(first_receipt.read_bytes(), first_bytes)

    @unittest.skipUnless(shutil.which('cc'), 'actual C compiler unavailable')
    def test_execution_mutant_refuses_altered_frozen_header_or_shadow(self):
        _, first_build = self.path_sensitive_fixture(self.root / 'normalized-provider')
        first_receipt = first_build()
        frozen = first_receipt.parent / 'source'
        header = frozen / 'src/fixture_path.h'
        original = header.read_bytes()
        output_root = self.root / 'refused-execution-mutant'
        args = (first_receipt, V.digest(first_receipt.read_bytes()), self.checkout,
                self.root / 'grammar.json', self.root / 'runtime.json', output_root, 'fixture')
        header.chmod(0o640)
        header.write_bytes(original + b'\n/* changed frozen header */\n')
        with self.assertRaisesRegex(V.GitCorpusError, 'frozen source bytes'):
            M.build(*args)
        self.assertFalse(output_root.exists())
        header.write_bytes(original)
        (frozen / 'src/untracked_shadow.h').write_bytes(b'#error injected header\n')
        with self.assertRaisesRegex(V.GitCorpusError, 'frozen source inventory'):
            M.build(*args)
        self.assertFalse(output_root.exists())

    @unittest.skipUnless(shutil.which('cc'), 'actual C compiler unavailable')
    def test_execution_mutant_refuses_changed_cached_library(self):
        _, first_build = self.path_sensitive_fixture(self.root / 'normalized-provider')
        first_receipt = first_build()
        args = (first_receipt, V.digest(first_receipt.read_bytes()), self.checkout,
                self.root / 'grammar.json', self.root / 'runtime.json',
                self.root / 'execution-mutant', 'fixture')
        result_path = M.build(*args)
        result = json.loads(result_path.read_bytes())
        library = Path(result['library']['path'])
        library.chmod(0o640)
        library.write_bytes(library.read_bytes() + b'changed')
        with self.assertRaisesRegex(V.GitCorpusError, 'retained test provider bytes'):
            M.build(*args)

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

    @unittest.skipUnless(shutil.which('cc'), 'actual C compiler unavailable')
    def test_real_compile_accepts_exact_imported_grammar_and_runtime(self):
        output = self.root / 'imported-provider'
        build = self.build_fixture(b'int fixture_value(void) { return 17; }\n', output)
        origin = str(self.root / 'verified-source-import')
        self.command('remote', 'set-url', 'origin', origin)
        receipt = json.loads(build().read_bytes())
        self.assertEqual(receipt['grammar']['checkout_origin'], origin)
        self.assertEqual(receipt['runtime']['checkout_origin'], origin)
        self.assertEqual(ctypes.CDLL(receipt['library']['path']).fixture_value(),17)
        self.assertFalse(receipt['canonical_corpus_admission_completed'])


    def ownership_environment(self):
        # Git's own t0033 uses this switch to exercise real ownership checks
        # without chown/root. Keep all persistent configuration private and exact.
        config = self.root / 'ownership.gitconfig'
        config.write_bytes(b'[user]\n\tname = Ownership fixture\n')
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith('GIT_')}
        environment.update({'GIT_TEST_ASSUME_DIFFERENT_OWNER': '1',
                            'GIT_CONFIG_NOSYSTEM': '1',
                            'GIT_CONFIG_GLOBAL': str(config)})
        return environment, config, config.read_bytes()

    def test_shared_checkout_uses_only_invocation_local_ownership_exception(self):
        locked = self.source_lock()
        environment, config, original = self.ownership_environment()
        with patch.dict(os.environ, environment, clear=True):
            refused = subprocess.run(['git', '-C', str(self.checkout), 'status', '--porcelain'],
                                     capture_output=True, check=False)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn(b'dubious ownership', refused.stderr)
            self.assertEqual(Q.verify_source(self.checkout, locked)['revision'], self.head)
            manifest, files = self.observe()
            self.assertEqual(manifest['commit'], self.head)
            self.assertEqual(files['src/main.h'], (self.checkout / 'src/main.h').read_bytes())
            again = subprocess.run(['git', '-C', str(self.checkout), 'status', '--porcelain'],
                                   capture_output=True, check=False)
            self.assertNotEqual(again.returncode, 0)
            self.assertIn(b'dubious ownership', again.stderr)
        self.assertEqual(config.read_bytes(), original)

    def test_ownership_exception_does_not_trust_another_checkout(self):
        other = self.root / 'other'
        other.mkdir()
        subprocess.run(['git', '-C', str(other), 'init', '-q'], check=True)
        environment, config, original = self.ownership_environment()
        with patch.dict(os.environ, environment, clear=True):
            self.assertEqual(V.git(self.checkout, 'rev-parse', 'HEAD').decode().strip(), self.head)
            with self.assertRaisesRegex(V.GitCorpusError, 'dubious ownership'):
                V.git(self.checkout, '-C', str(other), 'status', '--porcelain')
        self.assertEqual(config.read_bytes(), original)

    def test_git_owner_refuses_linked_checkout_and_linked_parent(self):
        linked_checkout = self.root / 'linked-checkout'
        linked_checkout.symlink_to(self.checkout, target_is_directory=True)
        linked_parent = self.root / 'linked-parent'
        linked_parent.symlink_to(self.root, target_is_directory=True)
        for selected in (linked_checkout, linked_parent / self.checkout.name,
                         Path('relative-checkout')):
            with self.subTest(path=str(selected)), self.assertRaisesRegex(
                    V.GitCorpusError, 'absolute physical directory'):
                V.git(selected, 'rev-parse', 'HEAD')

    @unittest.skipUnless(shutil.which('cc'), 'actual C compiler unavailable')
    def test_shared_imported_grammar_build_keeps_exact_source_verification(self):
        output = self.root / 'shared-imported-provider'
        build = self.build_fixture(b'int fixture_value(void) { return 23; }\n', output)
        origin = str(self.root / 'verified-shared-import')
        self.command('remote', 'set-url', 'origin', origin)
        locked = self.source_lock()
        self.command('update-index', '--assume-unchanged', 'src/main.h')
        environment, config, original = self.ownership_environment()
        with patch.dict(os.environ, environment, clear=True):
            receipt = json.loads(build().read_bytes())
            self.assertEqual(receipt['grammar']['checkout_origin'], origin)
            self.assertEqual(receipt['runtime']['checkout_origin'], origin)
            self.assertEqual(ctypes.CDLL(receipt['library']['path']).fixture_value(), 23)
            self.assertFalse(receipt['canonical_corpus_admission_completed'])
            (self.checkout / 'src/main.h').write_text('constexpr int fixture = 23;\n')
            with self.assertRaises(V.GitCorpusError):
                Q.verify_source(self.checkout, locked)
        self.assertEqual(config.read_bytes(), original)


    @unittest.skipUnless(shutil.which('cc'), 'actual C compiler unavailable')
    def test_grammar_build_resolves_parent_alias_once_and_checks_exact_shared_source(self):
        output = self.root / 'parent-alias-provider'
        self.build_fixture(b'int fixture_value(void) { return 29; }\n', output)
        locked = self.source_lock()
        linked_parent = self.root / 'source-parent'
        linked_parent.symlink_to(self.root, target_is_directory=True)
        selected = linked_parent / self.checkout.name
        environment, config, original = self.ownership_environment()
        with patch.dict(os.environ, environment, clear=True):
            self.assertEqual(Q.verify_source(selected, locked)['revision'], self.head)
            Q.acquire(selected, self.root / 'grammar.json', 'fixture')
            receipt_path = Q.build(selected, selected, self.root / 'grammar.json',
                                   self.root / 'runtime.json', 'fixture', output)
            receipt = json.loads(receipt_path.read_bytes())
            self.assertEqual(receipt['grammar']['git_archive_sha256'],
                             locked['git_archive_sha256'])
            self.assertEqual(ctypes.CDLL(receipt['library']['path']).fixture_value(), 29)
            self.assertFalse(receipt['canonical_corpus_admission_completed'])
            (self.checkout / 'src/main.h').write_text('constexpr int fixture = 29;\n')
            with self.assertRaises(V.GitCorpusError):
                Q.verify_source(selected, locked)
            with self.assertRaises(V.GitCorpusError):
                Q.build(selected, selected, self.root / 'grammar.json',
                        self.root / 'runtime.json', 'fixture', self.root / 'changed-provider')
        self.assertFalse((self.root / 'changed-provider').exists())
        self.assertEqual(config.read_bytes(), original)

    def test_grammar_boundaries_still_reject_direct_checkout_links(self):
        output = self.root / 'linked-provider'
        self.build_fixture(b'int fixture_value(void) { return 31; }\n', output)
        linked = self.root / 'linked-source'
        linked.symlink_to(self.checkout, target_is_directory=True)
        for invoke in (
            lambda: Q.verify_source(linked, self.source_lock()),
            lambda: Q.acquire(linked, self.root / 'grammar.json', 'fixture'),
            lambda: Q.build(linked, self.checkout, self.root / 'grammar.json',
                            self.root / 'runtime.json', 'fixture', output),
            lambda: Q.build(self.checkout, linked, self.root / 'grammar.json',
                            self.root / 'runtime.json', 'fixture', output),
        ):
            with self.assertRaisesRegex(V.GitCorpusError, 'non-symlink'):
                invoke()
        self.assertFalse(output.exists())


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


class GitReadbackReceiptTests(unittest.TestCase):
    def execute(self, witness='ab'*32, corrupt_last=False, span_count=4, response=None):
        profile='\\x'+'12'*32
        artifacts=[{'path':'one.cpp','sha256':hashlib.sha256(b'one').hexdigest(),'byte_count':3},
                   {'path':'two.cpp','sha256':hashlib.sha256('λ'.encode()).hexdigest(),'byte_count':2}]
        records=[{'artifact_index':index,'sha256':artifact['sha256'],
                  'output_bytes':artifact['byte_count'],'source_profile_id':profile}
                 for index,artifact in enumerate(artifacts)]
        if corrupt_last:
            records[-1]['sha256']='ef'*32
        returned={'structural_receipt_count':1,'structural_witness_fingerprint':witness,
                  'execution_witness_fingerprint':'cd'*32,
                  'structural_execution_receipt_id':'ef'*32,
                  'historical_structural_receipt_id':'01'*32,'records':records}
        result={'admission':{'profile_id':profile,'composition_working_set_receipt_id':'\\x'+'34'*32,
                            'source_fingerprint':'\\x'+'56'*32,'testimony_count':0,'evidence_node_count':0},
                'persisted_profile':{'claim_count':0,'file_count':2,'span_count':span_count},
                'execution_metrics':{'last':{'valid':True,'structural_execution_receipt_id':'ef'*32}}}
        identities={key:'78'*32 for key in ('source_epoch','identity_epoch','evidence_epoch','firmware_epoch',
                    'dependency_epoch','database_epoch','package_epoch','authority_fingerprint')}
        if response is None:
            response = subprocess.CompletedProcess([], 0, json.dumps(returned) + '\n', '')
        with patch.object(A.subprocess, 'run', return_value=response) as process:
            try:
                return A.verify_git_readback(['psql'],result,{'manifest':{'artifacts':artifacts,'byte_count':5}},
                                             identities,'90'*32,'91'*32,'92'*32)
            finally:
                self.assertEqual(process.call_count, 1)
                self.readback_sql = process.call_args.kwargs['input']
                self.readback_client_timeout = process.call_args.kwargs['timeout']

    def test_readback_retains_semantic_witness_identity_and_all_exact_artifacts(self):
        result=self.execute()
        self.assertEqual(result['structural_witness_fingerprint'],'ab'*32)
        self.assertEqual(result['execution_witness_fingerprint'],'cd'*32)
        self.assertEqual(result['structural_execution_receipt_id'],'ef'*32)
        self.assertEqual(result['historical_structural_receipt_id'],'01'*32)
        self.assertIn("WHERE receipt_id=decode('"+'ef'*32+"','hex')", self.readback_sql)
        self.assertIn('AND version=4', self.readback_sql)
        self.assertEqual(result['verified_file_count'],2)
        self.assertEqual(result['verified_byte_count'],5)
        self.assertTrue(result['all_artifacts_exact'])

    def test_readback_has_a_local_server_deadline_before_unchanged_client_timeout(self):
        self.execute()
        self.assertTrue(self.readback_sql.startswith(
            "BEGIN READ ONLY;\nSET LOCAL statement_timeout = '15min';\nWITH selected AS MATERIALIZED ("))
        self.assertTrue(self.readback_sql.endswith('::text;\nCOMMIT;'))
        self.assertEqual(self.readback_client_timeout, 1000)

    def test_server_cancellation_cannot_promote_partial_output_to_exact_readback(self):
        response = subprocess.CompletedProcess([], 3, '{}\n',
            'ERROR:  57014: canceling statement due to statement timeout\n')
        with self.assertRaisesRegex(A.AdmissionError,
                'exact Git source readback failed:.*57014: canceling statement due to statement timeout'):
            self.execute(response=response)
        self.assertEqual(self.readback_client_timeout, 1000)

    def test_extra_readback_rows_still_fail_the_single_result_parser(self):
        with self.assertRaisesRegex(A.AdmissionError, 'returned 2 rows; expected one'):
            self.execute(response=subprocess.CompletedProcess([], 0, '{}\n{}\n', ''))

    def test_large_committed_profile_reaches_readback_with_its_own_finite_bound(self):
        result = self.execute(span_count=5000001)
        self.assertIn('5000001::numeric,5::numeric)', self.readback_sql)
        self.assertTrue(result['all_artifacts_exact'])
        self.assertEqual(result['verified_file_count'], 2)

    def test_missing_or_malformed_structural_witness_identity_is_rejected(self):
        for witness in (None,'malformed','\\x'+'ab'*32):
            with self.subTest(witness=witness), self.assertRaisesRegex(A.AdmissionError,'verified canonical structural witness fingerprint'):
                self.execute(witness)

    def test_changed_later_artifact_is_rejected(self):
        for span_count in (4, 5000001):
            with self.subTest(span_count=span_count), self.assertRaisesRegex(A.AdmissionError,'two.cpp'):
                self.execute(corrupt_last=True, span_count=span_count)


if __name__ == '__main__':
    unittest.main()
