#!/usr/bin/env python3
"""Contract and mutation tests for the source-built toolchain package."""

from __future__ import annotations

import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools/toolchain/build-package.py"
SPEC = importlib.util.spec_from_file_location("laplace_toolchain_build", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
BUILD = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BUILD
SPEC.loader.exec_module(BUILD)


class ToolchainBuildTests(unittest.TestCase):
    def contract(self) -> dict[str, object]:
        return json.loads(
            (REPO_ROOT / "contracts/toolchain-build.json").read_text(encoding="utf-8")
        )

    def test_current_contract_is_valid(self) -> None:
        BUILD.validate_contract(self.contract(), REPO_ROOT)

    def test_dependency_order_is_exact(self) -> None:
        contract = self.contract()
        order = contract["build"]["component_order"]
        order[0], order[1] = order[1], order[0]
        with self.assertRaisesRegex(BUILD.ToolchainError, "dependency order"):
            BUILD.validate_contract(contract)

    def test_every_upstream_component_requires_a_test_suite(self) -> None:
        contract = self.contract()
        contract["build"]["components"]["cmake"]["test"] = []
        with self.assertRaisesRegex(BUILD.ToolchainError, "cmake.test"):
            BUILD.validate_contract(contract)

    def test_upstream_test_command_cannot_be_narrowed(self) -> None:
        contract = self.contract()
        contract["build"]["components"]["gnu-binutils"]["test"].append(
            "RUNTESTFLAGS=binutils-all/ar.exp"
        )
        with self.assertRaisesRegex(BUILD.ToolchainError, "complete selected suite"):
            BUILD.validate_contract(contract)

    def test_tcl_and_expect_must_use_shared_selected_prefix_linkage(self) -> None:
        for component_id in ("tcl", "expect"):
            with self.subTest(component_id):
                contract = self.contract()
                configure = contract["build"]["components"][component_id]["configure"]
                configure[configure.index("--enable-shared")] = "--disable-shared"
                with self.assertRaisesRegex(BUILD.ToolchainError, "shared linkage"):
                    BUILD.validate_contract(contract)

    def test_component_working_directory_cannot_enter_repository(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = root / "repository"
            repository.mkdir()
            external = root / "external"
            external.mkdir()
            self.assertEqual(
                BUILD.component_working_directory(
                    "tcl",
                    "private-copy-out-of-tree",
                    external / "source",
                    external / "build",
                    repository,
                ),
                external / "build",
            )
            with self.assertRaisesRegex(BUILD.ToolchainError, "outside the repository"):
                BUILD.component_working_directory(
                    "tcl",
                    "private-copy-out-of-tree",
                    external / "source",
                    repository,
                    repository,
                )

    def test_logged_process_receives_and_receipts_exact_working_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            working_directory = root / "work"
            working_directory.mkdir()
            log = root / "command.log"
            result = BUILD.run_logged(
                [
                    "/usr/bin/python3",
                    "-c",
                    "import os, sys; sys.exit(os.environ.get('PWD') != sys.argv[1])",
                    str(working_directory),
                ],
                working_directory,
                {"PATH": "/usr/bin:/bin"},
                log,
            )
            self.assertEqual(result["working_directory"], str(working_directory))
            self.assertEqual(
                result["execution_environment"], {"PWD": str(working_directory)}
            )

    def test_logged_process_rejects_divergent_caller_pwd(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            working_directory = root / "work"
            working_directory.mkdir()
            with self.assertRaisesRegex(BUILD.ToolchainError, "PWD differs"):
                BUILD.run_logged(
                    ["/usr/bin/true"],
                    working_directory,
                    {"PATH": "/usr/bin:/bin", "PWD": "/ambient/or/wrong"},
                    root / "command.log",
                )

    def test_selected_dynamic_linkage_rejects_host_runpath(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary) / "toolchain"
            provider = prefix / "lib/libtcl8.6.so"
            provider.parent.mkdir(parents=True)
            provider.write_bytes(b"selected tcl")
            receipt = {
                "path": str(prefix / "bin/tclsh8.6"),
                "sha256": "a" * 64,
                "needed": ["libc.so.6", "libtcl8.6.so"],
                "runpaths": ["/usr/lib"],
            }
            with self.assertRaisesRegex(BUILD.ToolchainError, "RUNPATH differs"):
                BUILD.verify_dynamic_linkage(
                    receipt,
                    prefix,
                    {"libtcl8.6.so": provider},
                    [prefix / "lib"],
                )

    def test_binutils_cannot_start_without_selected_expect_and_runtest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary)
            with self.assertRaisesRegex(BUILD.ToolchainError, "selected expect"):
                BUILD.verify_component_prerequisites("gnu-binutils", prefix)
            (prefix / "bin").mkdir()
            shutil.copy2("/usr/bin/true", prefix / "bin/expect")
            with self.assertRaisesRegex(BUILD.ToolchainError, "selected runtest"):
                BUILD.verify_component_prerequisites("gnu-binutils", prefix)

    def test_binutils_configure_cannot_drop_dejagnu_tools(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "configure.log"
            log.write_text(
                "checking for expect... expect\nchecking for runtest... no\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(BUILD.ToolchainError, "did not select runtest"):
                BUILD.verify_component_configure_log("gnu-binutils", log)

    def test_binutils_test_policy_rejects_ambient_optional_llvm_tools(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            ambient = root / "ambient"
            ambient.mkdir()
            shutil.copy2("/usr/bin/true", ambient / "clang")
            shutil.copy2("/usr/bin/true", ambient / "llvm-config")
            component = self.contract()["build"]["components"]["gnu-binutils"]
            environment, receipt = BUILD.step_environment(
                component,
                "gnu-binutils",
                "test",
                root,
                {
                    "PATH": f"{ambient}:/usr/bin:/bin",
                    "SOURCE_DATE_EPOCH": "1",
                },
            )
            self.assertNotIn("SOURCE_DATE_EPOCH", environment)
            for command in ("clang", "llvm-config"):
                path = Path(receipt["unselected_optional_commands"][command]["path"])
                self.assertEqual(shutil.which(command, path=environment["PATH"]), str(path))
                self.assertTrue(path.is_file())
                path.unlink()
                self.assertEqual(
                    shutil.which(command, path=environment["PATH"]),
                    str(ambient / command),
                )

    def test_binutils_test_cannot_leak_source_date_epoch(self) -> None:
        contract = self.contract()
        contract["build"]["components"]["gnu-binutils"]["test_policy"][
            "unset_environment"
        ] = []
        with self.assertRaisesRegex(BUILD.ToolchainError, "without SOURCE_DATE_EPOCH"):
            BUILD.validate_contract(contract)

    def test_unselected_gprofng_cannot_enter_configuration_or_manifest(self) -> None:
        for mutation in ("configure", "tools"):
            with self.subTest(mutation=mutation):
                contract = self.contract()
                component = contract["build"]["components"]["gnu-binutils"]
                if mutation == "configure":
                    component["configure"].remove("--disable-gprofng")
                else:
                    component["tools"].append("gprofng")
                with self.assertRaisesRegex(BUILD.ToolchainError, "gprofng"):
                    BUILD.validate_contract(contract)

    def test_dejagnu_unexpected_outcomes_are_rejected_even_after_zero_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy = self.contract()["build"]["components"]["gnu-binutils"][
                "test_policy"
            ]
            (root / "binutils.sum").write_text(
                "PASS: stable\nXFAIL: known upstream expectation\nFAIL: regression\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(BUILD.ToolchainError, "forbidden outcomes"):
                BUILD.verify_dejagnu_results("gnu-binutils", root, policy)

    def test_dejagnu_optional_llvm_exclusions_require_capability_binding(self) -> None:
        for mutation, expected_error in (
            ("missing", "capability_id"),
            ("wrong", "unknown optional capability"),
        ):
            with self.subTest(mutation=mutation):
                contract = self.contract()
                entry = contract["build"]["components"]["gnu-binutils"][
                    "test_policy"
                ]["expected_outcomes"]["UNTESTED"][0]
                if mutation == "missing":
                    entry.pop("capability_id")
                else:
                    entry["capability_id"] = "ambient-llvm"
                with self.assertRaisesRegex(BUILD.ToolchainError, expected_error):
                    BUILD.validate_contract(contract)

    def test_dejagnu_dispositions_and_lines_are_typed_unique_contracts(self) -> None:
        for mutation, expected_error in (
            ("missing-disposition", "disposition"),
            ("wrong-disposition", "unknown disposition"),
            ("duplicate-line", "duplicate expected outcome line"),
        ):
            with self.subTest(mutation=mutation):
                contract = self.contract()
                entries = contract["build"]["components"]["gnu-binutils"][
                    "test_policy"
                ]["expected_outcomes"]["UNTESTED"]
                if mutation == "missing-disposition":
                    entries[0].pop("disposition")
                elif mutation == "wrong-disposition":
                    entries[0]["disposition"] = "skip"
                else:
                    entries[1]["line"] = entries[0]["line"]
                with self.assertRaisesRegex(BUILD.ToolchainError, expected_error):
                    BUILD.validate_contract(contract)

    def test_clean_dejagnu_summaries_are_receipted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy = self.contract()["build"]["components"]["gnu-binutils"][
                "test_policy"
            ]
            expected = policy["expected_outcomes"]["UNTESTED"]
            (root / "binutils.sum").write_text(
                "PASS: stable\nXFAIL: known upstream expectation\nUNSUPPORTED: other target\n"
                + "\n".join(entry["line"] for entry in expected)
                + "\n",
                encoding="utf-8",
            )
            (root / "ld").mkdir()
            (root / "ld/ld.log").write_text(
                "compiler invocation -static cs1.c -o cs1.exe\n"
                "compiler invocation -static-pie cs2.c -o cs2.exe\n"
                "--plugin NAME      Load the specified plugin\n",
                encoding="utf-8",
            )
            receipt = BUILD.verify_dejagnu_results("gnu-binutils", root, policy)
            self.assertEqual(receipt["counts"]["PASS"], 1)
            self.assertEqual(receipt["counts"]["XFAIL"], 1)
            self.assertEqual(receipt["counts"]["UNTESTED"], 7)
            self.assertEqual(
                receipt["adjudications"]["UNTESTED"][0]["capability_id"],
                "llvm-lto-plugin-tests",
            )
            self.assertEqual(
                len(receipt["adjudications"]["UNTESTED"][-1]["evidence"]), 2
            )
            self.assertEqual(len(receipt["summary_files"]), 1)

    def test_static_bootstrap_disposition_requires_observed_plugin_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy = self.contract()["build"]["components"]["gnu-binutils"][
                "test_policy"
            ]
            expected = policy["expected_outcomes"]["UNTESTED"]
            (root / "binutils.sum").write_text(
                "\n".join(entry["line"] for entry in expected) + "\n",
                encoding="utf-8",
            )
            (root / "ld").mkdir()
            (root / "ld/ld.log").write_text(
                "compiler invocation -static cs1.c -o cs1.exe\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(BUILD.ToolchainError, "evidence predicate"):
                BUILD.verify_dejagnu_results("gnu-binutils", root, policy)

    def test_perl_local_path_defaults_are_fail_closed(self) -> None:
        contract = self.contract()
        configure = contract["build"]["components"]["perl"]["configure"]
        configure[configure.index("-Dlocincpth= ")] = "-Dlocincpth="
        with self.assertRaisesRegex(BUILD.ToolchainError, "disable local paths"):
            BUILD.validate_contract(contract)

    def test_perl_configure_output_rejects_local_library_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            (source / "config.sh").write_text(
                "\n".join(
                    (
                        "locincpth=' '",
                        "loclibpth=' '",
                        "glibpth='/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib /lib'",
                        "libpth='/usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib'",
                        "ccflags='-O2'",
                        "cppflags='-O2'",
                        "ldflags='-O2'",
                    )
                )
                + "\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(BUILD.ToolchainError, "system ABI library paths"):
                BUILD.verify_component_configuration("perl", source)

    def test_perl_configure_output_accepts_receipted_system_abi_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            includes = (
                "-nostdinc -isystem /usr/lib/gcc/x86_64-linux-gnu/11/include "
                "-isystem /usr/include/x86_64-linux-gnu -isystem /usr/include"
            )
            libraries = "/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib /lib"
            (source / "config.sh").write_text(
                "\n".join(
                    (
                        "locincpth=' '",
                        "loclibpth=' '",
                        f"glibpth='{libraries}'",
                        f"libpth='{libraries}'",
                        f"ccflags='-O2 {includes}'",
                        f"cppflags='{includes}'",
                        "ldflags='-O2'",
                    )
                )
                + "\n",
                encoding="utf-8",
            )
            result = BUILD.verify_component_configuration("perl", source)
            self.assertEqual(result["status"], "verified")

    def test_canonical_source_generation_cannot_be_a_build_directory(self) -> None:
        contract = self.contract()
        contract["build"]["components"]["gnu-make"]["source_mode"] = "immutable-out-of-tree"
        with self.assertRaisesRegex(BUILD.ToolchainError, "source mode"):
            BUILD.validate_contract(contract)

    def test_private_source_is_imported_from_locked_archive_recipe(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = root / "repository"
            work = root / "work"
            lock = repository / "dependencies/release-lock.json"
            lock.parent.mkdir(parents=True)
            lock.write_text(
                json.dumps({"archives": {"perl": {"version": "5.44.0"}}}),
                encoding="utf-8",
            )
            calls: list[tuple[str, Path]] = []

            class FakeRelease:
                @staticmethod
                def import_entry(
                    component_id: str,
                    entry: dict[str, object],
                    archive_root: Path,
                    destination: Path,
                ) -> None:
                    calls.append(("import", destination))
                    source = destination / component_id
                    source.mkdir(parents=True)
                    (source / "Configure").write_text("locked archive bytes", encoding="utf-8")

                @staticmethod
                def verify_imported_entry(
                    component_id: str,
                    entry: dict[str, object],
                    archive_root: Path,
                    destination: Path,
                ) -> None:
                    calls.append(("verify", destination))
                    self.assertEqual(
                        (destination / component_id / "Configure").read_text(encoding="utf-8"),
                        "locked archive bytes",
                    )

            contract = self.contract()
            contract["release_lock"] = "dependencies/release-lock.json"
            contract["logical_roots"]["archive_root"] = str(root / "archives")
            contract["logical_roots"]["source_generation"] = str(
                root / "must-not-be-read"
            )
            with mock.patch.object(BUILD, "load_release_module", return_value=FakeRelease):
                source = BUILD.component_source(contract, repository, "perl", work)
            self.assertEqual(source, work / "private-sources/perl")
            self.assertEqual(
                calls,
                [("import", source.parent), ("verify", source.parent)],
            )

    def test_private_source_timestamps_are_wall_clock_independent(self) -> None:
        epoch = 1_787_616_000
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            observed: list[list[tuple[str, int]]] = []
            for index, wall_time in enumerate((1_600_000_000, 1_900_000_000)):
                source = root / f"source-{index}"
                nested = source / "nested"
                nested.mkdir(parents=True)
                regular = nested / "input.txt"
                regular.write_text("same bytes", encoding="utf-8")
                link = source / "input-link"
                link.symlink_to("nested/input.txt")
                for path in (regular, nested, source):
                    BUILD.os.utime(path, (wall_time, wall_time), follow_symlinks=False)
                BUILD.normalize_source_timestamps(source, epoch)
                self.assertEqual(
                    BUILD.verify_normalized_source_timestamps(source, epoch), 3
                )
                observed.append(
                    [
                        (str(path.relative_to(source)), path.stat().st_mtime_ns)
                        for path in (source, nested, regular)
                    ]
                )
            self.assertEqual(observed[0], observed[1])

    def test_product_runtime_activation_claim_is_rejected(self) -> None:
        contract = self.contract()
        contract["activation"]["product_runtime_activation_eligible"] = True
        with self.assertRaisesRegex(BUILD.ToolchainError, "never claim"):
            BUILD.validate_contract(contract)

    def test_ambient_tool_and_loader_state_is_rejected(self) -> None:
        contract = self.contract()
        for variable in ("CC", "MAKEFLAGS", "LD_LIBRARY_PATH", "PKG_CONFIG_PATH", "PERL"):
            with self.subTest(variable=variable):
                with self.assertRaisesRegex(BUILD.ToolchainError, variable):
                    BUILD.validate_environment(contract, {variable: "/ambient"})

    def test_repository_local_build_roots_are_rejected(self) -> None:
        contract = self.contract()
        contract["logical_roots"]["build_root"] = str(REPO_ROOT / "build")
        with self.assertRaisesRegex(BUILD.ToolchainError, "outside the repository"):
            BUILD.validate_contract(contract, REPO_ROOT)

    def test_duplicate_json_key_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "duplicate.json"
            path.write_text('{"schema":"a","schema":"b"}', encoding="utf-8")
            with self.assertRaisesRegex(BUILD.ToolchainError, "duplicate JSON object key"):
                BUILD.read_json(path)

    def test_build_recipe_identity_changes_with_driver(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repository = Path(temporary)
            paths = (
                repository / "tools/toolchain/build-package.py",
                repository / "tools/dependencies/release-assets.py",
                repository / "dependencies/release-lock.json",
            )
            for path in paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(path.name, encoding="utf-8")
            first = BUILD.recipe_identity(self.contract(), repository)
            paths[0].write_text("mutated-driver", encoding="utf-8")
            second = BUILD.recipe_identity(self.contract(), repository)
            self.assertNotEqual(first["driver"]["sha256"], second["driver"]["sha256"])
            self.assertEqual(
                first["release_verifier"]["sha256"],
                second["release_verifier"]["sha256"],
            )

    def test_resume_requires_exact_input_addressed_plan(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            work = root / "work"
            prefix = root / "stage/toolchain"
            for path in (build, work, prefix):
                path.mkdir(parents=True)
            plan = {
                "build_directory": str(build),
                "work_directory": str(work),
                "prefix": str(prefix),
                "build_input_id": "a" * 64,
            }
            (build / "build-plan.json").write_text(
                json.dumps({**plan, "mutated": True}), encoding="utf-8"
            )
            with self.assertRaisesRegex(BUILD.ToolchainError, "exact persisted"):
                BUILD.prepare_plan(plan, resume=True)

    def fake_package(self, root: Path) -> tuple[Path, dict[str, object]]:
        prefix = root / "toolchain"
        binary_directory = prefix / "bin"
        binary_directory.mkdir(parents=True)
        tools: dict[str, dict[str, str]] = {}
        for tool_id in sorted(BUILD.REQUIRED_TOOL_IDS):
            path = binary_directory / tool_id
            shutil.copy2("/usr/bin/true", path)
            tools[tool_id] = {
                "path": str(path),
                "sha256": BUILD.sha256_file(path),
                "version": "synthetic tool v1",
            }
        manifest = {
            "schema": BUILD.CONSUMER_SCHEMA,
            "build_input_id": "b" * 64,
            "prefix": str(prefix),
            "tools": tools,
            "activation": {
                "scope": "build-toolchain-only",
                "product_runtime_activation_eligible": False,
            },
        }
        manifest_path = prefix / "share/laplace/toolchain-manifest.json"
        manifest_path.parent.mkdir(parents=True)
        manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        return prefix, manifest

    def test_consumer_manifest_requires_exact_selected_tool_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix, manifest = self.fake_package(Path(temporary))
            BUILD.verify_consumer_manifest(manifest, prefix)
            manifest["tools"]["readelf"]["sha256"] = "0" * 64
            with self.assertRaisesRegex(BUILD.ToolchainError, "readelf"):
                BUILD.verify_consumer_manifest(manifest, prefix)

    def test_consumer_manifest_cannot_escape_prefix(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix, manifest = self.fake_package(Path(temporary))
            manifest["tools"]["cmake"]["path"] = "/usr/bin/true"
            with self.assertRaisesRegex(BUILD.ToolchainError, "escapes"):
                BUILD.verify_consumer_manifest(manifest, prefix)

    def test_package_receipt_requires_compiler_and_linker_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix, manifest = self.fake_package(Path(temporary))
            receipt = {
                "schema": BUILD.PACKAGE_SCHEMA,
                "build_input_id": manifest["build_input_id"],
                "package_tree": BUILD.package_tree(prefix),
                "compiler_driver_traces": {},
                "linker_map_inputs": [],
                "activation": {"product_runtime_activation_eligible": False},
            }
            with self.assertRaisesRegex(BUILD.ToolchainError, "compiler driver"):
                BUILD.verify_package(self.contract(), prefix, receipt)

    def test_package_receipt_rejects_runtime_activation_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix, manifest = self.fake_package(Path(temporary))
            receipt = {
                "schema": BUILD.PACKAGE_SCHEMA,
                "build_input_id": manifest["build_input_id"],
                "package_tree": BUILD.package_tree(prefix),
                "compiler_driver_traces": {"c": {"trace": "present"}},
                "linker_map_inputs": [{"path": "/static", "sha256": "a" * 64}],
                "activation": {"product_runtime_activation_eligible": True},
            }
            with self.assertRaisesRegex(BUILD.ToolchainError, "illegally claims"):
                BUILD.verify_package(self.contract(), prefix, receipt)


if __name__ == "__main__":
    unittest.main()
