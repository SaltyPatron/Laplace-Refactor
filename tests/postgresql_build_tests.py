#!/usr/bin/env python3
"""Mutation checks for the PostgreSQL build and package boundary."""

from __future__ import annotations

import importlib.util
import hashlib
import json
import os
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

    def test_build_sandbox_is_networkless_and_mounts_only_declared_inputs(self) -> None:
        contract = self.contract()
        plan = {
            "source_root": "/evidence/source",
            "build_directory": "/work/build",
            "stage_directory": "/work/stage",
            "build_toolchain": {"prefix": "/evidence/toolchain"},
            "host_build_provider": {
                "roots": [
                    {"path": "/usr"},
                    {"path": "/opt/intel/oneapi/compiler/2026.1"},
                ],
                "files": [
                    {"path": "/etc/ld.so.cache"},
                    {"path": "/etc/nsswitch.conf"},
                ],
            },
        }
        command = ["/evidence/source/configure", "--disable-rpath"]
        overlays = [
            {
                "source_path": "/evidence/source/src/interfaces/ecpg/preproc/t",
                "overlay_path": "/work/build/.source-test-overlays/0",
            }
        ]

        sandbox = BUILD.sandboxed_build_command(contract, plan, command, overlays)

        self.assertEqual(sandbox[0], contract["host_build_provider"]["sandbox"]["executable"])
        self.assertIn("--unshare-all", sandbox)
        self.assertNotIn("--share-net", sandbox)
        self.assertIn("--tmpfs", sandbox)
        self.assertIn("/tmp", sandbox)
        self.assertIn("--proc", sandbox)
        self.assertIn("--dev", sandbox)
        for path in (
            "/usr",
            "/opt/intel/oneapi/compiler/2026.1",
            "/etc/ld.so.cache",
            "/etc/nsswitch.conf",
            "/evidence/source",
            "/evidence/toolchain",
        ):
            self.assertIn(["--ro-bind", path, path], [sandbox[index : index + 3] for index in range(len(sandbox) - 2)])
        for path in ("/work/build", "/work/stage"):
            self.assertIn(["--bind", path, path], [sandbox[index : index + 3] for index in range(len(sandbox) - 2)])
        self.assertIn(
            [
                "--bind",
                "/work/build/.source-test-overlays/0",
                "/evidence/source/src/interfaces/ecpg/preproc/t",
            ],
            [sandbox[index : index + 3] for index in range(len(sandbox) - 2)],
        )
        self.assertEqual(sandbox[-len(command) :], command)

    def test_source_test_overlay_receipts_only_declared_generated_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source/src/interfaces/ecpg/preproc/t"
            source.mkdir(parents=True)
            (source / "err_warn_msg.pgc").write_text("EXEC SQL SELECT 1;\n", encoding="utf-8")
            (source / "err_warn_msg_informix.pgc").write_text(
                "EXEC SQL SELECT 2;\n", encoding="utf-8"
            )
            build = root / "build"
            build.mkdir()
            plan = {
                "source_root": str(root / "source"),
                "build_directory": str(build),
            }

            overlays = BUILD.prepare_source_test_overlays(
                self.contract(), plan, "check-world"
            )
            overlay = Path(overlays[0]["overlay_path"])
            (overlay / "err_warn_msg.c").write_text("generated one\n", encoding="utf-8")
            (overlay / "err_warn_msg_informix.c").write_text(
                "generated two\n", encoding="utf-8"
            )

            receipt = BUILD.inspect_source_test_overlays(
                overlays, require_complete=True
            )
            self.assertTrue(receipt["accepted"])
            self.assertEqual(
                set(receipt["overlays"][0]["generated_files_remaining_after_test"]),
                {"err_warn_msg.c", "err_warn_msg_informix.c"},
            )

    def test_source_test_overlay_accepts_transient_outputs_removed_by_upstream_test(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source/src/interfaces/ecpg/preproc/t"
            source.mkdir(parents=True)
            (source / "err_warn_msg.pgc").write_text(
                "EXEC SQL SELECT 1;\n", encoding="utf-8"
            )
            (source / "err_warn_msg_informix.pgc").write_text(
                "EXEC SQL SELECT 2;\n", encoding="utf-8"
            )
            build = root / "build"
            build.mkdir()
            plan = {
                "source_root": str(root / "source"),
                "build_directory": str(build),
            }

            overlays = BUILD.prepare_source_test_overlays(
                self.contract(), plan, "check-world"
            )
            receipt = BUILD.inspect_source_test_overlays(
                overlays, require_complete=True
            )

            self.assertTrue(receipt["accepted"])
            self.assertEqual(
                receipt["overlays"][0]["generated_files_remaining_after_test"], {}
            )
            self.assertEqual(
                set(receipt["overlays"][0]["permitted_files_absent_after_test"]),
                {"err_warn_msg.c", "err_warn_msg_informix.c"},
            )

    def test_source_test_overlay_input_mutation_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source/src/interfaces/ecpg/preproc/t"
            source.mkdir(parents=True)
            (source / "err_warn_msg.pgc").write_text("selected input\n", encoding="utf-8")
            build = root / "build"
            build.mkdir()
            plan = {
                "source_root": str(root / "source"),
                "build_directory": str(build),
            }
            overlays = BUILD.prepare_source_test_overlays(
                self.contract(), plan, "check-world"
            )
            overlay = Path(overlays[0]["overlay_path"])
            (overlay / "err_warn_msg.pgc").write_text("mutated input\n", encoding="utf-8")
            with self.assertRaisesRegex(BUILD.BuildError, "outside its generated-file"):
                BUILD.inspect_source_test_overlays(overlays, require_complete=False)

    def toolchain_receipt(self, root: Path, contract: dict[str, object]) -> Path:
        prefix = root / "toolchain"
        binary = prefix / "bin/tool"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(Path("/bin/true").read_bytes())
        binary.chmod(0o755)
        digest = BUILD.sha256_file(binary)
        module_root = prefix / "lib/perl5/site_perl/5.44.0"
        module_paths = {
            "IO::Pty": module_root / "IO/Pty.pm",
            "IO::Tty": module_root / "IO/Tty.pm",
            "IPC::Run": module_root / "IPC/Run.pm",
        }
        for name, path in module_paths.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"fixture {name}\n", encoding="utf-8")
        native = module_root / "x86_64-linux/auto/IO/Tty/Tty.so"
        native.parent.mkdir(parents=True)
        native.write_bytes(b"fixture native provider\n")
        perl_modules = {
            name: {
                "path": str(path),
                "sha256": BUILD.sha256_file(path),
                "version": version,
                "native_providers": (
                    [{"path": str(native), "sha256": BUILD.sha256_file(native)}]
                    if name in ("IO::Pty", "IO::Tty")
                    else []
                ),
                "source_component": (
                    "io-tty" if name in ("IO::Pty", "IO::Tty") else "ipc-run"
                ),
            }
            for name, (path, version) in {
                name: (path, contract["build_toolchain"]["required_perl_modules"][name])
                for name, path in module_paths.items()
            }.items()
        }
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
                "perl_modules": perl_modules,
            },
            "activation": {
                "scope": "build-toolchain-only",
                "product_runtime_activation_eligible": False,
            },
            "bootstrap_inputs": {},
            "linker_map_inputs": [],
        }
        bootstrap_root = root / "bootstrap-inputs"
        bootstrap_root.mkdir()
        for name, requirement in contract["build_toolchain"][
            "required_bootstrap_inputs"
        ].items():
            bootstrap_input = bootstrap_root / name
            bootstrap_input.write_bytes(Path("/bin/true").read_bytes())
            bootstrap_input.chmod(0o755)
            requirement["path"] = str(bootstrap_input)
            requirement["sha256"] = BUILD.sha256_file(bootstrap_input)
            receipt["bootstrap_inputs"][name] = {
                "path": str(bootstrap_input),
                "sha256": requirement["sha256"],
                "version": requirement["version"],
            }
        contract["build"]["python"] = contract["build_toolchain"][
            "required_bootstrap_inputs"
        ]["python"]["path"]
        contract["build"]["shell"] = contract["build_toolchain"][
            "required_bootstrap_inputs"
        ]["sh"]["path"]
        linker_root = root / "linker-inputs"
        linker_root.mkdir()
        for role, requirement in contract["build_toolchain"][
            "required_linker_map_inputs"
        ].items():
            linker_input = linker_root / role
            linker_input.write_bytes(f"fixture {role}\n".encode("utf-8"))
            requirement["path"] = str(linker_input)
            receipt["linker_map_inputs"].append(
                {
                    "path": str(linker_input),
                    "sha256": BUILD.sha256_file(linker_input),
                    "size_bytes": linker_input.stat().st_size,
                }
            )
        path = root / "toolchain-receipt.json"
        path.write_text(json.dumps(receipt), encoding="utf-8")
        return path

    def runtime_receipt(self, root: Path) -> Path:
        prefix = root / "runtime"
        binary_directory = prefix / "bin"
        binary_directory.mkdir(parents=True)
        openssl = binary_directory / "openssl"
        openssl.write_text(
            "#!/bin/sh\nprintf '%s\\n' 'OpenSSL 4.0.1 9 Jun 2026 (Library: OpenSSL 4.0.1 9 Jun 2026)'\n",
            encoding="utf-8",
        )
        openssl.chmod(0o755)
        library_directory = prefix / "lib"
        library_directory.mkdir(parents=True)
        library = library_directory / "libfixture.so"
        library.write_bytes(b"runtime bytes\n")
        records = [
            {
                "path": "bin",
                "kind": "directory",
                "mode": f"{stat.S_IMODE(binary_directory.stat().st_mode):04o}",
                "size": 0,
                "sha256": None,
                "target": None,
                "elf": None,
            },
            {
                "path": "bin/openssl",
                "kind": "file",
                "mode": f"{stat.S_IMODE(openssl.stat().st_mode):04o}",
                "size": openssl.stat().st_size,
                "sha256": BUILD.sha256_file(openssl),
                "target": None,
                "elf": None,
            },
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
            "file_count": 2,
            "total_file_bytes": library.stat().st_size + openssl.stat().st_size,
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

    def test_upstream_absolute_rpath_generation_is_rejected(self) -> None:
        contract = self.contract()
        contract["build"]["configure_arguments"].remove("--disable-rpath")
        with self.assertRaisesRegex(BUILD.BuildError, "absolute rpath"):
            BUILD.validate_contract(contract)

    def io_uring_fixture(
        self, root: Path
    ) -> tuple[dict[str, object], dict[str, object], Path, Path]:
        contract = self.contract()
        source = root / "source"
        build = root / "build"
        source_test = source / contract["runtime_qualification"]["liburing"][
            "source_test"
        ]
        source_test.parent.mkdir(parents=True)
        source_test.write_text("fixture io_uring test\n", encoding="utf-8")
        contract["runtime_qualification"]["liburing"][
            "source_test_sha256"
        ] = BUILD.sha256_file(source_test)
        regress_log = build / contract["runtime_qualification"]["liburing"][
            "regress_log"
        ]
        regress_log.parent.mkdir(parents=True)
        assertions = [
            "ok 178 - io_uring: initdb",
            "ok 179 - io_uring: io_method set correctly",
            *[
                f"ok {number} - io_uring: fixture"
                for number in range(180, 353)
            ],
        ]
        regress_log.write_text(
            "# supported io_method values are: sync, worker, io_uring\n"
            "Name: io_uring\n"
            + "\n".join(assertions)
            + "\n1..529\n",
            encoding="utf-8",
        )
        server_log = build / contract["runtime_qualification"]["liburing"][
            "server_log"
        ]
        server_log.write_text(
            "LOG: starting PostgreSQL 18.6\n"
            "LOG: database system is ready to accept connections\n"
            "LOG: database system is shut down\n",
            encoding="utf-8",
        )
        build_log = build / "build.log"
        build_log.write_text("t/001_aio.pl ......... ok\n", encoding="utf-8")
        uname = os.uname()
        execution = {
            "disposition": "failed-under-observed-runtime-provider",
            "provider_observation": {
                "kernel_sysname": uname.sysname,
                "kernel_release": uname.release,
                "kernel_version": uname.version,
                "machine": uname.machine,
                "io_uring_disabled": int(
                    Path("/proc/sys/kernel/io_uring_disabled")
                    .read_text(encoding="ascii")
                    .strip()
                ),
            },
        }
        plan = {
            "source_root": str(source),
            "build_input_id": "a" * 64,
            "runtime_package": {
                "component_checkpoints": {"liburing": "b" * 64},
                "component_test_executions": {"liburing": execution},
                "runtime_provider_qualification": {
                    "requirements": {
                        "liburing": {
                            "component_checkpoint_sha256": "b" * 64,
                            "test_execution_sha256": BUILD.canonical_sha256(execution),
                            "observed_disposition": execution["disposition"],
                        }
                    }
                },
            },
        }
        return contract, plan, build, build_log

    def test_postgresql_selected_io_uring_path_is_qualified(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            contract, plan, build, build_log = self.io_uring_fixture(Path(temporary))
            receipt = BUILD.qualify_postgresql_io_uring(
                contract, plan, build, build_log
            )
            self.assertTrue(receipt["accepted"])
            self.assertEqual(receipt["regress_log"]["io_uring_assertion_count"], 175)
            self.assertEqual(receipt["regress_log"]["test_plan"], 529)

    def test_io_uring_failed_assertion_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            contract, plan, build, build_log = self.io_uring_fixture(Path(temporary))
            regress = build / contract["runtime_qualification"]["liburing"][
                "regress_log"
            ]
            regress.write_text(
                regress.read_text(encoding="utf-8").replace(
                    "ok 200 - io_uring:", "not ok 200 - io_uring:"
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(BUILD.BuildError, "failed assertion"):
                BUILD.qualify_postgresql_io_uring(contract, plan, build, build_log)

    def test_io_uring_assertion_boundary_mutation_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            contract, plan, build, build_log = self.io_uring_fixture(Path(temporary))
            regress = build / contract["runtime_qualification"]["liburing"][
                "regress_log"
            ]
            regress.write_text(
                regress.read_text(encoding="utf-8").replace(
                    "ok 352 - io_uring: fixture\n", ""
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(BUILD.BuildError, "assertion boundary"):
                BUILD.qualify_postgresql_io_uring(contract, plan, build, build_log)

    def test_io_uring_provider_host_mutation_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            contract, plan, build, build_log = self.io_uring_fixture(Path(temporary))
            plan["runtime_package"]["component_test_executions"]["liburing"][
                "provider_observation"
            ]["kernel_release"] = "different"
            execution = plan["runtime_package"]["component_test_executions"][
                "liburing"
            ]
            plan["runtime_package"]["runtime_provider_qualification"]["requirements"][
                "liburing"
            ]["test_execution_sha256"] = BUILD.canonical_sha256(execution)
            with self.assertRaisesRegex(BUILD.BuildError, "host differs"):
                BUILD.qualify_postgresql_io_uring(contract, plan, build, build_log)

    def test_absolute_installed_runpath_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary)
            binary = prefix / "pgsql-18/bin/postgres"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"\x7fELFfixture")
            observed = mock.Mock(
                returncode=0,
                stdout=(
                    " 0x000000000000001d (RUNPATH) Library runpath: "
                    "[$ORIGIN:/opt/laplace/current/pgsql-18/lib]\n"
                ),
                stderr="",
            )
            with mock.patch.object(BUILD.subprocess, "run", return_value=observed):
                with self.assertRaisesRegex(BUILD.BuildError, "non-package RUNPATH"):
                    BUILD.verify_package_relative_runpaths(prefix, Path("/readelf"))

    def test_package_relative_installed_runpath_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary)
            binary = prefix / "pgsql-18/bin/postgres"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"\x7fELFfixture")
            observed = mock.Mock(
                returncode=0,
                stdout=(
                    " 0x000000000000001d (RUNPATH) Library runpath: "
                    "[$ORIGIN:$ORIGIN/../lib:$ORIGIN/../../lib]\n"
                ),
                stderr="",
            )
            with mock.patch.object(BUILD.subprocess, "run", return_value=observed):
                receipt = BUILD.verify_package_relative_runpaths(
                    prefix, Path("/readelf")
                )
            self.assertTrue(receipt["package_relative_only"])
            self.assertEqual(receipt["runpath_entry_count"], 3)

    def test_tap_perl_module_selection_cannot_be_narrowed(self) -> None:
        contract = self.contract()
        contract["build_toolchain"]["required_perl_modules"].pop("IPC::Run")
        with self.assertRaisesRegex(BUILD.BuildError, "required_perl_modules"):
            BUILD.validate_contract(contract)

    def test_ambient_system_tzdata_is_rejected(self) -> None:
        contract = self.contract()
        contract["build"]["configure_arguments"].append(
            "--with-system-tzdata=/usr/share/zoneinfo"
        )
        with self.assertRaisesRegex(BUILD.BuildError, "selected bundled tzdata"):
            BUILD.validate_contract(contract)

    def test_closed_build_input_provider_boundary_is_explicit_and_fail_closed(self) -> None:
        contract = self.contract()
        self.assertEqual(contract["input_closure"]["status"], "complete")
        self.assertEqual(contract["input_closure"]["unselected_host_inputs"], [])
        contract["input_closure"]["status"] = "incomplete"
        with self.assertRaisesRegex(BUILD.BuildError, "must remain complete"):
            BUILD.validate_contract(contract)

    def test_source_test_overlay_boundary_cannot_be_widened(self) -> None:
        contract = self.contract()
        contract["build"]["source_test_overlays"][0][
            "permitted_transient_generated_files"
        ].append("unexpected.c")
        with self.assertRaisesRegex(BUILD.BuildError, "overlay boundary"):
            BUILD.validate_contract(contract)

    def test_host_provider_receipt_cannot_become_an_ambient_relative_path(self) -> None:
        contract = self.contract()
        contract["host_build_provider"]["receipt"] = "ambient/receipt.json"
        with self.assertRaisesRegex(BUILD.BuildError, "must be absolute"):
            BUILD.validate_contract(contract)

    def test_bootstrap_input_selection_cannot_be_narrowed(self) -> None:
        contract = self.contract()
        contract["build_toolchain"]["required_bootstrap_inputs"].pop("sh")
        with self.assertRaisesRegex(BUILD.BuildError, "required_bootstrap_inputs"):
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
            product = root / "stage/root/opt/laplace/current"
            openssl = product / "bin/openssl"
            openssl.parent.mkdir(parents=True)
            openssl.write_bytes(b"selected openssl\n")
            openssl.chmod(0o755)
            plan = {
                "build_toolchain": {"prefix": "/toolchain", "tools": tools},
                "runtime_package": {
                    "selected_build_executables": {
                        "openssl": {
                            "relative_path": "bin/openssl",
                            "sha256": BUILD.sha256_file(openssl),
                        }
                    }
                },
                "staged_product_prefix": str(product),
                "stage_directory": str(root / "stage"),
                "build_directory": str(root / "build/contains-41"),
            }
            environment = BUILD.build_environment(self.contract(), plan, home)
            self.assertEqual(environment["HOME"], str(home.resolve()))
            self.assertEqual(home.stat().st_mode & 0o7777, 0o700)
            self.assertEqual(environment["MAKE"], "/toolchain/bin/make")
            self.assertEqual(environment["OPENSSL"], str(openssl))
            self.assertEqual(environment["PYTHON"], "/usr/bin/python3.10")
            self.assertEqual(environment["SHELL"], "/usr/bin/dash")
            self.assertEqual(environment["CONFIG_SHELL"], "/usr/bin/dash")
            self.assertEqual(environment["PATH"].split(":")[0], str(openssl.parent))
            self.assertNotIn("/usr/lib", environment["PKG_CONFIG_LIBDIR"])
            self.assertIn("-ffile-prefix-map=", environment["CFLAGS"])
            self.assertIn("$$ORIGIN", environment["LDFLAGS"])

    def test_bootstrap_execution_binds_python_and_posix_shell(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            python = root / "python"
            python.write_text(
                "#!/bin/sh\nprintf '%s\\n' 'Python fixture-1'\n", encoding="utf-8"
            )
            python.chmod(0o755)
            shell = root / "sh"
            shell.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            shell.chmod(0o755)
            plan = {
                "build_toolchain": {
                    "bootstrap_inputs": {
                        "python": {
                            "path": str(python),
                            "sha256": BUILD.sha256_file(python),
                            "version": "Python fixture-1",
                        },
                        "sh": {
                            "path": str(shell),
                            "sha256": BUILD.sha256_file(shell),
                            "version": "fixture-sh",
                        },
                    }
                }
            }
            environment = {
                "PYTHON": str(python),
                "SHELL": str(shell),
                "CONFIG_SHELL": str(shell),
            }
            receipt = BUILD.verify_bootstrap_input_execution(
                plan,
                environment,
                executing_python=python,
                shebang_shell=shell,
            )
            self.assertEqual(receipt["python"]["version"], "Python fixture-1")
            self.assertEqual(receipt["sh"]["bin_sh_resolved_path"], str(shell))

            other_python = root / "other-python"
            other_python.write_bytes(python.read_bytes())
            other_python.chmod(0o755)
            with self.assertRaisesRegex(
                BUILD.BuildError, "Python bootstrap provider"
            ):
                BUILD.verify_bootstrap_input_execution(
                    plan,
                    environment,
                    executing_python=other_python,
                    shebang_shell=shell,
                )

    def test_build_recipe_identity_changes_with_driver_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            verifier = root / "tools/dependencies/release-assets.py"
            verifier.parent.mkdir(parents=True)
            verifier.write_text("verifier-v1", encoding="utf-8")
            (verifier.parent / "package_receipts.py").write_text(
                "receipt-verifier-v1", encoding="utf-8"
            )
            (verifier.parent / "elf-closure.py").write_text(
                "elf-closure-v1", encoding="utf-8"
            )
            host_verifier = root / "tools/postgresql/host-provider.py"
            host_verifier.parent.mkdir(parents=True)
            host_verifier.write_text("host-provider-v1", encoding="utf-8")
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
                with mock.patch.object(
                    BUILD, "package_selected_runtime_providers", return_value={}
                ):
                    observed, _, _ = BUILD.prepare_build_directory(
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

    def test_host_feature_and_intel_runtime_libraries_have_distinct_package_roles(self) -> None:
        closure = BUILD.classify_needed(
            self.contract(), {"libpq.so.5", "libssl.so.4", "libimf.so", "libc.so.6"}
        )
        self.assertEqual(
            closure["package"], ["libimf.so", "libpq.so.5", "libssl.so.4"]
        )
        self.assertEqual(closure["system_abi"], ["libc.so.6"])
        self.assertEqual(closure["unknown"], [])

    def test_toolchain_receipt_selects_every_required_build_tool(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            contract = self.contract()
            receipt = self.toolchain_receipt(Path(temporary), contract)
            selected = BUILD.verify_toolchain_receipt(
                contract, receipt
            )
            self.assertEqual(
                set(selected["tools"]),
                set(self.contract()["build_toolchain"]["required_tools"]),
            )
            self.assertEqual(
                set(selected["perl_modules"]),
                set(self.contract()["build_toolchain"]["required_perl_modules"]),
            )
            self.assertEqual(set(selected["bootstrap_inputs"]), {"python", "sh"})

    def test_toolchain_receipt_detects_bootstrap_input_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            contract = self.contract()
            receipt_path = self.toolchain_receipt(root, contract)
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            python = Path(receipt["bootstrap_inputs"]["python"]["path"])
            python.write_bytes(b"mutated Python bootstrap provider\n")
            python.chmod(0o755)
            with self.assertRaisesRegex(
                BUILD.BuildError, "bootstrap input bytes differ"
            ):
                BUILD.verify_toolchain_receipt(contract, receipt_path)

    def test_toolchain_receipt_rejects_missing_bootstrap_input(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            contract = self.contract()
            receipt_path = self.toolchain_receipt(root, contract)
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            receipt["bootstrap_inputs"].pop("sh")
            receipt_path.write_text(json.dumps(receipt), encoding="utf-8")
            with self.assertRaisesRegex(
                BUILD.BuildError, "omits required bootstrap input"
            ):
                BUILD.verify_toolchain_receipt(contract, receipt_path)

    def test_toolchain_receipt_detects_perl_module_provider_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            contract = self.contract()
            receipt_path = self.toolchain_receipt(root, contract)
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            provider = Path(receipt["consumer_manifest"]["perl_modules"]["IPC::Run"]["path"])
            provider.write_text("mutated provider\n", encoding="utf-8")
            with self.assertRaisesRegex(BUILD.BuildError, "Perl module digest mismatch"):
                BUILD.verify_toolchain_receipt(contract, receipt_path)

    def test_toolchain_receipt_detects_linker_input_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            contract = self.contract()
            receipt_path = self.toolchain_receipt(root, contract)
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            linker_input = Path(receipt["linker_map_inputs"][0]["path"])
            linker_input.write_bytes(b"mutated linker input\n")
            with self.assertRaisesRegex(
                BUILD.BuildError, "linker-map input (size|digest) differs"
            ):
                BUILD.verify_toolchain_receipt(contract, receipt_path)

    def test_configure_receipt_rejects_ambient_openssl_even_with_selected_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "configure.log"
            selected = "/stage/opt/laplace/current/bin/openssl"
            version = "OpenSSL 4.0.1 9 Jun 2026 (Library: OpenSSL 4.0.1 9 Jun 2026)"
            log.write_text(
                "\n".join(
                    [
                        f"checking for OPENSSL... {selected}",
                        f"configure: using openssl: {version}",
                        "checking for Perl modules required for TAP tests... yes",
                        "checking for OPENSSL... /usr/bin/openssl",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(BUILD.BuildError, "ambient OpenSSL"):
                BUILD.verify_configure_input_selection(
                    log,
                    {"OPENSSL": selected},
                    {"openssl": {"version_line": version}},
                )

    def test_runtime_receipt_binds_complete_component_set_and_exact_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = self.runtime_receipt(root)
            selected = BUILD.verify_runtime_receipt(self.contract(), path)
            self.assertEqual(selected["build_input_id"], "4" * 64)
            self.assertEqual(
                selected["selected_build_executables"]["openssl"]["version_line"],
                "OpenSSL 4.0.1 9 Jun 2026 (Library: OpenSSL 4.0.1 9 Jun 2026)",
            )
            receipt = json.loads(path.read_text(encoding="utf-8"))
            receipt["component_checkpoints"].pop("liburing")
            path.write_text(json.dumps(receipt), encoding="utf-8")
            with self.assertRaisesRegex(BUILD.BuildError, "evidence set"):
                BUILD.verify_runtime_receipt(self.contract(), path)

    def test_runtime_receipt_rejects_wrong_selected_openssl_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.runtime_receipt(Path(temporary))
            observed = mock.Mock(
                returncode=0,
                stdout="OpenSSL 3.0.2 15 Mar 2022 (Library: OpenSSL 3.0.2 15 Mar 2022)\n",
                stderr="",
            )
            with mock.patch.object(BUILD.subprocess, "run", return_value=observed):
                with self.assertRaisesRegex(BUILD.BuildError, "version differs"):
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
                "installed_runtime_provider": {
                    "files": {},
                    "lock_sha256": "8" * 64,
                    "provider_selection_sha256": "9" * 64,
                },
                "build_toolchain": {"linker_map_inputs": {}},
            }
            observed_build, receipt, providers = BUILD.prepare_build_directory(
                self.contract(), plan, resume=False
            )
            self.assertEqual(observed_build, build)
            self.assertEqual(providers["files"], {})
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

    def test_runtime_provider_packaging_is_receipted_and_resume_detects_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            product = root / "product"
            product.mkdir()
            intel = root / "libimf.so"
            intel.write_bytes(b"intel runtime\n")
            support = root / "libstdc++.so.6.0.30"
            support.write_bytes(b"compiler support\n")
            plan = {
                "installed_runtime_provider": {
                    "lock_sha256": "1" * 64,
                    "provider_selection_sha256": "2" * 64,
                    "files": {
                        "imf-runtime": {
                            "path": str(intel),
                            "sha256": BUILD.sha256_file(intel),
                            "size_bytes": intel.stat().st_size,
                            "class": "runtime-object",
                            "soname": "libimf.so",
                            "package_relative_path": "lib/libimf.so",
                        }
                    },
                },
                "build_toolchain": {
                    "linker_map_inputs": {
                        "cxx-runtime": {
                            "path": str(support),
                            "sha256": BUILD.sha256_file(support),
                            "size_bytes": support.stat().st_size,
                            "soname": "libstdc++.so.6",
                            "classification": "packaged-compiler-support",
                            "package_relative_path": "lib/libstdc++.so.6.0.30",
                            "package_aliases": ["lib/libstdc++.so.6"],
                        }
                    }
                },
            }
            receipt = BUILD.package_selected_runtime_providers(
                plan, product, resume=False
            )
            self.assertFalse(receipt["product_runtime_activated"])
            self.assertEqual((product / "lib/libimf.so").read_bytes(), b"intel runtime\n")
            self.assertEqual(
                (product / "lib/libstdc++.so.6").readlink(),
                Path("libstdc++.so.6.0.30"),
            )
            (product / "lib/libimf.so").write_bytes(b"mutated\n")
            with self.assertRaisesRegex(
                BUILD.BuildError, "resumed packaged runtime provider differs"
            ):
                BUILD.package_selected_runtime_providers(plan, product, resume=True)

    def test_recursive_closure_rejects_external_or_substituted_providers(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prefix = (root / "product").resolve()
            (prefix / "pgsql-18/lib").mkdir(parents=True)
            (prefix / "lib").mkdir()
            loader = root / "ld-linux-x86-64.so.2"
            loader.write_bytes(b"loader\n")
            packaged = prefix / "lib/libimf.so"
            packaged.write_bytes(b"intel\n")
            toolchain = {
                "tools": {"readelf": {"path": "/toolchain/readelf"}},
                "linker_map_inputs": {
                    "dynamic-loader": {
                        "path": str(loader),
                        "sha256": BUILD.sha256_file(loader),
                        "size_bytes": loader.stat().st_size,
                        "soname": "ld-linux-x86-64.so.2",
                        "classification": "platform-abi",
                        "package_aliases": [],
                    }
                },
            }
            installed = {
                "files": {
                    "imf-runtime": {
                        "path": "/provider/libimf.so",
                        "sha256": BUILD.sha256_file(packaged),
                        "size_bytes": packaged.stat().st_size,
                        "class": "runtime-object",
                        "soname": "libimf.so",
                        "package_relative_path": "lib/libimf.so",
                    }
                }
            }
            output = root / "closure.json"
            report = {
                "schema": "laplace.elf-closure/v1",
                "tool_version": "1.0.0",
                "inputs": {
                    "roots": [str(prefix)],
                    "custom_prefix": str(prefix),
                    "process_cwd": "/",
                    "environment_library_path_used": False,
                    "explicit_search_directories": sorted(
                        [str(prefix / "pgsql-18/lib"), str(prefix / "lib")]
                    ),
                },
                "host_loader": {
                    "path": str(loader.resolve()),
                    "sha256": BUILD.sha256_file(loader),
                },
                "summary": {
                    field: 0
                    for field in self.contract()["runtime_closure"][
                        "recursive_verifier"
                    ]["required_zero_summary_fields"]
                },
                "objects": [
                    {
                        "path": str(loader.resolve()),
                        "sha256": BUILD.sha256_file(loader),
                        "classification": "host-system",
                        "elf": {"soname": "ld-linux-x86-64.so.2"},
                    },
                    {
                        "path": str(packaged.resolve()),
                        "sha256": BUILD.sha256_file(packaged),
                        "classification": "custom-prefix",
                        "elf": {"soname": "libimf.so"},
                    },
                ],
            }

            def verify(candidate: dict[str, object]) -> dict[str, object]:
                output.write_text(json.dumps(candidate), encoding="utf-8")
                with mock.patch.object(
                    BUILD.subprocess, "run", return_value=mock.Mock(returncode=0)
                ):
                    return BUILD.verify_recursive_elf_closure(
                        self.contract(), REPO_ROOT, prefix, toolchain, installed, output
                    )

            self.assertTrue(verify(report)["verified"])
            external = json.loads(json.dumps(report))
            external["objects"].append(
                {
                    "path": "/ambient/libmystery.so",
                    "sha256": "3" * 64,
                    "classification": "external-prefix",
                    "elf": {"soname": "libmystery.so"},
                }
            )
            with self.assertRaisesRegex(BUILD.BuildError, "external runtime provider"):
                verify(external)
            omitted = json.loads(json.dumps(report))
            omitted["objects"] = [omitted["objects"][0]]
            with self.assertRaisesRegex(BUILD.BuildError, "omitted selected packaged provider"):
                verify(omitted)

    def test_execute_uses_selected_make_and_explicit_install_destdir(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            build.mkdir()
            selected_make = "/toolchain/bin/make"
            plan = {
                "build_directory": str(build),
                "stage_directory": str(root / "stage"),
                "source_root": str(root / "source"),
                "staged_product_prefix": str(root / "stage/root/opt/laplace/current"),
                "build_toolchain": {
                    "prefix": "/toolchain",
                    "tools": {
                        "make": {"path": selected_make},
                        "readelf": {"path": "/toolchain/bin/readelf"},
                    }
                },
                "runtime_package": {"receipt_path": str(root / "runtime.json")},
                "installed_runtime_provider": {},
                "configure_command": ["/source/configure"],
                "parallel_jobs": 6,
                "make_targets": ["world-bin", "check-world", "install-world-bin"],
                "build_input_id": "6" * 64,
                "recipe": {"driver": "fixture"},
                "release_prefix": "/opt/laplace/releases/fixture",
                "host_build_provider": {"provider_id": "8" * 64},
            }
            captured: list[list[str]] = []

            def capture(command: object, *_args: object, **_kwargs: object) -> None:
                captured.append(list(command))

            with mock.patch.object(
                BUILD, "prepare_build_directory", return_value=(build, {}, {})
            ), mock.patch.object(
                BUILD, "build_environment", return_value={}
            ), mock.patch.object(
                BUILD, "verify_build_input_execution", return_value={}
            ), mock.patch.object(
                BUILD, "verify_configure_input_selection", return_value={}
            ), mock.patch.object(
                BUILD, "prepare_source_test_overlays", return_value=[]
            ), mock.patch.object(
                BUILD, "run_logged", side_effect=capture
            ), mock.patch.object(
                BUILD, "verify_runtime_bytes_in_composed_tree"
            ), mock.patch.object(
                BUILD,
                "qualify_postgresql_io_uring",
                return_value={"schema": "fixture", "accepted": True},
            ), mock.patch.object(
                BUILD,
                "verify_package",
                return_value={
                    "recursive_elf_closure_verified": True,
                    "runpath_verification": {"package_relative_only": True},
                },
            ), mock.patch.object(
                BUILD,
                "sandboxed_build_command",
                side_effect=lambda _contract, _plan, command, _overlays=(): list(command),
            ), mock.patch.object(
                BUILD,
                "verify_host_build_provider",
                return_value=plan["host_build_provider"],
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
            ) as package_tree, mock.patch.object(
                BUILD,
                "verify_recursive_elf_closure",
                return_value={"verified": True},
            ):
                receipt = BUILD.verify_package(
                    self.contract(),
                    prefix,
                    Path("/toolchain/readelf"),
                    repository=REPO_ROOT,
                    toolchain={},
                    installed_provider={},
                    closure_output=Path(temporary) / "closure.json",
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
                "tools": {"readelf": {"path": "/toolchain/readelf"}},
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
                BUILD,
                "verify_host_build_provider",
                return_value={"provider_id": "a" * 64},
            ), mock.patch.object(
                BUILD,
                "verify_installed_runtime_provider",
                return_value={"provider_selection_sha256": "9" * 64},
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
