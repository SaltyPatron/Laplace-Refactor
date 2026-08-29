#!/usr/bin/env python3
"""Contract and deliberate-defect tests for the Unicode root boundary."""

from __future__ import annotations

import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools/validate-unicode-root-contracts.py"
SPEC = importlib.util.spec_from_file_location("unicode_root_contracts", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
VALIDATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VALIDATOR
SPEC.loader.exec_module(VALIDATOR)


class UnicodeRootContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        for filename in VALIDATOR.CONTRACT_FILES.values():
            shutil.copy2(REPO_ROOT / "contracts" / filename, self.root / filename)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def document(self, filename: str) -> tuple[Path, dict[str, object]]:
        path = self.root / filename
        return path, json.loads(path.read_text(encoding="utf-8"))

    def write(self, path: Path, document: dict[str, object]) -> None:
        path.write_text(json.dumps(document), encoding="utf-8")

    def test_all_seven_contracts_are_internally_closed(self) -> None:
        report = VALIDATOR.validate_contracts(self.root)
        self.assertEqual(report["contract_count"], 7)
        self.assertEqual(report["source_file_count"], 33)
        self.assertEqual(report["population"], 1114112)
        self.assertEqual(
            report["status"],
            "contracts-self-consistent-no-capability-or-activation-claim",
        )

    def test_root_stream_cannot_split_database_and_perfcache_calculation(self) -> None:
        path, document = self.document("unicode-root-stream.json")
        document["fanout"]["calculation_count"] = 2
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "one canonical producer"):
            VALIDATOR.validate_contracts(self.root)

    def test_root_stream_cannot_flatten_contractions_into_atoms(self) -> None:
        path, document = self.document("unicode-root-stream.json")
        document["frame_kinds"][2]["payload"] = "atom-property"
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "contraction sidecar"):
            VALIDATOR.validate_contracts(self.root)

    def test_ducet_position_cannot_drop_complete_weight_sequence(self) -> None:
        path, document = self.document("unicode-root-stream.json")
        document["payload_contracts"]["ducet-position-v1"]["collation_element"] = [
            "primary-u16be"
        ]
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "collation element"):
            VALIDATOR.validate_contracts(self.root)

    def test_root_manifest_cannot_drop_numeric_provider_receipt(self) -> None:
        path, document = self.document("unicode-root-stream.json")
        document["payload_contracts"]["root-manifest-v2"]["bindings"].remove(
            "canonical-numeric-provider-receipt"
        )
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "required binding"):
            VALIDATOR.validate_contracts(self.root)

    def test_root_manifest_cannot_drop_physicality_recipe(self) -> None:
        path, document = self.document("unicode-root-stream.json")
        document["payload_contracts"]["root-manifest-v2"]["bindings"].remove(
            "physicality-recipe"
        )
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "required binding"):
            VALIDATOR.validate_contracts(self.root)

    def test_root_manifest_cannot_drop_placement_rank_permutation(self) -> None:
        path, document = self.document("unicode-root-stream.json")
        document["payload_contracts"]["root-manifest-v2"]["bindings"].remove(
            "placement-rank-permutation"
        )
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "required binding"):
            VALIDATOR.validate_contracts(self.root)

    def test_physicality_recipe_cannot_use_execution_receipt_as_identity(self) -> None:
        path, document = self.document("unicode-atomic-physicality.json")
        document["geometry_epoch"]["preimage"] += " || numeric-provider-receipt"
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "semantic input"):
            VALIDATOR.validate_contracts(self.root)

    def test_physicality_recipe_must_use_canonical_constructor(self) -> None:
        path, document = self.document("unicode-atomic-physicality.json")
        document["physicality_identity"]["constructor"] = "unicode-private-hash"
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "canonical constructor"):
            VALIDATOR.validate_contracts(self.root)

    def test_atom_wire_cannot_drop_physicality_identity(self) -> None:
        path, document = self.document("unicode-atom-record.json")
        document["wire"]["fixed_fields"] = [
            item for item in document["wire"]["fixed_fields"]
            if item["name"] != "physicality_id"
        ]
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "physicality identity"):
            VALIDATOR.validate_contracts(self.root)

    def test_authoritative_unicode_17_source_bytes_match_when_available(self) -> None:
        source_root = Path("/vault/Data/UCD/Public/UCD/latest")
        if not source_root.is_dir():
            self.skipTest("authoritative local Unicode 17 mirror is unavailable")
        VALIDATOR.validate_contracts(self.root, source_root=source_root)

    def test_exact_oneapi_numeric_inventory_matches_when_available(self) -> None:
        compiler = Path("/opt/intel/oneapi/compiler/2026.1/bin/icx")
        if not compiler.is_file():
            self.skipTest("selected oneAPI inventory is unavailable")
        VALIDATOR.validate_contracts(self.root, verify_numeric_provider=True)

    def test_missing_line_break_source_is_rejected(self) -> None:
        path, document = self.document("unicode-source.json")
        document["files"] = [
            item for item in document["files"] if item["path"] != "ucd/LineBreak.txt"
        ]
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "source file set"):
            VALIDATOR.validate_contracts(self.root)

    def test_source_digest_change_is_rejected(self) -> None:
        path, document = self.document("unicode-source.json")
        document["files"][2]["sha256"] = "00" * 32
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "source digest changed"):
            VALIDATOR.validate_contracts(self.root)

    def test_realization_source_without_typed_later_plane_is_rejected(self) -> None:
        path, document = self.document("unicode-source.json")
        document["excluded_from_tier0_v1"] = [
            item for item in document["excluded_from_tier0_v1"]
            if item["data"] != "ucd/ArabicShaping.txt"
        ]
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "Arabic shaping"):
            VALIDATOR.validate_contracts(self.root)

    def test_atom_record_without_explicit_line_break_is_rejected(self) -> None:
        path, document = self.document("unicode-atom-record.json")
        document["variable_fields"][-1]["name"] = "unused_property"
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "property record"):
            VALIDATOR.validate_contracts(self.root)

    def test_atom_record_with_omitted_field_is_rejected(self) -> None:
        path, document = self.document("unicode-atom-record.json")
        del document["variable_fields"][9]
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "field identifiers"):
            VALIDATOR.validate_contracts(self.root)

    def test_atom_payload_kind_enum_change_is_rejected(self) -> None:
        path, document = self.document("unicode-atom-record.json")
        document["wire"]["payload_kinds"]["u8-boolean"] = 14
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "payload-kind"):
            VALIDATOR.validate_contracts(self.root)

    def test_normalization_properties_cannot_collapse_typed_values(self) -> None:
        path, document = self.document("unicode-atom-record.json")
        document["variable_fields"][17]["kind"] = (
            "sorted-canonical-ascii-key-value-set"
        )
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "typed value"):
            VALIDATOR.validate_contracts(self.root)

    def test_full_composition_exclusion_cannot_become_ambiguous(self) -> None:
        path, document = self.document("unicode-atom-record.json")
        document["variable_fields"][18]["name"] = "composition_exclusion"
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "property record"):
            VALIDATOR.validate_contracts(self.root)

    def test_atom_record_without_full_identity_fingerprint_is_rejected(self) -> None:
        path, document = self.document("unicode-atom-record.json")
        document["wire"]["fixed_fields"] = [
            item for item in document["wire"]["fixed_fields"]
            if item["name"] != "identity_preimage_fingerprint"
        ]
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "preimage fingerprint"):
            VALIDATOR.validate_contracts(self.root)

    def test_content_id_must_be_prefix_of_full_identity_fingerprint(self) -> None:
        path, document = self.document("unicode-atom-record.json")
        document["identity"]["content_id_equals_first_16_bytes_of_identity_preimage_fingerprint"] = False
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "not bound"):
            VALIDATOR.validate_contracts(self.root)

    def test_surrogate_address_promoted_to_unicode_text_is_rejected(self) -> None:
        path, document = self.document("ducet-totalization.json")
        document["surrogate_extension"]["is_unicode_text"] = True
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "overclaims Unicode text"):
            VALIDATOR.validate_contracts(self.root)

    def test_lup_placement_discriminator_cannot_become_a_ducet_weight(self) -> None:
        path, document = self.document("ducet-totalization.json")
        document["placement_totalization"]["tie_discriminator_is_ducet_weight"] = True
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "mislabeled"):
            VALIDATOR.validate_contracts(self.root)

    def test_lup_placement_discriminator_cannot_become_a_uca_level(self) -> None:
        path, document = self.document("ducet-totalization.json")
        document["placement_totalization"]["tie_discriminator_is_uca_level"] = True
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "mislabeled"):
            VALIDATOR.validate_contracts(self.root)

    def test_ducet_equivalence_and_lup_placement_cannot_collapse(self) -> None:
        path, document = self.document("ducet-totalization.json")
        document["placement_totalization"]["comparison"] = [
            "uca-17-ducet-equivalence-key-v1-with-LUP-v1-position-bytes"
        ]
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "totalization order"):
            VALIDATOR.validate_contracts(self.root)

    def test_both_full_uca_alternate_handling_suites_are_required(self) -> None:
        path, document = self.document("ducet-totalization.json")
        document["conformance"]["required_full_suites"].pop()
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "both official full UCA"):
            VALIDATOR.validate_contracts(self.root)

    def test_first_weight_ducet_approximation_is_rejected(self) -> None:
        path, document = self.document("ducet-totalization.json")
        document["uca_equivalence"]["preserve_complete_collation_element_sequence"] = False
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "reduced"):
            VALIDATOR.validate_contracts(self.root)

    def test_super_fibonacci_axis_swap_is_rejected(self) -> None:
        path, document = self.document("super-fibonacci-hopf.json")
        document["ordered_formula"][-2:] = ["z=R*cos(beta)", "w=R*sin(beta)"]
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "operation or axis order"):
            VALIDATOR.validate_contracts(self.root)

    def test_hopf_sign_change_is_rejected(self) -> None:
        path, document = self.document("super-fibonacci-hopf.json")
        document["hopf_map"]["base_y"] = "2*(x*w-y*z)"
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "axis/sign"):
            VALIDATOR.validate_contracts(self.root)

    def test_little_endian_hilbert_key_is_rejected(self) -> None:
        path, document = self.document("hilbert-numeric.json")
        document["hilbert"]["key_byte_order"] = "least-significant-byte-first"
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "byte order"):
            VALIDATOR.validate_contracts(self.root)

    def test_hilbert_bit_plane_orientation_change_is_rejected(self) -> None:
        path, document = self.document("hilbert-numeric.json")
        document["hilbert"]["index_bit_order"] = "for-bit-0-up-to-31-append-axis-0-through-3"
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "bit-plane orientation"):
            VALIDATOR.validate_contracts(self.root)

    def test_noncanonical_numeric_provider_cannot_publish(self) -> None:
        path, document = self.document("hilbert-numeric.json")
        document["validation_classes"][1]["may_publish"] = True
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "noncanonical"):
            VALIDATOR.validate_contracts(self.root)

    def test_unlocked_numeric_inventory_cannot_claim_activation(self) -> None:
        path, document = self.document("hilbert-numeric.json")
        document["canonical_root_builder"]["dependency_activation"] = "activated"
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "unlocked"):
            VALIDATOR.validate_contracts(self.root)

    def test_numeric_provider_lock_binding_cannot_drift(self) -> None:
        path, document = self.document("hilbert-numeric.json")
        document["canonical_root_builder"]["installed_provider_selection_sha256"] = "0" * 64
        self.write(path, document)
        with self.assertRaisesRegex(VALIDATOR.ContractError, "installed-provider lock"):
            VALIDATOR.validate_contracts(self.root)


if __name__ == "__main__":
    unittest.main()
