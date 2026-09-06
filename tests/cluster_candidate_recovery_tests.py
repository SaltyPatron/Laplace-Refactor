#!/usr/bin/env python3
"""Positive and deliberate-defect tests for empty PostgreSQL candidate recovery."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "tools/delivery/cluster_candidate_recovery.py"
SPEC = importlib.util.spec_from_file_location("laplace_cluster_candidate_recovery", MODULE)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE}")
RECOVERY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RECOVERY)


class ClusterCandidateRecoveryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.contract = {
            "schema": "laplace.postgresql-cluster-contract/v1",
            "package": {"active_link": str(self.root / "current")},
            "instance": {
                "id": "refactor",
                "config_directory": str(self.root / "config"),
                "data_directory": str(self.root / "data"),
                "wal_directory": str(self.root / "wal"),
                "temp_directory": str(self.root / "temp"),
                "perfcache_directory": str(self.root / "perfcache"),
                "socket_directory": str(self.root / "socket"),
                "log_directory": str(self.root / "log"),
            },
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_empty_reserved_candidate_leaves_are_removed_and_receipted(self) -> None:
        config = Path(self.contract["instance"]["config_directory"])
        wal = Path(self.contract["instance"]["wal_directory"])
        config.mkdir()
        wal.mkdir()

        receipt = RECOVERY.recover(self.contract)

        self.assertFalse(config.exists())
        self.assertFalse(wal.exists())
        self.assertEqual(receipt["schema"], RECOVERY.SCHEMA)
        self.assertEqual(receipt["instance_id"], "refactor")
        self.assertEqual(receipt["removed_count"], 2)
        self.assertEqual(receipt["blocked_count"], 0)
        self.assertEqual(
            receipt["receipt_sha256"],
            RECOVERY.document_identity(receipt, "receipt_sha256"),
        )

    def test_nonempty_unreceipted_candidate_remains_a_hard_boundary(self) -> None:
        config = Path(self.contract["instance"]["config_directory"])
        config.mkdir()
        marker = config / "postgresql.conf"
        marker.write_text("do not delete\n", encoding="utf-8")

        with self.assertRaisesRegex(RECOVERY.RecoveryError, "blocked-nonempty"):
            RECOVERY.recover(self.contract)

        self.assertEqual(marker.read_text(encoding="utf-8"), "do not delete\n")

    def test_symlink_candidate_is_never_followed_or_removed(self) -> None:
        outside = self.root / "outside"
        outside.mkdir()
        marker = outside / "marker"
        marker.write_text("outside\n", encoding="utf-8")
        config = Path(self.contract["instance"]["config_directory"])
        config.symlink_to(outside, target_is_directory=True)

        with self.assertRaisesRegex(RECOVERY.RecoveryError, "blocked-symlink"):
            RECOVERY.recover(self.contract)

        self.assertTrue(config.is_symlink())
        self.assertEqual(marker.read_text(encoding="utf-8"), "outside\n")

    def test_active_product_disables_candidate_recovery(self) -> None:
        config = Path(self.contract["instance"]["config_directory"])
        config.mkdir()
        release = self.root / "release"
        release.mkdir()
        active = Path(self.contract["package"]["active_link"])
        active.symlink_to(release, target_is_directory=True)

        receipt = RECOVERY.recover(self.contract)

        self.assertTrue(config.is_dir())
        self.assertTrue(active.is_symlink())
        self.assertTrue(receipt["active_product_present"])
        self.assertEqual(receipt["removed_count"], 0)
        self.assertEqual(receipt["blocked_count"], 0)
        self.assertTrue(
            all(result["state"] == "active-product-present" for result in receipt["results"])
        )

    def test_unknown_contract_or_relative_target_is_rejected(self) -> None:
        mutant = json.loads(json.dumps(self.contract))
        mutant["schema"] = "laplace.postgresql-cluster-contract/v0"
        with self.assertRaisesRegex(RECOVERY.RecoveryError, "unsupported"):
            RECOVERY.recover(mutant)

        mutant = json.loads(json.dumps(self.contract))
        mutant["instance"]["config_directory"] = "relative/config"
        with self.assertRaisesRegex(RECOVERY.RecoveryError, "must be absolute"):
            RECOVERY.recover(mutant)


if __name__ == "__main__":
    unittest.main(verbosity=2)
