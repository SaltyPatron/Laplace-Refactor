#!/usr/bin/env python3
"""Acceptance and deliberate-defect tests for isolated PostgreSQL activation."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import os
import stat
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
        write_json(self.resource_path, self.valid_resource_observation())
        write_json(self.collision_path, self.valid_collision_observation())
        write_json(self.manifest_path, self.create_package())

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def valid_resource_observation(self) -> dict[str, Any]:
        observation = {
            "schema": clusterctl.RESOURCE_SCHEMA,
            "source": "laplace_native_execution_authority",
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
                },
                "wal": {
                    "path": self.contract["instance"]["wal_directory"],
                    "available_bytes": 64 * 1024**3,
                },
                "temporary": {
                    "path": self.contract["instance"]["temp_directory"],
                    "available_bytes": 128 * 1024**3,
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
                    "sha256": hashlib.sha256(content).hexdigest(),
                    "mode": mode,
                    "runpath": runpath,
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
        bootstrap = next(
            item["content"] for item in plan["files"] if item["path"].endswith("bootstrap.sql")
        )
        for row_by_row in (" LOOP ", "CURSOR", "WITH RECURSIVE"):
            self.assertNotIn(row_by_row, bootstrap.upper())

    def test_activation_is_blocked_without_package_bytes(self) -> None:
        plan = self.plan(verify_bytes=False)
        self.assertTrue(plan["activation_blocked"])
        with self.assertRaisesRegex(clusterctl.ClusterError, "blocked"):
            clusterctl.apply_plan(plan, self.contract, self.activation_root, False)

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

    def test_runner_to_admin_peer_mapping_mutant_is_rejected(self) -> None:
        plan = self.plan()
        ident = next(item["content"] for item in plan["files"] if item["path"].endswith("pg_ident.conf"))
        ident += f"mutant {plan['instance']['os_user']} {plan['instance']['admin_role']}\n"
        self.replace_rendered(plan, "pg_ident.conf", ident)
        with self.assertRaisesRegex(clusterctl.ClusterError, "elevates"):
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
