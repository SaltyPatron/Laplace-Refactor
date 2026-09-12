#!/usr/bin/env python3
"""Regression coverage for the installed source-admission estate boundary."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys

if len(sys.argv) != 2:
    raise RuntimeError("expected path to admit_source_guard.py")
GUARD_PATH = Path(sys.argv[1])
sys.argv[:] = sys.argv[:1]


def load_guard(path: Path):
    spec = importlib.util.spec_from_file_location("laplace_admission_guard", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load guard module: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SourceAdmissionEstateBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.guard = load_guard(GUARD_PATH)

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.estate = self.root / "estate"
        self.source = self.estate / "ISO639"
        self.unicode = self.estate / "UCD/Public/UCD/latest"
        self.outside = self.root / "outside"
        self.source.mkdir(parents=True)
        self.unicode.mkdir(parents=True)
        self.outside.mkdir()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_accepts_and_canonicalizes_directories_inside_estate(self) -> None:
        argv = ["laplace-admit-source", "iso-639-3-20260415", str(self.source)]
        guarded = self.guard.guarded_arguments(argv, self.estate)
        self.assertEqual(guarded[2], str(self.source.resolve()))
        self.assertEqual(
            guarded[-2:], ["--unicode-root", str(self.unicode.resolve())]
        )

    def test_rejects_source_root_outside_estate(self) -> None:
        argv = ["laplace-admit-source", "iso-639-3-20260415", str(self.outside)]
        with self.assertRaises(self.guard.AdmissionGuardError):
            self.guard.guarded_arguments(argv, self.estate)

    def test_rejects_explicit_unicode_root_outside_estate(self) -> None:
        argv = [
            "laplace-admit-source",
            "iso-639-3-20260415",
            str(self.source),
            "--unicode-root",
            str(self.outside),
        ]
        with self.assertRaises(self.guard.AdmissionGuardError):
            self.guard.guarded_arguments(argv, self.estate)

    def test_rejects_symlink_escape(self) -> None:
        escape = self.estate / "escape"
        try:
            escape.symlink_to(self.outside, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symlink unavailable: {error}")
        argv = ["laplace-admit-source", "iso-639-3-20260415", str(escape)]
        with self.assertRaises(self.guard.AdmissionGuardError):
            self.guard.guarded_arguments(argv, self.estate)

    def test_rejects_relative_source_path(self) -> None:
        argv = ["laplace-admit-source", "iso-639-3-20260415", "ISO639"]
        with self.assertRaises(self.guard.AdmissionGuardError):
            self.guard.guarded_arguments(argv, self.estate)


if __name__ == "__main__":
    unittest.main()
