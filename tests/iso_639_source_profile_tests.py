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
CONTRACT = ROOT / "contracts" / "sources" / "iso-639-3-20260415.json"
GENERATOR = ROOT / "tools" / "contracts" / "generate-tabular-source-profile.py"
SOURCE = Path(os.environ.get("LAPLACE_ISO_639_SOURCE_ROOT", "/vault/Data/ISO639"))


class Iso639SourceProfileContract(unittest.TestCase):
    def compile(self, document: dict, expect_success: bool) -> bytes:
        with tempfile.TemporaryDirectory(prefix="laplace-iso-profile-") as temporary:
            temporary_path = Path(temporary)
            contract = temporary_path / "contract.json"
            output = temporary_path / "profile.h"
            contract.write_text(
                json.dumps(document, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [sys.executable, str(GENERATOR), "--contract", str(contract),
                 "--output", str(output)],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            self.assertEqual(
                result.returncode == 0,
                expect_success,
                result.stderr.decode("utf-8", errors="replace"),
            )
            return output.read_bytes() if expect_success else result.stderr

    def test_contract_compiles_deterministically_and_mutations_are_rejected(self) -> None:
        document = json.loads(CONTRACT.read_text(encoding="utf-8"))
        first = self.compile(document, True)
        second = self.compile(document, True)
        self.assertEqual(first, second)
        self.assertIn(b"Source contract:", first)

        missing_parent = copy.deepcopy(document)
        missing_parent["artifacts"][1]["parent"] = None
        self.compile(missing_parent, False)

        false_exact_archive = copy.deepcopy(document)
        false_exact_archive["artifacts"][0]["exact_distribution"] = True
        self.compile(false_exact_archive, False)

        bad_denominator = copy.deepcopy(document)
        bad_denominator["denominators"]["claims"] -= 1
        self.compile(bad_denominator, False)

        forced_mapping_denominator = copy.deepcopy(document)
        forced_mapping_denominator["denominators"]["mappings"] = \
            forced_mapping_denominator["denominators"]["claims"]
        self.compile(forced_mapping_denominator, False)

        impossible_coordinate_denominator = copy.deepcopy(document)
        impossible_coordinate_denominator["denominators"][
            "reference_coordinates"
        ] = impossible_coordinate_denominator["denominators"]["references"] + 1
        self.compile(impossible_coordinate_denominator, False)

        hidden_first_record = copy.deepcopy(document)
        hidden_first_record["artifacts"][1]["header_record_count"] = 0
        self.compile(hidden_first_record, False)

        changed_license = copy.deepcopy(document)
        changed_license["license"]["exact_notice_utf8"] += " "
        self.compile(changed_license, False)

        false_reference_resolution = copy.deepcopy(document)
        false_reference_resolution["highway_and_references"][
            "initial_disposition"
        ]["kind"] = "resolved"
        self.compile(false_reference_resolution, False)

        ambient_acquisition = copy.deepcopy(document)
        ambient_acquisition["artifacts"][0]["acquisition"]["transport"] = "http"
        ambient_acquisition["artifacts"][0]["acquisition"]["url"] = \
            "http://example.invalid/ambient.zip"
        self.compile(ambient_acquisition, False)

    def test_carrier_accepts_profile_declared_evidence_outcomes(self) -> None:
        document = json.loads(CONTRACT.read_text(encoding="utf-8"))
        expected = {
            "assertion": b"    1u,",
            "measurement": b"    2u,",
            "prediction": b"    3u,",
            "observed_consequence": b"    4u,",
            "mapping": b"    5u,",
            "definition": b"    6u,",
            "example": b"    7u,",
            "counterexample": b"    8u,",
            "unknown_boundary": b"    9u,",
        }
        for outcome, generated_value in expected.items():
            with self.subTest(outcome=outcome):
                variant = copy.deepcopy(document)
                variant["artifacts"][1]["outcome_type"] = outcome
                compiled = self.compile(variant, True)
                self.assertIn(generated_value, compiled)

        invalid = copy.deepcopy(document)
        invalid["artifacts"][1]["outcome_type"] = "tabular-means-mapping"
        self.compile(invalid, False)

    @unittest.skipUnless(SOURCE.is_dir(), "locked ISO 639 source root unavailable")
    def test_locked_archive_members_and_tabular_denominators_are_exact(self) -> None:
        document = json.loads(CONTRACT.read_text(encoding="utf-8"))
        artifacts = document["artifacts"]
        archive_path = SOURCE / artifacts[0]["local_discovery_path"]
        self.assertEqual(archive_path.stat().st_size, artifacts[0]["byte_count"])
        self.assertEqual(
            hashlib.sha256(archive_path.read_bytes()).hexdigest(),
            artifacts[0]["sha256"],
        )
        total_records = 0
        total_fields = 0
        total_claims = 0
        total_references = 0
        coordinates: set[tuple[str, str]] = set()
        with zipfile.ZipFile(archive_path) as archive:
            for artifact in artifacts[1:]:
                selected = (SOURCE / artifact["local_discovery_path"]).read_bytes()
                self.assertEqual(len(selected), artifact["byte_count"])
                self.assertEqual(hashlib.sha256(selected).hexdigest(), artifact["sha256"])
                self.assertEqual(archive.read(artifact["archive_member"]), selected)
                selected.decode("utf-8", errors="strict")
                self.assertTrue(selected.endswith(b"\n"))
                self.assertNotIn(b"\r\n", selected)
                rows = selected[:-1].split(b"\n")
                columns = artifact["columns"]
                header_count = artifact["header_record_count"]
                if header_count:
                    self.assertEqual(rows[0].decode("utf-8").split("\t"), columns)
                decoded_rows = [row.decode("utf-8").split("\t") for row in rows]
                self.assertTrue(all(len(row) == len(columns) for row in decoded_rows))
                self.assertEqual(len(rows), artifact["record_count"])
                self.assertEqual(len(rows) * len(columns), artifact["field_count"])
                self.assertEqual(
                    len(rows) - header_count, artifact["claim_count"])
                self.assertEqual(
                    (len(rows) - header_count) *
                    len(artifact.get("mapping_bindings", [])),
                    artifact["mapping_count"],
                )
                total_records += len(rows)
                total_fields += len(rows) * len(columns)
                total_claims += len(rows) - header_count
                for row in decoded_rows[header_count:]:
                    for binding in artifact["reference_bindings"]:
                        value = row[binding["column"]]
                        if value:
                            total_references += 1
                            coordinates.add((binding["namespace"], value))
        denominators = document["denominators"]
        self.assertEqual(total_records, denominators["records"])
        self.assertEqual(total_fields, denominators["fields"])
        self.assertEqual(total_claims, denominators["claims"])
        self.assertEqual(denominators["mappings"], 0)
        self.assertEqual(total_references, denominators["references"])
        self.assertEqual(
            len(coordinates), denominators["reference_coordinates"])
        self.assertNotEqual(
            len(coordinates) + 1, denominators["reference_coordinates"],
            "coordinate-denominator mutant escaped exact source evidence",
        )
        self.assertEqual(
            document["highway_and_references"]["initial_disposition"],
            {
                "kind": "unresolved",
                "count": total_references,
                "until_operation":
                    "typed ISO 639 authority-release-namespace Highway topology",
            },
        )


if __name__ == "__main__":
    unittest.main()
