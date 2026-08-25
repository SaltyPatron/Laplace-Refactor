#!/usr/bin/env python3
"""Contract and deliberate-defect tests for selected installed providers."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools/dependencies/verify-installed-lock.py"
SPEC = importlib.util.spec_from_file_location("verify_installed_lock", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
LOCK = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = LOCK
SPEC.loader.exec_module(LOCK)


class InstalledProviderLockTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.lock_path = self.root / "installed-lock.json"
        shutil.copy2(REPO_ROOT / "dependencies/installed-lock.json", self.lock_path)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def read(self) -> dict[str, object]:
        return json.loads(self.lock_path.read_text(encoding="utf-8"))

    def write(self, document: dict[str, object], refresh_global: bool = False) -> None:
        if refresh_global:
            payload = json.dumps(
                document["providers"],
                ensure_ascii=False,
                sort_keys=True,
                separators=(",", ":"),
            ).encode("utf-8")
            document["provider_selection_sha256"] = hashlib.sha256(payload).hexdigest()
        self.lock_path.write_text(json.dumps(document), encoding="utf-8")

    def test_contract_validates_without_installed_assets(self) -> None:
        report = LOCK.validate_lock(LOCK.load_lock(self.lock_path))
        self.assertEqual(report["file_count"], 43)
        self.assertEqual(len(report["providers"]), 3)

    def test_actual_installed_bytes_match_when_available(self) -> None:
        if not Path("/opt/intel/oneapi/compiler/2026.1").is_dir():
            self.skipTest("selected oneAPI installation is unavailable")
        report = LOCK.validate_lock(LOCK.load_lock(self.lock_path))
        LOCK.verify_files(report, Path("/usr/bin/readelf"), Path("/usr/bin/dpkg-query"))

    def test_version_drift_is_rejected(self) -> None:
        document = self.read()
        document["providers"]["onemkl"]["version"] = "2026.0.0"
        self.write(document, refresh_global=True)
        with self.assertRaisesRegex(LOCK.InstalledLockError, "onemkl version changed"):
            LOCK.validate_lock(LOCK.load_lock(self.lock_path))

    def test_runtime_object_hash_drift_is_rejected(self) -> None:
        document = self.read()
        document["providers"]["onetbb"]["files"][0]["sha256"] = "0" * 64
        self.write(document, refresh_global=True)
        with self.assertRaisesRegex(LOCK.InstalledLockError, "onetbb exact selected provider identity differs"):
            LOCK.validate_lock(LOCK.load_lock(self.lock_path))

    def test_runtime_object_omission_is_rejected(self) -> None:
        document = self.read()
        provider = document["providers"]["intel-oneapi-runtime"]
        provider["files"] = [
            entry for entry in provider["files"] if entry["role"] != "svml-runtime"
        ]
        provider["cmake_selection"]["targets"]["intel-math-runtime"].remove("svml-runtime")
        self.write(document, refresh_global=True)
        with self.assertRaisesRegex(LOCK.InstalledLockError, "exact selected provider identity differs"):
            LOCK.validate_lock(LOCK.load_lock(self.lock_path))

    def test_header_omission_is_rejected(self) -> None:
        document = self.read()
        provider = document["providers"]["onemkl"]
        provider["files"] = [
            entry for entry in provider["files"] if entry["role"] != "mkl-vml-functions-header"
        ]
        self.write(document, refresh_global=True)
        with self.assertRaisesRegex(LOCK.InstalledLockError, "exact selected provider identity differs"):
            LOCK.validate_lock(LOCK.load_lock(self.lock_path))

    def test_header_hash_drift_is_rejected(self) -> None:
        document = self.read()
        provider = document["providers"]["onemkl"]
        header = next(
            entry for entry in provider["files"]
            if entry["role"] == "mkl-root-header"
        )
        header["sha256"] = "f" * 64
        self.write(document, refresh_global=True)
        with self.assertRaisesRegex(LOCK.InstalledLockError, "exact selected provider identity differs"):
            LOCK.validate_lock(LOCK.load_lock(self.lock_path))

    def test_ambient_latest_root_is_rejected(self) -> None:
        document = self.read()
        document["providers"]["onetbb"]["immutable_root"] = "/opt/intel/oneapi/tbb/latest"
        self.write(document, refresh_global=True)
        with self.assertRaisesRegex(LOCK.InstalledLockError, "immutable root changed"):
            LOCK.validate_lock(LOCK.load_lock(self.lock_path))

    def test_selection_cannot_claim_runtime_activation(self) -> None:
        document = self.read()
        document["authority_boundaries"]["selection-activates-product-runtime"] = True
        self.write(document)
        with self.assertRaisesRegex(LOCK.InstalledLockError, "runtime activation"):
            LOCK.validate_lock(LOCK.load_lock(self.lock_path))

    def test_cmake_emission_requires_installed_byte_verification(self) -> None:
        output = self.root / "selected.cmake"
        result = subprocess.run(
            [sys.executable, str(MODULE_PATH), str(self.lock_path), "--cmake-output", str(output)],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires exact installed-file verification", result.stderr)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
