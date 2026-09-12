#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


class SourceProductTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        path = Path(__file__).resolve().parents[1] / "tools/source_product.py"
        spec = importlib.util.spec_from_file_location("laplace_source_product", path)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"cannot load {path}")
        cls.module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = cls.module
        spec.loader.exec_module(cls.module)

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.metadata = self.root / "metadata"
        self.profiles = self.metadata / "profiles"
        self.profiles.mkdir(parents=True)
        self.estate = self.root / "estate"
        self.estate.mkdir()
        (self.estate / "UCD/Public/UCD/latest").mkdir(parents=True)
        self.source = self.estate / "ExampleSource"
        self.source.mkdir()
        self.payload = b"alpha\nbeta\n"
        (self.source / "records.txt").write_bytes(self.payload)
        digest = hashlib.sha256(self.payload).hexdigest()
        boundary = {
            "schema": "laplace.configured-source-boundary/v1",
            "classification": "authority-selected-closure-ledger-not-source-truth",
            "profile_obligations": [
                {"id": "example-1", "state": "selected", "providers": ["text"]},
                {"id": "missing-1", "state": "selected", "providers": ["xml"]},
                {"id": "excluded-1", "state": "excluded-with-reason", "providers": []},
            ],
        }
        (self.metadata / "selected-source-boundary.json").write_text(
            json.dumps(boundary), encoding="utf-8"
        )
        profile = {
            "schema": "laplace.test-source-profile/v1",
            "coordinate": {"kind": 999, "authority": "test", "release": "1"},
            "artifacts": [
                {
                    "name": "records.txt",
                    "local_discovery_path": "records.txt",
                    "byte_count": len(self.payload),
                    "sha256": digest,
                }
            ],
            "recipe_program": {"coordinate": "laplace.recipe/test/1"},
            "denominators": {"files": 1, "records": 2},
        }
        (self.profiles / "example-1.json").write_text(json.dumps(profile), encoding="utf-8")
        self.jobs = self.root / "jobs"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_catalog_preserves_selected_boundary_and_missing_profile(self) -> None:
        value = self.module.catalog(self.metadata, self.estate)
        self.assertEqual(value["schema"], "laplace.product.source-catalog/v1")
        rows = {row["source_id"]: row for row in value["rows"]}
        self.assertTrue(rows["example-1"]["profile_available"])
        self.assertTrue(rows["example-1"]["preflight_available"])
        self.assertFalse(rows["missing-1"]["profile_available"])
        self.assertFalse(rows["missing-1"]["preflight_available"])
        self.assertFalse(rows["excluded-1"]["preflight_available"])

    def test_preflight_binds_exact_artifact_graph(self) -> None:
        plan = self.module.preflight("example-1", self.metadata, self.estate)
        self.assertEqual(plan["schema"], "laplace.product.source-ingestion-plan/v1")
        self.assertEqual(plan["source_id"], "example-1")
        self.assertEqual(plan["source_root"], str(self.source.resolve()))
        self.assertEqual(plan["physical_resolution"]["exact_artifacts"], 1)
        self.assertTrue(plan["physical_resolution"]["exact"])
        self.assertRegex(plan["plan_id"], r"^[0-9a-f]{64}$")

    def test_preflight_rejects_modified_selected_artifact(self) -> None:
        (self.source / "records.txt").write_bytes(b"modified\n")
        with self.assertRaisesRegex(self.module.SourceProductError, "exact physical artifact graph"):
            self.module.preflight("example-1", self.metadata, self.estate)

    def test_preflight_rejects_unprofiled_selected_source(self) -> None:
        with self.assertRaisesRegex(self.module.SourceProductError, "no installed exact profile"):
            self.module.preflight("missing-1", self.metadata, self.estate)

    def test_preflight_rejects_nonselected_disposition(self) -> None:
        with self.assertRaisesRegex(self.module.SourceProductError, "does not permit admission"):
            self.module.preflight("excluded-1", self.metadata, self.estate)

    def test_submission_is_idempotent_and_requires_reviewed_plan(self) -> None:
        plan = self.module.preflight("example-1", self.metadata, self.estate)
        payload = {
            "source_id": "example-1",
            "plan_id": plan["plan_id"],
            "idempotency_key": "browser-review-1",
        }
        first = self.module.submit(
            payload,
            metadata=self.metadata,
            estate=self.estate,
            jobs=self.jobs,
            launch=False,
        )
        second = self.module.submit(
            payload,
            metadata=self.metadata,
            estate=self.estate,
            jobs=self.jobs,
            launch=False,
        )
        self.assertEqual(first["job_id"], second["job_id"])
        self.assertEqual(second["state"], "queued")
        stored = self.module.job_status(first["job_id"], self.jobs)
        self.assertEqual(stored["plan"]["plan_id"], plan["plan_id"])

    def test_submission_rejects_stale_plan(self) -> None:
        with self.assertRaisesRegex(self.module.SourceProductError, "stale"):
            self.module.submit(
                {
                    "source_id": "example-1",
                    "plan_id": "0" * 64,
                    "idempotency_key": "browser-review-1",
                },
                metadata=self.metadata,
                estate=self.estate,
                jobs=self.jobs,
                launch=False,
            )


if __name__ == "__main__":
    unittest.main()
