#!/usr/bin/env python3

from __future__ import annotations

import copy
import hashlib
import importlib.util
import os
from pathlib import Path
import sys
import subprocess
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
import repository_inputs as inputs


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

    def test_content_equivalent_package_retains_original_commit(self) -> None:
        manifest = {"laplace": {"repository_commit": "a" * 40,
                                "repository_tree": "b" * 40,
                                "repository_build_fingerprint": "c" * 64}}
        proof.validate_manifest_source(manifest, "d" * 40, "e" * 40, "c" * 64)
        self.assertEqual(manifest["laplace"]["repository_commit"], "a" * 40)
        with self.assertRaisesRegex(proof.PackageProductProofError, "build inputs differ"):
            proof.validate_manifest_source(manifest, "a" * 40, "b" * 40, "f" * 64)

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


    def test_installer_publication_requires_explicit_opt_in(self) -> None:
        workflow = (REPOSITORY / ".github/workflows/package-product.yml").read_text(
            encoding="utf-8"
        )
        for event in ("workflow_call", "workflow_dispatch"):
            self.assertRegex(workflow,
                rf"  {event}:\n    inputs:\n      publish_installer:\n"
                r"        description: [^\n]+\n        type: boolean\n        default: false")
        uploads = [step for step in workflow.split("\n      - ")
                   if "uses: actions/upload-artifact@" in step]
        self.assertEqual(len(uploads), 1)
        self.assertIn("        if: inputs.publish_installer\n", uploads[0])
        self.assertIn("if-no-files-found: error", uploads[0])
        self.assertIn("env.LAPLACE_PRODUCT_INSTALLER_ARCHIVE", uploads[0])
        self.assertNotIn("actions/download-artifact@", workflow)
        self.assertLess(workflow.index("--target laplace_product_installer"),
                        workflow.index("uses: actions/upload-artifact@"))
        self.assertIn('"$(sha256sum "$installer_archive"', workflow)

    def test_packaging_verifies_isolated_installation_without_shared_database_control(self) -> None:
        workflow = (REPOSITORY / ".github/workflows/package-product.yml").read_text(
            encoding="utf-8"
        )
        for command in ("tools/product/prove-package.py", "--work-root", "--output",
                        ".source_package_verified == true", ".installed_package_verified == true",
                        ".installation_replay_identical == true",
                        "tools/delivery/product_distribution.py verify"):
            self.assertIn(command, workflow)
        for shared_control in ("pg_ctl", "pg_isready", "contracts/postgresql-cluster.json",
                               "LAPLACE_PACKAGE_PRODUCT_STARTED_DATABASE"):
            self.assertNotIn(shared_control, workflow)
        # Local cleanup cannot be held behind a slow distribution upload.
        self.assertLess(workflow.index("name: Clean isolated installation workspace"),
                        workflow.index("uses: actions/upload-artifact@"))
        self.assertIn("name: Clean isolated installation workspace\n        if: always()", workflow)




class ImmutableRepositoryInputsTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="laplace-immutable-inputs-")
        self.addCleanup(temporary.cleanup)
        self.repository = Path(temporary.name)
        self.git("init", "--quiet")
        self.git("config", "user.name", "Repository Input Test")
        self.git("config", "user.email", "repository-inputs@example.invalid")
        self.git("config", "core.filemode", "true")
        (self.repository / ".github/workflows").mkdir(parents=True)
        (self.repository / ".github/workflows/check.yml").write_text("name: first\n")
        (self.repository / ".gitignore").write_text("ignored.tmp\n")
        (self.repository / "binary.dat").write_bytes(bytes(range(256)) + b"\0\n")
        (self.repository / "α source.txt").write_text("Unicode source: 猫\n", encoding="utf-8")
        (self.repository / "tab\tline\n.dat").write_bytes(b"header\n\0tail")
        (self.repository / "run.sh").write_bytes(b"#!/bin/sh\nexit 0\n")
        (self.repository / "run.sh").chmod(0o755)
        (self.repository / "source-link").symlink_to("α source.txt")
        self.first = self.commit("first")
        self.original = inputs.repository_build_fingerprint(self.repository)

    def git(self, *arguments: str, data: bytes | None = None) -> bytes:
        return subprocess.run(
            ["git", "-C", str(self.repository), *arguments],
            input=data, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=10,
        ).stdout

    def commit(self, message: str) -> str:
        self.git("add", "-A")
        self.git("commit", "--quiet", "--allow-empty", "-m", message)
        return self.git("rev-parse", "HEAD").decode("ascii").strip()

    def immutable(self, commit: str | None = None) -> str:
        return inputs.repository_build_fingerprint_at_commit(self.repository, commit or self.first)

    def state(self) -> tuple[bytes, bytes, bytes]:
        return (
            self.git("show-ref"),
            self.git("status", "--porcelain=v1", "-z", "--untracked-files=all"),
            (self.repository / ".git/index").read_bytes(),
        )

    def test_committed_binary_unicode_symlink_and_executable_equal_worktree_owner(self) -> None:
        before = self.state()
        self.assertEqual(self.immutable(), self.original)
        self.assertEqual(self.state(), before)

    def test_identical_tree_and_workflow_only_successors_preserve_original_package_provenance(self) -> None:
        successor = self.commit("another commit with identical content")
        self.assertNotEqual(successor, self.first)
        self.assertEqual(self.immutable(successor), self.original)
        (self.repository / ".github/workflows/check.yml").write_text("name: changed\n")
        workflow_commit = self.commit("workflow-only change")
        self.assertNotEqual(self.git("rev-parse", self.first + "^{tree}"),
                            self.git("rev-parse", workflow_commit + "^{tree}"))
        self.assertEqual(self.immutable(workflow_commit), self.original)
        self.assertEqual(inputs.repository_build_fingerprint(self.repository), self.original)
        manifest = {"laplace": {
            "repository_commit": self.first,
            "repository_tree": self.git("rev-parse", self.first + "^{tree}").decode().strip(),
            "repository_build_fingerprint": self.original,
        }}
        proof.validate_manifest_source(manifest, workflow_commit,
            self.git("rev-parse", workflow_commit + "^{tree}").decode().strip(),
            self.immutable(workflow_commit))
        self.assertEqual(manifest["laplace"]["repository_commit"], self.first)

    def test_substantive_bytes_and_executable_mode_change_identity(self) -> None:
        (self.repository / "binary.dat").write_bytes(b"different native input\0")
        changed = self.commit("substantive input")
        changed_identity = self.immutable(changed)
        self.assertNotEqual(changed_identity, self.original)
        self.assertEqual(changed_identity, inputs.repository_build_fingerprint(self.repository))
        (self.repository / "run.sh").chmod(0o644)
        mode_commit = self.commit("remove executable bit")
        self.assertNotEqual(self.immutable(mode_commit), changed_identity)
        self.assertEqual(self.immutable(mode_commit), inputs.repository_build_fingerprint(self.repository))
        self.assertEqual(self.immutable(), self.original)

    def test_dirty_untracked_ignored_and_deleted_worktree_do_not_change_requested_commit(self) -> None:
        (self.repository / "ignored.tmp").write_text("excluded by existing owner\n")
        self.assertEqual(inputs.repository_build_fingerprint(self.repository), self.original)
        (self.repository / "untracked.txt").write_text("included by worktree owner\n")
        untracked = inputs.repository_build_fingerprint(self.repository)
        self.assertNotEqual(untracked, self.original)
        (self.repository / "α source.txt").write_text("dirty tracked source\n")
        dirty = inputs.repository_build_fingerprint(self.repository)
        self.assertNotEqual(dirty, untracked)
        (self.repository / "binary.dat").unlink()
        deleted = inputs.repository_build_fingerprint(self.repository)
        self.assertNotEqual(deleted, dirty)
        before = self.state()
        self.assertEqual(self.immutable(), self.original)
        self.assertEqual(self.state(), before)
        self.assertEqual(inputs.repository_build_fingerprint(self.repository), deleted)

    def test_exact_commit_refuses_revisions_missing_objects_trees_and_blobs(self) -> None:
        for identity in (
            "HEAD", self.first[:12], self.first.upper(), "0" * 40, None,
            self.git("rev-parse", self.first + "^{tree}").decode().strip(),
            self.git("rev-parse", self.first + ":binary.dat").decode().strip(),
        ):
            with self.subTest(identity=identity), self.assertRaises(ValueError):
                inputs.repository_build_fingerprint_at_commit(self.repository, identity)

    def test_submodule_input_is_rejected_without_checkout_or_ref_mutation(self) -> None:
        self.git("update-index", "--add", "--cacheinfo", "160000," + self.first + ",vendor/module")
        self.git("commit", "--quiet", "-m", "gitlink input")
        commit = self.git("rev-parse", "HEAD").decode().strip()
        before = self.state()
        with self.assertRaisesRegex(ValueError, "Unsupported committed"):
            self.immutable(commit)
        self.assertEqual(self.state(), before)

    def test_unexpected_raw_tree_mode_is_rejected(self) -> None:
        blob = self.git("rev-parse", self.first + ":binary.dat").decode().strip()
        malformed_tree = self.git("hash-object", "-t", "tree", "--literally", "-w", "--stdin",
            data=b"100664 invalid-mode\0" + bytes.fromhex(blob)).decode().strip()
        commit_bytes = (
            "tree " + malformed_tree + "\n"
            "author Repository Input Test <repository-inputs@example.invalid> 1 +0000\n"
            "committer Repository Input Test <repository-inputs@example.invalid> 1 +0000\n"
            "\nunexpected tree mode\n"
        ).encode()
        malformed_commit = self.git("hash-object", "-t", "commit", "--literally", "-w", "--stdin",
            data=commit_bytes).decode().strip()
        before = self.state()
        with self.assertRaises(ValueError):
            self.immutable(malformed_commit)
        self.assertEqual(self.state(), before)

    def test_git_replacement_ref_cannot_redirect_the_requested_commit(self) -> None:
        (self.repository / "binary.dat").write_bytes(b"replacement content")
        newer = self.commit("replacement candidate")
        self.assertNotEqual(self.immutable(newer), self.original)
        self.git("replace", self.first, newer)
        before = self.state()
        self.assertEqual(self.immutable(self.first), self.original)
        self.assertEqual(self.state(), before)


    def test_inherited_repository_overrides_cannot_redirect_commit_reads(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-other-repository-") as temporary:
            other = Path(temporary)
            self.git("-C", str(other), "init", "--quiet")
            with mock.patch.dict(os.environ, {
                "GIT_DIR": str(other / ".git"), "GIT_WORK_TREE": str(other),
                "GIT_COMMON_DIR": str(other / ".git"),
                "GIT_OBJECT_DIRECTORY": str(other / ".git/objects"),
                "GIT_ALTERNATE_OBJECT_DIRECTORIES": str(other / ".git/objects"),
            }):
                self.assertEqual(self.immutable(), self.original)



if __name__ == "__main__":
    unittest.main()
