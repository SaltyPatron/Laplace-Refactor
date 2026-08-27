#!/usr/bin/env python3
"""Contract and mutation checks for the staged PostgreSQL runtime graph."""

from __future__ import annotations

import copy
import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools/dependencies/build-runtime-graph.py"
SPEC = importlib.util.spec_from_file_location("laplace_runtime_build_graph", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
BUILD = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BUILD
SPEC.loader.exec_module(BUILD)


def passing_test_execution(component: dict[str, object]) -> dict[str, object]:
    policy = BUILD.component_test_policy(component)
    return {
        "scope": policy["scope"],
        "command": ["/fixture/test"],
        "process_return_code": 0,
        "exit_code": 0,
        "signal": None,
        "disposition": "passed",
        "package_gate": policy["package_gate"],
        "product_activation_gate": policy["product_activation_gate"],
        "provider_observation": {},
        "source_evidence": None,
    }


class RuntimeBuildGraphTests(unittest.TestCase):
    def contract(self) -> dict[str, object]:
        return BUILD.read_json(REPO_ROOT / "contracts/postgresql-runtime-build.json")

    def test_current_contract_is_valid_and_activation_is_fail_closed(self) -> None:
        contract = self.contract()
        BUILD.validate_contract(contract)
        self.assertEqual(contract["input_closure"]["status"], "incomplete")
        contract["input_closure"]["status"] = "complete"
        with self.assertRaisesRegex(BUILD.GraphError, "must remain incomplete"):
            BUILD.validate_contract(contract)

    def test_position_independent_executable_contract_is_mandatory(self) -> None:
        for field, value, message in (
            ("c_flags", "-fPIE", "C and C\\+\\+ compilation"),
            ("cxx_flags", "-fPIE", "C and C\\+\\+ compilation"),
            ("link_flags", "-pie", "executable linking"),
        ):
            contract = self.contract()
            contract["execution"][field] = [
                item for item in contract["execution"][field] if item != value
            ] or ["-Wl,--as-needed"]
            with self.assertRaisesRegex(BUILD.GraphError, message):
                BUILD.validate_contract(contract)

        contract = self.contract()
        contract["execution"]["executable_elf"]["copy_relocations"] = "allowed"
        with self.assertRaisesRegex(BUILD.GraphError, "COPY relocations"):
            BUILD.validate_contract(contract)

    def test_install_runpath_must_be_package_relative(self) -> None:
        contract = self.contract()
        contract["execution"]["install_runpath"] = (
            "/opt/laplace/releases/example/lib"
        )
        with self.assertRaisesRegex(BUILD.GraphError, "package-relative"):
            BUILD.validate_contract(contract)

    def test_install_prefix_must_be_the_stable_product_activation_path(self) -> None:
        contract = self.contract()
        contract["execution"]["install_prefix"] = "/opt/laplace/releases/private-slice"
        with self.assertRaisesRegex(BUILD.GraphError, "product activation prefix"):
            BUILD.validate_contract(contract)

    def test_absolute_build_paths_are_forbidden_in_compiler_outputs(self) -> None:
        for field, value, message in (
            ("build_root_mapping", "/build", "stable relative compiler path"),
            ("absolute_build_root_in_file_macro", "allowed", "__FILE__"),
            ("absolute_build_root_in_debug_info", "allowed", "debug information"),
        ):
            contract = self.contract()
            contract["execution"]["source_path_policy"][field] = value
            with self.assertRaisesRegex(BUILD.GraphError, message):
                BUILD.validate_contract(contract)

    def test_duplicate_contract_key_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "contract.json"
            path.write_text('{"schema":"first","schema":"second"}', encoding="utf-8")
            with self.assertRaisesRegex(BUILD.GraphError, "duplicate JSON object key"):
                BUILD.read_json(path)

    def test_ambient_loader_and_compiler_state_are_rejected(self) -> None:
        with self.assertRaisesRegex(BUILD.GraphError, "CC, LD_LIBRARY_PATH"):
            BUILD.validate_environment(
                self.contract(), {"CC": "ambient-cc", "LD_LIBRARY_PATH": "/ambient"}
            )

    def test_unknown_provider_is_rejected(self) -> None:
        contract = self.contract()
        contract["components"][0]["provider"] = "magic"
        with self.assertRaisesRegex(BUILD.GraphError, "unsupported provider"):
            BUILD.validate_contract(contract)

    def test_unknown_component_language_is_rejected(self) -> None:
        contract = self.contract()
        contract["components"][0]["languages"] = ["CUDA"]
        with self.assertRaisesRegex(BUILD.GraphError, "unsupported language"):
            BUILD.validate_contract(contract)

    def test_consumer_cannot_precede_its_dependency(self) -> None:
        contract = self.contract()
        contract["components"][0]["depends_on"] = ["libxml2"]
        with self.assertRaisesRegex(BUILD.GraphError, "must precede"):
            BUILD.validate_contract(contract)

    def test_provider_and_test_contract_must_agree(self) -> None:
        contract = self.contract()
        contract["components"][1]["test"] = "make-test"
        with self.assertRaisesRegex(BUILD.GraphError, "incompatible with provider"):
            BUILD.validate_contract(contract)

    def test_host_coupled_suite_requires_separate_provider_qualification(self) -> None:
        contract = self.contract()
        liburing = next(
            component for component in contract["components"] if component["id"] == "liburing"
        )
        self.assertEqual(
            liburing["test_policy"]["package_gate"],
            "record-exact-outcome-and-continue",
        )
        self.assertEqual(
            contract["runtime_provider_qualification"]["components"], ["liburing"]
        )

        contract["runtime_provider_qualification"]["components"] = ["zlib"]
        with self.assertRaisesRegex(BUILD.GraphError, "differ from component test policies"):
            BUILD.validate_contract(contract)

    def test_failed_host_coupled_suite_is_receipted_without_becoming_acceptance(self) -> None:
        component = copy.deepcopy(
            next(
                item
                for item in self.contract()["components"]
                if item["id"] == "liburing"
            )
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            evidence = root / "README"
            evidence.write_text("kernel-coupled suite\n", encoding="utf-8")
            component["test_policy"]["source_evidence"]["sha256"] = BUILD.sha256_file(
                evidence
            )
            with mock.patch.object(BUILD, "run_logged", return_value=2), mock.patch.object(
                BUILD,
                "runtime_provider_observation",
                return_value={
                    "kernel_sysname": "Linux",
                    "kernel_release": "fixture",
                    "kernel_version": "fixture-version",
                    "machine": "x86_64",
                    "io_uring_disabled": 0,
                },
            ):
                execution = BUILD.execute_component_test(
                    component,
                    ["/toolchain/make", "runtests"],
                    root,
                    {},
                    root / "build.log",
                    root,
                )
        self.assertEqual(execution["process_return_code"], 2)
        self.assertEqual(execution["exit_code"], 2)
        self.assertIsNone(execution["signal"])
        self.assertEqual(
            execution["disposition"], "failed-under-observed-runtime-provider"
        )
        self.assertEqual(
            execution["product_activation_gate"],
            "separate-selected-runtime-provider-qualification",
        )
        BUILD.validate_recorded_test_execution(component, execution)

        mutated = copy.deepcopy(execution)
        mutated["disposition"] = "passed"
        with self.assertRaisesRegex(BUILD.GraphError, "disposition mismatch"):
            BUILD.validate_recorded_test_execution(component, mutated)

        signaled = copy.deepcopy(execution)
        signaled["process_return_code"] = -9
        signaled["exit_code"] = None
        signaled["signal"] = 9
        signaled["disposition"] = (
            "terminated-by-signal-under-observed-runtime-provider"
        )
        BUILD.validate_recorded_test_execution(component, signaled)

        signaled["signal"] = 15
        with self.assertRaisesRegex(BUILD.GraphError, "signal mismatch"):
            BUILD.validate_recorded_test_execution(component, signaled)

    def test_compile_only_lz4_regression_is_rejected(self) -> None:
        contract = self.contract()
        contract["components"][1]["test"] = "compile-only"
        with self.assertRaisesRegex(BUILD.GraphError, "unsupported test contract"):
            BUILD.validate_contract(contract)

    def test_selected_tool_digest_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            compiler = Path(temporary) / "compiler"
            compiler.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            compiler.chmod(0o755)
            contract = self.contract()
            contract["compiler"]["c_compiler"] = {
                "path": str(compiler),
                "sha256": "0" * 64,
            }
            with self.assertRaisesRegex(BUILD.GraphError, "digest mismatch"):
                BUILD.verify_compilers(contract)

    def toolchain_receipt(self, root: Path) -> Path:
        prefix = root / "toolchain"
        binary = prefix / "bin" / "tool"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(Path("/bin/true").read_bytes())
        binary.chmod(0o755)
        digest = BUILD.sha256_file(binary)
        contract = self.contract()
        receipt = {
            "schema": BUILD.TOOLCHAIN_RECEIPT_SCHEMA,
            "build_input_id": "3" * 64,
            "package": {"prefix": str(prefix)},
            "consumer_manifest": {
                "schema": BUILD.TOOLCHAIN_MANIFEST_SCHEMA,
                "build_input_id": "3" * 64,
                "prefix": str(prefix),
                "tools": {
                    name: {"path": str(binary), "sha256": digest, "version": "fixture-v1"}
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

    def test_toolchain_receipt_binds_exact_packaged_tools(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            selected = BUILD.verify_toolchain_receipt(
                self.contract(), self.toolchain_receipt(Path(temporary))
            )
            self.assertEqual(selected["build_input_id"], "3" * 64)
            self.assertEqual(
                set(selected["tools"]), set(self.contract()["build_toolchain"]["required_tools"])
            )

    def test_toolchain_receipt_cannot_omit_a_required_tool(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.toolchain_receipt(Path(temporary))
            receipt = json.loads(path.read_text(encoding="utf-8"))
            receipt["consumer_manifest"]["tools"].pop("readelf")
            path.write_text(json.dumps(receipt), encoding="utf-8")
            with self.assertRaisesRegex(BUILD.GraphError, "omits required tool: readelf"):
                BUILD.verify_toolchain_receipt(self.contract(), path)

    def test_toolchain_receipt_cannot_claim_product_runtime_activation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.toolchain_receipt(Path(temporary))
            receipt = json.loads(path.read_text(encoding="utf-8"))
            receipt["activation"]["product_runtime_activation_eligible"] = True
            path.write_text(json.dumps(receipt), encoding="utf-8")
            with self.assertRaisesRegex(BUILD.GraphError, "cannot be product-runtime"):
                BUILD.verify_toolchain_receipt(self.contract(), path)

    def test_compiler_driver_trace_must_select_packaged_linker(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prefix = root / "toolchain"
            linker = prefix / "bin" / "ld"
            linker.parent.mkdir(parents=True)
            linker.write_text("fixture linker\n", encoding="utf-8")
            compiler = root / "compiler"
            compiler.write_text(
                f"#!/bin/sh\nprintf '%s\\n' '{linker} -pie' >&2\n",
                encoding="utf-8",
            )
            compiler.chmod(0o755)
            compilers = {
                "c_compiler": {"path": str(compiler)},
                "cxx_compiler": {"path": str(compiler)},
            }
            toolchain = {
                "prefix": str(prefix),
                "tools": {"ld": {"path": str(linker)}},
            }
            traces = BUILD.compiler_driver_trace(self.contract(), compilers, toolchain)
            self.assertEqual(traces["c_compiler"]["selected_linker"], str(linker))
            self.assertEqual(
                traces["c_compiler"]["absolute_inputs"],
                [{"path": str(linker), "sha256": BUILD.sha256_file(linker), "size": 15}],
            )
            compiler.write_text("#!/bin/sh\nprintf '%s\\n' '/usr/bin/ld' >&2\n", encoding="utf-8")
            with self.assertRaisesRegex(BUILD.GraphError, "did not select"):
                BUILD.compiler_driver_trace(self.contract(), compilers, toolchain)

            compiler.write_text(
                f"#!/bin/sh\nprintf '%s\\n' '{linker}' >&2\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(BUILD.GraphError, "did not select PIE"):
                BUILD.compiler_driver_trace(self.contract(), compilers, toolchain)

    def test_compiler_driver_trace_identity_ignores_random_temporary_objects(self) -> None:
        first = (
            ' "/opt/intel/clang" "-o" "/tmp/icx-abc123/null-deadbeef.o"\n'
            ' "/selected/ld" "/tmp/icx-abc123/null-deadbeef.o"\n'
        )
        second = (
            ' "/opt/intel/clang" "-o" "/tmp/icx-def456/null-feedface.o"\n'
            ' "/selected/ld" "/tmp/icx-def456/null-feedface.o"\n'
        )
        self.assertEqual(
            BUILD.normalize_compiler_driver_trace(first),
            BUILD.normalize_compiler_driver_trace(second),
        )
        mutated = second.replace('"/selected/ld"', '"/ambient/ld"')
        self.assertNotEqual(
            BUILD.normalize_compiler_driver_trace(first),
            BUILD.normalize_compiler_driver_trace(mutated),
        )

    def test_cmake_component_compilers_follow_declared_languages(self) -> None:
        contract = self.contract()
        captured: list[list[str]] = []

        def capture(command: object, *_args: object, **_kwargs: object) -> int:
            captured.append(list(command))
            return 0

        plan = {
            "tools": {
                name: {"path": f"/toolchain/{name}"}
                for name in ("cmake", "ninja", "ctest", "make", "ar", "ld", "nm", "objcopy", "objdump", "ranlib", "strip")
            },
            "install_prefix": "/opt/laplace/current",
            "staged_prefix": "/stage/fixture",
            "stage_directory": "/stage",
        }
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(
            BUILD, "run_logged", side_effect=capture
        ):
            root = Path(temporary)
            BUILD.cmake_component(
                contract,
                plan,
                contract["components"][0],
                root,
                root / "build",
                {},
                root / "log",
            )
        configure = captured[0]
        self.assertIn(
            f"-DCMAKE_C_COMPILER={contract['compiler']['c_compiler']['path']}", configure
        )
        self.assertFalse(any(item.startswith("-DCMAKE_CXX_COMPILER=") for item in configure))
        self.assertIn(
            f"-DCMAKE_INSTALL_RPATH={contract['execution']['install_runpath']}",
            configure,
        )

    def test_openssl_install_receives_destdir_as_a_make_assignment(self) -> None:
        contract = self.contract()
        captured: list[list[str]] = []

        def capture(command: object, *_args: object, **_kwargs: object) -> int:
            captured.append(list(command))
            return 0

        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(
            BUILD, "run_logged", side_effect=capture
        ):
            root = Path(temporary)
            stage = root / "stage"
            plan = {
                "tools": {
                    "make": {"path": "/toolchain/make"},
                    "perl": {"path": "/toolchain/perl"},
                },
                "install_prefix": "/opt/laplace/current",
                "stage_directory": str(stage),
            }
            BUILD.openssl_component(
                contract,
                plan,
                contract["components"][4],
                root / "source",
                root / "build",
                {},
                root / "log",
            )
        install = captured[-1]
        self.assertIn(f"DESTDIR={stage / 'root'}", install)
        self.assertNotIn("/opt/laplace/current", install)

    def test_build_environment_uses_declared_package_relative_runpath(self) -> None:
        contract = self.contract()
        tools = {
            name: {"path": f"/toolchain/bin/{name}"}
            for name in contract["build_toolchain"]["required_tools"]
        }
        with tempfile.TemporaryDirectory() as temporary:
            environment = BUILD.build_environment(
                contract,
                {
                    "tools": tools,
                    "staged_prefix": f"{temporary}/stage/root/opt/laplace/runtime",
                    "stage_directory": f"{temporary}/stage",
                    "build_directory": f"{temporary}/build/contains-41",
                    "toolchain_prefix": "/toolchain",
                },
                Path(temporary) / "home",
            )
        self.assertNotIn("-Wl,-rpath", environment["LDFLAGS"])
        cmake_environment = BUILD.provider_environment(contract, environment, "cmake")
        self.assertEqual(cmake_environment["LDFLAGS"], environment["LDFLAGS"])
        for provider in ("autotools", "openssl", "source-copy-make"):
            make_environment = BUILD.provider_environment(
                contract, environment, provider
            )
            self.assertIn(
                "-Wl,-rpath,'$$ORIGIN/../lib'", make_environment["LDFLAGS"]
            )
            self.assertNotIn(
                "-Wl,-rpath,'$ORIGIN/../lib' ", make_environment["LDFLAGS"]
            )
        self.assertNotIn("/opt/laplace/releases", environment["LDFLAGS"])
        for field in ("CFLAGS", "CXXFLAGS"):
            self.assertIn(
                f"-ffile-prefix-map={temporary}/build/contains-41=.",
                environment[field],
            )
            self.assertIn(
                f"-fdebug-prefix-map={temporary}/build/contains-41=.",
                environment[field],
            )

    def test_provider_environment_rejects_private_dispatch(self) -> None:
        with self.assertRaisesRegex(BUILD.GraphError, "unsupported provider environment"):
            BUILD.provider_environment(self.contract(), {"LDFLAGS": "-pie"}, "private")

    def test_release_generation_name_must_equal_lock_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock = root / "release-lock.json"
            lock.write_text("{}\n", encoding="utf-8")
            with self.assertRaisesRegex(BUILD.GraphError, "exact release-lock SHA-256"):
                BUILD.verify_release_generation(
                    REPO_ROOT, lock, root / "archives", root / "wrong-generation"
                )

    def test_build_sources_are_fresh_private_archive_extractions(self) -> None:
        class ReleaseFixture:
            def __init__(self) -> None:
                self.imported: list[tuple[str, Path]] = []
                self.verified: list[tuple[str, Path]] = []

            def import_entry(
                self, name: str, entry: dict[str, object], archive_root: Path, destination: Path
            ) -> None:
                self.imported.append((name, destination))
                (destination / name).mkdir()

            def verify_imported_entry(
                self, name: str, entry: dict[str, object], archive_root: Path, destination: Path
            ) -> None:
                self.verified.append((name, destination))

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = root / "repository"
            build_root = root / "build"
            repository.mkdir()
            build_root.mkdir()
            (repository / "release-lock.json").write_text(
                json.dumps({"archives": {"source-a": {}, "source-b": {}}}),
                encoding="utf-8",
            )
            contract = {
                "release_lock": "release-lock.json",
                "execution": {"source_date_epoch": "1787616000"},
            }
            plan = {
                "archive_root": str(root / "archives"),
                "components": [
                    {"source": "source-a"},
                    {"source": "source-a"},
                    {"source": "source-b"},
                ],
            }
            release = ReleaseFixture()
            with mock.patch.object(BUILD, "load_release_module", return_value=release):
                observed = BUILD.prepare_private_sources(
                    contract, plan, repository, build_root
                )
            expected = build_root / "sources"
            self.assertEqual(observed, expected)
            self.assertEqual(release.imported, [("source-a", expected), ("source-b", expected)])
            self.assertEqual(release.verified, release.imported)
            epoch_ns = int(self.contract()["execution"]["source_date_epoch"]) * 1_000_000_000
            self.assertEqual((expected / "source-a").stat().st_mtime_ns, epoch_ns)
            self.assertEqual((expected / "source-b").stat().st_mtime_ns, epoch_ns)

    def test_private_source_timestamp_normalization_is_wall_clock_independent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "source"
            root.mkdir()
            child = root / "input.c"
            child.write_text("int value;\n", encoding="utf-8")
            first_epoch = 1_700_000_000
            BUILD.normalize_tree_timestamps(root, first_epoch)
            first = (root.stat().st_mtime_ns, child.stat().st_mtime_ns)
            os.utime(root, ns=(1, 1))
            os.utime(child, ns=(2, 2))
            BUILD.normalize_tree_timestamps(root, first_epoch)
            self.assertEqual(
                (root.stat().st_mtime_ns, child.stat().st_mtime_ns), first
            )

    def test_plan_identity_changes_with_contract_and_outputs_remain_external(self) -> None:
        contract = self.contract()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            contract["execution"]["install_prefix"] = "/opt/laplace/current"
            contract["execution"]["build_root"] = str(root / "build")
            contract["execution"]["stage_root"] = str(root / "stage")
            compilers = {
                name: {"path": value["path"], "sha256": value["sha256"]}
                for name, value in contract["compiler"].items()
            }
            packaged_tools = {
                name: {"path": f"/toolchain/{name}", "sha256": "4" * 64, "version": "v1"}
                for name in contract["build_toolchain"]["required_tools"]
            }
            toolchain = {
                "receipt_path": str(root / "receipt.json"),
                "receipt_sha256": "5" * 64,
                "build_input_id": "6" * 64,
                "prefix": "/toolchain",
                "tools": packaged_tools,
            }
            with mock.patch.object(
                BUILD, "verify_compilers", return_value=compilers
            ), mock.patch.object(
                BUILD, "verify_toolchain_receipt", return_value=toolchain
            ), mock.patch.object(
                BUILD, "compiler_driver_trace", return_value={"fixture": "trace"}
            ), mock.patch.object(BUILD, "verify_release_generation", return_value="1" * 64):
                first = BUILD.create_plan(
                    contract, REPO_ROOT, root / "archives", root / "source", root / "receipt.json"
                )
                changed = copy.deepcopy(contract)
                changed["execution"]["jobs"] += 1
                second = BUILD.create_plan(
                    changed, REPO_ROOT, root / "archives", root / "source", root / "receipt.json"
                )
            self.assertNotEqual(first["build_input_id"], second["build_input_id"])
            self.assertFalse(first["activation_eligible"])
            self.assertEqual(
                Path(first["staged_prefix"]),
                Path(first["stage_directory"])
                / "root"
                / Path(first["install_prefix"]).relative_to("/"),
            )

    def test_repository_local_output_is_rejected(self) -> None:
        with self.assertRaisesRegex(BUILD.GraphError, "outside the repository"):
            BUILD.ensure_external(REPO_ROOT / "build", REPO_ROOT, "build directory")

    def test_package_receipt_cannot_claim_unproved_closure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary) / "package"
            prefix.mkdir()
            (prefix / "evidence.txt").write_text("exact bytes\n", encoding="utf-8")
            receipt = BUILD.package_receipt(
                self.contract(),
                {
                    "build_input_id": "2" * 64,
                    "install_prefix": "/opt/laplace/current",
                    "staged_prefix": str(prefix),
                    "tools": {"readelf": {"path": "/usr/bin/readelf"}},
                },
            )
            self.assertFalse(receipt["build_input_closure_complete"])
            self.assertFalse(receipt["static_link_closure_verified"])
            self.assertFalse(receipt["recursive_runtime_closure_verified"])
            self.assertFalse(receipt["activation_eligible"])

    def test_package_rejects_non_pie_and_copy_relocated_executables(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prefix = root / "package"
            prefix.mkdir()
            executable = prefix / "fixture"
            executable.write_bytes(b"\x7fELFfixture")
            executable.chmod(0o755)
            readelf = root / "readelf"
            readelf.write_text(
                """#!/bin/sh
case "$1" in
  -hW) printf '%s\\n' '  Type:                              EXEC (Executable file)' ;;
  -lW) printf '%s\\n' '      [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]' ;;
  -dW) printf '%s\\n' ' 0x0000000000000001 (NEEDED) Shared library: [libc.so.6]' ;;
  -rW) : ;;
esac
""",
                encoding="utf-8",
            )
            readelf.chmod(0o755)
            plan = {
                "build_input_id": "2" * 64,
                "install_prefix": "/opt/laplace/current",
                "staged_prefix": str(prefix),
                "tools": {"readelf": {"path": str(readelf)}},
            }
            with self.assertRaisesRegex(BUILD.GraphError, "not PIE"):
                BUILD.package_receipt(self.contract(), plan)

            text = readelf.read_text(encoding="utf-8")
            text = text.replace("EXEC (Executable file)", "DYN (Position-Independent Executable file)")
            text = text.replace("-rW) : ;;", "-rW) printf '%s\\n' 'R_X86_64_COPY' ;;")
            readelf.write_text(text, encoding="utf-8")
            with self.assertRaisesRegex(BUILD.GraphError, "COPY relocations"):
                BUILD.package_receipt(self.contract(), plan)

            readelf.write_text(
                text.replace("-rW) printf '%s\\n' 'R_X86_64_COPY' ;;", "-rW) : ;;"),
                encoding="utf-8",
            )
            receipt = BUILD.package_receipt(self.contract(), plan)
            elf = next(item["elf"] for item in receipt["files"] if item["path"] == "fixture")
            self.assertTrue(elf["executable"])
            self.assertEqual(elf["type"], "DYN")
            self.assertEqual(elf["copy_relocation_count"], 0)

    def test_package_rejects_absolute_runpath_and_accepts_declared_relative_runpath(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prefix = root / "package"
            prefix.mkdir()
            library = prefix / "libfixture.so"
            library.write_bytes(b"\x7fELFfixture")
            readelf = root / "readelf"
            readelf.write_text(
                """#!/bin/sh
case "$1" in
  -hW) printf '%s\\n' '  Type:                              DYN (Shared object file)' ;;
  -lW) : ;;
  -dW) printf '%s\\n' ' 0x000000000000001d (RUNPATH) Library runpath: [/opt/laplace/releases/example/lib]' ;;
  -rW) : ;;
esac
""",
                encoding="utf-8",
            )
            readelf.chmod(0o755)
            plan = {
                "build_input_id": "2" * 64,
                "install_prefix": "/opt/laplace/current",
                "staged_prefix": str(prefix),
                "tools": {"readelf": {"path": str(readelf)}},
            }
            with self.assertRaisesRegex(BUILD.GraphError, "non-package-relative RUNPATH"):
                BUILD.package_receipt(self.contract(), plan)

            readelf.write_text(
                readelf.read_text(encoding="utf-8").replace(
                    "/opt/laplace/releases/example/lib", "$ORIGIN/../lib"
                ),
                encoding="utf-8",
            )
            receipt = BUILD.package_receipt(self.contract(), plan)
            elf = next(item["elf"] for item in receipt["files"] if item["path"] == "libfixture.so")
            self.assertEqual(elf["runpaths"], ["$ORIGIN/../lib"])

    def test_component_checkpoint_resume_requires_exact_staged_tree_and_log(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_root = root / "build"
            staged_prefix = root / "stage" / "root" / "opt" / "laplace" / "runtime"
            component = {"id": "zlib", "source": "zlib"}
            plan = {
                "build_input_id": "7" * 64,
                "components": [component],
            }
            component_root = build_root / "components" / "zlib"
            component_root.mkdir(parents=True)
            (component_root / "build.log").write_text("tested and installed\n", encoding="utf-8")
            staged_prefix.mkdir(parents=True)
            library = staged_prefix / "libz.so"
            library.write_bytes(b"exact package bytes")
            BUILD.write_component_checkpoint(
                plan,
                build_root,
                staged_prefix,
                component,
                0,
                None,
                passing_test_execution(component),
            )
            completed = BUILD.completed_component_checkpoints(
                plan, build_root, staged_prefix
            )
            self.assertEqual([item["component_id"] for item in completed], ["zlib"])

            library.write_bytes(b"mutated package bytes")
            with self.assertRaisesRegex(BUILD.GraphError, "staged package tree differs"):
                BUILD.completed_component_checkpoints(plan, build_root, staged_prefix)

            library.write_bytes(b"exact package bytes")
            (component_root / "build.log").write_text("mutated log\n", encoding="utf-8")
            with self.assertRaisesRegex(BUILD.GraphError, "build log differs"):
                BUILD.completed_component_checkpoints(plan, build_root, staged_prefix)

    def test_component_checkpoint_chain_must_be_contiguous(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_root = root / "build"
            staged_prefix = root / "stage" / "runtime"
            staged_prefix.mkdir(parents=True)
            components = [
                {"id": "first", "source": "first"},
                {"id": "second", "source": "second"},
            ]
            plan = {"build_input_id": "8" * 64, "components": components}
            previous = None
            for index, component in enumerate(components):
                component_root = build_root / "components" / component["id"]
                component_root.mkdir(parents=True)
                (component_root / "build.log").write_text(
                    f"{component['id']} complete\n", encoding="utf-8"
                )
                (staged_prefix / component["id"]).write_text("bytes\n", encoding="utf-8")
                checkpoint = BUILD.write_component_checkpoint(
                    plan,
                    build_root,
                    staged_prefix,
                    component,
                    index,
                    previous,
                    passing_test_execution(component),
                )
                previous = checkpoint["checkpoint_sha256"]
            BUILD.checkpoint_path(build_root, "first").unlink()
            with self.assertRaisesRegex(BUILD.GraphError, "not contiguous"):
                BUILD.completed_component_checkpoints(plan, build_root, staged_prefix)


if __name__ == "__main__":
    unittest.main()
