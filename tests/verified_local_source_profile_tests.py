"""Check preserved-source recipes without substituting remote archive bytes."""
from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tarfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "contracts/sources/cili-pwn-mappings-20260903.json"
STAGE = Path("/vault/Data/.refresh-20260903/CILI")
spec = importlib.util.spec_from_file_location(
    "profile_generator", ROOT / "tools/contracts/generate-tabular-source-profile.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class PreservedSourceRecipe(unittest.TestCase):
    def setUp(self):
        self.document = json.loads(CONTRACT.read_text())

    def test_current_recipe_uses_existing_generic_compiler(self):
        output = generator.generate(self.document, CONTRACT)
        self.assertEqual(output, generator.generate(self.document, CONTRACT))
        self.assertIn("cili_pwn_mappings_20260903", output)
        self.assertEqual(self.document["denominators"]["mappings"], 235246)

    def test_local_acquisition_cannot_invent_remote_transport_or_omit_receipt(self):
        for edit in (lambda a: a.update(url="https://example.invalid/archive"),
                     lambda a: a["provenance"].pop("source_uri"),
                     lambda a: a["provenance"].update(receipt_sha256="f" * 63),
                     lambda a: a.update(transport="guessed")):
            document = copy.deepcopy(self.document)
            edit(document["artifacts"][0]["acquisition"])
            with self.assertRaises(ValueError):
                generator.validate(document)

    def test_historical_https_recipes_still_compile(self):
        for name in ("iso-639-3-20260415.json", "cili-pwn-mappings-20240611.json",
                     "fide-rating-list-202609.json"):
            path = ROOT / "contracts/sources" / name
            generator.generate(json.loads(path.read_text()), path)

    @unittest.skipUnless(STAGE.is_dir(), "preserved corpus is not mounted")
    def test_exact_archive_members_and_receipt_match_current_profile(self):
        artifacts = self.document["artifacts"]
        archive = STAGE / artifacts[0]["local_discovery_path"]
        receipt = Path(str(archive) + ".commit.tsv").read_bytes()
        self.assertEqual(hashlib.sha256(receipt).hexdigest(),
                         artifacts[0]["acquisition"]["provenance"]["receipt_sha256"])
        with tarfile.open(archive) as source:
            for artifact in artifacts:
                local = (STAGE / artifact["local_discovery_path"]).read_bytes()
                self.assertEqual(len(local), artifact["byte_count"])
                self.assertEqual(hashlib.sha256(local).hexdigest(), artifact["sha256"])
                if artifact["parent"] is not None:
                    self.assertEqual(source.extractfile(artifact["archive_member"]).read(),
                                     local)


if __name__ == "__main__":
    unittest.main()
