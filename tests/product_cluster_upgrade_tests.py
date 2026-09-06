#!/usr/bin/env python3
"""Positive and deliberate-defect tests for persistent cluster generation upgrade."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "tools/delivery/product_cluster_upgrade.py"
WRAPPER = ROOT / "tools/delivery/product_activation_reconcile.py"


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


UPGRADE = load("laplace_product_cluster_upgrade_tests", MODULE)
RECONCILE = load("laplace_product_activation_reconcile_tests", WRAPPER)


class ProductClusterUpgradeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-upgrade-test-")
        self.root = Path(self.temporary.name).resolve()
        self.old_id = "1" * 64
        self.new_id = "2" * 64
        self.active = self.root / "current"
        self.runtime = self.root / "runtime"
        self.contract = {
            "package": {"active_link": str(self.active)},
            "instance": {
                "receipt_directory": str(self.root / "receipts"),
                "port": 55433,
                "socket_directory": str(self.root / "socket"),
            },
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_active_package_identity_is_exact_content_addressed_target(self) -> None:
        self.active.symlink_to(f"releases/{self.old_id}")
        self.assertEqual(UPGRADE.active_package_id(self.contract), self.old_id)

        self.active.unlink()
        self.active.symlink_to("releases/not-a-package")
        with self.assertRaisesRegex(UPGRADE.UpgradeError, "canonical package id"):
            UPGRADE.active_package_id(self.contract)

    def test_active_generation_is_required_for_upgrade(self) -> None:
        with self.assertRaisesRegex(UPGRADE.UpgradeError, "active package symlink"):
            UPGRADE.active_package_id(self.contract)

    def test_active_collision_projection_preserves_lineage_but_not_fresh_collision(self) -> None:
        observation = {
            "schema": UPGRADE.clusterctl.COLLISION_SCHEMA,
            "source": "laplace_clusterctl_live_probe",
            "root": "/",
            "target": {"paths": [], "port": 55433, "socket_directory": "/tmp/x"},
            "collisions": [{"kind": "tcp-port", "target": 55433}],
            "inspection_errors": [],
        }
        observation["observation_sha256"] = UPGRADE.clusterctl.collision_observation_identity(
            observation
        )
        projected = UPGRADE._project_upgrade_collision(observation, self.old_id)
        self.assertEqual(projected["collisions"], [])
        self.assertEqual(projected["upgrade_predecessor_package_id"], self.old_id)
        self.assertEqual(
            projected["upgrade_source_observation_sha256"],
            observation["observation_sha256"],
        )
        self.assertEqual(
            projected["observation_sha256"],
            UPGRADE.clusterctl.collision_observation_identity(projected),
        )

    def test_unrelated_or_foreign_collision_cannot_be_reclassified_as_active_state(self) -> None:
        target = {
            "paths": ["/expected"],
            "port": 55433,
            "socket_directory": "/socket",
        }
        observation = {
            "schema": UPGRADE.clusterctl.COLLISION_SCHEMA,
            "source": "laplace_clusterctl_live_probe",
            "root": "/",
            "target": target,
            "collisions": [{"kind": "path", "target": "/unexpected"}],
            "inspection_errors": [],
        }
        observation["observation_sha256"] = UPGRADE.clusterctl.collision_observation_identity(
            observation
        )
        with mock.patch.object(UPGRADE.clusterctl, "collision_target", return_value=target):
            with self.assertRaisesRegex(UPGRADE.UpgradeError, "unrelated collision"):
                UPGRADE._validate_active_collisions(observation, self.contract)

        observation["collisions"] = [
            {"kind": "process", "target": 1234, "owner_uid": os.geteuid() + 1}
        ]
        observation["observation_sha256"] = UPGRADE.clusterctl.collision_observation_identity(
            observation
        )
        with mock.patch.object(UPGRADE.clusterctl, "collision_target", return_value=target):
            with self.assertRaisesRegex(UPGRADE.UpgradeError, "unrelated collision"):
                UPGRADE._validate_active_collisions(observation, self.contract)

    def test_generated_predecessor_file_must_match_receipt_before_mutation(self) -> None:
        config = self.root / "postgresql.conf"
        config.write_text("expected\n", encoding="utf-8")
        digest = UPGRADE.clusterctl.sha256_file(config)
        plan = {
            "files": [
                {
                    "path": str(config),
                    "sha256": digest,
                    "content": "expected\n",
                    "mode": 0o640,
                }
            ]
        }
        backup = UPGRADE._verify_existing_generated_files(plan)
        self.assertEqual(backup[str(config)]["sha256"], digest)
        config.write_text("operator drift\n", encoding="utf-8")
        with self.assertRaisesRegex(UPGRADE.UpgradeError, "changed outside"):
            UPGRADE._verify_existing_generated_files(plan)

    def test_persistent_state_directory_inode_is_preserved(self) -> None:
        data = self.root / "data"
        data.mkdir()
        identities = UPGRADE._verify_state_directories(
            {"state_directories": [str(data)]}
        )
        UPGRADE._verify_state_identities(identities)
        replacement = self.root / "replacement"
        data.rename(replacement)
        data.mkdir()
        with self.assertRaisesRegex(UPGRADE.UpgradeError, "identity changed"):
            UPGRADE._verify_state_identities(identities)

    def test_upgrade_source_contains_no_initdb_execution(self) -> None:
        source = MODULE.read_text(encoding="utf-8")
        self.assertNotIn('commands["initdb"]', source)
        self.assertNotIn("_initdb_command(", source)
        self.assertIn('"initdb_executed": False', source)

    def test_reconciler_routes_active_and_fresh_state_to_distinct_lifecycles(self) -> None:
        contract_path = self.root / "contract.json"
        contract_path.write_text(json.dumps(self.contract), encoding="utf-8")
        package = self.root / "package.json"
        resource = self.root / "resource.json"
        evidence = self.root / "evidence"
        package.write_text("{}", encoding="utf-8")
        resource.write_text("{}", encoding="utf-8")

        self.active.symlink_to(f"releases/{self.old_id}")
        with mock.patch.object(
            RECONCILE.upgrade, "upgrade_product", return_value={"path": "upgrade"}
        ) as upgraded, mock.patch.object(
            RECONCILE, "fresh_activate_product", return_value={"path": "fresh"}
        ) as fresh:
            result = RECONCILE.reconcile_cluster_activation(
                contract_path, package, resource, evidence, False
            )
        self.assertEqual(result, {"path": "upgrade"})
        upgraded.assert_called_once()
        fresh.assert_not_called()

        self.active.unlink()
        with mock.patch.object(
            RECONCILE.upgrade, "upgrade_product", return_value={"path": "upgrade"}
        ) as upgraded, mock.patch.object(
            RECONCILE, "fresh_activate_product", return_value={"path": "fresh"}
        ) as fresh:
            result = RECONCILE.reconcile_cluster_activation(
                contract_path, package, resource, evidence, False
            )
        self.assertEqual(result, {"path": "fresh"})
        upgraded.assert_not_called()
        fresh.assert_called_once()


if __name__ == "__main__":
    unittest.main(verbosity=2)
