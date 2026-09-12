#!/usr/bin/env python3
"""Regression coverage for the installed source-admission estate/runtime boundary."""
from __future__ import annotations

import importlib.util
import json
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


class SourceAdmissionUnpublishedPackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.guard = load_guard(GUARD_PATH)

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.receipts = self.root / "receipts"
        (self.receipts / "cluster-activation").mkdir(parents=True)
        (self.receipts / "unicode").mkdir()
        self.tool_root = self.root / "branch-release"
        self.tool_root.mkdir()
        self.active = self.root / "missing-current"
        self.live = {
            "activation_epoch_id": "11" * 16,
            "activation_epoch_fingerprint": "22" * 32,
            "geometry_epoch": "33" * 32,
            "perfcache_epoch": "22" * 32,
            "numeric_epoch": "44" * 32,
        }

    def tearDown(self) -> None:
        self.temp.cleanup()

    def add_generation(
        self,
        package_byte: str,
        request_byte: str,
        *,
        geometry_epoch: str | None = None,
        numeric_epoch: str | None = None,
    ) -> Path:
        package = package_byte * 64
        request = request_byte * 64
        generation = self.receipts / "cluster-activation" / package
        generation.mkdir()
        (generation / "unicode-product-activation.json").write_text(
            json.dumps(
                {
                    "schema": "laplace.unicode-product-activation-receipt/v1",
                    "phase": "product-activated",
                    "package_id": package,
                    "request_fingerprint": request,
                    "activation_epoch_id": self.live["activation_epoch_id"],
                    "activation_epoch_fingerprint": self.live[
                        "activation_epoch_fingerprint"
                    ],
                }
            ),
            encoding="utf-8",
        )
        identities = self.receipts / "unicode" / request
        identities.mkdir()
        (identities / "identities.json").write_text(
            json.dumps(
                {
                    "schema": "laplace.unicode-activation-identities/v1",
                    "request_fingerprint": request,
                    "geometry_epoch": geometry_epoch or self.live["geometry_epoch"],
                    "perfcache_epoch": self.live["perfcache_epoch"],
                    "numeric_epoch": numeric_epoch or self.live["numeric_epoch"],
                }
            ),
            encoding="utf-8",
        )
        return generation

    def argv(self) -> list[str]:
        return [
            "laplace-admit-source",
            "iso-639-3-20260415",
            "/vault/Data/ISO639",
            "--tool-root",
            str(self.tool_root),
            "--receipt-root",
            str(self.receipts),
        ]

    def test_binds_unique_live_activation_for_prepublication_package_proof(self) -> None:
        generation = self.add_generation("a", "b")
        values = self.guard.bind_unpublished_runtime(
            self.argv(), active_path=self.active, live_state=self.live
        )
        self.assertEqual(values[-2:], ["--active", str(generation.resolve())])

    def test_rejects_ambiguous_live_activation_receipts(self) -> None:
        self.add_generation("a", "b")
        self.add_generation("c", "d")
        with self.assertRaisesRegex(
            self.guard.AdmissionGuardError, "exactly one activation receipt"
        ):
            self.guard.bind_unpublished_runtime(
                self.argv(), active_path=self.active, live_state=self.live
            )

    def test_ignores_nonmatching_activation_receipt(self) -> None:
        generation = self.add_generation("a", "b")
        self.add_generation("c", "d", numeric_epoch="55" * 32)
        values = self.guard.bind_unpublished_runtime(
            self.argv(), active_path=self.active, live_state=self.live
        )
        self.assertEqual(values[-2:], ["--active", str(generation.resolve())])

    def test_explicit_active_selector_is_never_rewritten(self) -> None:
        explicit = self.root / ("e" * 64)
        explicit.mkdir()
        values = self.argv() + ["--active", str(explicit)]
        self.assertEqual(
            self.guard.bind_unpublished_runtime(
                values, active_path=self.active, live_state=self.live
            ),
            values,
        )

    def test_ordinary_use_without_tool_root_stays_fail_closed(self) -> None:
        values = [
            "laplace-admit-source",
            "iso-639-3-20260415",
            "/vault/Data/ISO639",
        ]
        self.assertEqual(
            self.guard.bind_unpublished_runtime(
                values, active_path=self.active, live_state=self.live
            ),
            values,
        )

    def test_broken_active_selector_is_not_bypassed(self) -> None:
        broken = self.root / "current"
        try:
            broken.symlink_to(self.root / "missing-release", target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symlink unavailable: {error}")
        values = self.argv()
        self.assertEqual(
            self.guard.bind_unpublished_runtime(
                values, active_path=broken, live_state=self.live
            ),
            values,
        )


if __name__ == "__main__":
    unittest.main()
