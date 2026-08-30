#!/usr/bin/env python3

from __future__ import annotations

import copy
import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/product/prove-package.py"
SPEC = importlib.util.spec_from_file_location("laplace_package_product_proof_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load package-product proof module")
proof = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = proof
SPEC.loader.exec_module(proof)


class PackageProductProofTests(unittest.TestCase):
    def installation_fixture(self, root: Path) -> tuple[dict, dict, str, Path, Path]:
        package_id = "a" * 64
        logical_root = f"/opt/laplace/releases/{package_id}"
        manifest = {"package_id": package_id, "root": logical_root}
        manifest_sha = "b" * 64
        source_root = root / "source"
        source_root.mkdir()
        installation_root = root / "install"
        installation_root.mkdir()
        installed_release = proof.prefixed(installation_root, logical_root)
        installed_release.mkdir(parents=True)
        installation = {
            "schema": proof.INSTALLATION_SCHEMA,
            "phase": "installed",
            "package_id": package_id,
            "package_manifest_sha256": manifest_sha,
            "package_root": logical_root,
            "installation_root": str(installation_root),
            "installed_release": str(installed_release),
            "source_physical_root": str(source_root.resolve()),
            "file_count": 1,
            "symlink_count": 0,
            "total_file_bytes": 3,
            "source_package_verified": True,
            "installed_package_verified": True,
            "overwrite_performed": False,
        }
        installation["installation_receipt_sha256"] = proof.document_identity(
            installation, "installation_receipt_sha256"
        )
        return installation, manifest, manifest_sha, source_root, installation_root

    def test_installation_receipt_accepts_exact_verified_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.installation_fixture(Path(temporary))
            proof.validate_installation(*fixture)

    def test_installation_receipt_rejects_semantic_mutations(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            installation, manifest, manifest_sha, source, root = self.installation_fixture(
                Path(temporary)
            )
            for field, value in (
                ("source_package_verified", False),
                ("installed_package_verified", False),
                ("overwrite_performed", True),
                ("package_id", "c" * 64),
            ):
                with self.subTest(field=field):
                    mutated = copy.deepcopy(installation)
                    mutated[field] = value
                    mutated["installation_receipt_sha256"] = proof.document_identity(
                        mutated, "installation_receipt_sha256"
                    )
                    with self.assertRaises(proof.PackageProductProofError):
                        proof.validate_installation(
                            mutated, manifest, manifest_sha, source, root
                        )

    def test_installation_receipt_rejects_identity_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            installation, manifest, manifest_sha, source, root = self.installation_fixture(
                Path(temporary)
            )
            installation["installation_receipt_sha256"] = "d" * 64
            with self.assertRaises(proof.PackageProductProofError):
                proof.validate_installation(
                    installation, manifest, manifest_sha, source, root
                )

    def test_binding_canonicalization_is_stable(self) -> None:
        value = {"b": 2, "a": 1}
        expected = b'{"a":1,"b":2}\n'
        self.assertEqual(proof.canonical_bytes(value), expected)
        self.assertEqual(
            hashlib.sha256(expected).hexdigest(),
            hashlib.sha256(proof.canonical_bytes(value)).hexdigest(),
        )


if __name__ == "__main__":
    unittest.main()
