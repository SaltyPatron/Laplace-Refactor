#!/usr/bin/env python3
"""Reproducibility and mutation checks for the generated Unicode source manifest."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
GENERATOR = REPO_ROOT / "tools/contracts/generate-unicode-source.py"
CONTRACT_NAMES = (
    "unicode-source.json",
    "unicode-atom-record.json",
    "ducet-totalization.json",
    "super-fibonacci-hopf.json",
    "hilbert-numeric.json",
)


def generate(contracts: Path, output: Path) -> bytes:
    subprocess.run(
        [sys.executable, str(GENERATOR), "--contracts", str(contracts),
         "--output", str(output)],
        check=True,
        text=True,
        capture_output=True,
    )
    return output.read_bytes()


class UnicodeSourceGenerationTests(unittest.TestCase):
    def test_generation_is_byte_reproducible_and_contract_sensitive(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-unicode-manifest-") as raw:
            root = Path(raw)
            contracts = root / "contracts"
            contracts.mkdir()
            for name in CONTRACT_NAMES:
                shutil.copy2(REPO_ROOT / "contracts" / name, contracts / name)

            first = generate(contracts, root / "first.h")
            replay = generate(contracts, root / "replay.h")
            self.assertEqual(first, replay)
            self.assertIn(b"LAPLACE_UNICODE_GENERATED_SOURCE_COUNT 32u", first)

            atom = contracts / "unicode-atom-record.json"
            atom.write_bytes(atom.read_bytes() + b"\n")
            changed = generate(contracts, root / "changed.h")
            self.assertNotEqual(first, changed)


if __name__ == "__main__":
    unittest.main()
