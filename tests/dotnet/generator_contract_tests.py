#!/usr/bin/env python3
"""Exercise deterministic generation and the missing/changed-operation control."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile


def run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(arguments, check=False, text=True, capture_output=True)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: generator_contract_tests.py GENERATOR CONTRACT", file=sys.stderr)
        return 64
    generator = pathlib.Path(sys.argv[1])
    contract = pathlib.Path(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="laplace-dotnet-generator-") as directory:
        first = pathlib.Path(directory) / "first.cs"
        second = pathlib.Path(directory) / "second.cs"
        for output in (first, second):
            result = run(
                sys.executable,
                str(generator),
                "--contract",
                str(contract),
                "--output",
                str(output),
            )
            if result.returncode != 0:
                print(result.stderr, file=sys.stderr)
                return 1
        if first.read_bytes() != second.read_bytes():
            print("repeated .NET ISA generation differs", file=sys.stderr)
            return 1
        verified = run(
            sys.executable,
            str(generator),
            "--contract",
            str(contract),
            "--verify",
            str(first),
        )
        if verified.returncode != 0:
            print(verified.stderr, file=sys.stderr)
            return 1

        source = first.read_text(encoding="utf-8")
        declaration = "public readonly struct IdentityCodepointBatch :"
        if source.count(declaration) != 1:
            print("generated identity declaration is not unique", file=sys.stderr)
            return 1
        first.write_text(source.replace(declaration, "", 1), encoding="utf-8", newline="\n")
        mutation = run(
            sys.executable,
            str(generator),
            "--contract",
            str(contract),
            "--verify",
            str(first),
        )
        if mutation.returncode == 0:
            print("missing generated operation mutation was not detected", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
