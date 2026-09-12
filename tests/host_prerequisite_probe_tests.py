#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "host_prerequisite_probe", ROOT / "tools/delivery/host_prerequisite_probe.py"
)
assert SPEC is not None and SPEC.loader is not None
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


class HostPrerequisiteProbeTests(unittest.TestCase):
    def test_present_file_is_not_reported_missing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "receipt.json"
            path.write_text("{}\n", encoding="utf-8")
            result = probe.describe(path)
            self.assertEqual(result["state"], "present")
            self.assertEqual(result["kind"], "file")
            self.assertTrue(result["readable"])

    def test_absent_file_reports_missing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = probe.describe(Path(directory) / "absent.json")
            self.assertEqual(result["state"], "missing")

    def test_chain_stops_at_first_absent_component(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "missing" / "receipt.json"
            result = probe.chain(path)
            self.assertEqual(result[-1]["state"], "missing")
            self.assertTrue(result[-1]["path"].endswith("/missing"))


if __name__ == "__main__":
    unittest.main()
