#!/usr/bin/env python3
"""Exercise two live native grammar handles with two actual shared libraries."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--library', required=True, type=Path)
    parser.add_argument('--probe', required=True, type=Path)
    args = parser.parse_args()
    source = (b'extern const void* tree_sitter_cpp(void);\n'
              b'const void* tree_sitter_fixture_b(void) { return tree_sitter_cpp(); }\n')
    library = args.library.resolve(strict=True)
    probe = args.probe.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='laplace-grammar-handles-') as temporary:
        root = Path(temporary)
        wrapper_source = root / 'fixture.c'
        wrapper_library = root / 'fixture.so'
        wrapper_source.write_bytes(source)
        command = [args.compiler, '-shared', '-fPIC', str(wrapper_source),
                   str(library), '-o', str(wrapper_library)]
        build = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if build.returncode:
            print(json.dumps({'phase': 'wrapper-build-failed', 'command': command,
                              'stdout': build.stdout, 'stderr': build.stderr}))
            return 1
        wrapper_library.chmod(0o440)
        result = subprocess.run([str(probe), str(library), str(wrapper_library)],
                                capture_output=True, text=True, timeout=60)
        print(json.dumps({
            'schema': 'laplace.verified-grammar-loader-test/v1',
            'original_sha256': hashlib.sha256(library.read_bytes()).hexdigest(),
            'wrapper_source_sha256': hashlib.sha256(source).hexdigest(),
            'wrapper_library_sha256': hashlib.sha256(wrapper_library.read_bytes()).hexdigest(),
            'compiler_command': command, 'probe_exit_code': result.returncode,
            'probe_stdout': result.stdout, 'probe_stderr': result.stderr,
            'passed': result.returncode == 0,
        }, sort_keys=True))
        return 0 if result.returncode == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
