#!/usr/bin/env python3
"""Acceptance and deliberate-defect tests for isolated PostgreSQL activation."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/postgresql/clusterctl.py"
SPEC = importlib.util.spec_from_file_location("laplace_clusterctl", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
clusterctl = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = clusterctl
SPEC.loader.exec_module(clusterctl)


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def rehash_plan(plan: dict[str, Any]) -> None:
    core = dict(plan)
    core.pop("plan_sha256", None)
    plan["plan_sha256"] = clusterctl.sha256_bytes(clusterctl.canonical_bytes(core))


class PostgreSQLClusterContract(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-pg-cluster-")
        self.root = Path(self.temporary.name)
        self.contract = clusterctl.load_json(REPOSITORY / "contracts/postgresql-cluster.json")
        self.contract_path = self.root / "contract.json"
        self.resource_path = self.root / "resource.json"
        self.collision_path = self.root / "collision.json"
        self.manifest_path = self.root / "package.json"
        self.package_physical_root = self.root / "package-root"
        self.activation_root = self.root / "activation-root"
        self.activation_root.mkdir()
        write_json(self.contract_path, self.contract)
        package = self.create_package()
        write_json(self.manifest_path, package)
        write_json(self.resource_path, self.valid_resource_observation(package))
        write_json(self.collision_path, self.valid_collision_observation())

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def valid_resource_observation(
        self, package: dict[str, Any] | None = None
    ) -> dict[str, Any]:
        if package is None:
            package = clusterctl.load_json(self.manifest_path)
        observation = {
            "schema": clusterctl.RESOURCE_SCHEMA,
            "source": "laplace_native_execution_authority",
            "package_id": package["package_id"],
            "package_manifest_sha256": clusterctl.sha256_bytes(
                clusterctl.canonical_bytes(package)
            ),
            "topology_receipt": "1" * 64,
            "root_grant_receipt": "2" * 64,
            "partition_receipt": "3" * 64,
            "processor_allocation_receipt": "4" * 64,
            "storage_observation_receipt": "5" * 64,
            "grant": {
                "processor_ids": [20, 21, 28, 29],
                "cpu_slots": 4,
                "memory_bytes": 12 * 1024**3,
                "io_slots": 2,
            },
            "storage": {
                "data": {
                    "path": self.contract["instance"]["data_directory"],
                    "available_bytes": 256 * 1024**3,
                    "backing_path": "/opt/laplace",
                    "fragment_bytes": 4096,
                },
                "wal": {
                    "path": self.contract["instance"]["wal_directory"],
                    "available_bytes": 64 * 1024**3,
                    "backing_path": "/var/lib",
                    "fragment_bytes": 4096,
                },
                "temporary": {
                    "path": self.contract["instance"]["temp_directory"],
                    "available_bytes": 128 * 1024**3,
                    "backing_path": "/",
                    "fragment_bytes": 4096,
                },
            },
            "native_authority": {
                "schema": clusterctl.NATIVE_RESOURCE_SCHEMA,
                "observer": {
                    "path": clusterctl.RESOURCE_OBSERVER_PATH,
                    "sha256": "6" * 64,
                },
                "allowed_processor_count": 8,
                "processor_ids": [20, 21, 28, 29],
                "partition_grant": {
                    "cpu_slots": 4,
                    "memory_bytes": 12 * 1024**3,
                    "io_slots": 2,
                },
            },
        }
        observation["observation_sha256"] = clusterctl.resource_observation_identity(
            observation
        )
        return observation

    def valid_collision_observation(self) -> dict[str, Any]:
        observation = {
            "schema": clusterctl.COLLISION_SCHEMA,
            "source": "laplace_typed_fixture",
            "root": "/",
            "target": clusterctl.collision_target(self.contract),
            "collisions": [],
            "inspection_errors": [],
        }
        observation["observation_sha256"] = clusterctl.collision_observation_identity(
            observation
        )
        return observation

    def create_package(self) -> dict[str, Any]:
        entries: list[dict[str, Any]] = []
        for relative in self.contract["package"]["required_files"]:
            content = f"fixture:{relative}\n".encode("utf-8")
            mode = 0o755 if "/bin/" in relative or relative.endswith(".so") else 0o644
            runpath = ["$ORIGIN/../.."] if relative.endswith("laplace_pg.so") else []
            entries.append(
                {
                    "path": relative,
                    "kind": "file",
                    "sha256": hashlib.sha256(content).hexdigest(),
                    "mode": mode,
                    "runpath": runpath,
                }
            )
        link_target = "liblaplace_engine.so.2.0.0"
        entries.append(
            {
                "path": "lib/liblaplace_engine.so.2",
                "kind": "symlink",
                "target": link_target,
                "sha256": hashlib.sha256(link_target.encode("utf-8")).hexdigest(),
                "runpath": [],
            }
        )
        manifest = {
            "schema": clusterctl.PACKAGE_SCHEMA,
            "postgresql": {"version": "18.6", "pg_config": "pgsql-18/bin/pg_config"},
            "capabilities": {
                "app_effect_boundary": 1,
                "exact_loaded_object_identity": 1,
                "topology_bound_processor_allocation": 1,
            },
            "loader_environment": {},
            "activation_eligible": True,
            "activation_gates": {
                "postgresql_build_input_closure": True,
                "postgresql_runtime_provider_qualification": True,
                "postgresql_package_activation": True,
                "recursive_elf_closure": True,
            },
            "files": entries,
            "loaded_objects": self.contract["package"]["required_loaded_objects"],
        }
        package_id = clusterctl.package_identity(manifest)
        logical_root = f"/opt/laplace/releases/{package_id}"
        manifest["package_id"] = package_id
        manifest["root"] = logical_root
        physical = clusterctl.prefixed(self.package_physical_root, logical_root)
        for entry in entries:
            target = physical / entry["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists() or target.is_symlink():
                target.unlink()
            if entry["kind"] == "symlink":
                target.symlink_to(entry["target"])
            else:
                target.write_bytes(f"fixture:{entry['path']}\n".encode("utf-8"))
                target.chmod(entry["mode"])
        return manifest

    def plan(self, verify_bytes: bool = True) -> dict[str, Any]:
        return clusterctl.build_plan(
            self.contract_path,
            self.manifest_path,
            self.resource_path,
            self.collision_path,
            self.package_physical_root if verify_bytes else None,
        )

    @staticmethod
    def replace_rendered(plan: dict[str, Any], suffix: str, content: str) -> None:
        entry = next(item for item in plan["files"] if item["path"].endswith(suffix))
        entry["content"] = content
        entry["sha256"] = hashlib.sha256(content.encode("utf-8")).hexdigest()
        rehash_plan(plan)

    @staticmethod
    def loaded_observation(plan: dict[str, Any]) -> dict[str, Any]:
        observation = {
            "schema": clusterctl.LOADED_SCHEMA,
            "source": "laplace_typed_fixture",
            "package_id": plan["package_id"],
            "port": plan["instance"]["port"],
            "socket_directory": plan["instance"]["socket_directory"],
            "data_directory": plan["instance"]["data_directory"],
            "service": plan["instance"]["service"],
            "system_identifier": "8672946663471807927",
            "loaded_objects": copy.deepcopy(plan["required_loaded_objects"]),
            "config_files": [
                {"path": item["path"], "sha256": item["sha256"]}
                for item in plan["files"]
            ],
        }
        observation["observation_sha256"] = clusterctl.state_observation_identity(
            observation
        )
        return observation

    @staticmethod
    def stopped_observation(plan: dict[str, Any]) -> dict[str, Any]:
        observation = {
            "schema": clusterctl.STOPPED_SCHEMA,
            "source": "laplace_typed_fixture",
            "service": plan["instance"]["service"],
            "data_directory": plan["instance"]["data_directory"],
            "service_state": "inactive",
            "postmaster_pid": None,
        }
        observation["observation_sha256"] = clusterctl.state_observation_identity(
            observation
        )
        return observation

    def write_manifest(self, mutation: Any) -> None:
        document = clusterctl.load_json(self.manifest_path)
        mutation(document)
        document["package_id"] = clusterctl.package_identity(document)
        document["root"] = f"/opt/laplace/releases/{document['package_id']}"
        write_json(self.manifest_path, document)

    def test_live_plan_preserves_static_service_and_existing_receipt_root(self) -> None:
        collision = self.valid_collision_observation()
        collision["source"] = "laplace_clusterctl_live_probe"
        collision["observation_sha256"] = clusterctl.collision_observation_identity(collision)
        write_json(self.collision_path, collision)
        plan = self.plan()
        clusterctl.validate_plan(plan, self.contract)
        self.assertFalse(any(row["path"].endswith(".service") for row in plan["files"]))
        self.assertNotIn(self.contract["instance"]["receipt_directory"], plan["state_directories"])
        self.assertFalse(any(Path(argv[0]).name == "runuser" for argv in plan["commands"].values()))
        self.assertNotIn("daemon_reload", plan["commands"])
        broken = copy.deepcopy(plan)
        broken["runtime_link"] = "/tmp/changed-runtime"
        with self.assertRaisesRegex(clusterctl.ClusterError, "plan digest"):
            clusterctl.validate_plan(broken, self.contract)

    def test_peer_file_digest_is_checked_before_provider_projection(self) -> None:
        plan = self.plan()
        next(row for row in plan["files"] if row["path"].endswith("pg_ident.conf"))["sha256"] = "0" * 64
        plan.pop("plan_sha256")
        plan["plan_sha256"] = clusterctl.sha256_bytes(clusterctl.canonical_bytes(plan))
        with self.assertRaisesRegex(clusterctl.ClusterError, "rendered file digest"):
            clusterctl.validate_plan(plan, self.contract)

    def test_verified_plan_is_isolated_and_resource_bounded(self) -> None:
        plan = self.plan()
        clusterctl.validate_plan(plan)
        self.assertTrue(plan["package_verified"])
        self.assertFalse(plan["activation_blocked"])
        self.assertEqual(plan["instance"]["port"], 55433)
        self.assertEqual(plan["settings"]["shared_buffers"], "2048MB")
        self.assertEqual(plan["settings"]["effective_cache_size"], "6144MB")
        self.assertEqual(plan["settings"]["max_worker_processes"], "8")
        self.assertEqual(plan["settings"]["max_parallel_workers"], "2")
        rendered = "\n".join(item["content"] for item in plan["files"])
        self.assertNotIn("LD_LIBRARY_PATH", rendered)
        self.assertIn("local all all reject", rendered)
        self.assertIn("AllowedCPUs=20 21 28 29", rendered)
        self.assertNotIn("ExecStartPost=", rendered)
        self.assertEqual(
            plan["commands"]["probe_readiness"][0],
            f"{plan['package_root']}/pgsql-18/bin/pg_isready",
        )
        bootstrap = next(
            item["content"] for item in plan["files"] if item["path"].endswith("bootstrap.sql")
        )
        for row_by_row in (" LOOP ", "CURSOR", "WITH RECURSIVE"):
            self.assertNotIn(row_by_row, bootstrap.upper())
        for signature in (
            "identity_codepoint_calculate_batch(laplace.execution_context, integer[])",
            "identity_codepoint_execute_batch(laplace.execution_context, integer[])",
            "trajectory_composition_decode_calculate_batch(laplace.execution_context, bytea[])",
            "trajectory_composition_decode_execute_batch(laplace.execution_context, bytea[])",
            "unicode_tier0_resolve_batch(bytea, bytea, integer[])",
            "unicode_identity_reverse_resolve_batch(bytea, bytea, bytea[], bytea[])",
        ):
            self.assertIn(signature, bootstrap)

    def test_same_major_patch_profile_uses_exact_selected_18_3_package(self) -> None:
        contract = clusterctl.load_json(self.contract_path)
        contract["package"]["postgresql_version"] = "18.3"
        write_json(self.contract_path, contract)
        self.contract = contract
        package = clusterctl.load_json(self.manifest_path)
        package["postgresql"]["version"] = "18.3"
        package["package_id"] = clusterctl.package_identity(package)
        package["root"] = f"/opt/laplace/releases/{package['package_id']}"
        write_json(self.manifest_path, package)
        write_json(self.resource_path, self.valid_resource_observation(package))
        physical = clusterctl.prefixed(self.package_physical_root, package["root"])
        original = next(self.package_physical_root.glob("opt/laplace/releases/*"))
        if original != physical:
            original.rename(physical)
        plan = self.plan()
        clusterctl.validate_plan(plan, contract)
        self.assertEqual(plan["postgresql_version"], "18.3")
        self.assertIn(
            "Description=Laplace refactor PostgreSQL 18.3 cluster",
            next(
                item["content"]
                for item in plan["files"]
                if item["path"].endswith(".service")
            ),
        )

    def test_native_resource_observation_finalization_binds_packaged_observer(self) -> None:
        native = self.valid_resource_observation()
        package_id = native.pop("package_id")
        package_manifest_sha256 = native.pop("package_manifest_sha256")
        native["schema"] = clusterctl.RESOURCE_CANDIDATE_SCHEMA
        native.pop("observation_sha256")
        native["native_authority"].pop("observer")
        result = clusterctl.finalize_native_resource_observation(
            native,
            {
                "path": clusterctl.RESOURCE_OBSERVER_PATH,
                "kind": "file",
                "sha256": "7" * 64,
            },
            self.contract,
            package_id,
            package_manifest_sha256,
        )
        self.assertEqual(
            result["native_authority"]["observer"],
            {"path": clusterctl.RESOURCE_OBSERVER_PATH, "sha256": "7" * 64},
        )
        self.assertEqual(
            result["observation_sha256"],
            clusterctl.resource_observation_identity(result),
        )

    def test_resource_command_uses_runner_contract_and_exact_observer(self) -> None:
        native = self.valid_resource_observation()
        native["schema"] = clusterctl.RESOURCE_CANDIDATE_SCHEMA
        for field in ("package_id", "package_manifest_sha256", "observation_sha256"):
            native.pop(field)
        native["native_authority"].pop("observer")
        output = self.root / "cli-resources.json"
        with mock.patch.object(clusterctl.subprocess, "run", return_value=
                               subprocess.CompletedProcess([], 0, json.dumps(native), "")) as run:
            self.assertEqual(clusterctl.main([
                "observe-resources", "--contract", str(self.contract_path),
                "--package-manifest", str(self.manifest_path),
                "--package-physical-root", str(self.package_physical_root),
                "--output", str(output)]), 0)
        manifest = clusterctl.load_json(self.manifest_path)
        self.assertEqual(run.call_args.args[0][0], str(clusterctl.prefixed(
            self.package_physical_root, manifest["root"]) / clusterctl.RESOURCE_OBSERVER_PATH))
        result = clusterctl.load_json(output)
        clusterctl.validate_resource_package_binding(result, manifest)
        self.assertEqual(result["grant"], native["grant"])
        self.assertEqual(clusterctl.load_json(self.contract_path), self.contract)

    def test_resource_observation_package_binding_mutants_are_rejected(self) -> None:
        for field, value in (
            ("package_id", "a" * 64),
            ("package_manifest_sha256", "b" * 64),
        ):
            observation = self.valid_resource_observation()
            observation[field] = value
            observation["observation_sha256"] = (
                clusterctl.resource_observation_identity(observation)
            )
            expected = (
                "resource observation package identity differs"
                if field == "package_id"
                else "resource observation package manifest differs"
            )
            package = clusterctl.load_json(self.manifest_path)
            with self.assertRaisesRegex(clusterctl.ClusterError, expected):
                clusterctl.validate_resource_package_binding(observation, package)

    def test_cluster_contract_cannot_omit_native_resource_observer(self) -> None:
        contract = copy.deepcopy(self.contract)
        contract["package"]["required_files"].remove(
            clusterctl.RESOURCE_OBSERVER_PATH
        )
        with self.assertRaisesRegex(clusterctl.ClusterError, "resource observer"):
            clusterctl.validate_contract(contract)

    def test_cluster_contract_cannot_omit_unicode_activation_identity_provider(self) -> None:
        contract = copy.deepcopy(self.contract)
        contract["package"]["required_files"].remove(
            clusterctl.UNICODE_ACTIVATION_IDENTITY_PATH
        )
        with self.assertRaisesRegex(clusterctl.ClusterError, "activation identity"):
            clusterctl.validate_contract(contract)

    def test_native_processor_allocation_drift_is_rejected(self) -> None:
        observation = self.valid_resource_observation()
        observation["native_authority"]["processor_ids"] = [20, 21, 28, 30]
        observation["observation_sha256"] = clusterctl.resource_observation_identity(
            observation
        )
        with self.assertRaisesRegex(clusterctl.ClusterError, "processor allocation"):
            clusterctl.validate_resource_observation(observation, self.contract)

    def test_resource_observation_without_exact_native_observer_is_rejected(self) -> None:
        observation = self.valid_resource_observation()
        observation["native_authority"]["observer"].pop("sha256")
        observation["observation_sha256"] = clusterctl.resource_observation_identity(
            observation
        )
        with self.assertRaisesRegex(clusterctl.ClusterError, "packaged native observer"):
            clusterctl.validate_resource_observation(observation, self.contract)

    def test_cli_installs_package_with_repository_runner_ownership(self) -> None:
        receipt = self.root / "cli-installation.json"
        result = subprocess.run(
            [sys.executable, str(MODULE_PATH), "install-package",
             "--contract", str(self.contract_path),
             "--package-manifest", str(self.manifest_path),
             "--package-physical-root", str(self.package_physical_root),
             "--root", str(self.activation_root), "--receipt", str(receipt)],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        installation = clusterctl.load_json(receipt)
        self.assertEqual(installation["phase"], "installed")
        self.assertTrue(clusterctl.verify_package(
            clusterctl.load_json(self.manifest_path), self.contract,
            self.activation_root).verified)

    def test_cli_plan_preserves_runner_owned_lifecycle(self) -> None:
        output = self.root / "cli-plan.json"
        result = subprocess.run(
            [sys.executable, str(MODULE_PATH), "plan",
             "--contract", str(self.contract_path),
             "--package-manifest", str(self.manifest_path),
             "--resource-observation", str(self.resource_path),
             "--collision-observation", str(self.collision_path),
             "--package-physical-root", str(self.package_physical_root),
             "--output", str(output)],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(clusterctl.load_json(output), self.plan())

    def test_exact_package_install_is_atomic_verified_and_replay_safe(self) -> None:
        manifest = clusterctl.load_json(self.manifest_path)
        first = clusterctl.install_package(
            manifest,
            self.contract,
            self.package_physical_root,
            self.activation_root,
            False,
        )
        installed = clusterctl.prefixed(self.activation_root, manifest["root"])
        self.assertTrue(installed.is_dir())
        self.assertTrue(
            clusterctl.verify_package(
                manifest, self.contract, self.activation_root
            ).verified
        )
        second = clusterctl.install_package(
            manifest,
            self.contract,
            self.package_physical_root,
            self.activation_root,
            False,
        )
        self.assertEqual(first, second)
        self.assertEqual(first["schema"], clusterctl.INSTALLATION_SCHEMA)
        self.assertEqual(first["phase"], "installed")
        self.assertFalse(first["overwrite_performed"])

    def test_package_install_refuses_a_different_existing_release(self) -> None:
        manifest = clusterctl.load_json(self.manifest_path)
        clusterctl.install_package(
            manifest,
            self.contract,
            self.package_physical_root,
            self.activation_root,
            False,
        )
        installed = clusterctl.prefixed(self.activation_root, manifest["root"])
        first = next(
            entry for entry in manifest["files"] if entry["kind"] == "file"
        )
        (installed / first["path"]).write_bytes(b"mutated installed package\n")
        with self.assertRaisesRegex(clusterctl.ClusterError, "already exists"):
            clusterctl.install_package(
                manifest,
                self.contract,
                self.package_physical_root,
                self.activation_root,
                False,
            )

    def test_package_install_refuses_tampered_source_bytes(self) -> None:
        manifest = clusterctl.load_json(self.manifest_path)
        source = clusterctl.prefixed(
            self.package_physical_root, manifest["root"]
        )
        first = next(
            entry for entry in manifest["files"] if entry["kind"] == "file"
        )
        (source / first["path"]).write_bytes(b"mutated source package\n")
        with self.assertRaisesRegex(clusterctl.ClusterError, "source package"):
            clusterctl.install_package(
                manifest,
                self.contract,
                self.package_physical_root,
                self.activation_root,
                False,
            )

    def test_system_package_install_requires_runner_identity(self) -> None:
        manifest = clusterctl.load_json(self.manifest_path)
        with self.assertRaisesRegex(clusterctl.ClusterError, "requires laplace-runner"):
            clusterctl.install_package(
                manifest,
                self.contract,
                self.package_physical_root,
                Path("/"),
                False,
            )

    def test_activation_is_blocked_without_package_bytes(self) -> None:
        plan = self.plan(verify_bytes=False)
        self.assertTrue(plan["activation_blocked"])
        with self.assertRaisesRegex(clusterctl.ClusterError, "blocked"):
            clusterctl.apply_plan(plan, self.contract, self.activation_root, False)

    def test_inert_complete_product_manifest_is_rejected(self) -> None:
        manifest = clusterctl.load_json(self.manifest_path)
        manifest["activation_eligible"] = False
        manifest["activation_gates"]["postgresql_build_input_closure"] = False
        manifest["package_id"] = clusterctl.package_identity(manifest)
        manifest["root"] = f"/opt/laplace/releases/{manifest['package_id']}"
        write_json(self.manifest_path, manifest)
        with self.assertRaisesRegex(clusterctl.ClusterError, "not activation eligible"):
            self.plan()

    def test_activation_refuses_a_plan_from_another_contract(self) -> None:
        plan = self.plan()
        contract = copy.deepcopy(self.contract)
        contract["resource_policy"]["maximum_connections"] = 20
        with self.assertRaisesRegex(clusterctl.ClusterError, "supplied cluster contract"):
            clusterctl.apply_plan(plan, contract, self.activation_root, False)

    def test_live_state_collision_blocks_all_generated_file_writes(self) -> None:
        plan = self.plan()
        state = clusterctl.prefixed(self.activation_root, plan["state_directories"][-1])
        state.mkdir(parents=True)
        with self.assertRaisesRegex(clusterctl.ClusterError, "target collision"):
            clusterctl.apply_plan(plan, self.contract, self.activation_root, False)
        for entry in plan["files"]:
            self.assertFalse(clusterctl.prefixed(self.activation_root, entry["path"]).exists())

    def test_package_digest_mismatch_blocks_activation(self) -> None:
        manifest = clusterctl.load_json(self.manifest_path)
        first = manifest["files"][0]
        physical = clusterctl.prefixed(
            self.package_physical_root, manifest["root"]
        ).joinpath(first["path"])
        physical.write_bytes(b"tampered\n")
        plan = self.plan()
        self.assertFalse(plan["package_verified"])
        with self.assertRaisesRegex(clusterctl.ClusterError, "blocked"):
            clusterctl.apply_plan(plan, self.contract, self.activation_root, False)

    def test_package_identity_label_mutant_is_rejected(self) -> None:
        manifest = clusterctl.load_json(self.manifest_path)
        manifest["package_id"] = "f" * 64
        manifest["root"] = f"/opt/laplace/releases/{manifest['package_id']}"
        write_json(self.manifest_path, manifest)
        with self.assertRaisesRegex(clusterctl.ClusterError, "canonical manifest"):
            self.plan()

    def test_unmanifested_package_file_blocks_activation(self) -> None:
        manifest = clusterctl.load_json(self.manifest_path)
        physical = clusterctl.prefixed(self.package_physical_root, manifest["root"])
        unexpected = physical / "pgsql-18/bin/unmanifested"
        unexpected.write_bytes(b"unexpected\n")
        unexpected.chmod(0o755)
        plan = self.plan()
        self.assertFalse(plan["package_verified"])
        self.assertIn("tree differs", plan["package_verification"])

    def test_manifested_internal_symlink_is_verified(self) -> None:
        plan = self.plan()
        self.assertTrue(plan["package_verified"])
        manifest = clusterctl.load_json(self.manifest_path)
        link = next(item for item in manifest["files"] if item["kind"] == "symlink")
        physical = clusterctl.prefixed(self.package_physical_root, manifest["root"])
        (physical / link["path"]).unlink()
        (physical / link["path"]).symlink_to("forged.so")
        plan = self.plan()
        self.assertFalse(plan["package_verified"])
        self.assertIn("symlink", plan["package_verification"])

    def test_broken_manifested_symlink_blocks_activation(self) -> None:
        manifest = clusterctl.load_json(self.manifest_path)
        link = next(item for item in manifest["files"] if item["kind"] == "symlink")
        old_physical = clusterctl.prefixed(
            self.package_physical_root, manifest["root"]
        )
        link["target"] = "missing.so"
        link["sha256"] = hashlib.sha256(b"missing.so").hexdigest()
        manifest.pop("package_id")
        manifest.pop("root")
        manifest["package_id"] = clusterctl.package_identity(manifest)
        manifest["root"] = f"/opt/laplace/releases/{manifest['package_id']}"
        new_physical = clusterctl.prefixed(
            self.package_physical_root, manifest["root"]
        )
        new_physical.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(old_physical, new_physical)
        target = new_physical / link["path"]
        target.unlink()
        target.symlink_to(link["target"])
        write_json(self.manifest_path, manifest)
        plan = self.plan()
        self.assertFalse(plan["package_verified"])
        self.assertIn("target is absent", plan["package_verification"])

    def test_escaping_package_symlink_is_rejected(self) -> None:
        def mutate(document: dict[str, Any]) -> None:
            link = next(item for item in document["files"] if item["kind"] == "symlink")
            link["target"] = "../../../outside"
            link["sha256"] = hashlib.sha256(link["target"].encode("utf-8")).hexdigest()

        self.write_manifest(mutate)
        with self.assertRaisesRegex(clusterctl.ClusterError, "escapes its package"):
            self.plan()

    def test_escaping_package_runpath_is_rejected(self) -> None:
        self.write_manifest(
            lambda document: document["files"][0].update(
                runpath=["$ORIGIN/../../../../outside"]
            )
        )
        with self.assertRaisesRegex(clusterctl.ClusterError, "escaping RUNPATH"):
            self.plan()

    def test_trust_authentication_mutant_is_rejected(self) -> None:
        plan = self.plan()
        hba = next(item["content"] for item in plan["files"] if item["path"].endswith("pg_hba.conf"))
        self.replace_rendered(plan, "pg_hba.conf", hba.replace("peer map=", "trust # map="))
        with self.assertRaisesRegex(clusterctl.ClusterError, "trust"):
            clusterctl.validate_plan(plan)

    def test_occupied_target_observation_is_rejected_without_a_blacklist(self) -> None:
        observation = self.valid_collision_observation()
        observation["collisions"].append(
            {"kind": "tcp-port", "target": self.contract["instance"]["port"]}
        )
        observation["observation_sha256"] = clusterctl.collision_observation_identity(
            observation
        )
        write_json(self.collision_path, observation)
        with self.assertRaisesRegex(clusterctl.ClusterError, "target collision"):
            self.plan()

    def test_live_fixture_probe_detects_an_occupied_product_path(self) -> None:
        occupied = clusterctl.prefixed(
            self.activation_root, self.contract["instance"]["data_directory"]
        )
        occupied.mkdir(parents=True)
        observation = clusterctl.inspect_collisions(self.contract, self.activation_root)
        with self.assertRaisesRegex(clusterctl.ClusterError, "target collision"):
            clusterctl.validate_collision_observation(observation, self.contract)

    def test_inaccessible_collision_target_is_receipted_and_fails_closed(self) -> None:
        protected = self.contract["instance"]["data_directory"]
        real_lstat = os.lstat

        def permission_mutant(path: Any, *args: Any, **kwargs: Any) -> os.stat_result:
            if str(path).endswith(protected):
                raise PermissionError(13, "Permission denied", str(path))
            return real_lstat(path, *args, **kwargs)

        with mock.patch.object(
            clusterctl.os, "lstat", side_effect=permission_mutant
        ):
            observation = clusterctl.inspect_collisions(
                self.contract, self.activation_root
            )
        self.assertEqual(observation["collisions"], [])
        self.assertEqual(observation["inspection_errors"][0]["operation"], "lstat")
        self.assertEqual(observation["inspection_errors"][0]["target"], protected)
        with self.assertRaisesRegex(clusterctl.ClusterError, "inspection is incomplete"):
            clusterctl.validate_collision_observation(observation, self.contract)

    def test_nonpackage_postmaster_mutant_is_rejected(self) -> None:
        plan = self.plan()
        service = next(
            item["content"] for item in plan["files"] if item["path"].endswith(".service")
        )
        service = service.replace(
            f"{plan['package_root']}/pgsql-18/bin/postgres",
            "/tmp/unmanaged-postgresql/bin/postgres",
        )
        self.replace_rendered(plan, ".service", service)
        with self.assertRaisesRegex(clusterctl.ClusterError, "immutable package postmaster"):
            clusterctl.validate_plan(plan)

    def test_undeclared_peer_mapping_mutant_is_rejected(self) -> None:
        plan = self.plan()
        ident = next(item["content"] for item in plan["files"] if item["path"].endswith("pg_ident.conf"))
        ident += f"mutant {plan['instance']['os_user']} {plan['instance']['admin_role']}\n"
        self.replace_rendered(plan, "pg_ident.conf", ident)
        with self.assertRaisesRegex(clusterctl.ClusterError, "declared runner mappings"):
            clusterctl.validate_plan(plan)

    def test_ambient_loader_environment_mutants_are_rejected(self) -> None:
        for name in ("LD_LIBRARY_PATH", "LD_PRELOAD"):
            with self.subTest(name=name):
                self.write_manifest(lambda value: value["loader_environment"].update({name: "/tmp"}))
                with self.assertRaisesRegex(clusterctl.ClusterError, "ambient loader"):
                    self.plan()
                write_json(self.manifest_path, self.create_package())

    def test_service_ambient_loader_mutant_is_rejected(self) -> None:
        plan = self.plan()
        service = next(
            item["content"] for item in plan["files"] if item["path"].endswith(".service")
        )
        self.replace_rendered(plan, ".service", service + "Environment=LD_LIBRARY_PATH=/tmp\n")
        with self.assertRaisesRegex(clusterctl.ClusterError, "ambient loader"):
            clusterctl.validate_plan(plan)

    def test_oversized_resource_mutants_are_rejected(self) -> None:
        resource = self.valid_resource_observation()
        resource["grant"]["memory_bytes"] = 65 * 1024**3
        resource["native_authority"]["partition_grant"]["memory_bytes"] = (
            65 * 1024**3
        )
        resource["observation_sha256"] = clusterctl.resource_observation_identity(
            resource
        )
        write_json(self.resource_path, resource)
        with self.assertRaisesRegex(clusterctl.ClusterError, "outside the declared policy"):
            self.plan()

        resource = self.valid_resource_observation()
        resource["storage"]["wal"]["available_bytes"] = 8 * 1024**3
        resource["observation_sha256"] = clusterctl.resource_observation_identity(
            resource
        )
        write_json(self.resource_path, resource)
        with self.assertRaisesRegex(clusterctl.ClusterError, "WAL budget"):
            self.plan()

    def test_nonpackage_pg_config_mutants_are_rejected(self) -> None:
        for value in ("/srv/unrelated-postgresql/bin/pg_config", "pgsql/bin/pg_config"):
            with self.subTest(value=value):
                self.write_manifest(
                    lambda document: document["postgresql"].update(pg_config=value)
                )
                with self.assertRaises(clusterctl.ClusterError):
                    self.plan()
                write_json(self.manifest_path, self.create_package())

    def test_loaded_object_and_invalid_system_identity_mutants_are_rejected(self) -> None:
        plan = self.plan()
        observation = self.loaded_observation(plan)
        observation["loaded_objects"][0]["sha256"] = "f" * 64
        observation["observation_sha256"] = clusterctl.state_observation_identity(
            observation
        )
        with self.assertRaisesRegex(clusterctl.ClusterError, "loaded object"):
            clusterctl.verify_loaded(plan, self.contract, observation)
        observation = self.loaded_observation(plan)
        observation["system_identifier"] = "0"
        observation["observation_sha256"] = clusterctl.state_observation_identity(
            observation
        )
        with self.assertRaisesRegex(clusterctl.ClusterError, "positive system"):
            clusterctl.verify_loaded(plan, self.contract, observation)

    def test_live_loaded_observation_is_derived_from_exact_process_and_files(self) -> None:
        plan = self.plan()
        package_source = clusterctl.prefixed(
            self.package_physical_root, plan["package_root"]
        )
        package_target = clusterctl.prefixed(self.activation_root, plan["package_root"])
        package_target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(package_source, package_target, symlinks=True)
        clusterctl.apply_plan(plan, self.contract, self.activation_root, False)
        service_receipt = {
            "label": "observe-postmaster-state",
            "argv": ["pg_ctl", "status"],
            "exit_code": 0,
            "stdout_sha256": "a" * 64,
            "stderr_sha256": "b" * 64,
        }
        paths = {item["path"] for item in plan["required_loaded_objects"]}
        observation = clusterctl.compose_loaded_observation(
            plan,
            self.contract,
            self.activation_root,
            1201,
            1208,
            "8672946663471807927",
            paths,
            "c" * 64,
            service_receipt,
        )
        clusterctl.verify_loaded(plan, self.contract, observation)
        with self.assertRaisesRegex(clusterctl.ClusterError, "omits required"):
            clusterctl.compose_loaded_observation(
                plan,
                self.contract,
                self.activation_root,
                1201,
                1208,
                "8672946663471807927",
                paths - {plan["required_loaded_objects"][0]["path"]},
                "c" * 64,
                service_receipt,
            )

    def test_complete_activation_restarts_before_committing_active_package(self) -> None:
        plan = self.plan()
        staged = clusterctl.apply_plan(
            plan, self.contract, self.activation_root, False
        )
        executed: list[str] = []
        recorded: list[str] = []
        observations = 0

        def executor(label: str, command: Any, timeout: int) -> dict[str, Any]:
            executed.append(label)
            return {
                "label": label,
                "argv": list(command),
                "exit_code": 0,
                "stdout_sha256": "d" * 64,
                "stderr_sha256": "e" * 64,
            }

        def readiness(label: str, command: Any, timeout: int) -> dict[str, Any]:
            return executor(label, command, timeout)

        def observer(*_arguments: Any) -> dict[str, Any]:
            nonlocal observations
            observation = self.loaded_observation(plan)
            observations += 1
            observation["postmaster_pid"] = 2000 + observations
            observation["backend_pid"] = 3000 + observations
            observation["observation_sha256"] = clusterctl.state_observation_identity(
                observation
            )
            return observation

        result = clusterctl.execute_cluster_activation(
            plan,
            self.contract,
            staged,
            self.activation_root,
            False,
            [],
            observer=observer,
            recorder=lambda stem, _document: recorded.append(stem),
            executor=executor,
            readiness=readiness,
        )
        self.assertEqual(result["phase"], "activated")
        self.assertTrue(result["restart_proven"])
        self.assertFalse(result["boot_enabled"])
        self.assertFalse(result["service_integration_required"])
        self.assertEqual(result["lifecycle_provider"], "pg_ctl")
        self.assertEqual(recorded, ["loaded-initial", "loaded-restart"])
        self.assertEqual(
            executed,
            [
                "initialize-cluster",
                "start-candidate-postmaster",
                "candidate-readiness",
                "bootstrap-product-database",
                "stop-candidate-for-restart-proof",
                "start-candidate-after-restart",
                "restart-readiness",
            ],
        )
        active = clusterctl.prefixed(self.activation_root, plan["active_link"])
        self.assertEqual(os.readlink(active), f"releases/{plan['package_id']}")

    def test_restart_without_a_new_postmaster_is_rejected_before_commit(self) -> None:
        plan = self.plan()
        staged = clusterctl.apply_plan(
            plan, self.contract, self.activation_root, False
        )
        executed: list[str] = []

        def executor(label: str, command: Any, timeout: int) -> dict[str, Any]:
            executed.append(label)
            return {
                "label": label,
                "argv": list(command),
                "exit_code": 0,
                "stdout_sha256": "d" * 64,
                "stderr_sha256": "e" * 64,
            }

        def readiness(label: str, command: Any, timeout: int) -> dict[str, Any]:
            return executor(label, command, timeout)

        def observer(*_arguments: Any) -> dict[str, Any]:
            observation = self.loaded_observation(plan)
            observation["postmaster_pid"] = 2001
            observation["backend_pid"] = 3001
            observation["observation_sha256"] = clusterctl.state_observation_identity(
                observation
            )
            return observation

        with self.assertRaisesRegex(clusterctl.ClusterError, "original postmaster"):
            clusterctl.execute_cluster_activation(
                plan,
                self.contract,
                staged,
                self.activation_root,
                False,
                [],
                observer=observer,
                executor=executor,
                readiness=readiness,
            )
        active = clusterctl.prefixed(self.activation_root, plan["active_link"])
        self.assertFalse(active.exists())
        self.assertIn("stop-candidate-for-restart-proof", executed)

    def test_fixture_stage_commit_and_remove_preserve_database_state(self) -> None:
        plan = self.plan()
        receipt = clusterctl.apply_plan(
            plan, self.contract, self.activation_root, False
        )
        for directory in plan["state_directories"]:
            target = clusterctl.prefixed(self.activation_root, directory)
            self.assertTrue(target.is_dir())
            self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o700)
        committed = clusterctl.commit_plan(
            plan,
            self.contract,
            receipt,
            self.loaded_observation(plan),
            self.activation_root,
            False,
        )
        active = clusterctl.prefixed(self.activation_root, plan["active_link"])
        self.assertTrue(active.is_symlink())
        self.assertEqual(os.readlink(active), f"releases/{plan['package_id']}")
        removed = clusterctl.remove_activation(
            plan,
            self.contract,
            committed,
            self.stopped_observation(plan),
            self.activation_root,
            False,
        )
        self.assertEqual(removed["phase"], "removed")
        self.assertFalse(active.exists())
        self.assertFalse(active.is_symlink())
        for directory in plan["state_directories"]:
            self.assertTrue(clusterctl.prefixed(self.activation_root, directory).is_dir())

    def test_remove_refuses_changed_generated_file(self) -> None:
        plan = self.plan()
        receipt = clusterctl.apply_plan(
            plan, self.contract, self.activation_root, False
        )
        generated = clusterctl.prefixed(self.activation_root, receipt["installed_files"][0]["path"])
        generated.write_text("operator mutation\n", encoding="utf-8")
        with self.assertRaisesRegex(clusterctl.ClusterError, "refusing removal"):
            clusterctl.remove_activation(
                plan,
                self.contract,
                receipt,
                self.stopped_observation(plan),
                self.activation_root,
                False,
            )

    def test_commit_and_remove_restore_the_previous_active_package(self) -> None:
        plan = self.plan()
        active = clusterctl.prefixed(self.activation_root, plan["active_link"])
        active.parent.mkdir(parents=True)
        os.symlink("releases/previous", active)
        receipt = clusterctl.apply_plan(
            plan, self.contract, self.activation_root, False
        )
        committed = clusterctl.commit_plan(
            plan,
            self.contract,
            receipt,
            self.loaded_observation(plan),
            self.activation_root,
            False,
        )
        self.assertEqual(committed["previous_active_target"], "releases/previous")
        clusterctl.remove_activation(
            plan,
            self.contract,
            committed,
            self.stopped_observation(plan),
            self.activation_root,
            False,
        )
        self.assertEqual(os.readlink(active), "releases/previous")

    def test_remove_refuses_a_running_candidate(self) -> None:
        plan = self.plan()
        receipt = clusterctl.apply_plan(
            plan, self.contract, self.activation_root, False
        )
        observation = self.stopped_observation(plan)
        observation["service_state"] = "active"
        observation["postmaster_pid"] = 1234
        observation["observation_sha256"] = clusterctl.state_observation_identity(
            observation
        )
        with self.assertRaisesRegex(clusterctl.ClusterError, "inactive"):
            clusterctl.remove_activation(
                plan,
                self.contract,
                receipt,
                observation,
                self.activation_root,
                False,
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
