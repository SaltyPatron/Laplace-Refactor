#!/usr/bin/env python3
"""Contract and mutation tests for the source-built toolchain package."""

from __future__ import annotations

import importlib.util
import json
import os
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

    def test_postgresql_tap_perl_modules_cannot_be_omitted(self) -> None:
        contract = self.contract()
        contract["build"]["components"]["ipc-run"]["perl_modules"] = []
        with self.assertRaisesRegex(BUILD.ToolchainError, "at least one tool or Perl module"):
            BUILD.validate_contract(contract)

    def test_postgresql_tap_perl_module_suite_cannot_be_narrowed(self) -> None:
        contract = self.contract()
        contract["build"]["components"]["io-tty"]["test"] = ["{make}", "test", "TEST_FILES=t/constants.t"]
        with self.assertRaisesRegex(BUILD.ToolchainError, "complete selected suite"):
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
                [min(os.sched_getaffinity(0))],
            )
            self.assertEqual(result["working_directory"], str(working_directory))
            self.assertEqual(
                result["execution_environment"], {"PWD": str(working_directory)}
            )
            self.assertEqual(result["processor_affinity"], [min(os.sched_getaffinity(0))])

    def test_logged_process_inherits_the_declared_processor_affinity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            processor_id = min(os.sched_getaffinity(0))
            result = BUILD.run_logged(
                [
                    "/usr/bin/python3",
                    "-c",
                    (
                        "import os,sys; "
                        "sys.exit(os.sched_getaffinity(0) != {int(sys.argv[1])})"
                    ),
                    str(processor_id),
                ],
                root,
                {"PATH": "/usr/bin:/bin"},
                root / "affinity.log",
                [processor_id],
            )
            self.assertEqual(result["processor_affinity"], [processor_id])

    def test_processor_affinity_prefers_distinct_physical_cores(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            topology_root = Path(temporary)
            assignments = {
                0: (0, 0),
                1: (0, 1),
                2: (0, 0),
                3: (0, 1),
            }
            for processor_id, (package_id, core_id) in assignments.items():
                directory = topology_root / f"cpu{processor_id}/topology"
                directory.mkdir(parents=True)
                (directory / "physical_package_id").write_text(
                    str(package_id), encoding="ascii"
                )
                (directory / "core_id").write_text(str(core_id), encoding="ascii")
            with mock.patch.object(BUILD.os, "sched_getaffinity", return_value=set(assignments)):
                selection = BUILD.select_processor_affinity(3, topology_root)
            self.assertEqual(selection["selected_processor_ids"], [0, 1, 2])
            self.assertEqual(
                selection["selected_processors"],
                [
                    {"processor_id": 0, "package_id": 0, "core_id": 0},
                    {"processor_id": 1, "package_id": 0, "core_id": 1},
                    {"processor_id": 2, "package_id": 0, "core_id": 0},
                ],
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

    def test_cmake_optional_test_capabilities_and_nested_make_are_exact(self) -> None:
        mutations = {
            "drop_java_gate": lambda component: component["configure"].remove(
                "-DCMake_TEST_Java=OFF"
            ),
            "disable_selected_bootstrap_test": lambda component: component["configure"].__setitem__(
                component["configure"].index("-DCMake_TEST_BOOTSTRAP=ON"),
                "-DCMake_TEST_BOOTSTRAP=OFF",
            ),
            "ambient_primary_make": lambda component: component["configure"].__setitem__(
                component["configure"].index("-DCMAKE_MAKE_PROGRAM={make}"),
                "-DCMAKE_MAKE_PROGRAM=/usr/bin/gmake",
            ),
            "ambient_nested_make": lambda component: component["configure"].__setitem__(
                component["configure"].index(
                    "-DCMake_TEST_EXPLICIT_MAKE_PROGRAM={make}"
                ),
                "-DCMake_TEST_EXPLICIT_MAKE_PROGRAM=/usr/bin/gmake",
            ),
            "missing_result_policy": lambda component: component[
                "test_capability_selection"
            ].pop("result_policy"),
            "narrowed_selected_suite": lambda component: component[
                "test_capability_selection"
            ]["result_policy"].__setitem__("expected_selected_test_count", 1),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                contract = self.contract()
                mutate(contract["build"]["components"]["cmake"])
                with self.assertRaisesRegex(BUILD.ToolchainError, "cmake"):
                    BUILD.validate_contract(contract)

    def test_cmake_configure_cache_proves_selected_test_capabilities(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            selected_make = root / "prefix/bin/make"
            selected_make.parent.mkdir(parents=True)
            selected_make.touch()
            values = {
                **BUILD.EXPECTED_CMAKE_TEST_CAPABILITY_CACHE,
                "CMAKE_MAKE_PROGRAM": str(selected_make),
                "CMake_TEST_EXPLICIT_MAKE_PROGRAM": str(selected_make),
            }
            cache = root / "CMakeCache.txt"
            cache.write_text(
                "".join(f"{key}:STRING={value}\n" for key, value in values.items()),
                encoding="utf-8",
            )
            generated = root / "Tests/CTestTestfile.cmake"
            generated.parent.mkdir()
            generated.write_text(
                f'add_test("nested" "--build-makeprogram" "{selected_make}")\n',
                encoding="utf-8",
            )
            receipt = BUILD.verify_cmake_test_configuration(
                "cmake", root, selected_make
            )
            self.assertEqual(receipt["selected"], values)
            generated.write_text(
                'add_test("nested" "--build-makeprogram" "/usr/bin/gmake")\n',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(BUILD.ToolchainError, "outside the selected"):
                BUILD.verify_cmake_test_configuration("cmake", root, selected_make)
            generated.write_text(
                f'add_test("nested" "--build-makeprogram" "{selected_make}")\n',
                encoding="utf-8",
            )
            cache.write_text(
                cache.read_text(encoding="utf-8").replace(
                    "CMake_TEST_Java:STRING=OFF", "CMake_TEST_Java:STRING=ON"
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(BUILD.ToolchainError, "capability cache differs"):
                BUILD.verify_cmake_test_configuration("cmake", root, selected_make)

    def test_cmake_test_results_prove_execution_not_generated_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            selected_make = root / "prefix/bin/make"
            selected_make.parent.mkdir(parents=True)
            selected_make.touch()
            test_log = root / "test.log"
            test_log.write_text(
                "697/697 Test #212: BootstrapTest ....   Passed  1.00 sec\n"
                "100% tests passed out of 697\n",
                encoding="utf-8",
            )
            last_test = root / "Testing/Temporary/LastTest.log"
            last_test.parent.mkdir(parents=True)
            last_test.write_text(
                '"BootstrapTest" start time: now\n'
                "-- running bootstrap: /source/bootstrap --parallel=6\n"
                f"Makefile processor on this system is: {selected_make}\n"
                f"CMake has bootstrapped.  Now run {selected_make}.\n"
                "Test Passed.\n"
                '"BootstrapTest" end time: later\n',
                encoding="utf-8",
            )
            secondary = root / "Tests/BootstrapTest"
            secondary.mkdir(parents=True)
            (secondary / "CMakeCache.txt").write_text(
                "CMAKE_MAKE_PROGRAM:FILEPATH=/usr/bin/gmake\n", encoding="utf-8"
            )
            (secondary / "CTestTestfile.cmake").write_text(
                'add_test("generated" "--build-makeprogram" "/usr/bin/gmake")\n',
                encoding="utf-8",
            )

            receipt = BUILD.verify_cmake_test_results(
                "cmake",
                root,
                test_log,
                selected_make,
                6,
                BUILD.EXPECTED_CMAKE_TEST_RESULT_POLICY,
            )
            self.assertEqual(receipt["selected_suite"]["test_count"], 697)
            self.assertEqual(
                receipt["bootstrap_execution"]["make_program"], str(selected_make)
            )
            self.assertEqual(
                receipt["secondary_generated_suite"]["configured_make_program"],
                "/usr/bin/gmake",
            )
            self.assertEqual(
                receipt["secondary_generated_suite"]["other_make_reference_count"],
                1,
            )
            self.assertEqual(
                receipt["secondary_generated_suite"]["disposition"],
                "configured-but-not-executed-by-bootstrap-test",
            )

            mutations = {
                "ambient_executed_make": lambda: last_test.write_text(
                    last_test.read_text(encoding="utf-8").replace(
                        str(selected_make), "/usr/bin/gmake"
                    ),
                    encoding="utf-8",
                ),
                "expanded_parallelism": lambda: last_test.write_text(
                    last_test.read_text(encoding="utf-8").replace(
                        "--parallel=6", "--parallel=12"
                    ),
                    encoding="utf-8",
                ),
                "incomplete_suite": lambda: test_log.write_text(
                    test_log.read_text(encoding="utf-8").replace(
                        "100% tests passed out of 697", "99% tests passed out of 697"
                    ),
                    encoding="utf-8",
                ),
            }
            original_last_test = last_test.read_text(encoding="utf-8")
            original_test_log = test_log.read_text(encoding="utf-8")
            for name, mutate in mutations.items():
                with self.subTest(name=name):
                    last_test.write_text(original_last_test, encoding="utf-8")
                    test_log.write_text(original_test_log, encoding="utf-8")
                    mutate()
                    with self.assertRaises(BUILD.ToolchainError):
                        BUILD.verify_cmake_test_results(
                            "cmake",
                            root,
                            test_log,
                            selected_make,
                            6,
                            BUILD.EXPECTED_CMAKE_TEST_RESULT_POLICY,
                        )

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
        perl = binary_directory / "perl"
        perl.write_text(
            "#!/usr/bin/python3\n"
            "import json, os, pathlib, sys\n"
            "module = sys.argv[-1]\n"
            "versions = {'IO::Pty':'1.31','IO::Tty':'1.31','IPC::Run':'20260402.0'}\n"
            "suffixes = {'IO::Pty':'IO/Pty.pm','IO::Tty':'IO/Tty.pm','IPC::Run':'IPC/Run.pm'}\n"
            "prefix = pathlib.Path(os.path.abspath(sys.argv[0])).parent.parent\n"
            "provider = next(p for p in prefix.rglob(pathlib.Path(suffixes[module]).name) if p.as_posix().endswith(suffixes[module]))\n"
            "print(json.dumps({'module':module,'provider':str(provider),'version':versions[module]},sort_keys=True))\n",
            encoding="utf-8",
        )
        perl.chmod(0o755)
        tools["perl"] = {
            "path": str(perl),
            "sha256": BUILD.sha256_file(perl),
            "version": "synthetic Perl v1",
        }
        site = prefix / "lib/perl5/site_perl/5.44.0"
        for relative in ("IO/Pty.pm", "IO/Tty.pm", "IPC/Run.pm"):
            provider = site / relative
            provider.parent.mkdir(parents=True, exist_ok=True)
            provider.write_text(f"synthetic {relative}\n", encoding="utf-8")
        native = site / "x86_64-linux/auto/IO/Tty/Tty.so"
        native.parent.mkdir(parents=True)
        native.write_bytes(b"synthetic native provider\n")
        perl_modules = BUILD.installed_perl_module_receipts(prefix)
        manifest = {
            "schema": BUILD.CONSUMER_SCHEMA,
            "build_input_id": "b" * 64,
            "prefix": str(prefix),
            "tools": tools,
            "perl_modules": perl_modules,
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

    def test_consumer_manifest_detects_selected_perl_module_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix, manifest = self.fake_package(Path(temporary))
            BUILD.verify_consumer_manifest(manifest, prefix)
            provider = Path(manifest["perl_modules"]["IPC::Run"]["path"])
            provider.write_text("mutated IPC::Run provider\n", encoding="utf-8")
            with self.assertRaisesRegex(BUILD.ToolchainError, "Perl module receipts differ"):
                BUILD.verify_consumer_manifest(manifest, prefix)

    def test_consumer_manifest_detects_selected_native_perl_provider_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix, manifest = self.fake_package(Path(temporary))
            BUILD.verify_consumer_manifest(manifest, prefix)
            provider = Path(
                manifest["perl_modules"]["IO::Tty"]["native_providers"][0]["path"]
            )
            provider.write_bytes(b"mutated native provider\n")
            with self.assertRaisesRegex(BUILD.ToolchainError, "Perl module receipts differ"):
                BUILD.verify_consumer_manifest(manifest, prefix)

    def test_consumer_manifest_cannot_escape_prefix(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix, manifest = self.fake_package(Path(temporary))
            manifest["tools"]["cmake"]["path"] = "/usr/bin/true"
            with self.assertRaisesRegex(BUILD.ToolchainError, "escapes"):
                BUILD.verify_consumer_manifest(manifest, prefix)

    def test_logical_package_prefix_can_map_to_a_separate_physical_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            physical_prefix, manifest = self.fake_package(root / "physical")
            logical_prefix = root / "logical-toolchain"
            logical_prefix.symlink_to(physical_prefix, target_is_directory=True)
            manifest["prefix"] = str(logical_prefix)
            for tool in manifest["tools"].values():
                relative = Path(tool["path"]).relative_to(physical_prefix)
                tool["path"] = str(logical_prefix / relative)
            for module in manifest["perl_modules"].values():
                relative = Path(module["path"]).relative_to(physical_prefix)
                module["path"] = str(logical_prefix / relative)
                for provider in module["native_providers"]:
                    relative = Path(provider["path"]).relative_to(physical_prefix)
                    provider["path"] = str(logical_prefix / relative)
            manifest_path = physical_prefix / "share/laplace/toolchain-manifest.json"
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            receipt = {
                "schema": BUILD.PACKAGE_SCHEMA,
                "build_input_id": manifest["build_input_id"],
                "package_tree": BUILD.package_tree(logical_prefix),
                "installed_perl_modules": manifest["perl_modules"],
                "compiler_driver_traces": {"c": {"trace": "present"}},
                "linker_map_inputs": [{"path": "/static", "sha256": "a" * 64}],
                "activation": {"product_runtime_activation_eligible": False},
            }
            result = BUILD.verify_package(
                self.contract(), logical_prefix, receipt
            )
            self.assertEqual(result["prefix"], str(logical_prefix))
            self.assertEqual(result["physical_prefix"], str(physical_prefix))
            with self.assertRaisesRegex(BUILD.ToolchainError, "prefix differs"):
                BUILD.verify_package(self.contract(), physical_prefix, receipt)

    def test_consumer_manifest_rejects_physical_symlink_escape(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prefix, manifest = self.fake_package(root)
            escape = prefix / "bin/escape"
            escape.symlink_to("/usr/bin/true")
            manifest["tools"]["cmake"] = {
                "path": str(escape),
                "sha256": BUILD.sha256_file(Path("/usr/bin/true")),
                "version": "synthetic escaped tool v1",
            }
            with self.assertRaisesRegex(BUILD.ToolchainError, "physical package prefix"):
                BUILD.verify_consumer_manifest(manifest, prefix)

    def test_package_receipt_requires_compiler_and_linker_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix, manifest = self.fake_package(Path(temporary))
            receipt = {
                "schema": BUILD.PACKAGE_SCHEMA,
                "build_input_id": manifest["build_input_id"],
                "package_tree": BUILD.package_tree(prefix),
                "installed_perl_modules": manifest["perl_modules"],
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
                "installed_perl_modules": manifest["perl_modules"],
                "compiler_driver_traces": {"c": {"trace": "present"}},
                "linker_map_inputs": [{"path": "/static", "sha256": "a" * 64}],
                "activation": {"product_runtime_activation_eligible": True},
            }
            with self.assertRaisesRegex(BUILD.ToolchainError, "illegally claims"):
                BUILD.verify_package(self.contract(), prefix, receipt)


    def checkpoint_fixture(self, root: Path, processor_ids=None):
        """Real small child commands; these are checkpoint controls, not upstream builds."""
        prefix = root / "stage"
        prefix.mkdir()
        (prefix / "kept").write_text("retained provider bytes")
        plan = {
            "build_input_id": "a" * 64, "component_order": list(BUILD.EXPECTED_ORDER),
            "build_directory": str(root / "build"),
            "source_normalization": {"source_date_epoch": "1787616000"},
            "processor_affinity": {"selected_processor_ids": processor_ids or [min(os.sched_getaffinity(0))]},
        }
        component = BUILD.EXPECTED_ORDER[0]
        directory = root / "build/components" / component
        directory.mkdir(parents=True)
        steps = []
        for name in ("configure", "build", "test", "install"):
            steps.append(BUILD.run_logged([sys.executable, "-c", "print('checkpoint control')"],
                directory, {"PATH": "/usr/bin:/bin"}, directory / (name + ".log"),
                plan["processor_affinity"]["selected_processor_ids"]))
        normalization = {component: {"source_date_epoch": "1787616000", "normalized_object_count": 1}}
        path = root / "build/completed-components.json"
        BUILD.write_checkpoint(plan, prefix, path, [component], {component: steps}, normalization)
        return plan, prefix, path, {component: steps}, normalization

    def test_resume_preserves_actual_prior_command_receipts_and_normalization(self):
        with tempfile.TemporaryDirectory() as temporary:
            plan, prefix, path, steps, normalization = self.checkpoint_fixture(Path(temporary))
            completed, observed_steps, observed_normalization = BUILD.read_checkpoint(plan, prefix, path)
            self.assertEqual(completed, BUILD.EXPECTED_ORDER[:1])
            self.assertEqual(observed_steps, steps)
            self.assertEqual(observed_normalization, normalization)
            self.assertEqual(len(observed_steps[completed[0]]), 4)
            self.assertFalse(path.with_name(path.name + ".partial").exists())

    def test_checkpoint_preserves_actual_affinity_when_topology_order_is_not_numeric(self):
        processors = sorted(os.sched_getaffinity(0))
        if len(processors) < 2:
            self.skipTest("requires two available processors for the actual affinity control")
        selected = [processors[1], processors[0]]
        with tempfile.TemporaryDirectory() as temporary:
            plan, prefix, path, steps, _ = self.checkpoint_fixture(Path(temporary), selected)
            _, observed, _ = BUILD.read_checkpoint(plan, prefix, path)
            self.assertEqual(plan["processor_affinity"]["selected_processor_ids"], selected)
            for step in observed[BUILD.EXPECTED_ORDER[0]]:
                self.assertEqual(step["processor_affinity"], sorted(selected))
            self.assertEqual(observed, steps)

    def test_resume_rejects_names_only_evidence_and_modified_stage_or_log(self):
        with tempfile.TemporaryDirectory() as temporary:
            plan, prefix, path, steps, _ = self.checkpoint_fixture(Path(temporary))
            original = path.read_bytes()
            path.write_text(json.dumps({"build_input_id": plan["build_input_id"],
                                        "completed": BUILD.EXPECTED_ORDER[:1]}))
            with self.assertRaisesRegex(BUILD.ToolchainError, "names-only"):
                BUILD.read_checkpoint(plan, prefix, path)
            path.write_bytes(original)
            (prefix / "kept").write_text("modified staged bytes")
            with self.assertRaisesRegex(BUILD.ToolchainError, "staged tree changed"):
                BUILD.read_checkpoint(plan, prefix, path)
            (prefix / "kept").write_text("retained provider bytes")
            Path(steps[BUILD.EXPECTED_ORDER[0]][2]["log_path"]).write_text("lost test evidence")
            with self.assertRaisesRegex(BUILD.ToolchainError, "execution evidence differs"):
                BUILD.read_checkpoint(plan, prefix, path)

    def test_resume_rejects_missing_prior_evidence_even_with_recomputed_checkpoint_digest(self):
        with tempfile.TemporaryDirectory() as temporary:
            plan, prefix, path, _, _ = self.checkpoint_fixture(Path(temporary))
            value = BUILD.read_json(path)
            value["component_steps"] = {}
            value.pop("checkpoint_sha256")
            value["checkpoint_sha256"] = BUILD.canonical_sha256(value)
            path.write_text(json.dumps(value))
            with self.assertRaisesRegex(BUILD.ToolchainError, "evidence is incomplete"):
                BUILD.read_checkpoint(plan, prefix, path)

    def historical_provider_fixture(self, root: Path):
        """Protocol-only package fixture, with no claim of historical upstream execution."""
        prefix, manifest = self.fake_package(root / "base")
        lock = BUILD.read_json(REPO_ROOT / self.contract()["release_lock"])
        sources = {
            name: {"version": lock["archives"][name]["version"],
                   "archive_sha256": lock["archives"][name]["sha256"],
                   "tree_sha256": lock["archives"][name]["tree_sha256"]}
            for name in BUILD.EXPECTED_ORDER
        }
        sources["cmake"] = {"version": "4.4.2", "archive_sha256": "a" * 64,
                            "tree_sha256": "b" * 64}
        manifest_path = prefix / "share/laplace/toolchain-manifest.json"
        linker_input = root / "protocol-only-linker-input.a"
        linker_input.write_bytes(b"protocol-only fixture; not an actual linker archive\n")
        linker_record = {"path": str(linker_input), "sha256": BUILD.sha256_file(linker_input),
                         "size_bytes": linker_input.stat().st_size}
        receipt = {
            "schema": BUILD.PACKAGE_SCHEMA, "build_input_id": manifest["build_input_id"],
            "package_tree": BUILD.package_tree(prefix),
            "package": {"prefix": str(prefix), "consumer_manifest_path": str(manifest_path),
                        "consumer_manifest_sha256": BUILD.sha256_file(manifest_path)},
            "consumer_manifest": manifest, "installed_tools": manifest["tools"],
            "installed_perl_modules": manifest["perl_modules"],
            "source_inputs": sources, "source_generation_after": sources,
            "component_steps": {},  # Missing historical step records remain honestly absent.
            "compiler_driver_traces": {"c": {"fixture": "protocol-only"}},
            "linker_map_inputs": [linker_record],
            "activation": {"scope": "build-toolchain-only", "product_runtime_activation_eligible": False},
        }
        path = root / "historical-receipt.json"
        path.write_text(json.dumps(receipt))
        return path, receipt

    def test_reuse_authenticates_original_provider_without_inventing_missing_test_records(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path, receipt = self.historical_provider_fixture(root)
            before = BUILD.package_tree(Path(receipt["package"]["prefix"]))
            binding, observed = BUILD.verify_reuse_provider(self.contract(), REPO_ROOT,
                                                            path, BUILD.sha256_file(path))
            self.assertEqual(observed["component_steps"], {})
            self.assertEqual(binding["prefix"], receipt["package"]["prefix"])
            self.assertEqual(binding["package_tree"], before)
            self.assertEqual(BUILD.package_tree(Path(binding["prefix"])), before)
            with self.assertRaisesRegex(BUILD.ToolchainError, "digest differs"):
                BUILD.verify_reuse_provider(self.contract(), REPO_ROOT, path, "0" * 64)

    def test_reuse_rejects_changed_unchanged_source_or_provider_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path, receipt = self.historical_provider_fixture(root)
            original = path.read_bytes()
            receipt["source_inputs"]["gnu-make"]["archive_sha256"] = "0" * 64
            path.write_text(json.dumps(receipt))
            with self.assertRaisesRegex(BUILD.ToolchainError, "unchanged provider source differs"):
                BUILD.verify_reuse_provider(self.contract(), REPO_ROOT, path, BUILD.sha256_file(path))
            path.write_bytes(original)
            (Path(receipt["package"]["prefix"]) / "bin/make").write_bytes(b"modified provider")
            with self.assertRaises(BUILD.ToolchainError):
                BUILD.verify_reuse_provider(self.contract(), REPO_ROOT, path, BUILD.sha256_file(path))

    def test_component_destination_does_not_replace_verified_dependency_tool_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path, receipt = self.historical_provider_fixture(root)
            provider = Path(receipt["package"]["prefix"])
            stage, build, source = root / "new-cmake", root / "build", root / "source"
            build.mkdir()
            plan = {"prefix": str(stage), "build_directory": str(build), "work_directory": str(root),
                    "bootstrap_inputs": {item["id"]: item for item in self.contract()["bootstrap"]["tools"]}}
            environment = BUILD.build_environment(self.contract(), plan, "cmake", provider_prefix=provider)
            command = BUILD.format_command(self.contract()["build"]["components"]["cmake"]["configure"],
                source, build, stage, 6, plan["bootstrap_inputs"], provider_prefix=provider)
            self.assertIn("--prefix=" + str(stage), command)
            self.assertIn("-DCMAKE_MAKE_PROGRAM=" + str(provider / "bin/make"), command)
            self.assertEqual(environment["MAKE"], str(provider / "bin/make"))
            self.assertEqual(environment["PERL"], str(provider / "bin/perl"))
            self.assertEqual(environment["CFLAGS"], "-B" + str(provider / "bin"))
            self.assertFalse(stage.exists())
            (provider / "bin/make").unlink()
            with self.assertRaisesRegex(BUILD.ToolchainError, "omits selected Make"):
                BUILD.format_command(["{make}"], source, build, stage, 6,
                                     plan["bootstrap_inputs"], provider_prefix=provider)

    def composed_fixture(self, root: Path):
        """Authenticated protocol fixtures, NOT execution of CMake's 697 upstream tests."""
        provider_path, historical = self.historical_provider_fixture(root)
        contract = self.contract()
        for key, value in (("build_root", root / "build"), ("work_root", root / "work"),
                           ("stage_root", root / "stage")):
            contract["logical_roots"][key] = str(value)
        base, _ = BUILD.verify_reuse_provider(contract, REPO_ROOT, provider_path, BUILD.sha256_file(provider_path))
        lock = BUILD.read_json(REPO_ROOT / contract["release_lock"])
        entry = lock["archives"]["cmake"]
        bootstrap = {item["id"]: item for item in contract["bootstrap"]["tools"]}
        normalization = {"source_date_epoch": contract["environment"]["source_date_epoch"]}
        affinity = {"selected_processor_ids": [min(os.sched_getaffinity(0))]}
        identity = {"contract": contract, "provider": base, "bootstrap": bootstrap,
                    "source": entry, "source_normalization": normalization,
                    "processor_affinity": affinity, "recipe": {"fixture": "protocol-only"}}
        bid = BUILD.canonical_sha256(identity)
        build = root / "build" / bid
        prefix = root / "stage" / bid / "cmake"
        work = root / "work" / bid
        source = {"cmake": {"version": entry["version"], "archive_sha256": entry["sha256"],
                            "tree_sha256": entry["tree_sha256"]}}
        inherited = {"disposition": "authenticated-historical-evidence-not-reexecuted",
                     "source_receipt_sha256": base["source_receipt_sha256"],
                     "retained_component_step_names": [],
                     "not_reexecuted_components": [name for name in BUILD.EXPECTED_ORDER if name != "cmake"]}
        plan = {"schema": BUILD.PLAN_SCHEMA, "build_input_id": bid, "identity": identity,
                "repository": str(REPO_ROOT), "build_directory": str(build),
                "work_directory": str(work), "prefix": str(prefix), "component_order": ["cmake"],
                "source_inputs": source, "source_normalization": normalization,
                "bootstrap_inputs": bootstrap, "recipe": identity["recipe"],
                "provider": base, "parallel_jobs": 6, "processor_affinity": affinity,
                "inherited_execution": inherited}
        binary = prefix / "bin"
        binary.mkdir(parents=True)
        for name in ("cmake", "ctest", "cpack"):
            executable = binary / name
            executable.write_text("#!/bin/sh\nprintf '%s\\n' '" + name + " version 4.4.3'\n")
            executable.chmod(0o755)
        directory = build / "components/cmake"
        directory.mkdir(parents=True)
        selected_make = Path(base["prefix"]) / "bin/make"
        values = {**BUILD.EXPECTED_CMAKE_TEST_CAPABILITY_CACHE,
                  "CMAKE_MAKE_PROGRAM": str(selected_make), "CMake_TEST_EXPLICIT_MAKE_PROGRAM": str(selected_make)}
        (directory / "CMakeCache.txt").write_text("".join(f"{key}:STRING={value}\n" for key, value in values.items()))
        (directory / "CTestTestfile.cmake").write_text(
            f'add_test("protocol-only" "--build-makeprogram" "{selected_make}")\n')
        # Actual owner captures this inventory before BootstrapTest creates another tree.
        configuration = BUILD.verify_cmake_test_configuration("cmake", directory, selected_make)
        last_test = directory / "Testing/Temporary/LastTest.log"
        last_test.parent.mkdir(parents=True)
        last_test.write_text('"BootstrapTest" start time: fixture\n'
            "-- running bootstrap: /protocol-only/bootstrap --parallel=6\n"
            f"Makefile processor on this system is: {selected_make}\n"
            f"CMake has bootstrapped.  Now run {selected_make}.\nTest Passed.\n"
            '"BootstrapTest" end time: fixture\n')
        secondary = directory / "Tests/BootstrapTest"
        secondary.mkdir(parents=True)
        (secondary / "CMakeCache.txt").write_text("CMAKE_MAKE_PROGRAM:FILEPATH=/usr/bin/gmake\n")
        (secondary / "CTestTestfile.cmake").write_text(
            'add_test("protocol-only-secondary" "--build-makeprogram" "/usr/bin/gmake")\n')
        steps = []
        for name in ("configure", "build", "test", "install"):
            log = directory / (name + ".log")
            log.write_text("protocol-only fixture; not actual upstream execution\n" +
                ("697/697 Test #212: BootstrapTest .... Passed 1.00 sec\n100% tests passed out of 697\n"
                 if name == "test" else name + "\n"))
            command = BUILD.format_command(contract["build"]["components"]["cmake"][name],
                work / "private-sources/cmake", directory, prefix, 6, bootstrap, provider_prefix=Path(base["prefix"]))
            steps.append({"command": command, "working_directory": str(directory),
                "execution_environment": {"PWD": str(directory)}, "processor_affinity": affinity["selected_processor_ids"],
                "exit_code": 0, "log_path": str(log), "log_sha256": BUILD.sha256_file(log)})
        steps[0]["test_capability_contract"] = configuration
        steps[2]["test_result_contract"] = BUILD.verify_cmake_test_results(
            "cmake", directory, directory / "test.log", selected_make, 6, BUILD.EXPECTED_CMAKE_TEST_RESULT_POLICY)
        qualified = {"schema": BUILD.CMAKE_QUALIFICATION_SCHEMA, "build_input_id": bid, "plan": plan,
            "build_plan_sha256": BUILD.canonical_sha256(plan), "source_inputs": source, "source_generation_after": source,
            "component_steps": {"cmake": steps},
            "source_normalization": {"cmake": {"source_date_epoch": normalization["source_date_epoch"],
                                             "normalized_object_count": 1}},
            "provider_before": base, "provider_after": base, "inherited_execution": inherited,
            "installed_tools": BUILD.cmake_tool_receipts(prefix, "4.4.3"),
            "compiler_driver_traces": {"fixture": "protocol-only"}, "linker_map_inputs": historical["linker_map_inputs"],
            "bootstrap_inputs": bootstrap, "package_tree": BUILD.package_tree(prefix),
            "consumer_activation_eligible": False, "product_runtime_activation_eligible": False}
        qualification_path = build / "cmake-component-qualification.json"
        qualification_path.write_text(json.dumps(qualified))
        output = root / "selected-toolchain.json"
        receipt = BUILD.compose_cmake_toolchain(qualification_path, BUILD.sha256_file(qualification_path), output)
        return output, receipt, receipt["consumer_manifest"], qualification_path, qualified

    def test_composed_selection_preserves_both_roots_and_original_perl_without_single_prefix_claim(self):
        with tempfile.TemporaryDirectory() as temporary:
            output, receipt, manifest, _, _ = self.composed_fixture(Path(temporary))
            before = {name: BUILD.package_tree(Path(root["prefix"])) for name, root in manifest["provider_roots"].items()}
            self.assertEqual(BUILD.verify_composed_toolchain_receipt(receipt, output), manifest)
            self.assertNotIn("prefix", receipt["package"])
            self.assertNotIn("prefix", manifest)
            self.assertEqual(receipt["inherited_execution"]["retained_component_step_names"], [])
            self.assertEqual(set(receipt["component_steps"]), {"cmake"})
            for name, tool in manifest["tools"].items():
                self.assertEqual(tool["provider_root"], "cmake" if name in ("cmake", "ctest", "cpack") else "base")
                self.assertTrue(Path(tool["path"]).is_relative_to(Path(manifest["provider_roots"][tool["provider_root"]]["prefix"])))
            self.assertEqual(before, {name: BUILD.package_tree(Path(root["prefix"]))
                                     for name, root in manifest["provider_roots"].items()})

    def test_qualified_replay_separates_original_configure_inventory_from_later_secondary_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            _, _, _, _, qualified = self.composed_fixture(Path(temporary))
            configuration = qualified["component_steps"]["cmake"][0]["test_capability_contract"]
            self.assertEqual([entry["path"] for entry in configuration["generated_make_program_references"]],
                             ["CTestTestfile.cmake"])
            result = qualified["component_steps"]["cmake"][2]["test_result_contract"]
            self.assertEqual(result["secondary_generated_suite"]["other_make_reference_count"], 1)
            self.assertEqual(result["secondary_generated_suite"]["disposition"],
                             "configured-but-not-executed-by-bootstrap-test")
            self.assertEqual(BUILD.verify_cmake_component_receipt(qualified), qualified["installed_tools"])
            primary = Path(qualified["plan"]["build_directory"]) / "components/cmake/CTestTestfile.cmake"
            primary.write_text(primary.read_text() + "# actual configure input changed\n")
            with self.assertRaisesRegex(BUILD.ToolchainError, "configure test file changed"):
                BUILD.verify_cmake_component_receipt(qualified)

    def test_copied_aggregate_evidence_keeps_original_provider_and_manifest_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output, receipt, manifest, _, _ = self.composed_fixture(root)
            copied = root / "published-receipt.json"
            shutil.copy2(output, copied)
            self.assertEqual(BUILD.verify_composed_toolchain_receipt(receipt, copied), manifest)
            self.assertEqual(receipt["package"]["consumer_manifest_path"],
                             str(root / "selected-toolchain-manifest.json"))

    def test_composed_selection_rejects_modified_base_cmake_or_qualification_evidence(self):
        for mutation in ("base", "cmake", "proof"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                output, receipt, manifest, qualification, _ = self.composed_fixture(Path(temporary))
                if mutation == "proof":
                    qualification.write_text(qualification.read_text() + " ")
                else:
                    tool = "make" if mutation == "base" else "cmake"
                    Path(manifest["tools"][tool]["path"]).write_bytes(b"modified qualified bytes")
                with self.assertRaises(BUILD.ToolchainError):
                    BUILD.verify_composed_toolchain_receipt(receipt, output)

    def test_component_qualification_keeps_exact_test_command_and_selected_suite_evidence(self):
        for mutation in ("command", "suite"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                _, _, _, _, qualified = self.composed_fixture(Path(temporary))
                if mutation == "command":
                    qualified["component_steps"]["cmake"][2]["command"].append("-RBootstrapTest")
                    pattern = "command or working directory differs"
                else:
                    log = Path(qualified["component_steps"]["cmake"][2]["log_path"])
                    log.write_text(log.read_text().replace("100% tests passed out of 697", "100% tests passed out of 1"))
                    qualified["component_steps"]["cmake"][2]["log_sha256"] = BUILD.sha256_file(log)
                    pattern = "exact complete pass summary"
                with self.assertRaisesRegex(BUILD.ToolchainError, pattern):
                    BUILD.verify_cmake_component_receipt(qualified)


if __name__ == "__main__":
    unittest.main()
