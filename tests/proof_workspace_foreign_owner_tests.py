#!/usr/bin/env python3
"""Regression tests for disposable proof workspace authority and namespace coverage."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/proof_workspace_cleanup.py"
SPEC = importlib.util.spec_from_file_location("laplace_proof_workspace_cleanup", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
CLEANUP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CLEANUP)


class ProofWorkspaceForeignOwnerTests(unittest.TestCase):
    def test_foreign_owned_workspace_is_reported_without_deletion(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            target = root / "lp-pg.ForeignOwner"
            target.mkdir()
            marker = target / "keep"
            marker.write_text("foreign authority\n", encoding="utf-8")
            old = time.time() - 600
            os.utime(target, (old, old))

            actual_owner_uid = target.lstat().st_uid
            foreign_effective_uid = actual_owner_uid + 1
            with mock.patch.object(
                CLEANUP.os, "geteuid", return_value=foreign_effective_uid
            ):
                receipt = CLEANUP.cleanup(
                    root,
                    [target.name],
                    minimum_age_seconds=300,
                    proc_root=root / "no-proc",
                )

            result = receipt["results"][0]
            self.assertEqual(result["state"], "foreign-owner")
            self.assertEqual(result["owner_uid"], actual_owner_uid)
            self.assertEqual(result["effective_uid"], foreign_effective_uid)
            self.assertEqual(result["removed_entries"], 0)
            self.assertEqual(marker.read_text(encoding="utf-8"), "foreign authority\n")

    def test_historical_cleanup_aliases_cover_every_declared_producer_namespace(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            names = (
                "laplace-postgres-test.Abc123",
                "laplace-postgres-semantic-cognition.Sem123",
                "laplace-postgres-target-attention.Att123",
                "laplace-postgres-observation-cognition.Obs123",
                "laplace-postgres-discourse.Dis123",
                "laplace-model-export-cli.Mod123",
                "lp-pg.Pg123",
                "lp-sc-pg.Sc123",
                "lp-ta-pg.Ta123",
                "lp-oc-pg.Oc123",
                "lp-discourse-pg.Di123",
                "lp-mx-pg.Mx123",
            )
            for name in names:
                (root / name).mkdir()
            (root / "laplace-unrelated").mkdir()
            (root / "lp-unrelated.Bad123").mkdir()

            discovered = CLEANUP.discover_names(
                root, ["laplace-postgres-test.", "lp-pg."]
            )

            self.assertEqual(discovered, sorted(names))
            self.assertNotIn("laplace-unrelated", discovered)
            self.assertNotIn("lp-unrelated.Bad123", discovered)

    def test_individual_declared_prefix_does_not_expand_to_siblings(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            semantic = root / "laplace-postgres-semantic-cognition.Sem123"
            attention = root / "laplace-postgres-target-attention.Att123"
            semantic.mkdir()
            attention.mkdir()

            self.assertEqual(
                CLEANUP.discover_names(
                    root, ["laplace-postgres-semantic-cognition."]
                ),
                [semantic.name],
            )


if __name__ == "__main__":
    unittest.main()
