#!/usr/bin/env python3
"""Mutation checks for the PostgreSQL build and package boundary."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


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
            home = Path(temporary) / "private-home"
            environment = BUILD.build_environment(self.contract(), home)
            self.assertEqual(environment["HOME"], str(home.resolve()))
            self.assertEqual(home.stat().st_mode & 0o7777, 0o700)

    def test_build_recipe_identity_changes_with_driver_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            verifier = root / "tools/dependencies/release-assets.py"
            verifier.parent.mkdir(parents=True)
            verifier.write_text("verifier-v1", encoding="utf-8")
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
            plan = {"build_directory": str(build), "prefix": str(root / "package")}
            (build / "build-plan.json").write_text(
                json.dumps({**plan, "mutated": True}), encoding="utf-8"
            )
            with self.assertRaisesRegex(BUILD.BuildError, "exact plan"):
                BUILD.prepare_build_directory(plan, resume=True)
            (build / "build-plan.json").write_text(json.dumps(plan), encoding="utf-8")
            self.assertEqual(BUILD.prepare_build_directory(plan, resume=True), build)

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
            self.contract(), {"libpq.so.5", "libssl.so.3", "libimf.so", "libc.so.6"}
        )
        self.assertEqual(closure["package"], ["libpq.so.5"])
        self.assertEqual(closure["system_abi"], ["libc.so.6"])
        self.assertEqual(closure["selected_but_unpacked"], ["libimf.so", "libssl.so.3"])


if __name__ == "__main__":
    unittest.main()
