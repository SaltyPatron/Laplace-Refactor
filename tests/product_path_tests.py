#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
import os
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/product_path.py"
SPEC = importlib.util.spec_from_file_location("laplace_product_path_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product-path module")
product_path = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = product_path
SPEC.loader.exec_module(product_path)


class ProductPathTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = product_path.load_json(REPOSITORY / "contracts/product-path.json")
        product_path.validate_contract(self.contract)

    def classify(self, *paths: str) -> dict:
        return product_path.classify(self.contract, list(paths))

    def test_docs_only_never_requires_physical_runner(self) -> None:
        result = self.classify("README.md", "docs/product/ROADMAP.md")
        self.assertTrue(result["hosted_only"])
        self.assertEqual(result["required_evidence"], ["hosted"])
        self.assertFalse(result["requires_custom_stack"])
        self.assertFalse(result["blocked"])

    def test_merge_profile_defers_physical_acceptance_without_claiming_it(self) -> None:
        result = product_path.classify(self.contract, [
            "integrations/postgresql/extension/src/composition_pg.c",
            "tools/product/build-package.py",
        ], "merge")
        self.assertEqual(result["required_evidence"], ["hosted"])
        self.assertEqual(result["deferred_evidence"], [
            "custom-stack", "package-product", "postgresql-product",
        ])
        self.assertFalse(result["requires_custom_stack"])
        self.assertFalse(result["requires_postgresql_product"])
        self.assertFalse(result["requires_package_product"])
        self.assertIn("postgresql-product", result["selected_evidence"])

    def test_merge_profile_still_requires_hosted_provider(self) -> None:
        broken = copy.deepcopy(self.contract)
        next(row for row in broken["evidence"] if row["id"] == "hosted")["implemented"] = False
        self.assertTrue(product_path.classify(broken, ["engine/src/composition.cpp"], "merge")["blocked"])

    def test_native_change_requires_custom_stack(self) -> None:
        result = self.classify("engine/src/composition.cpp")
        self.assertFalse(result["hosted_only"])
        self.assertIn("native", result["classes"])
        self.assertEqual(result["required_evidence"], ["custom-stack", "hosted"])
        self.assertTrue(result["requires_custom_stack"])
        self.assertFalse(result["blocked"])

    def test_postgresql_change_requires_exact_product_provider(self) -> None:
        result = self.classify("integrations/postgresql/extension/src/composition_pg.c")
        self.assertIn("postgresql", result["classes"])
        self.assertTrue(result["requires_custom_stack"])
        self.assertTrue(result["requires_postgresql_product"])
        self.assertIn("postgresql-product", result["required_evidence"])
        self.assertNotIn("postgresql-product", result["unimplemented_evidence"])
        self.assertFalse(result["blocked"])

    def test_postgresql_proof_control_plane_requires_its_own_physical_proof(self) -> None:
        for path in (
            ".github/workflows/postgresql-product.yml",
            "tests/postgresql_product_proof_tests.py",
            "tools/postgresql/prove-product.py",
        ):
            with self.subTest(path=path):
                result = self.classify(path)
                self.assertIn("postgresql", result["classes"])
                self.assertTrue(result["requires_postgresql_product"])
                self.assertFalse(result["blocked"])

    def test_package_change_requires_exact_package_provider(self) -> None:
        result = self.classify("tools/product/build-package.py")
        self.assertIn("package", result["classes"])
        self.assertTrue(result["requires_package_product"])
        self.assertIn("package-product", result["required_evidence"])
        self.assertNotIn("package-product", result["unimplemented_evidence"])
        self.assertFalse(result["blocked"])

    def test_source_admission_runtime_requires_package_product(self) -> None:
        for path in ("tools/admit_source.py", "tools/admit_source_guard.py"):
            with self.subTest(path=path):
                result = self.classify(path)
                self.assertIn("package", result["classes"])
                self.assertTrue(result["requires_package_product"])
                self.assertIn("package-product", result["required_evidence"])
                self.assertFalse(result["blocked"])

    def test_package_proof_control_plane_requires_its_own_physical_proof(self) -> None:
        for path in (
            ".github/workflows/package-product.yml",
            "tests/package_product_proof_tests.py",
            "tools/receipt_store.cpp",
            "tests/receipt_store_test.cmake",
            "tests/registry.d/receipt_store.json",
        ):
            with self.subTest(path=path):
                result = self.classify(path)
                self.assertIn("package", result["classes"])
                self.assertTrue(result["requires_package_product"])
                self.assertFalse(result["blocked"])

    def test_delivery_change_requires_package_but_control_plane_does_not(self) -> None:
        delivery = self.classify("tools/delivery/product_activation.py")
        self.assertIn("package", delivery["classes"])
        self.assertTrue(delivery["requires_package_product"])
        self.assertFalse(delivery["blocked"])
        control = self.classify("tools/delivery/product_path.py")
        self.assertEqual(control["classes"], ["product-semantic"])
        self.assertTrue(control["requires_custom_stack"])
        self.assertFalse(control["requires_package_product"])
        self.assertFalse(control["blocked"])

    def test_deleted_semantic_path_has_same_classification_as_modified_path(self) -> None:
        result = self.classify("integrations/postgresql/extension/src/composition_pg.c")
        self.assertTrue(result["requires_postgresql_product"])
        self.assertFalse(result["blocked"])

    def test_git_rename_preserves_deleted_semantic_source_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            status = Path(temporary) / "changed.status"
            status.write_bytes(
                b"R100\0engine/src/composition.cpp\0docs/composition.md\0"
            )
            paths = product_path.read_git_name_status_z(status)
        self.assertEqual(
            paths,
            ["engine/src/composition.cpp", "docs/composition.md"],
        )
        result = product_path.classify(self.contract, paths)
        self.assertFalse(result["hosted_only"])
        self.assertIn("native", result["classes"])
        self.assertTrue(result["requires_custom_stack"])

    def test_git_rename_with_missing_destination_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            status = Path(temporary) / "changed.status"
            status.write_bytes(b"R100\0engine/src/composition.cpp\0")
            with self.assertRaisesRegex(product_path.ProductPathError, "truncated"):
                product_path.read_git_name_status_z(status)

    def test_git_name_status_requires_nul_termination(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            status = Path(temporary) / "changed.status"
            status.write_bytes(b"M\0engine/src/composition.cpp")
            with self.assertRaisesRegex(product_path.ProductPathError, "NUL terminated"):
                product_path.read_git_name_status_z(status)

    def test_mixed_docs_and_semantic_change_is_semantic(self) -> None:
        result = self.classify("docs/product/ROADMAP.md", "managed/Laplace.Managed/Isa.cs")
        self.assertFalse(result["hosted_only"])
        self.assertIn("managed", result["classes"])
        self.assertTrue(result["requires_custom_stack"])

    def test_contract_and_workflow_changes_require_custom_stack(self) -> None:
        result = self.classify(
            "contracts/product-path.json", ".github/workflows/product-path.yml"
        )
        self.assertEqual(result["classes"], ["ci-test", "contract"])
        self.assertTrue(result["requires_custom_stack"])
        self.assertFalse(result["blocked"])

    def test_unknown_path_fails_closed_into_product_semantics(self) -> None:
        result = self.classify("new-runtime-surface.bin")
        self.assertEqual(result["classes"], ["product-semantic"])
        self.assertEqual(result["unmatched_semantic_paths"], ["new-runtime-surface.bin"])
        self.assertTrue(result["requires_custom_stack"])
        self.assertFalse(result["blocked"])

    def test_noncanonical_paths_are_rejected(self) -> None:
        for path in ("../engine/src/composition.cpp", "/tmp/file", "engine/../README.md"):
            with self.subTest(path=path), self.assertRaises(product_path.ProductPathError):
                self.classify(path)

    def test_missing_provider_reference_is_rejected(self) -> None:
        broken = copy.deepcopy(self.contract)
        broken["class_rules"][0]["evidence"].append("invented-provider")
        with self.assertRaisesRegex(product_path.ProductPathError, "unknown evidence"):
            product_path.validate_contract(broken)

    def test_exclusion_must_be_covered_by_positive_pattern(self) -> None:
        broken = copy.deepcopy(self.contract)
        package = next(row for row in broken["class_rules"] if row["id"] == "package")
        package["exclude_patterns"].append("engine/src/composition.cpp")
        with self.assertRaisesRegex(product_path.ProductPathError, "exclusion is not covered"):
            product_path.validate_contract(broken)

    def test_provider_implementation_state_cannot_be_omitted(self) -> None:
        mutated = copy.deepcopy(self.contract)
        provider = next(
            row for row in mutated["evidence"] if row["id"] == "postgresql-product"
        )
        provider.pop("implemented")
        with self.assertRaisesRegex(product_path.ProductPathError, "implementation state"):
            product_path.validate_contract(mutated)

    def test_postgresql_provider_failure_state_blocks_postgresql_changes(self) -> None:
        mutated = copy.deepcopy(self.contract)
        provider = next(
            row for row in mutated["evidence"] if row["id"] == "postgresql-product"
        )
        provider["implemented"] = False
        result = product_path.classify(
            mutated, ["integrations/postgresql/extension/src/composition_pg.c"]
        )
        self.assertTrue(result["blocked"])
        self.assertIn("postgresql-product", result["unimplemented_evidence"])

    def test_physical_providers_are_declared_implemented(self) -> None:
        providers = {row["id"]: row for row in self.contract["evidence"]}
        self.assertTrue(providers["postgresql-product"]["implemented"])
        self.assertEqual(
            providers["postgresql-product"]["check"], "postgresql-product-proof"
        )
        self.assertTrue(providers["package-product"]["implemented"])
        self.assertEqual(providers["package-product"]["check"], "package-product-proof")


    def test_exact_audit_workflow_requests_policy_verification_without_native_work(self) -> None:
        self.assertEqual(self.contract["audit_only_paths"],
                         [".github/workflows/final-convergence-audit.yml"])
        result = self.classify(*self.contract["audit_only_paths"])
        self.assertTrue(result["audit_only"])
        self.assertTrue(result["hosted_only"])
        self.assertFalse(result["requires_native"])
        self.assertEqual(result["required_evidence"], ["hosted"])
        self.assertEqual(result["selected_evidence"], ["hosted"])
        self.assertEqual(result["classes"], [])
        self.assertFalse(result["blocked"])
        for key in ("requires_custom_stack", "requires_postgresql_product", "requires_package_product"):
            self.assertFalse(result[key])

    def test_audit_exception_does_not_extend_to_mixed_or_other_paths(self) -> None:
        audit = self.contract["audit_only_paths"][0]
        for path in ("engine/src/composition.cpp", "tools/admit_source.py",
                     ".github/workflows/ci.yml", ".github/workflows/product-path.yml",
                     ".github/workflows/another-audit.yml", "README.md", "docs/audit.md",
                     "unrecognized-surface"):
            with self.subTest(path=path):
                mixed = self.classify(audit, path)
                self.assertFalse(mixed["audit_only"])
                self.assertTrue(mixed["requires_native"])
                self.assertTrue(mixed["requires_custom_stack"])
        docs = self.classify("README.md")
        self.assertTrue(docs["hosted_only"])
        self.assertFalse(docs["audit_only"])
        self.assertTrue(docs["requires_native"])

    def test_audit_rename_and_copy_keep_both_sides_while_delete_is_exact(self) -> None:
        audit = self.contract["audit_only_paths"][0]
        for kind in ("A", "M", "D", "T"):
            with self.subTest(kind=kind):
                paths = product_path.parse_git_name_status_z(f"{kind}\0{audit}\0".encode())
                self.assertTrue(product_path.classify(self.contract, paths)["audit_only"])
        for kind in ("R100", "C100"):
            for first, second in (("engine/source.cpp", audit), (audit, "docs/audit.md")):
                with self.subTest(kind=kind, first=first):
                    paths = product_path.parse_git_name_status_z(
                        f"{kind}\0{first}\0{second}\0".encode())
                    result = product_path.classify(self.contract, paths)
                    self.assertFalse(result["audit_only"])
                    self.assertTrue(result["requires_native"])

    def test_audit_path_policy_cannot_use_globs_or_noncanonical_paths(self) -> None:
        for value in ([".github/workflows/*.yml"], ["engine/file.yml"],
                      [".github/workflows/../ci.yml"], [], [""]):
            with self.subTest(value=value):
                contract = copy.deepcopy(self.contract)
                contract["audit_only_paths"] = value
                with self.assertRaises(product_path.ProductPathError):
                    product_path.validate_contract(contract)

    def test_audit_still_requires_an_implemented_hosted_provider(self) -> None:
        contract = copy.deepcopy(self.contract)
        next(row for row in contract["evidence"] if row["id"] == "hosted")["implemented"] = False
        result = product_path.classify(contract, contract["audit_only_paths"])
        self.assertTrue(result["blocked"])
        self.assertEqual(result["unimplemented_evidence"], ["hosted"])


class AuditOnlyGitProofTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.contract = product_path.load_json(REPOSITORY / "contracts/product-path.json")
        self.audit = self.root / self.contract["audit_only_paths"][0]
        self.audit.parent.mkdir(parents=True)
        self.audit.write_text("name: initial audit\n")
        (self.root / "engine.cpp").write_text("initial semantic content\n")
        self.git("init", "--quiet")
        self.git("config", "user.name", "Product path fixture")
        self.git("config", "user.email", "product-path@example.invalid")
        self.base = self.commit()

    def git(self, *arguments: str) -> str:
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith("GIT_")}
        result = subprocess.run(["git", "-C", str(self.root), *arguments],
                                check=True, capture_output=True, text=True,
                                env=environment, timeout=10)
        return result.stdout.strip()

    def commit(self) -> str:
        self.git("add", "-A")
        self.git("commit", "--quiet", "-m", "fixture")
        return self.git("rev-parse", "HEAD")

    def audit_commit(self) -> str:
        self.audit.write_text("name: revised audit\n")
        return self.commit()

    def verify(self, head: str, checkout: str | None = None) -> dict:
        return product_path.verify_audit_only(self.contract, self.root, self.base,
                                             head, checkout or head)

    def test_actual_current_commit_and_cli_emit_unrequested_native_scope(self) -> None:
        head = self.audit_commit()
        result = self.verify(head)
        self.assertEqual(result["checkout_sha"], head)
        self.assertEqual(result["classification"]["paths"], self.contract["audit_only_paths"])
        self.assertEqual(result["native_tests"], "not-requested")
        execution = subprocess.run(
            [sys.executable, str(MODULE_PATH), "verify-audit-only", "--contract",
             str(REPOSITORY / "contracts/product-path.json"), "--repository", str(self.root),
             "--base", self.base, "--head", head, "--expected-checkout", head],
            capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(execution.returncode, 0, execution.stderr)
        self.assertEqual(json.loads(execution.stdout), result)

    def test_real_mixed_diff_and_deleted_semantic_file_refuse(self) -> None:
        self.audit.write_text("name: changed audit\n")
        (self.root / "engine.cpp").unlink()
        head = self.commit()
        with self.assertRaisesRegex(product_path.ProductPathError, "requires native"):
            self.verify(head)

    def test_real_audit_deletion_remains_audit_only(self) -> None:
        self.audit.unlink()
        result = self.verify(self.commit())
        self.assertTrue(result["classification"]["audit_only"])

    def test_real_rename_into_audit_cannot_hide_semantic_source(self) -> None:
        self.audit.unlink()
        self.git("mv", "engine.cpp", self.contract["audit_only_paths"][0])
        with self.assertRaisesRegex(product_path.ProductPathError, "requires native"):
            self.verify(self.commit())

    def test_stale_checkout_dirty_source_and_empty_delta_refuse(self) -> None:
        head = self.audit_commit()
        with self.assertRaisesRegex(product_path.ProductPathError, "checkout differs"):
            self.verify(head, self.base)
        self.audit.write_text("uncommitted changed policy\n")
        with self.assertRaisesRegex(product_path.ProductPathError, "tracked modifications"):
            self.verify(head)
        self.git("checkout", "--", ".")
        with self.assertRaises(product_path.ProductPathError):
            product_path.verify_audit_only(self.contract, self.root, head, head, head)

    def test_unknown_noncommit_symbolic_and_unrelated_head_refuse(self) -> None:
        head = self.audit_commit()
        blob = self.git("rev-parse", "HEAD:engine.cpp")
        for bad in ("main", "0" * 40, blob, ""):
            with self.subTest(bad=bad), self.assertRaises(product_path.ProductPathError):
                self.verify(bad, head)
        self.git("checkout", "--quiet", "--detach", self.base)
        (self.root / "engine.cpp").write_text("different branch\n")
        unrelated = self.commit()
        self.git("checkout", "--quiet", "--detach", head)
        with self.assertRaises(product_path.ProductPathError):
            self.verify(unrelated, head)

    def test_environment_and_replace_refs_cannot_select_a_different_delta(self) -> None:
        audit_head = self.audit_commit()
        with mock.patch.dict(os.environ, {"GIT_DIR": "/absent", "GIT_WORK_TREE": "/absent"}):
            self.assertEqual(self.verify(audit_head)["checkout_sha"], audit_head)
        self.git("checkout", "--quiet", "--detach", self.base)
        (self.root / "engine.cpp").write_text("semantic change\n")
        semantic = self.commit()
        self.git("replace", semantic, audit_head)
        with self.assertRaises(product_path.ProductPathError):
            self.verify(semantic)

    def test_failed_git_and_deadline_cannot_emit_success(self) -> None:
        head = self.audit_commit()
        with mock.patch.object(product_path.subprocess, "run",
                               side_effect=subprocess.TimeoutExpired("git", 45)):
            with self.assertRaisesRegex(product_path.ProductPathError, "verification failed"):
                self.verify(head)
        unavailable = copy.deepcopy(self.contract)
        next(row for row in unavailable["evidence"] if row["id"] == "hosted")["implemented"] = False
        with self.assertRaisesRegex(product_path.ProductPathError, "requires native"):
            product_path.verify_audit_only(unavailable, self.root, self.base, head, head)


if __name__ == "__main__":
    unittest.main()
