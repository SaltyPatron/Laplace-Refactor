#!/usr/bin/env python3
"""Contract and mutation checks for the staged PostgreSQL runtime graph."""

from __future__ import annotations

import copy
import importlib.util
import json
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
                f"#!/bin/sh\nprintf '%s\\n' '{linker}' >&2\n",
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
            contract = {"release_lock": "release-lock.json"}
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

    def test_plan_identity_changes_with_contract_and_outputs_remain_external(self) -> None:
        contract = self.contract()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            contract["execution"]["final_prefix_root"] = str(root / "releases")
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
                / Path(first["final_prefix"]).relative_to("/"),
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
                    "final_prefix": "/opt/laplace/releases/example",
                    "staged_prefix": str(prefix),
                    "tools": {"readelf": {"path": "/usr/bin/readelf"}},
                },
            )
            self.assertFalse(receipt["build_input_closure_complete"])
            self.assertFalse(receipt["static_link_closure_verified"])
            self.assertFalse(receipt["recursive_runtime_closure_verified"])
            self.assertFalse(receipt["activation_eligible"])


if __name__ == "__main__":
    unittest.main()
