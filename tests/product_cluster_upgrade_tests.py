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
ADOPTION_MODULE = ROOT / "tools/delivery/product_predecessor_adoption.py"


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


UPGRADE = load("laplace_product_cluster_upgrade_tests", MODULE)
RECONCILE = load("laplace_product_activation_reconcile_tests", WRAPPER)
ADOPTION = load("laplace_product_predecessor_adoption_tests", ADOPTION_MODULE)


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

    def _legacy_predecessor_fixture(self) -> tuple[Path, Path, dict, dict, dict]:
        (self.root / "contract.json").write_text(
            json.dumps(self.contract), encoding="utf-8"
        )
        self.active.symlink_to(f"releases/{self.old_id}")
        self.runtime.symlink_to(f"../releases/{self.old_id}")
        directory = self.root / "receipts" / "cluster-activation" / self.old_id
        directory.mkdir(parents=True)
        plan_path = directory / "cluster-plan.json"
        plan = {
            "package_id": self.old_id,
            "commands": {
                "stop_candidate": ["pg_ctl", "stop"],
                "start_candidate": ["pg_ctl", "start"],
                "probe_readiness": ["pg_isready"],
            },
        }
        plan_path.write_text(json.dumps(plan), encoding="utf-8")
        legacy = {
            "schema": ADOPTION.clusterctl.ACTIVATION_SCHEMA,
            "phase": "activated",
            "package_id": self.old_id,
            "cluster_plan_path": str(plan_path),
            "historical_field": "preserve-me",
        }
        receipt_path = directory / "activation-complete.json"
        receipt_path.write_text(json.dumps(legacy), encoding="utf-8")
        initial = {
            "postmaster_pid": 101,
            "system_identifier": "7000000000000000001",
            "loaded_objects": [{"path": "/old/object", "sha256": "a" * 64}],
            "config_files": [{"path": "/old/config", "sha256": "b" * 64}],
            "observation_sha256": "c" * 64,
        }
        restarted = dict(initial)
        restarted["postmaster_pid"] = 202
        restarted["observation_sha256"] = "d" * 64
        return receipt_path, plan_path, legacy, initial, restarted

    def test_incomplete_active_predecessor_is_reproved_not_assumed(self) -> None:
        receipt_path, _plan_path, legacy, initial, restarted = self._legacy_predecessor_fixture()
        commands: list[str] = []

        def execute(label, _command, _timeout):
            commands.append(label)
            return {"label": label, "exit_code": 0}

        with mock.patch.object(ADOPTION.clusterctl, "RUNTIME_LINK", str(self.runtime)), mock.patch.object(
            ADOPTION.clusterctl, "require_fixture_or_root"
        ), mock.patch.object(ADOPTION.clusterctl, "validate_contract"), mock.patch.object(
            ADOPTION.clusterctl, "validate_plan"
        ), mock.patch.object(
            ADOPTION.clusterctl,
            "observe_loaded_live",
            side_effect=[initial, restarted],
        ) as observed, mock.patch.object(
            ADOPTION.clusterctl, "verify_loaded"
        ) as verified, mock.patch.object(
            ADOPTION.clusterctl, "execute_activation_command", side_effect=execute
        ), mock.patch.object(
            ADOPTION.clusterctl,
            "await_postgresql_ready",
            return_value={"label": "ready", "exit_code": 0},
        ), mock.patch.object(ADOPTION.clusterctl, "_stopped_live"):
            normalized = ADOPTION.ensure_current_predecessor_receipt(
                self.root / "contract.json"
            )

        self.assertTrue(normalized["restart_proven"])
        self.assertEqual(normalized["lifecycle_provider"], ADOPTION.clusterctl.LIFECYCLE_PROVIDER)
        self.assertTrue(normalized["predecessor_live_reproof"])
        self.assertEqual(normalized["historical_field"], "preserve-me")
        self.assertEqual(normalized["system_identifier"], initial["system_identifier"])
        self.assertEqual(
            normalized["activation_receipt_sha256"],
            ADOPTION._activation_identity(normalized),
        )
        self.assertEqual(observed.call_count, 2)
        self.assertEqual(verified.call_count, 2)
        self.assertEqual(
            commands,
            [
                "stop-predecessor-for-receipt-adoption",
                "start-predecessor-for-receipt-adoption",
            ],
        )
        persisted = json.loads(receipt_path.read_text(encoding="utf-8"))
        self.assertEqual(persisted, normalized)
        archive = Path(normalized["predecessor_historical_receipt_archive"])
        self.assertTrue(archive.is_file())
        self.assertEqual(json.loads(archive.read_text(encoding="utf-8")), legacy)

    def test_current_predecessor_receipt_does_not_restart_again(self) -> None:
        receipt_path, _plan_path, legacy, _initial, _restarted = self._legacy_predecessor_fixture()
        current = dict(legacy)
        current.update(
            {
                "restart_proven": True,
                "lifecycle_provider": ADOPTION.clusterctl.LIFECYCLE_PROVIDER,
            }
        )
        current["activation_receipt_sha256"] = ADOPTION._activation_identity(current)
        receipt_path.write_text(json.dumps(current), encoding="utf-8")

        with mock.patch.object(ADOPTION.clusterctl, "RUNTIME_LINK", str(self.runtime)), mock.patch.object(
            ADOPTION.clusterctl, "require_fixture_or_root"
        ), mock.patch.object(ADOPTION.clusterctl, "validate_contract"), mock.patch.object(
            ADOPTION.clusterctl, "validate_plan"
        ), mock.patch.object(
            ADOPTION.clusterctl, "observe_loaded_live"
        ) as observed:
            result = ADOPTION.ensure_current_predecessor_receipt(
                self.root / "contract.json"
            )

        self.assertEqual(result, current)
        observed.assert_not_called()

    def test_invalid_live_predecessor_is_never_promoted(self) -> None:
        receipt_path, _plan_path, legacy, initial, _restarted = self._legacy_predecessor_fixture()
        with mock.patch.object(ADOPTION.clusterctl, "RUNTIME_LINK", str(self.runtime)), mock.patch.object(
            ADOPTION.clusterctl, "require_fixture_or_root"
        ), mock.patch.object(ADOPTION.clusterctl, "validate_contract"), mock.patch.object(
            ADOPTION.clusterctl, "validate_plan"
        ), mock.patch.object(
            ADOPTION.clusterctl, "observe_loaded_live", return_value=initial
        ), mock.patch.object(
            ADOPTION.clusterctl,
            "verify_loaded",
            side_effect=ADOPTION.clusterctl.ClusterError("deliberate live identity defect"),
        ):
            with self.assertRaisesRegex(
                ADOPTION.clusterctl.ClusterError, "deliberate live identity defect"
            ):
                ADOPTION.ensure_current_predecessor_receipt(
                    self.root / "contract.json"
                )

        self.assertEqual(json.loads(receipt_path.read_text(encoding="utf-8")), legacy)

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
            RECONCILE.adoption,
            "ensure_current_predecessor_receipt",
            return_value={"phase": "activated"},
        ) as adopted, mock.patch.object(
            RECONCILE.upgrade, "upgrade_product", return_value={"path": "upgrade"}
        ) as upgraded, mock.patch.object(
            RECONCILE, "fresh_activate_product", return_value={"path": "fresh"}
        ) as fresh:
            result = RECONCILE.reconcile_cluster_activation(
                contract_path, package, resource, evidence, False
            )
        self.assertEqual(result, {"path": "upgrade"})
        adopted.assert_called_once_with(contract_path)
        upgraded.assert_called_once()
        fresh.assert_not_called()

        self.active.unlink()
        with mock.patch.object(
            RECONCILE.adoption,
            "ensure_current_predecessor_receipt",
            return_value={"phase": "activated"},
        ) as adopted, mock.patch.object(
            RECONCILE.upgrade, "upgrade_product", return_value={"path": "upgrade"}
        ) as upgraded, mock.patch.object(
            RECONCILE, "fresh_activate_product", return_value={"path": "fresh"}
        ) as fresh:
            result = RECONCILE.reconcile_cluster_activation(
                contract_path, package, resource, evidence, False
            )
        self.assertEqual(result, {"path": "fresh"})
        adopted.assert_not_called()
        upgraded.assert_not_called()
        fresh.assert_called_once()


if __name__ == "__main__":
    unittest.main(verbosity=2)
