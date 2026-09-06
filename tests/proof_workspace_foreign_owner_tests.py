#!/usr/bin/env python3
"""Regression tests for foreign-owned disposable proof workspace boundaries."""

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


if __name__ == "__main__":
    unittest.main()
