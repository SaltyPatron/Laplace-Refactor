#!/usr/bin/env python3
from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "contracts" / "sources" / "cili-pwn-mappings-20240611.json"
GENERATOR = ROOT / "tools" / "contracts" / "generate-tabular-source-profile.py"
SOURCE = Path(
    os.environ.get(
        "LAPLACE_CILI_SOURCE_ROOT",
        "/__laplace_locked_cili_source_is_not_configured__",
    )
)


class CiliSourceProfileContract(unittest.TestCase):
    def compile(self, document: dict, expect_success: bool) -> bytes:
        with tempfile.TemporaryDirectory(prefix="laplace-cili-profile-") as temporary:
            contract = Path(temporary) / "cili-pwn-mappings-20240611.json"
            output = Path(temporary) / "profile.h"
            contract.write_text(
                json.dumps(document, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [sys.executable, str(GENERATOR), "--contract", str(contract),
                 "--output", str(output)],
                check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            self.assertEqual(result.returncode == 0, expect_success,
                             result.stderr.decode("utf-8", errors="replace"))
            return output.read_bytes() if expect_success else result.stderr

    def test_contract_is_generic_deterministic_and_mutation_sensitive(self) -> None:
        document = json.loads(CONTRACT.read_text(encoding="utf-8"))
        first = self.compile(document, True)
        self.assertEqual(first, self.compile(document, True))
        self.assertIn(b"namespace laplace::generated::cili_pwn_mappings_20240611", first)
        self.assertIn(b"http://www.w3.org/2002/07/owl#sameAs", first)

        invented_header = copy.deepcopy(document)
        invented_header["artifacts"][1]["header_record_count"] = 1
        self.compile(invented_header, False)

        claim_mapping_conflation = copy.deepcopy(document)
        claim_mapping_conflation["artifacts"][1]["mapping_bindings"] = []
        self.compile(claim_mapping_conflation, False)

        non_relation = copy.deepcopy(document)
        non_relation["artifacts"][1]["mapping_bindings"][0]["relation_kind"] = 7
        self.compile(non_relation, False)

        impossible_coordinate_denominator = copy.deepcopy(document)
        impossible_coordinate_denominator["denominators"][
            "reference_coordinates"
        ] = impossible_coordinate_denominator["denominators"]["references"] + 1
        self.compile(impossible_coordinate_denominator, False)

        source_specific_name_does_not_control_generated_namespace = copy.deepcopy(document)
        source_specific_name_does_not_control_generated_namespace["profile"]["name"] = \
            "Changed descriptive label"
        changed = self.compile(source_specific_name_does_not_control_generated_namespace, True)
        self.assertIn(b"namespace laplace::generated::cili_pwn_mappings_20240611", changed)

    @unittest.skipUnless(SOURCE.is_dir(), "locked CILI source root unavailable")
    def test_locked_members_are_exact_headerless_bit_recomposable_content(self) -> None:
        document = json.loads(CONTRACT.read_text(encoding="utf-8"))
        artifacts = document["artifacts"]
        acquisition = json.loads(
            (SOURCE / "acquisition-receipt.json").read_text(encoding="utf-8"))
        profile_bytes = json.dumps(
            document, ensure_ascii=False, sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        self.assertEqual(
            acquisition["schema"],
            "laplace.tabular-source-acquisition-receipt/v1")
        self.assertEqual(
            acquisition["profile_sha256"],
            hashlib.sha256(profile_bytes).hexdigest())
        self.assertEqual(
            acquisition["artifacts"],
            [{
                "name": artifact["name"],
                "local_discovery_path": artifact["local_discovery_path"],
                "byte_count": artifact["byte_count"],
                "sha256": artifact["sha256"],
            } for artifact in artifacts])
        archive_path = SOURCE / artifacts[0]["local_discovery_path"]
        self.assertEqual(archive_path.stat().st_size, artifacts[0]["byte_count"])
        self.assertEqual(hashlib.sha256(archive_path.read_bytes()).hexdigest(),
                         artifacts[0]["sha256"])
        records = fields = mappings = references = 0
        coordinates: set[tuple[str, str]] = set()
        with zipfile.ZipFile(archive_path) as archive:
            for artifact in artifacts[1:]:
                selected = (SOURCE / artifact["local_discovery_path"]).read_bytes()
                self.assertEqual(hashlib.sha256(selected).hexdigest(), artifact["sha256"])
                self.assertEqual(archive.read(artifact["archive_member"]), selected)
                self.assertTrue(selected.endswith(b"\n"))
                self.assertNotIn(b"\r", selected)
                rows = selected[:-1].split(b"\n")
                self.assertEqual(artifact["header_record_count"], 0)
                self.assertEqual(rows[0], b"i1\t00001740-a")
                decoded = [row.decode("utf-8").split("\t") for row in rows]
                self.assertTrue(all(len(row) == 2 for row in decoded))
                self.assertTrue(all(row[0] and row[1] for row in decoded))
                self.assertEqual(len(rows), artifact["record_count"])
                records += len(rows)
                fields += len(rows) * 2
                mappings += len(rows) * len(artifact["mapping_bindings"])
                references += len(rows) * len(artifact["reference_columns"])
                for row in decoded:
                    for binding in artifact["reference_bindings"]:
                        coordinates.add((
                            binding["namespace"], row[binding["column"]]
                        ))
        expected = document["denominators"]
        self.assertEqual(records, expected["records"])
        self.assertEqual(fields, expected["fields"])
        self.assertEqual(mappings, expected["mappings"])
        self.assertEqual(references, expected["references"])
        self.assertEqual(len(coordinates), expected["reference_coordinates"])
        self.assertNotEqual(
            len(coordinates) + 1, expected["reference_coordinates"],
            "coordinate-denominator mutant escaped exact source evidence",
        )


if __name__ == "__main__":
    unittest.main()
