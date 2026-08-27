#!/usr/bin/env python3
"""Mutation checks for the PostgreSQL build and package boundary."""

from __future__ import annotations

import importlib.util
import hashlib
import json
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools/postgresql/build-package.py"
SPEC = importlib.util.spec_from_file_location("laplace_postgresql_build", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
BUILD = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BUILD
SPEC.loader.exec_module(BUILD)


class PostgreSQLBuildTests(unittest.TestCase):
    def contract(self) -> dict[str, object]:
        return json.loads(
            (REPO_ROOT / "contracts/postgresql-build.json").read_text(encoding="utf-8")
        )

    def toolchain_receipt(self, root: Path) -> Path:
        prefix = root / "toolchain"
        binary = prefix / "bin/tool"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(Path("/bin/true").read_bytes())
        binary.chmod(0o755)
        digest = BUILD.sha256_file(binary)
        contract = self.contract()
        receipt = {
            "schema": contract["build_toolchain"]["receipt_schema"],
            "build_input_id": "3" * 64,
            "package": {"prefix": str(prefix)},
            "consumer_manifest": {
                "schema": contract["build_toolchain"]["consumer_manifest_schema"],
                "build_input_id": "3" * 64,
                "prefix": str(prefix),
                "tools": {
                    name: {
                        "path": str(binary),
                        "sha256": digest,
                        "version": "fixture-v1",
                    }
                    for name in contract["build_toolchain"]["required_tools"]
                },
            },
            "activation": {
                "scope": "build-toolchain-only",
                "product_runtime_activation_eligible": False,
            },
        }
        path = root / "toolchain-receipt.json"
        path.write_text(json.dumps(receipt), encoding="utf-8")
        return path

    def runtime_receipt(self, root: Path) -> Path:
        prefix = root / "runtime"
        library_directory = prefix / "lib"
        library_directory.mkdir(parents=True)
        library = library_directory / "libfixture.so"
        library.write_bytes(b"runtime bytes\n")
        records = [
            {
                "path": "lib",
                "kind": "directory",
                "mode": f"{stat.S_IMODE(library_directory.stat().st_mode):04o}",
                "size": 0,
                "sha256": None,
                "target": None,
                "elf": None,
            },
            {
                "path": "lib/libfixture.so",
                "kind": "file",
                "mode": f"{stat.S_IMODE(library.stat().st_mode):04o}",
                "size": library.stat().st_size,
                "sha256": BUILD.sha256_file(library),
                "target": None,
                "elf": None,
            },
        ]
        digest = hashlib.sha256()
        for record in records:
            encoded = json.dumps(record, sort_keys=True, separators=(",", ":")).encode(
                "utf-8"
            )
            digest.update(len(encoded).to_bytes(8, "big"))
            digest.update(encoded)
        contract = self.contract()
        component_tests = {
            name: {
                "scope": "upstream-component-test",
                "command": ["/toolchain/test", name],
                "process_return_code": 0,
                "exit_code": 0,
                "signal": None,
                "disposition": "passed",
                "package_gate": "required-pass",
                "product_activation_gate": "component-test-pass",
                "provider_observation": {},
                "source_evidence": None,
            }
            for name in contract["runtime_package"]["required_components"]
        }
        component_tests["liburing"] = {
            "scope": "upstream-userspace-library-and-live-kernel-regression",
            "command": ["/toolchain/make", "runtests"],
            "process_return_code": 2,
            "exit_code": 2,
            "signal": None,
            "disposition": "failed-under-observed-runtime-provider",
            "package_gate": "record-exact-outcome-and-continue",
            "product_activation_gate": "separate-selected-runtime-provider-qualification",
            "provider_observation": {
                "kernel_sysname": "Linux",
                "kernel_release": "fixture",
                "kernel_version": "fixture-version",
                "machine": "x86_64",
                "io_uring_disabled": 0,
            },
            "source_evidence": {
                "path": "README",
                "sha256": "1" * 64,
                "meaning": "fixture kernel-coupled suite evidence",
            },
        }
        checkpoints = {
            name: "5" * 64
            for name in contract["runtime_package"]["required_components"]
        }
        receipt = {
            "schema": contract["runtime_package"]["receipt_schema"],
            "build_input_id": "4" * 64,
            "install_prefix": contract["runtime_package"]["install_prefix"],
            "staged_prefix": str(prefix),
            "tree_sha256": digest.hexdigest(),
            "file_count": 1,
            "total_file_bytes": library.stat().st_size,
            "files": records,
            "component_checkpoints": checkpoints,
            "component_logs": {
                name: "6" * 64
                for name in contract["runtime_package"]["required_components"]
            },
            "component_test_executions": component_tests,
            "runtime_provider_qualification": {
                "schema": contract["runtime_package"][
                    "provider_qualification_receipt_schema"
                ],
                "complete": False,
                "required_before_product_activation": True,
                "required_components": ["liburing"],
                "requirements": {
                    "liburing": {
                        "component_checkpoint_sha256": checkpoints["liburing"],
                        "test_execution_sha256": BUILD.canonical_sha256(
                            component_tests["liburing"]
                        ),
                        "observed_disposition": component_tests["liburing"][
                            "disposition"
                        ],
                    }
                },
            },
            "plan_sha256": "7" * 64,
            "build_input_closure_complete": False,
            "static_link_closure_verified": False,
            "recursive_runtime_closure_verified": False,
            "activation_eligible": False,
        }
        path = root / "runtime-receipt.json"
        path.write_text(json.dumps(receipt), encoding="utf-8")
        return path

    def test_current_contract_and_release_join_are_valid(self) -> None:
        contract = self.contract()
        BUILD.validate_contract(contract)
        release = BUILD.selected_release(contract, REPO_ROOT)
        self.assertEqual(release["version"], "18.6")

    def test_tap_omission_is_rejected(self) -> None:
        contract = self.contract()
        contract["build"]["configure_arguments"].remove("--enable-tap-tests")
        with self.assertRaisesRegex(BUILD.BuildError, "enable TAP"):
            BUILD.validate_contract(contract)

    def test_ambient_system_tzdata_is_rejected(self) -> None:
        contract = self.contract()
        contract["build"]["configure_arguments"].append(
            "--with-system-tzdata=/usr/share/zoneinfo"
        )
        with self.assertRaisesRegex(BUILD.BuildError, "selected bundled tzdata"):
            BUILD.validate_contract(contract)

    def test_incomplete_build_input_closure_is_explicit_and_fail_closed(self) -> None:
        contract = self.contract()
        self.assertEqual(contract["input_closure"]["status"], "incomplete")
        self.assertTrue(contract["input_closure"]["unselected_host_inputs"])
        contract["input_closure"]["status"] = "complete"
        with self.assertRaisesRegex(BUILD.BuildError, "must remain incomplete"):
            BUILD.validate_contract(contract)

    def test_duplicate_contract_key_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "contract.json"
            path.write_text('{"schema":"first","schema":"second"}', encoding="utf-8")
            with self.assertRaisesRegex(BUILD.BuildError, "duplicate JSON object key"):
                BUILD.read_json(path)

    def test_ambient_pgxs_is_rejected(self) -> None:
        contract = self.contract()
        with self.assertRaisesRegex(BUILD.BuildError, "PGXS"):
            BUILD.validate_environment(contract, {"PGXS": "/ambient/pgxs.mk"})

    def test_build_home_does_not_inherit_the_callers_home(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            home = root / "private-home"
            tools = {
                name: {"path": f"/toolchain/bin/{name}"}
                for name in self.contract()["build_toolchain"]["required_tools"]
            }
            plan = {
                "build_toolchain": {"prefix": "/toolchain", "tools": tools},
                "staged_product_prefix": str(root / "stage/root/opt/laplace/current"),
                "stage_directory": str(root / "stage"),
                "build_directory": str(root / "build/contains-41"),
            }
            environment = BUILD.build_environment(self.contract(), plan, home)
            self.assertEqual(environment["HOME"], str(home.resolve()))
            self.assertEqual(home.stat().st_mode & 0o7777, 0o700)
            self.assertEqual(environment["MAKE"], "/toolchain/bin/make")
            self.assertNotIn("/usr/lib", environment["PKG_CONFIG_LIBDIR"])
            self.assertIn("-ffile-prefix-map=", environment["CFLAGS"])
            self.assertIn("$$ORIGIN", environment["LDFLAGS"])

    def test_build_recipe_identity_changes_with_driver_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            verifier = root / "tools/dependencies/release-assets.py"
            verifier.parent.mkdir(parents=True)
            verifier.write_text("verifier-v1", encoding="utf-8")
            (verifier.parent / "package_receipts.py").write_text(
                "receipt-verifier-v1", encoding="utf-8"
            )
            driver = root / "driver.py"
            driver.write_text("driver-v1", encoding="utf-8")
            first = BUILD.build_recipe_identity(self.contract(), root, driver)
            driver.write_text("driver-v2", encoding="utf-8")
            second = BUILD.build_recipe_identity(self.contract(), root, driver)
            self.assertNotEqual(
                first["build_driver"]["sha256"], second["build_driver"]["sha256"]
            )
            self.assertEqual(
                first["release_verifier"]["sha256"],
                second["release_verifier"]["sha256"],
            )

    def test_cxx_compiler_is_not_optional(self) -> None:
        contract = self.contract()
        contract["toolchain"].pop("cxx_compiler")
        with self.assertRaisesRegex(BUILD.BuildError, "cxx_compiler"):
            BUILD.validate_contract(contract)

    def test_repository_local_outputs_are_rejected(self) -> None:
        with self.assertRaisesRegex(BUILD.BuildError, "outside the repository"):
            BUILD.ensure_external(REPO_ROOT / "build", REPO_ROOT, "build root")

    def test_private_build_directory_clears_inherited_setgid(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary) / "shared"
            parent.mkdir()
            parent.chmod(0o2775)
            build = parent / "build"
            BUILD.create_private_build_directory(build)
            self.assertEqual(build.stat().st_mode & 0o7777, 0o700)
            child = build / "child"
            child.mkdir()
            self.assertEqual(child.stat().st_mode & 0o2000, 0)

    def test_resume_requires_the_exact_persisted_plan(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            build.mkdir(mode=0o700)
            stage = root / "stage"
            stage.mkdir(mode=0o700)
            product = stage / "root/opt/laplace/current"
            product.mkdir(parents=True)
            plan = {
                "build_directory": str(build),
                "stage_directory": str(stage),
                "staged_product_prefix": str(product),
                "staged_postgresql_prefix": str(product / "pgsql-18"),
                "runtime_package": {"receipt_path": str(root / "receipt.json")},
            }
            (build / "build-plan.json").write_text(
                json.dumps({**plan, "mutated": True}), encoding="utf-8"
            )
            with mock.patch.object(
                BUILD, "reverify_runtime_input", return_value={"files": []}
            ), mock.patch.object(BUILD, "verify_runtime_bytes_in_composed_tree"):
                with self.assertRaisesRegex(BUILD.BuildError, "exact plan"):
                    BUILD.prepare_build_directory(self.contract(), plan, resume=True)
                (build / "build-plan.json").write_text(
                    json.dumps(plan), encoding="utf-8"
                )
                observed, _ = BUILD.prepare_build_directory(
                    self.contract(), plan, resume=True
                )
                self.assertEqual(observed, build)

    def test_source_must_be_a_named_member_of_verified_release_import(self) -> None:
        with self.assertRaisesRegex(BUILD.BuildError, "verified release import"):
            BUILD.verify_release_import(
                self.contract(), REPO_ROOT, Path("/archives"), Path("/tmp/not-postgresql")
            )

    def test_runtime_library_not_in_a_declared_category_is_unknown(self) -> None:
        closure = BUILD.classify_needed(self.contract(), {"libc.so.6", "libmystery.so.1"})
        self.assertEqual(closure["system_abi"], ["libc.so.6"])
        self.assertEqual(closure["unknown"], ["libmystery.so.1"])

    def test_host_feature_and_intel_runtime_libraries_block_activation(self) -> None:
        closure = BUILD.classify_needed(
            self.contract(), {"libpq.so.5", "libssl.so.4", "libimf.so", "libc.so.6"}
        )
        self.assertEqual(closure["package"], ["libpq.so.5", "libssl.so.4"])
        self.assertEqual(closure["system_abi"], ["libc.so.6"])
        self.assertEqual(closure["selected_but_unpacked"], ["libimf.so"])

    def test_toolchain_receipt_selects_every_required_build_tool(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            selected = BUILD.verify_toolchain_receipt(
                self.contract(), self.toolchain_receipt(Path(temporary))
            )
            self.assertEqual(
                set(selected["tools"]),
                set(self.contract()["build_toolchain"]["required_tools"]),
            )

    def test_runtime_receipt_binds_complete_component_set_and_exact_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = self.runtime_receipt(root)
            selected = BUILD.verify_runtime_receipt(self.contract(), path)
            self.assertEqual(selected["build_input_id"], "4" * 64)
            receipt = json.loads(path.read_text(encoding="utf-8"))
            receipt["component_checkpoints"].pop("liburing")
            path.write_text(json.dumps(receipt), encoding="utf-8")
            with self.assertRaisesRegex(BUILD.BuildError, "evidence set"):
                BUILD.verify_runtime_receipt(self.contract(), path)

    def test_runtime_receipt_cannot_promote_deferred_provider_qualification(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = self.runtime_receipt(root)
            receipt = json.loads(path.read_text(encoding="utf-8"))
            receipt["runtime_provider_qualification"]["complete"] = True
            path.write_text(json.dumps(receipt), encoding="utf-8")
            with self.assertRaisesRegex(BUILD.BuildError, "cannot claim"):
                BUILD.verify_runtime_receipt(self.contract(), path)

    def test_runtime_provider_qualification_binds_exact_failed_test_execution(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = self.runtime_receipt(root)
            receipt = json.loads(path.read_text(encoding="utf-8"))
            receipt["component_test_executions"]["liburing"]["process_return_code"] = 0
            path.write_text(json.dumps(receipt), encoding="utf-8")
            with self.assertRaisesRegex(BUILD.BuildError, "test identity mismatch"):
                BUILD.verify_runtime_receipt(self.contract(), path)

    def test_ordinary_runtime_component_failure_cannot_use_provider_deferral(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = self.runtime_receipt(root)
            receipt = json.loads(path.read_text(encoding="utf-8"))
            execution = receipt["component_test_executions"]["zlib"]
            execution["process_return_code"] = 1
            execution["exit_code"] = 1
            execution["disposition"] = "failed-under-observed-runtime-provider"
            path.write_text(json.dumps(receipt), encoding="utf-8")
            with self.assertRaisesRegex(BUILD.BuildError, "ordinary runtime component"):
                BUILD.verify_runtime_receipt(self.contract(), path)

    def test_runtime_receipt_detects_physical_byte_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = self.runtime_receipt(root)
            (root / "runtime/lib/libfixture.so").write_bytes(b"mutated\n")
            with self.assertRaisesRegex(BUILD.BuildError, "bytes differ"):
                BUILD.verify_runtime_receipt(self.contract(), path)

    def test_runtime_tree_is_copied_once_and_remains_immutable_during_composition(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            receipt_path = self.runtime_receipt(root)
            selected = BUILD.verify_runtime_receipt(self.contract(), receipt_path)
            build = root / "product-build"
            stage = root / "product-stage"
            product = stage / "root/opt/laplace/current"
            plan = {
                "build_directory": str(build),
                "stage_directory": str(stage),
                "staged_product_prefix": str(product),
                "staged_postgresql_prefix": str(product / "pgsql-18"),
                "runtime_package": selected,
            }
            observed_build, receipt = BUILD.prepare_build_directory(
                self.contract(), plan, resume=False
            )
            self.assertEqual(observed_build, build)
            self.assertEqual(
                (product / "lib/libfixture.so").read_bytes(), b"runtime bytes\n"
            )
            (product / "pgsql-18").mkdir()
            BUILD.verify_runtime_bytes_in_composed_tree(
                plan, receipt, allow_additions=True
            )
            (product / "lib/libfixture.so").write_bytes(b"overwritten\n")
            with self.assertRaisesRegex(BUILD.BuildError, "bytes differ"):
                BUILD.verify_runtime_bytes_in_composed_tree(
                    plan, receipt, allow_additions=True
                )

    def test_execute_uses_selected_make_and_explicit_install_destdir(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            build.mkdir()
            selected_make = "/toolchain/bin/make"
            plan = {
                "build_directory": str(build),
                "stage_directory": str(root / "stage"),
                "staged_product_prefix": str(root / "stage/root/opt/laplace/current"),
                "build_toolchain": {
                    "tools": {
                        "make": {"path": selected_make},
                        "readelf": {"path": "/toolchain/bin/readelf"},
                    }
                },
                "runtime_package": {"receipt_path": str(root / "runtime.json")},
                "configure_command": ["/source/configure"],
                "parallel_jobs": 6,
                "make_targets": ["world-bin", "check-world", "install-world-bin"],
                "build_input_id": "6" * 64,
                "recipe": {"driver": "fixture"},
                "release_prefix": "/opt/laplace/releases/fixture",
            }
            captured: list[list[str]] = []

            def capture(command: object, *_args: object, **_kwargs: object) -> None:
                captured.append(list(command))

            with mock.patch.object(
                BUILD, "prepare_build_directory", return_value=(build, {})
            ), mock.patch.object(
                BUILD, "build_environment", return_value={}
            ), mock.patch.object(
                BUILD, "run_logged", side_effect=capture
            ), mock.patch.object(
                BUILD, "verify_runtime_bytes_in_composed_tree"
            ), mock.patch.object(
                BUILD, "verify_package", return_value={}
            ), mock.patch.object(
                BUILD, "sha256_file", return_value="7" * 64
            ):
                BUILD.execute_plan(self.contract(), plan)
            self.assertEqual(captured[1][0], selected_make)
            self.assertEqual(captured[2][0], selected_make)
            self.assertEqual(captured[3][0], selected_make)
            self.assertIn(
                f"DESTDIR={root / 'stage/root'}", captured[3]
            )

    def test_package_verification_uses_pgsql_subtree_and_selected_readelf(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary) / "product"
            binary = prefix / "pgsql-18/bin"
            binary.mkdir(parents=True)
            (binary / "pg_config").write_text("fixture\n", encoding="utf-8")
            (binary / "postgres").write_text("fixture\n", encoding="utf-8")
            responses = [
                mock.Mock(stdout="PostgreSQL 18.6\n"),
                mock.Mock(stdout="'--enable-tap-tests'\n"),
            ]
            with mock.patch.object(
                BUILD.subprocess, "run", side_effect=responses
            ), mock.patch.object(
                BUILD,
                "package_tree",
                return_value=("8" * 64, 2, 16, {"libc.so.6"}),
            ) as package_tree:
                receipt = BUILD.verify_package(
                    self.contract(), prefix, Path("/toolchain/readelf")
                )
            package_tree.assert_called_once_with(prefix.resolve(), Path("/toolchain/readelf"))
            self.assertEqual(receipt["version"], "PostgreSQL 18.6")

    def test_product_plan_identity_binds_runtime_and_toolchain_receipts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "sources/postgresql"
            source.mkdir(parents=True)
            (source / "configure").write_text("#!/bin/sh\n", encoding="utf-8")
            contract = self.contract()
            contract["execution"]["build_root"] = str(root / "build")
            contract["execution"]["stage_root"] = str(root / "stage")
            contract["execution"]["release_root"] = str(root / "releases")
            compilers = {
                "c": {"path": "/compiler/c", "sha256": "1" * 64},
                "cxx": {"path": "/compiler/cxx", "sha256": "2" * 64},
            }
            toolchain = {
                "receipt_path": str(root / "toolchain.json"),
                "receipt_sha256": "3" * 64,
                "build_input_id": "4" * 64,
                "prefix": "/toolchain",
                "tools": {},
            }
            runtime = {
                "receipt_path": str(root / "runtime.json"),
                "receipt_sha256": "5" * 64,
                "build_input_id": "6" * 64,
                "install_prefix": "/opt/laplace/current",
                "staged_prefix": str(root / "runtime"),
                "tree_sha256": "7" * 64,
                "file_count": 1,
                "total_file_bytes": 1,
                "component_checkpoints": {},
            }
            with mock.patch.object(
                BUILD, "validate_compiler", side_effect=[compilers["c"], compilers["cxx"]] * 2
            ), mock.patch.object(
                BUILD, "verify_toolchain_receipt", return_value=toolchain
            ), mock.patch.object(
                BUILD, "verify_runtime_receipt", return_value=runtime
            ), mock.patch.object(
                BUILD, "verify_release_import", return_value={"verified": True}
            ):
                first = BUILD.create_plan(
                    contract,
                    REPO_ROOT,
                    root / "archives",
                    source,
                    root / "toolchain.json",
                    root / "runtime.json",
                )
                changed_runtime = dict(runtime)
                changed_runtime["receipt_sha256"] = "8" * 64
                with mock.patch.object(
                    BUILD, "verify_runtime_receipt", return_value=changed_runtime
                ):
                    second = BUILD.create_plan(
                        contract,
                        REPO_ROOT,
                        root / "archives",
                        source,
                        root / "toolchain.json",
                        root / "runtime.json",
                    )
            self.assertNotEqual(first["build_input_id"], second["build_input_id"])
            self.assertEqual(first["postgresql_install_prefix"], "/opt/laplace/current/pgsql-18")


if __name__ == "__main__":
    unittest.main()
