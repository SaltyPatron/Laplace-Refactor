#!/usr/bin/env python3
"""Generated highway parity, append-only, and deliberate-defect acceptance."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools/contracts/generate-highway.py"
CONTRACT = ROOT / "contracts/highway.json"


def run(contract: Path, output: Path, previous: Path | None = None) -> subprocess.CompletedProcess[str]:
    command = [
        sys.executable,
        str(GENERATOR),
        "--contract",
        str(contract),
        "--output-root",
        str(output),
    ]
    if previous is not None:
        command.extend(["--previous-contract", str(previous)])
    return subprocess.run(command, text=True, capture_output=True, check=False)


class HighwayGenerationTests(unittest.TestCase):
    def test_all_mirrors_are_reproducible_and_bound_to_one_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-highway-") as raw:
            root = Path(raw)
            first = root / "first"
            replay = root / "replay"
            self.assertEqual(run(CONTRACT, first).returncode, 0)
            self.assertEqual(run(CONTRACT, replay).returncode, 0)
            first_files = {
                path.relative_to(first): path.read_bytes()
                for path in first.rglob("*") if path.is_file()
            }
            replay_files = {
                path.relative_to(replay): path.read_bytes()
                for path in replay.rglob("*") if path.is_file()
            }
            self.assertEqual(first_files, replay_files)
            self.assertEqual(
                set(first_files),
                {
                    Path("laplace/contract/highway.h"),
                    Path("postgresql/highway.sql"),
                    Path("sql/highway.sql"),
                    Path("csharp/HighwayContract.g.cs"),
                    Path("documentation/highway.md"),
                    Path("diagnostics/highway.json"),
                    Path("perfcache/highway-registry.json"),
                    Path("manifest.json"),
                },
            )
            manifest = json.loads(first_files[Path("manifest.json")])
            fingerprint = manifest["semantic_fingerprint"]
            for relative in (
                "laplace/contract/highway.h",
                "postgresql/highway.sql",
                "sql/highway.sql",
                "csharp/HighwayContract.g.cs",
                "documentation/highway.md",
                "diagnostics/highway.json",
                "perfcache/highway-registry.json",
            ):
                self.assertIn(fingerprint.encode(), first_files[Path(relative)])
            verify = subprocess.run(
                [sys.executable, str(GENERATOR), "--contract", str(CONTRACT),
                 "--verify-root", str(first)],
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(verify.returncode, 0, verify.stderr)
            (first / "sql/highway.sql").write_bytes(b"drift")
            drift = subprocess.run(
                [sys.executable, str(GENERATOR), "--contract", str(CONTRACT),
                 "--verify-root", str(first)],
                text=True, capture_output=True, check=False,
            )
            self.assertNotEqual(drift.returncode, 0)
            self.assertIn("mirror differs", drift.stderr)

    def test_registry_defects_and_history_rewrites_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-highway-mutants-") as raw:
            root = Path(raw)
            original = json.loads(CONTRACT.read_text(encoding="utf-8"))

            def rejected(name: str, value: dict, message: str, previous: Path | None = None) -> None:
                path = root / f"{name}.json"
                path.write_text(json.dumps(value), encoding="utf-8")
                result = run(path, root / f"out-{name}", previous)
                self.assertNotEqual(result.returncode, 0, name)
                self.assertIn(message, result.stderr)

            mutant = json.loads(json.dumps(original))
            mutant["kinds"][0]["id"] = 0
            rejected("zero", mutant, "identifiers are not append-only")

            mutant = json.loads(json.dumps(original))
            mutant["mirrors"].append("native")
            rejected("duplicate-mirror", mutant, "mirrors are incomplete or repeated")

            duplicate = CONTRACT.read_text(encoding="utf-8").replace(
                '"version": 1,', '"version": 1, "version": 1,', 1
            )
            duplicate_path = root / "duplicate.json"
            duplicate_path.write_text(duplicate, encoding="utf-8")
            result = run(duplicate_path, root / "out-duplicate")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("duplicate JSON key", result.stderr)

            previous = root / "previous.json"
            shutil.copy2(CONTRACT, previous)
            mutant = json.loads(json.dumps(original))
            mutant["version"] = 2
            for row in mutant["kinds"]:
                row["id"] += 20
            rejected("renumber", mutant, "renumbered or reminted", previous)

            mutant = json.loads(json.dumps(original))
            mutant["version"] = 2
            mutant["kinds"] = mutant["kinds"][:-1]
            rejected("removed", mutant, "history was removed", previous)

            mutant = json.loads(json.dumps(original))
            mutant["version"] = 2
            mutant["kinds"][0]["aliases"] = [
                {"name": "grammar", "introduced": 1, "retired": 0}
            ]
            rejected("backdated-alias", mutant, "backdated", previous)


if __name__ == "__main__":
    unittest.main()
