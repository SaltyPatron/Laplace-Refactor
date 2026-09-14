#!/usr/bin/env python3
"""Recovery law for a committed Unicode root whose canonical admission leaf was lost."""
from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "retained_unicode_recovery_tests", ROOT / "tools/postgresql/unicodectl.py"
)
assert spec is not None and spec.loader is not None
u = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = u
spec.loader.exec_module(u)


class RetainedUnicodeAdmissionRecovery(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.contract = u.load_json(ROOT / "contracts/unicode-product-activation.json")
        self.identity = {"schema": u.IDENTITY_SCHEMA}
        for index, (name, settings) in enumerate(
            self.contract["identity_provider"]["fields"].items(), 1
        ):
            self.identity[name] = f"{index:02x}" * settings["bytes"]

        self.original = {
            key: key
            for key in (
                "schema",
                "activation_contract_sha256",
                "cluster_contract_sha256",
                "cluster_system_identifier",
                "source_contract_sha256",
                "source_evidence_sha256",
                "source_root",
                "unicode_postgresql_contract_sha256",
                "operation",
                "execution_context",
                "expected_result",
            )
        }
        self.original.update(
            package_id="old",
            orchestrator_sha256="old-code",
            cluster_plan_sha256="old-plan",
        )
        self.current = dict(
            self.original,
            package_id="new",
            orchestrator_sha256="new-code",
            cluster_plan_sha256="new-plan",
        )
        self.inspection = {
            name: self.identity[name]
            for name in ("activation_epoch_id", "activation_epoch_fingerprint")
        }
        self.calls: list[Path] = []

        def native(_executable: Path, path: Path, _contract: dict) -> tuple[dict, dict]:
            self.calls.append(path)
            self.assertEqual(path.read_bytes(), u.canonical_bytes(self.original))
            return copy.deepcopy(self.identity), {
                "exit_code": 0,
                "label": "native-retained-request",
            }

        self.native = native

    def _canonical_folder(self) -> Path:
        return (
            self.root
            / self.contract["receipt"]["directory_name"]
            / self.identity["request_fingerprint"]
        )

    def _write_canonical_admission(self) -> Path:
        folder = self._canonical_folder()
        folder.mkdir(parents=True)
        u.write_immutable(folder / "request.json", self.original)
        u.write_immutable(folder / "identities.json", self.identity)
        return folder

    def _write_staged_request(self, value: dict | None = None) -> Path:
        value = self.original if value is None else value
        content = u.canonical_bytes(value)
        directory = self.root / ".unicode-activation"
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / f"request-{u.sha256_bytes(content)}.json"
        path.write_bytes(content)
        return path

    def execute(self):
        return u.retained_root_identities(
            self.root,
            self.inspection,
            self.current,
            self.contract,
            Path("/verified/native-identify"),
            self.native,
        )

    def test_package_only_cluster_contract_churn_does_not_relabel_unicode(self) -> None:
        self._write_canonical_admission()
        self.current["cluster_contract_sha256"] = "new-package-contract"
        identity, request, command = self.execute()
        self.assertEqual(identity, self.identity)
        self.assertEqual(request, self.original)
        self.assertEqual(command["label"], "native-retained-request")

    def test_missing_canonical_leaf_is_recovered_from_exact_staged_request(self) -> None:
        staged = self._write_staged_request()
        identity, request, command = self.execute()
        self.assertEqual(identity, self.identity)
        self.assertEqual(request, self.original)
        self.assertEqual(command["label"], "native-retained-request")
        folder = self._canonical_folder()
        self.assertEqual((folder / "request.json").read_bytes(), u.canonical_bytes(self.original))
        self.assertEqual((folder / "identities.json").read_bytes(), u.canonical_bytes(self.identity))
        self.assertEqual(self.calls, [staged])

    def test_staged_request_name_must_bind_exact_canonical_bytes(self) -> None:
        content = u.canonical_bytes(self.original)
        directory = self.root / ".unicode-activation"
        directory.mkdir(parents=True)
        (directory / ("request-" + "ff" * 32 + ".json")).write_bytes(content)
        with self.assertRaisesRegex(u.UnicodeActivationError, "staged Unicode request digest differs"):
            self.execute()
        self.assertEqual(self.calls, [])

    def test_unrelated_staged_requests_are_not_relabelled_as_the_committed_root(self) -> None:
        unrelated = dict(self.original, source_evidence_sha256="other-source")
        self._write_staged_request(unrelated)
        with self.assertRaisesRegex(u.UnicodeActivationError, "one exact retained admission"):
            self.execute()
        self.assertEqual(self.calls, [])

    def test_candidate_admission_is_published_only_after_committed_epoch_selection(self) -> None:
        source = (ROOT / "tools/postgresql/unicodectl.py").read_text(encoding="utf-8")
        inspect_at = source.index('"inspect-unicode-product-state"')
        publish_at = source.index('write_immutable(evidence_directory / "request.json"')
        self.assertLess(inspect_at, publish_at)


if __name__ == "__main__":
    unittest.main(verbosity=2)
