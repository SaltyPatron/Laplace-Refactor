#!/usr/bin/env python3
"""Mutation checks for dependency capability intent."""

from __future__ import annotations

import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools/dependencies/verify-intent.py"
SPEC = importlib.util.spec_from_file_location("verify_dependency_intent", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
INTENT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = INTENT
SPEC.loader.exec_module(INTENT)


class DependencyIntentTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "dependencies").mkdir()
        for name in (
            "intent.json",
            "lock.json",
            "release-lock.json",
            "artifact-lock.json",
            "roots.json",
            "tree-sitter-grammars.lock.json",
        ):
            shutil.copy2(REPO_ROOT / "dependencies" / name, self.root / "dependencies" / name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def read_intent(self) -> dict[str, object]:
        path = self.root / "dependencies/intent.json"
        return json.loads(path.read_text(encoding="utf-8"))

    def write_intent(self, document: dict[str, object]) -> None:
        path = self.root / "dependencies/intent.json"
        path.write_text(json.dumps(document), encoding="utf-8")

    def test_current_intent_is_valid(self) -> None:
        component_count, lock_count, _, root_count = INTENT.validate(self.root)
        self.assertGreater(component_count, 0)
        self.assertGreater(lock_count, 0)
        self.assertGreater(root_count, 0)

    def test_missing_lock_join_is_rejected(self) -> None:
        document = self.read_intent()
        component = next(item for item in document["components"] if item["id"] == "blake3")
        component["lock_entries"] = []
        component["selection"] = "selection-required"
        self.write_intent(document)
        with self.assertRaisesRegex(INTENT.IntentError, "no dependency intent"):
            INTENT.validate(self.root)

    def test_duplicate_lock_join_is_rejected(self) -> None:
        document = self.read_intent()
        component = next(item for item in document["components"] if item["id"] == "googletest")
        component["lock_entries"].append("git:blake3")
        self.write_intent(document)
        with self.assertRaisesRegex(INTENT.IntentError, "is claimed by"):
            INTENT.validate(self.root)

    def test_empty_capability_intent_is_rejected(self) -> None:
        document = self.read_intent()
        document["components"][0]["intent"] = []
        self.write_intent(document)
        with self.assertRaisesRegex(INTENT.IntentError, "must be a non-empty array"):
            INTENT.validate(self.root)

    def test_unknown_lock_join_is_rejected(self) -> None:
        document = self.read_intent()
        component = next(item for item in document["components"] if item["id"] == "blake3")
        component["lock_entries"].append("git:not-declared")
        self.write_intent(document)
        with self.assertRaisesRegex(INTENT.IntentError, "unknown source locks"):
            INTENT.validate(self.root)

    def test_missing_external_capability_is_rejected(self) -> None:
        document = self.read_intent()
        document["components"] = [
            item for item in document["components"] if item["id"] != "stockfish"
        ]
        self.write_intent(document)
        with self.assertRaisesRegex(INTENT.IntentError, "critical dependency intent"):
            INTENT.validate(self.root)

    def test_missing_downloaded_model_root_is_rejected(self) -> None:
        path = self.root / "dependencies/roots.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        document["roots"] = [
            item for item in document["roots"] if item["id"] != "downloaded-model-data"
        ]
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(INTENT.IntentError, "critical dependency root"):
            INTENT.validate(self.root)

    def test_release_and_git_version_divergence_is_rejected(self) -> None:
        path = self.root / "dependencies/lock.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        document["dependencies"]["postgresql"]["version"] = "REL_18_5"
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(INTENT.IntentError, "Git/release version mismatch"):
            INTENT.validate(self.root)

    def test_release_and_git_license_divergence_is_rejected(self) -> None:
        path = self.root / "dependencies/lock.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        document["dependencies"]["postgresql"]["licenses"][0]["sha256"] = "0" * 64
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(INTENT.IntentError, "license identity mismatch"):
            INTENT.validate(self.root)

    def test_duplicate_json_key_is_rejected_before_values_are_discarded(self) -> None:
        path = self.root / "dependencies/intent.json"
        content = path.read_text(encoding="utf-8")
        content = content.replace(
            '"id": "libxml2",\n      "class": "product-runtime",',
            '"id": "libxml2",\n      "id": "discarded-duplicate",\n      "class": "product-runtime",',
        )
        path.write_text(content, encoding="utf-8")
        with self.assertRaisesRegex(INTENT.IntentError, "duplicate JSON object key: id"):
            INTENT.validate(self.root)


if __name__ == "__main__":
    unittest.main()
