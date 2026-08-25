#!/usr/bin/env python3
"""Mutation checks for the requirement graph verifier."""

from __future__ import annotations

import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools/tests/requirements_trace.py"
SPEC = importlib.util.spec_from_file_location("requirements_trace", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
TRACE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = TRACE
SPEC.loader.exec_module(TRACE)


class RequirementTraceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "requirements/features").mkdir(parents=True)
        (self.root / "tests").mkdir()
        shutil.copy2(REPO_ROOT / "requirements/product.yaml", self.root / "requirements/product.yaml")
        shutil.copy2(REPO_ROOT / "requirements/alignment.yaml", self.root / "requirements/alignment.yaml")
        shutil.copytree(
            REPO_ROOT / "requirements/features",
            self.root / "requirements/features",
            dirs_exist_ok=True,
        )
        shutil.copy2(REPO_ROOT / "tests/registry.json", self.root / "tests/registry.json")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def replace(self, relative: str, before: str, after: str) -> None:
        path = self.root / relative
        content = path.read_text(encoding="utf-8")
        self.assertIn(before, content)
        path.write_text(content.replace(before, after, 1), encoding="utf-8")

    def test_current_graph_is_valid(self) -> None:
        report = TRACE.validate(self.root)
        self.assertGreater(report.direct_requirement_count, 0)
        self.assertGreater(report.registered_test_count, 0)

    def test_unknown_product_join_is_rejected(self) -> None:
        self.replace(
            "requirements/alignment.yaml",
            "      - LP-PRODUCT-001\n",
            "      - LP-NOT-DECLARED-001\n",
        )
        with self.assertRaisesRegex(TRACE.TraceError, "unknown product requirements"):
            TRACE.validate(self.root)

    def test_unjoined_product_requirement_is_rejected(self) -> None:
        alignment = self.root / "requirements/alignment.yaml"
        content = alignment.read_text(encoding="utf-8")
        content = content.replace("      - LP-UNIVERSAL-001\n", "")
        alignment.write_text(content, encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "no alignment domain"):
            TRACE.validate(self.root)

    def test_unknown_registry_evidence_is_rejected(self) -> None:
        path = self.root / "tests/registry.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        document["tests"][0]["evidence_targets"] = ["LP-TEST-NOT-DECLARED"]
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(TRACE.TraceError, "unknown evidence targets"):
            TRACE.validate(self.root)

    def test_unknown_feature_evidence_is_rejected(self) -> None:
        feature = next((self.root / "requirements/features").glob("*.feature"))
        feature.write_text(
            "@LP-TEST-NOT-DECLARED\n" + feature.read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(TRACE.TraceError, "unknown evidence target"):
            TRACE.validate(self.root)


if __name__ == "__main__":
    unittest.main()
