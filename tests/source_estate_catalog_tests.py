#!/usr/bin/env python3
"""Tests for authority-safe source-estate catalog and processing resolution."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "sources" / "catalog-source-estate.py"


def load_tool():
    spec = importlib.util.spec_from_file_location("laplace_source_estate_catalog", TOOL)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load source estate catalog tool")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


catalog_tool = load_tool()


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class SourceEstateCatalogTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.profile_root = self.root / "profiles"
        self.profile_root.mkdir()
        self.source_root = self.root / "sources"
        self.source_root.mkdir()

        self.profile_model = {
            "schema": "laplace.source-profile-model/v1",
            "required_sections": {
                "artifact_graph": ["exact artifacts"],
                "syntax_authority": ["provider"],
                "recipe_program": ["recipe"],
            },
        }
        self.inventory = {
            "schema": "laplace.vault-source-model-inventory/v1",
            "classification": "observed-development-state-not-source-authority",
            "observed_at_utc": "2026-08-31T00:00:00Z",
            "data_root": {
                "entries": [
                    {
                        "name": "RenamableSource",
                        "status": "present-versioned-unprofiled",
                        "roles": ["testimony"],
                        "modalities": ["tabular"],
                        "missing": ["profile closure"],
                    },
                    {
                        "name": "BrokenSource",
                        "status": "nonconforming-to-declared-contract",
                        "roles": ["mapping"],
                        "modalities": ["tabular"],
                        "missing": ["exact artifact bytes"],
                    },
                    {
                        "name": "NeedsProfile",
                        "status": "present-versioned-unprofiled",
                        "roles": ["syntax"],
                        "modalities": ["XML"],
                        "missing": ["syntax authority", "AST mapping"],
                    },
                    {
                        "name": "DuplicateSource",
                        "status": "duplicate",
                        "roles": ["mirror"],
                        "modalities": ["tabular"],
                        "missing": ["independent evidence root"],
                    },
                ]
            },
            "model_root": {"entries": []},
        }

        exact = b"alpha\tbeta\n"
        broken = b"alpha\tBETA\n"
        (self.source_root / "RenamableSource").mkdir()
        (self.source_root / "RenamableSource" / "table.tab").write_bytes(exact)
        (self.source_root / "BrokenSource").mkdir()
        (self.source_root / "BrokenSource" / "table.tab").write_bytes(broken)
        (self.source_root / "NeedsProfile").mkdir()
        (self.source_root / "NeedsProfile" / "data.xml").write_text(
            "<root/>", encoding="utf-8"
        )
        (self.source_root / "DuplicateSource").mkdir()
        (self.source_root / "DuplicateSource" / "table.tab").write_bytes(exact)

        profile = {
            "schema": "laplace.tabular-source-profile/v1",
            "coordinate": {
                "kind": 17,
                "authority": "Example Authority",
                "release": "2026-08-31",
                "namespace": "example",
                "local_identifier": "example-table",
                "version": 1,
            },
            "artifacts": [
                {
                    "local_discovery_path": "table.tab",
                    "byte_count": len(exact),
                    "sha256": digest(exact),
                }
            ],
            "syntax_authority": {"provider": "laplace.tabular-source-contract/v1"},
            "recipe_program": {"coordinate": "laplace.recipe/tabular-source/1"},
        }
        (self.profile_root / "example.json").write_text(
            json.dumps(profile), encoding="utf-8"
        )
        self.profiles = catalog_tool.load_profiles(self.profile_root)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def build(self):
        return catalog_tool.build_catalog(
            self.inventory,
            self.profile_model,
            self.profiles,
            source_root=self.source_root,
        )

    @staticmethod
    def by_name(catalog):
        return {entry["observed_name"]: entry for entry in catalog["entries"]}

    def test_exact_profile_selects_declared_provider_and_recipe(self) -> None:
        # Two exact physical copies are intentionally ambiguous. The resolver
        # may not choose one because its directory name or traversal order looks
        # preferable, even if observed inventory marks one copy as duplicate.
        catalog = self.build()
        entries = self.by_name(catalog)
        self.assertEqual(
            entries["RenamableSource"]["processing"]["disposition"],
            "ambiguous-exact-profile",
        )
        self.assertNotIn(
            "syntax_provider", entries["RenamableSource"]["processing"]
        )
        self.assertEqual(
            entries["BrokenSource"]["processing"]["disposition"],
            "profile-nonconforming",
        )
        self.assertEqual(
            entries["NeedsProfile"]["processing"]["disposition"],
            "profile-required",
        )
        self.assertEqual(
            entries["DuplicateSource"]["processing"]["disposition"],
            "excluded-from-profile-selection",
        )

        # Remove the duplicate physical copy; now exact profile evidence is
        # unique and the repository-declared parser and recipe can be selected.
        (self.source_root / "DuplicateSource" / "table.tab").unlink()
        catalog = self.build()
        resolved = self.by_name(catalog)["RenamableSource"]["processing"]
        self.assertEqual(resolved["disposition"], "resolved-exact-profile")
        self.assertEqual(
            resolved["syntax_provider"], "laplace.tabular-source-contract/v1"
        )
        self.assertEqual(
            resolved["recipe_coordinate"], "laplace.recipe/tabular-source/1"
        )

    def test_directory_name_cannot_select_processing(self) -> None:
        (self.source_root / "DuplicateSource" / "table.tab").unlink()
        before = self.build()
        before_processing = self.by_name(before)["RenamableSource"]["processing"]

        old = self.source_root / "RenamableSource"
        renamed = self.source_root / "CompletelyDifferentName"
        old.rename(renamed)
        renamed_inventory = copy.deepcopy(self.inventory)
        renamed_inventory["data_root"]["entries"][0]["name"] = (
            "CompletelyDifferentName"
        )
        after = catalog_tool.build_catalog(
            renamed_inventory,
            self.profile_model,
            self.profiles,
            source_root=self.source_root,
        )
        after_entry = self.by_name(after)["CompletelyDifferentName"]
        self.assertEqual(before_processing, after_entry["processing"])
        self.assertFalse(after_entry["source_identity_from_path"])

    def test_unprofiled_xml_is_not_guessed_into_a_parser(self) -> None:
        (self.source_root / "DuplicateSource" / "table.tab").unlink()
        catalog = self.build()
        processing = self.by_name(catalog)["NeedsProfile"]["processing"]
        self.assertEqual(processing["disposition"], "profile-required")
        self.assertNotIn("syntax_provider", processing)
        self.assertNotIn("recipe_coordinate", processing)
        self.assertEqual(
            processing["proposal"]["syntax_selection"],
            "unresolved-until-versioned-profile-binds-provider",
        )

    def test_deliberate_path_authority_defect_is_rejected(self) -> None:
        (self.source_root / "DuplicateSource" / "table.tab").unlink()
        catalog = self.build()
        mutant = copy.deepcopy(catalog)
        target = self.by_name(mutant)["NeedsProfile"]
        target["source_identity_from_path"] = True
        with self.assertRaisesRegex(ValueError, "local path entered source identity"):
            catalog_tool.validate_catalog(
                mutant, sorted(self.profile_model["required_sections"])
            )

    def test_deliberate_parser_guess_defect_is_rejected(self) -> None:
        (self.source_root / "DuplicateSource" / "table.tab").unlink()
        catalog = self.build()
        mutant = copy.deepcopy(catalog)
        processing = self.by_name(mutant)["NeedsProfile"]["processing"]
        processing["syntax_provider"] = (
            "tree-sitter-xml-because-extension-looked-right"
        )
        with self.assertRaisesRegex(ValueError, "unresolved source selected a parser"):
            catalog_tool.validate_catalog(
                mutant, sorted(self.profile_model["required_sections"])
            )


if __name__ == "__main__":
    unittest.main()
