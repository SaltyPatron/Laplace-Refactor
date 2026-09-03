#!/usr/bin/env python3

from __future__ import annotations

import copy
import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


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

    def receipt_store_fixture(self, root: Path) -> tuple[Path, Path, Path, bytes]:
        store = root / "laplace_receipt_store"
        store.write_bytes(b"native-store-placeholder")
        store.chmod(0o700)
        receipt = root / "receipt.json"
        receipt_bytes = b'{"schema":"laplace.test-receipt/v1"}\n'
        receipt.write_bytes(receipt_bytes)
        durable_root = root / "durable"
        durable_root.mkdir()
        return store, receipt, durable_root, receipt_bytes

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

    def test_manifest_source_accepts_exact_checked_out_identity(self) -> None:
        commit = "a" * 40
        tree = "b" * 40
        manifest = {
            "laplace": {
                "repository_commit": commit,
                "repository_tree": tree,
            }
        }
        proof.validate_manifest_source(manifest, commit, tree)

    def test_manifest_source_rejects_stale_or_wrong_identity(self) -> None:
        commit = "a" * 40
        tree = "b" * 40
        manifest = {
            "laplace": {
                "repository_commit": commit,
                "repository_tree": tree,
            }
        }
        mutations = (
            {"repository_commit": "c" * 40, "repository_tree": tree},
            {"repository_commit": commit, "repository_tree": "d" * 40},
            {"repository_commit": commit},
            {},
        )
        for laplace in mutations:
            with self.subTest(laplace=laplace), self.assertRaisesRegex(
                proof.PackageProductProofError, "checked-out source identity"
            ):
                proof.validate_manifest_source({"laplace": laplace}, commit, tree)

    def test_manifest_source_rejects_non_git_object_expectation(self) -> None:
        manifest = {
            "laplace": {
                "repository_commit": "a" * 40,
                "repository_tree": "b" * 40,
            }
        }
        with self.assertRaisesRegex(proof.PackageProductProofError, "Git object"):
            proof.validate_manifest_source(manifest, "a" * 39, "b" * 40)

    def test_store_receipt_fetches_exact_published_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            store, receipt, durable_root, receipt_bytes = self.receipt_store_fixture(
                Path(temporary)
            )
            digest = "e" * 64
            with mock.patch.object(proof, "run") as run, mock.patch.object(
                proof, "run_bytes"
            ) as run_bytes:
                run.side_effect = [
                    mock.Mock(stdout=f"{digest}\n"),
                    mock.Mock(stdout=""),
                ]
                run_bytes.return_value = mock.Mock(stdout=receipt_bytes)
                self.assertEqual(
                    proof.store_receipt(store, receipt, durable_root), digest
                )

            self.assertEqual(run.call_count, 2)
            self.assertEqual(
                run.call_args_list[0].args[0],
                [
                    str(store),
                    "put",
                    "--receipt",
                    str(receipt),
                    "--root",
                    str(durable_root),
                ],
            )
            self.assertEqual(
                run.call_args_list[1].args[0],
                [
                    str(store),
                    "verify",
                    "--digest",
                    digest,
                    "--root",
                    str(durable_root),
                ],
            )
            self.assertEqual(
                run_bytes.call_args.args[0],
                [
                    str(store),
                    "get",
                    "--digest",
                    digest,
                    "--root",
                    str(durable_root),
                ],
            )

    def test_store_receipt_rejects_successful_wrong_byte_retrieval(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            store, receipt, durable_root, _ = self.receipt_store_fixture(Path(temporary))
            digest = "f" * 64
            with mock.patch.object(proof, "run") as run, mock.patch.object(
                proof, "run_bytes"
            ) as run_bytes:
                run.side_effect = [
                    mock.Mock(stdout=f"{digest}\n"),
                    mock.Mock(stdout=""),
                ]
                run_bytes.return_value = mock.Mock(stdout=b"wrong retained bytes\n")
                with self.assertRaisesRegex(
                    proof.PackageProductProofError, "retrieval differs"
                ):
                    proof.store_receipt(store, receipt, durable_root)

    def test_command_failure_retains_inner_and_outer_output(self) -> None:
        completed = mock.Mock(
            returncode=1,
            stdout="inner sandbox build failure\n",
            stderr="outer package wrapper failure\n",
        )
        with mock.patch.object(proof.subprocess, "run", return_value=completed):
            with self.assertRaises(proof.PackageProductProofError) as caught:
                proof.run(["/usr/bin/false"], "product package composition")
        message = str(caught.exception)
        self.assertIn("outer package wrapper failure", message)
        self.assertIn("inner sandbox build failure", message)

    def test_binding_canonicalization_is_stable(self) -> None:
        value = {"b": 2, "a": 1}
        expected = b'{"a":1,"b":2}\n'
        self.assertEqual(proof.canonical_bytes(value), expected)
        self.assertEqual(
            hashlib.sha256(expected).hexdigest(),
            hashlib.sha256(proof.canonical_bytes(value)).hexdigest(),
        )

    def test_installer_reconfigure_uses_proved_package_postgresql(self) -> None:
        workflow = (REPOSITORY / ".github/workflows/package-product.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("physical_release=$(jq -er '.physical_root'", workflow)
        self.assertIn('pg_config="$physical_release/pgsql-18/bin/pg_config"', workflow)
        self.assertIn('-DLAPLACE_PG_CONFIG="$pg_config"', workflow)
        self.assertIn('-DLAPLACE_PG_PHYSICAL_ROOT="$pg_physical_root"', workflow)
        self.assertNotIn('-DLAPLACE_PG_CONFIG=/opt/laplace/pgsql-18/bin/pg_config', workflow)


if __name__ == "__main__":
    unittest.main()
