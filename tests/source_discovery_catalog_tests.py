#!/usr/bin/env python3
"""Behavioral and deliberate-defect tests for source discovery cataloging."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = REPOSITORY_ROOT / "tools" / "sources" / "catalog-source-root.py"
CONTRACT_PATH = REPOSITORY_ROOT / "contracts" / "source-discovery-catalog.json"

spec = importlib.util.spec_from_file_location("laplace_source_catalog", TOOL_PATH)
assert spec is not None and spec.loader is not None
catalog_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog_tool)


class SourceDiscoveryCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        catalog_tool.validate_contract(cls.contract)

    def setUp(self) -> None:
        self.temporary = Path(tempfile.mkdtemp(prefix="laplace-source-catalog-test."))
        self.addCleanup(lambda: shutil.rmtree(self.temporary, ignore_errors=True))

    def make_fixture(self, root: Path) -> None:
        root.mkdir(parents=True)
        (root / "a.json").write_text('{"value": [2, 5, 5]}\n', encoding="utf-8")
        (root / "nested").mkdir()
        (root / "nested" / "copy.json").write_bytes((root / "a.json").read_bytes())
        (root / "not-really.txt").write_bytes(
            bytes.fromhex("89504e470d0a1a0a") + b"not-a-complete-png"
        )
        (root / "invalid-utf8.txt").write_bytes(b"\xff\xfd\xfc")
        with zipfile.ZipFile(root / "bundle.zip", "w") as archive:
            archive.writestr("safe/member.txt", "member")
            archive.writestr("../unsafe.txt", "unsafe")
        try:
            os.symlink("a.json", root / "link-to-a")
        except (OSError, NotImplementedError):
            pass

    def build(self, root: Path, label: str = "fixture") -> dict:
        catalog = catalog_tool.build_catalog(self.contract, root, label)
        catalog_tool.validate_catalog(catalog, self.contract)
        return catalog

    def test_relocation_and_host_metadata_do_not_change_artifact_digest(self) -> None:
        first = self.temporary / "first"
        second = self.temporary / "elsewhere" / "second"
        self.make_fixture(first)
        shutil.copytree(first, second, symlinks=True)
        for index, path in enumerate(sorted(second.rglob("*"))):
            if not path.is_symlink():
                os.utime(path, (1_700_000_000 + index, 1_700_000_000 + index))
        a = self.build(first, "first-label")
        b = self.build(second, "second-label")
        self.assertEqual(a["catalog_digest_sha256"], b["catalog_digest_sha256"])
        self.assertEqual(a["entries"], b["entries"])
        self.assertNotEqual(a["catalog_label"], b["catalog_label"])
        self.assertIn("laplace-content-identity", a["nonclaims"])

    def test_duplicate_digests_retain_distinct_path_occurrences(self) -> None:
        root = self.temporary / "source"
        self.make_fixture(root)
        catalog = self.build(root)
        groups = catalog["summary"]["duplicate_file_digest_groups"]
        self.assertEqual(len(groups), 1)
        self.assertEqual(groups[0]["paths"], ["a.json", "nested/copy.json"])
        rows = {entry["path"]: entry for entry in catalog["entries"]}
        self.assertEqual(rows["a.json"]["sha256"], rows["nested/copy.json"]["sha256"])

    def test_magic_and_extension_remain_separate_and_conflict_is_visible(self) -> None:
        root = self.temporary / "source"
        self.make_fixture(root)
        catalog = self.build(root)
        row = next(entry for entry in catalog["entries"] if entry["path"] == "not-really.txt")
        candidates = {candidate["id"]: candidate for candidate in row["format_candidates"]}
        self.assertIn("png", candidates)
        self.assertIn("signature:png", candidates["png"]["evidence"])
        self.assertTrue(row["encoding_observation"]["strict_utf8"] is False)
        self.assertFalse(row["extension_exact_conflict"],
                         "an unknown .txt extension is not an asserted conflicting format")

        (root / "lie.json").write_bytes(bytes.fromhex("89504e470d0a1a0a") + b"binary")
        catalog = self.build(root)
        row = next(entry for entry in catalog["entries"] if entry["path"] == "lie.json")
        self.assertTrue(row["extension_exact_conflict"])
        ids = {candidate["id"] for candidate in row["format_candidates"]}
        self.assertTrue({"json", "png"} <= ids)
        json_probe = next(probe for probe in row["structure_probes"]
                          if probe["kind"] == "json-document")
        self.assertEqual(json_probe["disposition"], "invalid")

    def test_invalid_utf8_is_not_promoted_to_text(self) -> None:
        root = self.temporary / "source"
        self.make_fixture(root)
        catalog = self.build(root)
        row = next(entry for entry in catalog["entries"] if entry["path"] == "invalid-utf8.txt")
        self.assertEqual(row["encoding_observation"]["kind"], "binary-or-other")
        self.assertFalse(row["encoding_observation"]["strict_utf8"])

    def test_archive_inventory_never_extracts_and_marks_unsafe_member_paths(self) -> None:
        root = self.temporary / "source"
        self.make_fixture(root)
        catalog = self.build(root)
        row = next(entry for entry in catalog["entries"] if entry["path"] == "bundle.zip")
        probe = next(probe for probe in row["structure_probes"]
                     if probe["kind"] == "zip-central-directory")
        self.assertEqual(probe["disposition"], "observed")
        safety = {member["name"]: member["path_safety"] for member in probe["members"]}
        self.assertEqual(safety["safe/member.txt"], "relative")
        self.assertEqual(safety["../unsafe.txt"], "unsafe")
        self.assertFalse((root / "unsafe.txt").exists())

    def test_symlink_is_recorded_and_not_followed(self) -> None:
        root = self.temporary / "source"
        self.make_fixture(root)
        catalog = self.build(root)
        rows = {entry["path"]: entry for entry in catalog["entries"]}
        if "link-to-a" not in rows:
            self.skipTest("symlinks are unavailable on this platform")
        self.assertEqual(rows["link-to-a"]["entry_kind"], "symlink")
        self.assertEqual(rows["link-to-a"]["disposition"], "not-followed")
        self.assertNotIn("byte_count", rows["link-to-a"])

    def test_content_change_changes_catalog_artifact_digest(self) -> None:
        root = self.temporary / "source"
        self.make_fixture(root)
        before = self.build(root)
        (root / "a.json").write_text('{"value": [2, 5, 6]}\n', encoding="utf-8")
        after = self.build(root)
        self.assertNotEqual(
            before["catalog_digest_sha256"], after["catalog_digest_sha256"]
        )

    def test_deliberate_host_path_injection_is_rejected(self) -> None:
        root = self.temporary / "source"
        self.make_fixture(root)
        catalog = self.build(root)
        catalog["absolute_root"] = str(root.resolve())
        with self.assertRaisesRegex(catalog_tool.CatalogError, "host-local"):
            catalog_tool.validate_catalog(catalog, self.contract)

    def test_dispatch_workflow_is_read_only_and_not_a_pr_gate(self) -> None:
        workflow = (REPOSITORY_ROOT / ".github" / "workflows" / "source-catalog.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("workflow_dispatch:", workflow)
        self.assertNotIn("pull_request:", workflow)
        self.assertNotIn("\n  push:", workflow)
        self.assertIn("LAPLACE_SOURCE_ESTATE_ROOT: /vault/Data", workflow)
        self.assertIn('test "$(id -un)" = laplace-runner', workflow)
        self.assertNotIn("psql ", workflow)
        self.assertNotIn("dropdb", workflow)
        self.assertNotIn("createdb", workflow)
        self.assertIn("laplace-source-estate-catalog.json", workflow)
        self.assertIn("laplace-source-estate-summary.json", workflow)
        self.assertNotIn("${{ env.LAPLACE_SOURCE_ESTATE_ROOT }}", workflow)

    def test_deliberate_extension_authority_mutation_is_detected_by_behavior(self) -> None:
        root = self.temporary / "source"
        self.make_fixture(root)
        (root / "lie.json").write_bytes(bytes.fromhex("89504e470d0a1a0a") + b"binary")
        catalog = self.build(root)
        row = next(entry for entry in catalog["entries"] if entry["path"] == "lie.json")
        self.assertEqual(row["format_candidate_disposition"], "multiple-candidates")
        self.assertTrue(row["extension_exact_conflict"])
        self.assertEqual(
            {candidate["id"] for candidate in row["format_candidates"]},
            {"json", "png"},
        )


if __name__ == "__main__":
    unittest.main()
