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
