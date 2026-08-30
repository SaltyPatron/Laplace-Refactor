#!/usr/bin/env python3
"""Acceptance and deliberate-defect tests for complete product packaging."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/product/build-package.py"
SPEC = importlib.util.spec_from_file_location("laplace_product_package", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class ProductPackageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = PACKAGE.load_json(REPOSITORY / "contracts/product-package.json")
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-product-package-")
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def provider(self, name: str, filename: str, soname: str) -> dict[str, object]:
        source = self.root / "providers" / name / filename
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_bytes(f"provider:{name}:{filename}\n".encode("utf-8"))
        return {
            "role": f"{name}-runtime",
            "class": "runtime-object",
            "path": str(source),
            "bytes": source.stat().st_size,
            "sha256": PACKAGE.sha256_file(source),
            "soname": soname,
        }

    def test_current_contract_is_exact(self) -> None:
        PACKAGE.validate_contract(self.contract)

    def test_activation_gate_cannot_be_removed(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["activation"].pop("requires_recursive_elf_closure")
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "activation gates"):
            PACKAGE.validate_contract(mutant)

    def test_postgresql_version_cannot_be_widened(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["postgresql"]["version"] = "PostgreSQL 18"
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "exact PostgreSQL 18.6"):
            PACKAGE.validate_contract(mutant)

    def test_postgresql_publication_receipt_cannot_be_bypassed(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["postgresql"].pop("publication_receipt_schema")
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "publication receipt"):
            PACKAGE.validate_contract(mutant)

    def test_product_build_cannot_return_to_owner_private_roots(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["build"]["root"] = "/build/laplace/build/product"
        mutant["build"]["stage_root"] = "/build/laplace/stage/product"
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "CI runner workspace"):
            PACKAGE.validate_contract(mutant)

    def test_all_selected_oneapi_provider_families_are_required(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["laplace"]["required_installed_providers"].remove("onemkl")
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "provider set"):
            PACKAGE.validate_contract(mutant)

    def test_onetbb_hwloc_runtime_dependency_cannot_be_omitted(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["laplace"]["required_runtime_dependencies"] = []
        mutant["runtime_dependencies"] = {}
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "runtime dependency set"):
            PACKAGE.validate_contract(mutant)

    def test_runtime_dependency_bytes_are_exact(self) -> None:
        mutant = copy.deepcopy(self.contract)
        provider_root = self.root / "runtime-dependency"
        provider_file = provider_root / "lib/libhwloc.so.15.10.2"
        provider_file.parent.mkdir(parents=True)
        provider_file.write_bytes(b"selected runtime dependency\n")
        mutant["runtime_dependencies"]["onetbb-hwloc"] = {
            "immutable_root": str(provider_root),
            "package_versions": {},
            "files": [
                {
                    "path": str(provider_file),
                    "bytes": provider_file.stat().st_size,
                    "sha256": "0" * 64,
                }
            ],
        }
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "bytes differ"):
            PACKAGE.verify_runtime_dependencies(mutant)

    def test_package_database_provider_cannot_be_omitted(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["host_build_provider"]["additional_receipted_files"] = []
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "additional host"):
            PACKAGE.validate_contract(mutant)

    def test_blake3_revision_root_cannot_be_collapsed_to_source_subtree(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["build"]["blake3_root"] = mutant["build"]["blake3_source"]
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "BLAKE3 source"):
            PACKAGE.validate_contract(mutant)

    def test_c_utility_uses_cxx_linker_for_cxx_engine_closure(self) -> None:
        tools_cmake = (REPOSITORY / "tools/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            "set_property(TARGET laplace_identity_inspect PROPERTY LINKER_LANGUAGE CXX)",
            tools_cmake,
        )

    def test_product_library_directory_cannot_diverge_from_manifest(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["build"]["install_libdir"] = "lib64"
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "canonical lib"):
            PACKAGE.validate_contract(mutant)

    def test_product_platform_abi_expansion_cannot_be_narrowed(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["runtime_closure"]["allowed_additional_platform_abi_sonames"].remove(
            "librt.so.1"
        )
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "platform ABI"):
            PACKAGE.validate_contract(mutant)

    def test_native_resource_observer_cannot_be_omitted(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["package"]["required_files"].remove("bin/laplace_resource_observe")
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "resource observer"):
            PACKAGE.validate_contract(mutant)

    def test_unicode_activation_identity_provider_cannot_be_omitted(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["package"]["required_files"].remove(
            "bin/laplace_unicode_activation_identify"
        )
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "activation identity"):
            PACKAGE.validate_contract(mutant)

    def test_external_runtime_edges_cannot_escape_the_zero_gate(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["runtime_closure"]["required_zero_summary_fields"].remove(
            "custom_to_external_edge_count"
        )
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "zero gates"):
            PACKAGE.validate_contract(mutant)

    def test_provider_runtime_and_soname_are_copied(self) -> None:
        prefix = self.root / "package"
        prefix.mkdir()
        item = self.provider("onetbb", "libtbb.so.12.19", "libtbb.so.12")
        receipts = PACKAGE.copy_provider_files({"onetbb": [item]}, prefix)
        runtime = prefix / "lib/libtbb.so.12.19"
        alias = prefix / "lib/libtbb.so.12"
        self.assertEqual(PACKAGE.sha256_file(runtime), item["sha256"])
        self.assertTrue(alias.is_symlink())
        self.assertEqual(os.readlink(alias), "libtbb.so.12.19")
        self.assertEqual(len(receipts), 2)

    def test_conflicting_provider_destination_is_rejected(self) -> None:
        prefix = self.root / "package"
        destination = prefix / "lib/libmkl_rt.so.3"
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"wrong\n")
        item = self.provider("onemkl", "libmkl_rt.so.3", "libmkl_rt.so.3")
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "conflicts"):
            PACKAGE.copy_provider_files({"onemkl": [item]}, prefix)

    def test_manifest_records_internal_symlink_without_flattening(self) -> None:
        prefix = self.root / "package"
        library = prefix / "lib/liblaplace_engine.so.2.0.0"
        library.parent.mkdir(parents=True)
        library.write_bytes(b"not-elf\n")
        library.chmod(0o755)
        alias = prefix / "lib/liblaplace_engine.so.2"
        alias.symlink_to(library.name)
        entries = PACKAGE.manifest_entries(prefix, Path("/usr/bin/readelf"))
        link = next(item for item in entries if item["kind"] == "symlink")
        self.assertEqual(link["target"], library.name)
        self.assertEqual(
            link["sha256"], hashlib.sha256(library.name.encode("utf-8")).hexdigest()
        )

    def test_escaping_symlink_is_rejected(self) -> None:
        prefix = self.root / "package"
        prefix.mkdir()
        (prefix / "escape").symlink_to("../../outside")
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "escapes"):
            PACKAGE.manifest_entries(prefix, Path("/usr/bin/readelf"))

    def test_broken_internal_symlink_is_rejected(self) -> None:
        prefix = self.root / "package"
        prefix.mkdir()
        (prefix / "broken").symlink_to("missing")
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "target is absent"):
            PACKAGE.manifest_entries(prefix, Path("/usr/bin/readelf"))

    def test_provider_bytes_are_reverified_after_overlay(self) -> None:
        prefix = self.root / "package"
        prefix.mkdir()
        item = self.provider("onemkl", "libmkl_rt.so.3", "libmkl_rt.so.3")
        receipts = PACKAGE.copy_provider_files({"onemkl": [item]}, prefix)
        (prefix / "lib/libmkl_rt.so.3").write_bytes(b"overlay damage\n")
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "after overlay"):
            PACKAGE.verify_copied_provider_files(receipts, prefix)

    def test_dirty_repository_cannot_be_activation_eligible(self) -> None:
        plan = {
            "recipe": {"repository": {"clean": False}},
            "product_build_input_closure_complete": True,
            "postgresql": {
                "build_input_closure_complete": True,
                "runtime_provider_qualification_complete": True,
                "activation_eligible": True,
            },
        }
        gates = PACKAGE.product_activation_gates(plan, True)
        self.assertFalse(gates["clean_repository"])
        self.assertFalse(all(gates.values()))

    def test_product_build_input_closure_cannot_be_promoted_by_other_gates(self) -> None:
        plan = {
            "recipe": {"repository": {"clean": True}},
            "product_build_input_closure_complete": False,
            "postgresql": {
                "build_input_closure_complete": True,
                "runtime_provider_qualification_complete": True,
                "activation_eligible": True,
            },
        }
        gates = PACKAGE.product_activation_gates(plan, True)
        self.assertFalse(gates["product_build_input_closure"])
        self.assertFalse(all(gates.values()))

    def test_product_selection_reuses_only_an_exact_receipted_plan(self) -> None:
        plan_id = "c" * 64
        build = self.root / "product/build" / plan_id
        stage = self.root / "product/stage" / plan_id
        build.mkdir(parents=True)
        release = stage / "root/opt/laplace/releases" / ("a" * 64)
        release.mkdir(parents=True)
        manifest = {
            "schema": PACKAGE.MANIFEST_SCHEMA,
            "package_id": "a" * 64,
            "root": f"/opt/laplace/releases/{'a' * 64}",
        }
        manifest_path = build / "package-manifest.json"
        manifest_path.write_bytes(PACKAGE.canonical_bytes(manifest))
        receipt = {
            "schema": PACKAGE.RECEIPT_SCHEMA,
            "plan_sha256": "b" * 64,
            "package_id": "a" * 64,
            "manifest": str(manifest_path),
            "manifest_sha256": PACKAGE.sha256_file(manifest_path),
            "physical_root": str(release),
            "activation_eligible": True,
            "build_input_closure_complete": True,
            "product_activated": False,
        }
        receipt_path = build / "package-receipt.json"
        receipt_path.write_bytes(PACKAGE.canonical_bytes(receipt))
        plan = {
            "plan_id": plan_id,
            "plan_sha256": "b" * 64,
            "build_directory": str(build),
            "stage_directory": str(stage),
        }
        selection = PACKAGE.select_or_build_product(
            self.contract, REPOSITORY, plan
        )
        self.assertFalse(selection["built_new"])
        self.assertEqual(selection["product_receipt"], str(receipt_path))

        receipt["plan_sha256"] = "d" * 64
        receipt_path.write_bytes(PACKAGE.canonical_bytes(receipt))
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "exact plan"):
            PACKAGE.select_or_build_product(self.contract, REPOSITORY, plan)

    def test_incomplete_same_plan_workspace_is_repaired_under_lock(self) -> None:
        plan_id = "c" * 64
        plan_sha = "d" * 64
        package_id = "e" * 64
        product_root = self.root / "product"
        build = product_root / "build" / plan_id
        stage = product_root / "stage" / plan_id
        build.mkdir(parents=True)
        stage.mkdir(parents=True)
        (build / "partial").write_text("incomplete\n", encoding="utf-8")
        plan = {
            "plan_id": plan_id,
            "plan_sha256": plan_sha,
            "build_directory": str(build),
            "stage_directory": str(stage),
        }

        def execute(_contract: dict, _repository: Path, supplied: dict) -> dict:
            self.assertEqual(supplied, plan)
            self.assertFalse(build.exists())
            self.assertFalse(stage.exists())
            build.mkdir(parents=True)
            release = stage / "root/opt/laplace/releases" / package_id
            release.mkdir(parents=True)
            manifest = {
                "package_id": package_id,
                "root": f"/opt/laplace/releases/{package_id}",
            }
            manifest_path = build / "package-manifest.json"
            manifest_path.write_bytes(PACKAGE.canonical_bytes(manifest))
            receipt = {
                "schema": PACKAGE.RECEIPT_SCHEMA,
                "plan_sha256": plan_sha,
                "package_id": package_id,
                "manifest": str(manifest_path),
                "manifest_sha256": PACKAGE.sha256_file(manifest_path),
                "physical_root": str(release),
                "activation_eligible": True,
                "build_input_closure_complete": True,
                "product_activated": False,
            }
            (build / "package-receipt.json").write_bytes(
                PACKAGE.canonical_bytes(receipt)
            )
            return receipt

        with mock.patch.object(PACKAGE, "execute_plan", side_effect=execute) as execute_mock:
            selected = PACKAGE.select_or_build_product(self.contract, REPOSITORY, plan)
        execute_mock.assert_called_once()
        self.assertTrue(selected["built_new"])
        self.assertEqual(selected["package_id"], package_id)
        self.assertTrue((product_root / "locks" / f"{plan_id}.lock").is_file())

    def test_product_plan_lock_rejects_cross_root_cleanup_authority(self) -> None:
        plan_id = "f" * 64
        plan = {
            "plan_id": plan_id,
            "build_directory": str(self.root / "product-a/build" / plan_id),
            "stage_directory": str(self.root / "product-b/stage" / plan_id),
        }
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "share a product root"):
            PACKAGE.product_plan_lock_path(plan)

    def test_product_plan_lock_rejects_wrong_named_roots(self) -> None:
        plan_id = "f" * 64
        plan = {
            "plan_id": plan_id,
            "build_directory": str(self.root / "product/work" / plan_id),
            "stage_directory": str(self.root / "product/stage" / plan_id),
        }
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "named roots"):
            PACKAGE.product_plan_lock_path(plan)

    def test_selected_tool_bytes_are_reverified(self) -> None:
        tool = self.root / "cmake"
        tool.write_bytes(b"selected cmake\n")
        record = {"path": str(tool), "sha256": PACKAGE.sha256_file(tool)}
        self.assertEqual(
            PACKAGE.verify_receipted_file(record, "test.cmake"), tool
        )
        tool.write_bytes(b"ambient replacement\n")
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "bytes differ"):
            PACKAGE.verify_receipted_file(record, "test.cmake")

    def test_build_provider_tree_mutation_changes_receipt(self) -> None:
        root = self.root / "provider"
        root.mkdir()
        (root / "header.h").write_text("selected\n", encoding="utf-8")
        before = PACKAGE.exact_tree_receipt(root)
        (root / "header.h").write_text("mutated\n", encoding="utf-8")
        after = PACKAGE.exact_tree_receipt(root)
        self.assertNotEqual(before["tree_sha256"], after["tree_sha256"])

    def test_build_command_is_network_isolated_with_read_only_inputs(self) -> None:
        repository = self.root / "repository"
        build = self.root / "build"
        stage = self.root / "stage"
        toolchain = self.root / "toolchain"
        provider = self.root / "provider"
        for path in (repository, build, stage, toolchain, provider):
            path.mkdir()
        plan = {
            "repository_root": str(repository),
            "build_directory": str(build),
            "stage_directory": str(stage),
            "product_toolchain": {"prefix": str(toolchain)},
            "host_build_provider": {
                "roots": [{"path": "/usr"}],
                "files": [],
            },
            "build_input_roots": {
                "provider": {"path": str(provider)},
            },
            "build_input_files": {},
        }
        command = [str(toolchain / "bin/cmake"), "--version"]
        sandbox = PACKAGE.sandboxed_build_command(
            self.contract, plan, command, repository
        )
        self.assertIn("--unshare-all", sandbox)
        triples = [sandbox[index : index + 3] for index in range(len(sandbox) - 2)]
        for path in (repository, toolchain, provider):
            self.assertIn(["--ro-bind", str(path), str(path)], triples)
        for path in (build, stage):
            self.assertIn(["--bind", str(path), str(path)], triples)
        self.assertEqual(sandbox[-len(command) :], command)

    def test_duplicate_contract_key_is_rejected(self) -> None:
        path = self.root / "duplicate.json"
        path.write_text('{"schema":"first","schema":"second"}\n', encoding="utf-8")
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "duplicate JSON key"):
            PACKAGE.load_json(path)


if __name__ == "__main__":
    unittest.main()
