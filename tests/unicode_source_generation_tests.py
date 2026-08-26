#!/usr/bin/env python3
"""Reproducibility and mutation checks for the generated Unicode source manifest."""

from __future__ import annotations

import shutil
import hashlib
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
    "unicode-root-stream.json",
    "ducet-totalization.json",
    "super-fibonacci-hopf.json",
    "hilbert-numeric.json",
    "unicode-atomic-physicality.json",
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
            self.assertIn(b"LAPLACE_UNICODE_GENERATED_SOURCE_COUNT 33u", first)
            self.assertIn(b"LAPLACE_UNICODE_GENERATED_CONTRACT_COUNT 7u", first)
            self.assertIn(
                b"LAPLACE_UNICODE_ATOMIC_PHYSICALITY_CONTRACT_INDEX 6u",
                first,
            )
            self.assertIn(
                b"LAPLACE_UNICODE_ATOMIC_PHYSICALITY_RECIPE_VERSION UINT32_C(1)",
                first,
            )
            physicality_digest = hashlib.sha256(
                (contracts / "unicode-atomic-physicality.json").read_bytes()
            ).digest()
            initializer = ", ".join(
                f"0x{byte:02x}u" for byte in physicality_digest
            ).encode("ascii")
            self.assertIn(initializer, first)

            source = contracts / "unicode-source.json"
            source_text = source.read_text(encoding="utf-8")
            source.write_text(
                source_text.replace('"file_count": 33', '"file_count": 32'),
                encoding="utf-8",
            )
            with self.assertRaises(subprocess.CalledProcessError):
                generate(contracts, root / "invalid-count.h")
            source.write_text(source_text, encoding="utf-8")

            atom = contracts / "unicode-atom-record.json"
            atom.write_bytes(atom.read_bytes() + b"\n")
            changed = generate(contracts, root / "changed.h")
            self.assertNotEqual(first, changed)

            physicality = contracts / "unicode-atomic-physicality.json"
            physicality.write_bytes(physicality.read_bytes() + b"\n")
            changed_physicality = generate(
                contracts, root / "changed-physicality.h"
            )
            self.assertNotEqual(changed, changed_physicality)


if __name__ == "__main__":
    unittest.main()
