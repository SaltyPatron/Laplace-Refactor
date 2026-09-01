#!/usr/bin/env python3
"""Acceptance and deliberate-defect tests for standalone product distributions."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import tarfile
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/product_distribution.py"
SPEC = importlib.util.spec_from_file_location("laplace_product_distribution_tests", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
distribution = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(distribution)


class ProductDistributionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-product-distribution-")
        self.root = Path(self.temporary.name)
        self.source_root = self.root / "source-root"
        self.build = self.root / "build"
        self.output = self.root / "output"
        self.unicode_source = self.root / "unicode-source"
        self.unicode_source.mkdir()
        unicode_content = b"Unicode fixture 1.0\n"
        (self.unicode_source / "UnicodeData.txt").write_bytes(unicode_content)
        self.unicode_contract = self.root / "unicode-source.json"
        self.unicode_contract.write_text(
            json.dumps(
                {
                    "schema": "laplace.unicode-source-contract/v1",
                    "unicode_version": "1.0.0",
                    "file_count": 1,
                    "authority": {
                        "local_verified_reference_root": str(self.unicode_source)
                    },
                    "files": [
                        {
                            "path": "UnicodeData.txt",
                            "bytes": len(unicode_content),
                            "sha256": hashlib.sha256(unicode_content).hexdigest(),
                            "role": "fixture",
                        }
                    ],
                },
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        files = []
        for relative, content, mode in (
            ("pgsql-18/bin/postgres", b"postgres fixture\n", 0o755),
            ("pgsql-18/bin/pg_config", b"pg_config fixture\n", 0o755),
            ("bin/laplace_resource_observe", b"observer fixture\n", 0o755),
        ):
            files.append(
                {
                    "path": relative,
                    "kind": "file",
                    "sha256": hashlib.sha256(content).hexdigest(),
                    "mode": mode,
                    "runpath": [],
                }
            )
        files.append(
            {
                "path": "pgsql-18/bin/postmaster",
                "kind": "symlink",
                "target": "postgres",
                "sha256": hashlib.sha256(b"postgres").hexdigest(),
                "runpath": [],
            }
        )
        self.manifest = {
            "schema": distribution.PACKAGE_SCHEMA,
            "postgresql": {"version": "18.6", "pg_config": "pgsql-18/bin/pg_config"},
            "capabilities": {"exact_loaded_object_identity": 1},
            "loader_environment": {},
            "files": files,
            "loaded_objects": ["pgsql-18/bin/postgres"],
            "activation_eligible": True,
            "activation_gates": {"fixture": True},
            "provenance": {"repository_commit": "1" * 40},
        }
        package_id = distribution.package_identity(self.manifest)
        self.manifest["package_id"] = package_id
        self.manifest["root"] = f"/opt/laplace/releases/{package_id}"
        release = self.source_root / self.manifest["root"].lstrip("/")
        for entry in files:
            target = release / entry["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            if entry["kind"] == "symlink":
                target.symlink_to(entry["target"])
            else:
                content = (
                    b"postgres fixture\n"
                    if entry["path"].endswith("postgres")
                    else b"pg_config fixture\n"
                    if entry["path"].endswith("pg_config")
                    else b"observer fixture\n"
                )
                target.write_bytes(content)
                target.chmod(entry["mode"])
        self.build.mkdir()
        manifest_path = self.build / "package-manifest.json"
        manifest_path.write_bytes(distribution.canonical_bytes(self.manifest))
        self.receipt_path = self.build / "package-receipt.json"
        receipt = {
            "schema": distribution.RECEIPT_SCHEMA,
            "package_id": package_id,
            "manifest": str(manifest_path),
            "manifest_sha256": distribution.sha256_file(manifest_path),
            "physical_root": str(release),
        }
        self.receipt_path.write_bytes(distribution.canonical_bytes(receipt))

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def build_bundle(self) -> tuple[dict[str, object], Path]:
        result = distribution.build_bundle(
            REPOSITORY,
            self.receipt_path,
            self.output,
            self.unicode_contract,
            self.unicode_source,
        )
        bundle = self.output / f"laplace-installer-{result['bundle_id']}"
        return result, bundle

    def test_bundle_has_standalone_entrypoint_control_and_exact_payload(self) -> None:
        result, bundle = self.build_bundle()
        verified = distribution.verify_bundle(bundle / "installer-manifest.json")
        self.assertEqual(verified, result)
        self.assertTrue(os.access(bundle / "install", os.X_OK))
        install = (bundle / "install").read_text(encoding="utf-8")
        self.assertIn("--distribution", install)
        self.assertNotIn(str(REPOSITORY), install)
        self.assertTrue((bundle / "control/tools/delivery/product_host.py").is_file())
        self.assertTrue(
            (
                bundle
                / "payload/root"
                / self.manifest["root"].lstrip("/")
                / "pgsql-18/bin/postgres"
            ).is_file()
        )

    def test_materialization_is_exact_and_replayable(self) -> None:
        result, bundle = self.build_bundle()
        first = distribution.materialize(bundle / "installer-manifest.json", self.root / "host")
        second = distribution.materialize(bundle / "installer-manifest.json", self.root / "host")
        self.assertFalse(first["replay"])
        self.assertTrue(second["replay"])
        self.assertEqual(first["package_id"], result["package"]["id"])
        receipt = self.root / "host" / first["product_receipt"].lstrip("/")
        self.assertTrue(receipt.is_file())
        source_root = self.root / "host" / first["package_source_root"].lstrip("/")
        distribution.verify_package_manifest(self.manifest, source_root)
        unicode_root = (
            self.root
            / "host/opt/laplace/sources/unicode/1.0.0/UnicodeData.txt"
        )
        self.assertTrue(unicode_root.is_file())

    def test_changed_control_file_is_detected(self) -> None:
        _result, bundle = self.build_bundle()
        target = bundle / "control/tools/delivery/product_host.py"
        target.chmod(0o755)
        target.write_bytes(target.read_bytes() + b"# changed\n")
        with self.assertRaisesRegex(distribution.DistributionError, "bytes differ"):
            distribution.verify_bundle(bundle / "installer-manifest.json")

    def test_changed_payload_is_detected(self) -> None:
        _result, bundle = self.build_bundle()
        target = (
            bundle
            / "payload/root"
            / self.manifest["root"].lstrip("/")
            / "pgsql-18/bin/postgres"
        )
        target.write_bytes(b"changed\n")
        with self.assertRaisesRegex(distribution.DistributionError, "bytes differ"):
            distribution.verify_bundle(bundle / "installer-manifest.json")

    def test_archive_is_deterministic_and_preserves_product_entrypoint(self) -> None:
        result, bundle = self.build_bundle()
        archive = self.root / "laplace-product-installer-fixture.tar"
        first = distribution.archive_bundle(bundle / "installer-manifest.json", archive)
        second = distribution.archive_bundle(bundle / "installer-manifest.json", archive)
        self.assertEqual(first, second)
        self.assertEqual(first["bundle_id"], result["bundle_id"])
        self.assertEqual(first["archive_sha256"], distribution.sha256_file(archive))
        with tarfile.open(archive, "r") as package:
            entrypoint = package.getmember(f"{bundle.name}/install")
            postmaster = package.getmember(
                f"{bundle.name}/payload/root/{self.manifest['root'].lstrip('/')}/"
                "pgsql-18/bin/postmaster"
            )
        self.assertEqual(stat.S_IMODE(entrypoint.mode), 0o555)
        self.assertTrue(postmaster.issym())
        self.assertEqual(postmaster.linkname, "postgres")

    def test_changed_archive_is_rejected_on_replay(self) -> None:
        _result, bundle = self.build_bundle()
        archive = self.root / "laplace-product-installer-fixture.tar"
        distribution.archive_bundle(bundle / "installer-manifest.json", archive)
        archive.chmod(0o644)
        archive.write_bytes(archive.read_bytes() + b"changed\n")
        with self.assertRaisesRegex(distribution.DistributionError, "archive bytes differ"):
            distribution.archive_bundle(bundle / "installer-manifest.json", archive)

    def test_cmake_and_ci_publish_the_verified_installer_target(self) -> None:
        cmake = (REPOSITORY / "CMakeLists.txt").read_text(encoding="utf-8")
        workflow = (
            REPOSITORY / ".github/workflows/package-product.yml"
        ).read_text(encoding="utf-8")
        self.assertIn("add_custom_target(laplace_product_installer", cmake)
        self.assertIn("--archive-output", cmake)
        self.assertIn("--target laplace_product_installer", workflow)
        self.assertIn("product_distribution.py verify", workflow)
        self.assertIn("actions/upload-artifact@", workflow)
        self.assertIn("compression-level: 0", workflow)
        self.assertIn("'.archive' \"$installer_selection\"", workflow)
        self.assertIn("LAPLACE_PRODUCT_INSTALLER_ARCHIVE_SHA256", workflow)


if __name__ == "__main__":
    unittest.main(verbosity=2)
