#!/usr/bin/env python3
"""Validate the versioned Unicode-root contracts."""

from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from pathlib import Path
from typing import Any


class ContractError(RuntimeError):
    """Raised when a Unicode-root contract is incomplete or internally inconsistent."""


CONTRACT_FILES = {
    "source": "unicode-source.json",
    "atom": "unicode-atom-record.json",
    "stream": "unicode-root-stream.json",
    "physicality": "unicode-atomic-physicality.json",
    "ducet": "ducet-totalization.json",
    "geometry": "super-fibonacci-hopf.json",
    "hilbert": "hilbert-numeric.json",
}

EXPECTED_SOURCE_FILES = {
    "ReadMe.txt": (963, "f383c12b8c7ae362391b8202d45b99e2befa3c3ed473930233092b4bce318f18"),
    "ucd/ReadMe.txt": (740, "9fe1a90bd32659d7953616283dc2bffaa165518aae9ace026040c42c559ba606"),
    "ucd/UnicodeData.txt": (2198209, "2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c"),
    "ucd/extracted/DerivedBidiClass.txt": (173433, "4867b4b7f0731ed1bfcd34cc6251211ff1542541fce0734b6fbda139ee80b3a4"),
    "ucd/SpecialCasing.txt": (17049, "efc25faf19de21b92c1194c111c932e03d2a5eaf18194e33f1156e96de4c9588"),
    "ucd/CaseFolding.txt": (87539, "ff8d8fefbf123574205085d6714c36149eb946d717a0c585c27f0f4ef58c4183"),
    "ucd/BidiBrackets.txt": (8891, "dadbaf38a0d0246e5b805bf8725cb81b7c621f93d030595635f5ba2c2f179428"),
    "ucd/BidiMirroring.txt": (26827, "a2f16fb873ab4fcdf3221cb1a8a85a134ddd6ed03603181823ff5206af3741ce"),
    "ucd/EastAsianWidth.txt": (201595, "ea7ce50f3444a050333448dffef1cadd9325af55cbb764b4a2280faf52170a33"),
    "ucd/Blocks.txt": (11663, "c0edefaf1a19771e830a82735472716af6bf3c3975f6c2a23ffbe2580fbbcb15"),
    "ucd/PropList.txt": (145465, "130dcddcaadaf071008bdfce1e7743e04fdfbc910886f017d9f9ac931d8c64dd"),
    "ucd/DerivedCoreProperties.txt": (1134783, "24c7fed1195c482faaefd5c1e7eb821c5ee1fb6de07ecdbaa64b56a99da22c08"),
    "ucd/DerivedNormalizationProps.txt": (1377582, "71fd6a206a2c0cdd41feb6b7f656aa31091db45e9cedc926985d718397f9e488"),
    "ucd/CompositionExclusions.txt": (9007, "2f239196ef3b5b61db5cc476e9bd80f534d15aa1b74e1be1dea5d042a344c85f"),
    "ucd/HangulSyllableType.txt": (51360, "5a57450afde0d082bc5026f7458649eac3b615490cc7e3d916b0367f1593c0e3"),
    "ucd/Scripts.txt": (192460, "9f5e50d3abaee7d6ce09480f325c706f485ae3240912527e651954d2d6b035bf"),
    "ucd/ScriptExtensions.txt": (20707, "ec2107e58825a1586acee8e0911ce18260394ac8b87e535ca325f1ccbeb06bc6"),
    "ucd/PropertyAliases.txt": (9619, "4441f573caf952ffece1d7c892e7715bd7136dfc26f96eb6f268bf1e474715fb"),
    "ucd/PropertyValueAliases.txt": (81858, "64e9a5f76f7a1e8b5a47d6a1f9a26522a251208f5276bdfa1559dac7cf2e827a"),
    "ucd/emoji/emoji-data.txt": (107324, "2cb2bb9455cda83e8481541ecf5b6dfda66a3bb89efa3fa7c5297eccf607b72b"),
    "ucd/auxiliary/GraphemeBreakProperty.txt": (99377, "d6b51d1d2ae5c33b451b7ed994b48f1f4dc62b2272a5831e7fd418514a6bae89"),
    "ucd/auxiliary/WordBreakProperty.txt": (114445, "72274cac1e6b919507db35655c3e175aa27274668a1ece95c28d2069f2ad9852"),
    "ucd/auxiliary/SentenceBreakProperty.txt": (221233, "871c0c985ad95125e25b302414065a10839d068970bceb383ecec138f22a0a18"),
    "ucd/LineBreak.txt": (263180, "e6a18fa91f8f6a6f8e534b1d3f128c21ada45bfe152eb6b1bcc5e15fd8ac92e6"),
    "ucd/NormalizationTest.txt": (2827429, "5019ffd530751a741900c849c0e010332f142a3612234639bd200b82138a87db"),
    "ucd/auxiliary/GraphemeBreakTest.txt": (126570, "e2d134d2c52919bace503ebb6a551c1855fe1a1faec18478c78fff254a1793ec"),
    "ucd/auxiliary/WordBreakTest.txt": (322136, "1de23a75f37904abc7d206239ee8d34f8fdf0fb4ab32a7174dfbabbde25419b2"),
    "ucd/auxiliary/SentenceBreakTest.txt": (87946, "12cb47d028ded0c1cb8a28558f95479cbcd24559c46977015c82f3b50a1cc6e4"),
    "ucd/auxiliary/LineBreakTest.txt": (3166819, "e69884e0dde6a8724873f885d68c52dc14518abf9ae4ca9e2283b8773db3b752"),
    "uca/ReadMe.txt": (886, "79b778c37de3d989d6fa21e27c058b587731037024b2eaaa158036ef9ffc6088"),
    "uca/allkeys.txt": (2304434, "2503d09367c2639a4fb8fd55e81aaacb0d9fb4ea26600333329bd12456b99ecd"),
    "uca/decomps.txt": (633213, "a5056d31b6e3f08a8756f205682bd0b0d2f9ea1fb64af83a326425b89a3ea0db"),
    "uca/CollationTest.zip": (4953980, "9ba92cb7627c2d09ba537dc2035d006f44f0cafb4e2198f147897fdd9d71bb10"),
}

EXPECTED_STANDARDS = [
    ("unicode", "17.0.0", None, "https://www.unicode.org/versions/Unicode17.0.0/"),
    ("uts10", "17.0.0", 53, "https://www.unicode.org/reports/tr10/tr10-53.html"),
    ("uax14", "17.0.0", 55, "https://www.unicode.org/reports/tr14/tr14-55.html"),
    ("uax15", "17.0.0", 57, "https://www.unicode.org/reports/tr15/tr15-57.html"),
    ("uax29", "17.0.0", 47, "https://www.unicode.org/reports/tr29/tr29-47.html"),
    ("uax44", "17.0.0", 36, "https://www.unicode.org/reports/tr44/tr44-36.html"),
]

EXPECTED_VARIABLE_FIELDS = [
    "general_category",
    "canonical_combining_class",
    "bidi_class",
    "bidi_paired_bracket",
    "bidi_mirroring_glyph",
    "canonical_decomposition",
    "compatibility_decomposition",
    "numeric_value",
    "simple_case_mappings",
    "full_case_mappings",
    "case_folding",
    "block",
    "east_asian_width",
    "script",
    "script_extensions",
    "prop_list",
    "derived_core_properties",
    "normalization_properties",
    "full_composition_exclusion",
    "hangul_syllable_type",
    "grapheme_cluster_break",
    "word_break",
    "sentence_break",
    "indic_conjunct_break",
    "extended_pictographic",
    "line_break",
]

EXPECTED_PROVIDER_HASHES = {
    "/opt/intel/oneapi/compiler/2026.1/bin/icx": "be7fa7c3c74c2ba2b8e1b50b0a61e37986441727899e25f3545636bde157c083",
    "/opt/intel/oneapi/compiler/2026.1/bin/compiler/clang-22": "bcab0d4b5e8f2dc6551198d7c282b9146dc84cf42b1332ff4c8212119176a036",
    "/opt/intel/oneapi/mkl/2026.1/lib/libmkl_rt.so.3": "b2ff0e31d7cd18c91813d8f6500f37665597d89de22649d90687aa6bf7bd2c0f",
    "/opt/intel/oneapi/mkl/2026.1/lib/libmkl_core.so.3": "b4f086651ee5a8471140d53fedfdbbaba606287784763b0739f282872c31aa9b",
    "/opt/intel/oneapi/mkl/2026.1/lib/libmkl_sequential.so.3": "4e7ff529bec90a1b0ce70299258a64a2ebae7a5a6f3ec031306a6ddb39b5a650",
    "/opt/intel/oneapi/mkl/2026.1/lib/libmkl_vml_avx2.so.3": "707ab79a97ab3d07d0e337bbc78383c2bde925f18d54acf930b2fd110550ed27",
    "/opt/intel/oneapi/mkl/2026.1/include/mkl_version.h": "ad8144d3ca6212c5e45e8ed69bb76f71a318ed5aebdbe7fc1982790249452a03",
    "/opt/intel/oneapi/mkl/2026.1/include/mkl_vml.h": "ccec6cbab8c6b020fd9615b192ec0fb7d8a07925c976c6b4b4faf1efbedc4908",
    "/opt/intel/oneapi/mkl/2026.1/include/mkl_vml_defines.h": "c2315ccb4ff10c42d8da1771454ce9f9cccfdecd0e2986585907724430253706",
    "/opt/intel/oneapi/mkl/2026.1/include/mkl_vml_functions.h": "e236ba7ade2e11938b9c5a70f3905d38c78a726fa11afa4ba1bbba39929b8b5f",
    "/opt/intel/oneapi/mkl/2026.1/include/mkl_service.h": "02ff230034b626727576886decbdce2a01b76728e59b9d708e884d5bd153e2c7",
}


def _read_json(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ContractError(f"cannot read contract {path}: {error}") from error
    if not isinstance(document, dict):
        raise ContractError(f"contract must be a JSON object: {path}")
    return document


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ContractError(message)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _validate_source(document: dict[str, Any], source_root: Path | None) -> None:
    _require(document.get("schema") == "laplace.unicode-source-contract/v1", "Unicode source schema changed")
    _require(document.get("unicode_version") == "17.0.0", "Unicode version must remain 17.0.0 for root v1")
    _require(document.get("population") == {"minimum_position": 0, "maximum_position": 1114111, "count": 1114112}, "Unicode position population changed")
    authority = document.get("authority", {})
    _require(authority.get("official_release_root") == "https://www.unicode.org/Public/17.0.0/", "Unicode source is not bound to the versioned official release")
    _require(authority.get("license") == "Unicode-3.0", "Unicode source license changed")
    standards = [
        (item.get("id"), item.get("version"), item.get("revision"), item.get("url"))
        for item in document.get("standards", [])
    ]
    _require(standards == EXPECTED_STANDARDS, "Unicode standard revision set changed")
    entries = document.get("files", [])
    _require(isinstance(entries, list), "Unicode source file table is not an array")
    _require(document.get("file_count") == len(EXPECTED_SOURCE_FILES), "Unicode source file_count changed")
    by_path = {item.get("path"): item for item in entries if isinstance(item, dict)}
    _require(len(entries) == len(by_path), "Unicode source file table has duplicate or invalid paths")
    _require(set(by_path) == set(EXPECTED_SOURCE_FILES), "Unicode source file set changed, including required UAX14 inputs")
    for relative, (size, digest) in EXPECTED_SOURCE_FILES.items():
        entry = by_path[relative]
        _require(entry.get("bytes") == size, f"Unicode source size changed: {relative}")
        _require(entry.get("sha256") == digest, f"Unicode source digest changed: {relative}")
        _require(isinstance(entry.get("role"), str) and entry["role"], f"Unicode source role is missing: {relative}")
        if source_root is not None:
            path = source_root / relative
            _require(path.is_file(), f"Unicode source file is missing: {path}")
            _require(path.stat().st_size == size, f"Unicode source byte size differs: {path}")
            _require(_sha256(path) == digest, f"Unicode source bytes differ: {path}")
    roles = {item["path"]: item["role"] for item in entries}
    _require(roles["ucd/SpecialCasing.txt"] == "full-case-mappings-with-context-and-locale-conditions", "full casing is not an explicit Tier-0 source")
    _require(roles["ucd/CaseFolding.txt"] == "full-and-simple-case-folding", "case folding is not an explicit Tier-0 source")
    _require(roles["ucd/BidiBrackets.txt"] == "bidi-paired-bracket-and-type", "bidi bracket data is not an explicit Tier-0 source")
    _require(roles["ucd/BidiMirroring.txt"] == "bidi-mirroring-glyph", "bidi mirroring data is not an explicit Tier-0 source")
    _require(roles["ucd/extracted/DerivedBidiClass.txt"] == "complete-assigned-and-range-sensitive-default-bidi-class", "complete Bidi_Class defaults are not an explicit Tier-0 source")
    _require(roles["ucd/EastAsianWidth.txt"] == "east-asian-width-property", "East_Asian_Width is not an explicit Tier-0 source")
    _require(roles["ucd/LineBreak.txt"] == "uax14-line-break-property", "Line_Break is not an explicit Tier-0 property source")
    _require(roles["ucd/auxiliary/LineBreakTest.txt"] == "uax14-conformance", "UAX14 conformance input is missing")
    excluded = {
        item.get("data"): item.get("disposition")
        for item in document.get("excluded_from_tier0_v1", [])
        if isinstance(item, dict)
    }
    _require(excluded.get("ucd/VerticalOrientation.txt") == "vertical-text-realization-plane-v1-not-atom-authority", "Vertical_Orientation lacks a typed later-plane disposition")
    _require(excluded.get("ucd/ArabicShaping.txt") == "script-shaping-realization-plane-v1-not-atom-authority", "Arabic shaping lacks a typed later-plane disposition")
    _require(excluded.get("emoji sequences, ZWJ sequences, and variation sequences") == "emoji-sequence-realization-plane-v1-not-atom-authority; atom-level-Extended_Pictographic-remains-in-tier0-v1", "emoji sequences lack a typed later-plane disposition")
    if source_root is not None:
        for marker in document.get("version_markers", []):
            expected = marker.get("contains")
            if expected is None:
                continue
            text = (source_root / marker["path"]).read_text(encoding="utf-8", errors="strict")
            _require(expected in text, f"Unicode version marker is missing: {marker['path']}: {expected}")
        with zipfile.ZipFile(source_root / "uca/CollationTest.zip") as archive:
            members = set(archive.namelist())
        _require("CollationTest/CollationTest_NON_IGNORABLE.txt" in members, "official full NON_IGNORABLE collation suite is absent from the bound archive")
        _require("CollationTest/CollationTest_SHIFTED.txt" in members, "official full SHIFTED collation suite is absent from the bound archive")


def _validate_atom(document: dict[str, Any]) -> None:
    _require(document.get("schema") == "laplace.unicode-atom-record-contract/v2", "Unicode atom-record schema changed")
    _require(document.get("population") == 1114112, "Unicode atom-record population changed")
    _require(document.get("canonical_stream_order") == "codepoint-position-ascending", "Unicode atom stream lost direct position order")
    _require(document.get("record_index_equals_codepoint_position") is True, "Unicode atom record index is no longer direct")
    wire = document.get("wire", {})
    _require(wire.get("header_bytes") == 196 and wire.get("magic_hex") == "4c554152", "Unicode atom wire header changed")
    _require(wire.get("payload_kinds") == {
        "canonical-ascii-property-value": 1,
        "u8": 2,
        "optional-u32le-position-and-canonical-ascii-type": 3,
        "optional-u32le-position": 4,
        "u32le-position-sequence": 5,
        "tagged-u32le-position-sequence": 6,
        "canonical-ascii-rational-or-empty": 7,
        "sorted-tagged-u32le-positions": 8,
        "sorted-lower-title-upper-position-sequences-with-context-and-locale-condition-tokens": 9,
        "sorted-status-and-u32le-position-sequences": 10,
        "sorted-canonical-ascii-set": 11,
        "sorted-canonical-ascii-key-value-set": 12,
        "u8-boolean": 13,
        "sorted-typed-normalization-property-set": 14,
    }, "atom payload-kind enum changed or is incomplete")
    fixed = wire.get("fixed_fields", [])
    offsets = [field.get("offset") for field in fixed]
    _require(offsets == sorted(offsets) and len(offsets) == len(set(offsets)), "Unicode fixed fields are not uniquely ordered")
    _require(any(field.get("name") == "coordinate_bits" and field.get("axis_order") == ["x", "y", "z", "w"] for field in fixed), "Unicode atom coordinate axes changed")
    _require(any(field.get("name") == "hilbert_key" and field.get("encoding") == "bytes[16]-hilbert-most-significant-byte-first" for field in fixed), "Unicode atom Hilbert bytes changed")
    _require(any(field.get("name") == "identity_preimage_fingerprint" and field.get("encoding") == "blake3-256-bytes[32]" and field.get("rule") == "first-16-bytes-equal-content-id" for field in fixed), "full Unicode identity-preimage fingerprint is missing")
    _require(any(field.get("name") == "geometry_epoch" and field.get("offset") == 132 for field in fixed), "Unicode atom geometry epoch is missing")
    _require(any(field.get("name") == "physicality_id" and field.get("offset") == 164 and field.get("authority") == "laplace_persistence_atomic_point_physicality" for field in fixed), "canonical Unicode atomic physicality identity is missing")
    fields = document.get("variable_fields", [])
    _require([field.get("id") for field in fields] == list(range(1, 27)), "Unicode variable field identifiers changed")
    _require([field.get("name") for field in fields] == EXPECTED_VARIABLE_FIELDS, "Unicode Tier-0 property record changed")
    _require(fields[17].get("kind") == "sorted-typed-normalization-property-set", "normalization properties lost typed value distinctions")
    _require(fields[18].get("name") == "full_composition_exclusion", "normalization composition exclusion semantics are ambiguous")
    defaults = document.get("derived_default_semantics", {})
    _require(defaults.get("full_case_mappings", "").startswith("an absent field-10 payload"), "full case-mapping default is not explicit")
    _require(defaults.get("case_folding", "").startswith("an absent field-11 payload"), "case-folding default is not explicit")
    _require(defaults.get("normalization_properties", {}).get("explicit_empty_mapping") == "value-kind-4 and never absence", "explicit empty normalization mappings can collapse into default identity")
    _require(defaults.get("script_extensions") == "an uncovered position emits the effective Script value as a singleton set", "Script_Extensions fallback is incomplete")
    line_break = fields[-1]
    _require(line_break.get("standard") == "UAX14-17.0.0-revision-55", "Line_Break is not explicitly versioned in the atom record")
    position_classes = document.get("position_classes", [])
    _require([item.get("name") for item in position_classes] == ["assigned-scalar", "unassigned-or-reserved-scalar", "private-use-scalar", "noncharacter-scalar", "surrogate-lup-address"], "Unicode position classes changed")
    identity = document.get("identity", {})
    _require(identity.get("algorithm") == "BLAKE3-128", "Unicode public content identity changed")
    _require(identity.get("full_preimage_fingerprint_algorithm") == "BLAKE3-256", "Unicode identity collision guard changed")
    _require(identity.get("content_id_equals_first_16_bytes_of_identity_preimage_fingerprint") is True, "Unicode content ID is not bound to the full preimage fingerprint")
    _require(identity.get("full_digest_is_collision_guard_not_public_entity_id") is True, "Unicode full digest crossed the public identity boundary")
    _require(identity.get("rank_and_geometry_excluded") is True, "rank or geometry entered atom identity")
    _require(document.get("record_authority", {}).get("activation") == "forbidden-by-this-contract-slice", "contract-only work claims Unicode activation")
    physicality = document.get("physicality", {})
    _require(physicality.get("contract") == "laplace.unicode-atomic-physicality-contract/v1", "Unicode atom physicality contract changed")
    _require(physicality.get("id_is_content_identity") is False, "physicality identity collapsed into content identity")
    _require(physicality.get("id_is_calculated_once_by_root_producer") is True and physicality.get("sinks_consume_emitted_id_without_minting") is True, "Unicode sinks may mint physicality independently")


def _validate_atomic_physicality(document: dict[str, Any]) -> None:
    _require(document.get("schema") == "laplace.unicode-atomic-physicality-contract/v1", "Unicode atomic-physicality schema changed")
    _require(document.get("recipe") == "Laplace-Unicode-Atomic-Physicality-v1" and document.get("recipe_version") == 1, "Unicode atomic-physicality recipe changed")
    _require(document.get("population") == 1114112, "Unicode atomic-physicality population changed")
    variant = document.get("variant", {})
    _require(variant == {
        "physicality_type": "atomic_point",
        "vertex_class": "none",
        "structural_form": "atomic_point",
        "dimension_count": 4,
        "flags": "none",
        "coordinate_axes": ["x", "y", "z", "w"],
        "radius": "canonical-positive-zero-binary64",
        "logical_count": 1,
        "vertex_count": 0,
        "trajectory_fingerprint": "canonical-all-zero-inactive-payload",
        "trajectory_inactivity_authority": "typed-atomic-point-variant-not-zero-as-sentinel",
    }, "Unicode atomic-point variant changed")
    placement = document.get("placement", {})
    _require(placement.get("atom_position_to_numeric_rank") == "complete-DUCET-placement-rank-permutation" and placement.get("rank_permutation_fingerprint") == "laplace_unicode_placement_summary.rank_permutation_fingerprint", "Unicode physicality lost placement-permutation authority")
    geometry = document.get("geometry_recipe", {})
    _require(geometry.get("population") == 1114112 and geometry.get("rank_domain") == {"minimum": 0, "maximum": 1114111}, "Unicode physicality geometry domain changed")
    _require([geometry.get("phi_binary64_be"), geometry.get("psi_binary64_be"), geometry.get("two_pi_binary64_be")] == ["3ff6a09e667f3bcd", "3ff88a3eaa601609", "401921fb54442d18"], "Unicode physicality geometry constants changed")
    _require(geometry.get("ordered_formula") == [
        "s=binary64(rank)+0.5", "t=s/binary64(1114112)",
        "d=two_pi*s", "r=sqrt(t)", "R=sqrt(1.0-t)",
        "alpha=d/phi", "beta=d/psi", "x=r*sin(alpha)",
        "y=r*cos(alpha)", "z=R*sin(beta)", "w=R*cos(beta)",
    ] and geometry.get("axis_order") == ["x", "y", "z", "w"], "Unicode physicality geometry law changed")
    coordinate = document.get("coordinate_table_fingerprint", {})
    _require(coordinate.get("algorithm") == "BLAKE3-256" and coordinate.get("domain") == "laplace-unicode-atomic-coordinate-table-v1" and coordinate.get("order") == "codepoint-position-ascending", "Unicode coordinate-table fingerprint changed")
    epoch = document.get("geometry_epoch", {})
    _require(epoch.get("algorithm") == "BLAKE3-256" and epoch.get("domain") == "laplace-unicode-root-geometry-epoch-v1", "Unicode geometry epoch changed")
    _require(epoch.get("preimage") == "domain || physicality-recipe-fingerprint || placement-rank-permutation-fingerprint || coordinate-table-fingerprint", "Unicode geometry epoch lost a semantic input")
    recipe = document.get("recipe_fingerprint", {})
    _require(recipe == {
        "algorithm": "SHA-256",
        "preimage": "exact-unicode-atomic-physicality.json-bytes",
        "generated_authority": "laplace_unicode_generated_contracts",
    }, "Unicode physicality recipe fingerprint authority changed")
    identity = document.get("physicality_identity", {})
    _require(identity.get("constructor") == "laplace_persistence_atomic_point_physicality" and identity.get("atom_wire_carries") == "calculated-physicality-id", "Unicode physicality no longer uses the canonical constructor")
    _require(identity.get("sink_rule") == "PostgreSQL-and-Tier0-consume-the-emitted-id-and-reject-mismatch-without-recalculation-as-authority", "Unicode sink physicality authority changed")
    excluded = set(document.get("excluded_from_physicality_identity", []))
    _require({"numeric-provider-receipt", "numeric-provider-fingerprint", "numeric-environment-fingerprint", "package-receipt", "resource-receipt", "Hopf-S2-view", "Hilbert-locality-key"} <= excluded, "execution evidence or derived locality entered physicality identity")


def _validate_stream(document: dict[str, Any]) -> None:
    _require(document.get("schema") == "laplace.unicode-root-stream-contract/v2", "Unicode root-stream schema changed")
    _require(document.get("record_type") == 65536 and document.get("record_version") == 2, "Unicode root stream type or version changed")
    _require(document.get("canonical_order") == ["atom", "ducet-position", "ducet-contraction", "normalization-composition", "root-manifest"], "Unicode root section order changed")
    wire = document.get("wire", {})
    _require(wire.get("header_bytes") == 32 and wire.get("magic_hex") == "4c555246", "Unicode root frame header changed")
    fields = wire.get("fixed_fields", [])
    _require([field.get("offset") for field in fields] == [0, 4, 6, 8, 12, 14, 16, 24, 28], "Unicode root frame layout changed")
    kinds = document.get("frame_kinds", [])
    _require([item.get("id") for item in kinds] == [1, 2, 3, 4, 5], "Unicode root frame-kind enum changed")
    _require([item.get("name") for item in kinds] == document.get("canonical_order"), "Unicode root frame kinds and section order diverged")
    _require(kinds[0].get("payload") == "Laplace-Unicode-Atom-Record-v2" and kinds[0].get("count") == 1114112, "Unicode atom section no longer carries the complete atom population")
    _require(kinds[1].get("payload") == "complete-collation-element-sequence-plus-equivalence-key-and-provenance" and kinds[1].get("count") == 1114112, "Unicode DUCET position sidecar was reduced or made partial")
    _require(kinds[2].get("payload") == "source-sequence-plus-complete-collation-element-sequence-and-provenance", "Unicode DUCET contraction sidecar was flattened or reduced")
    _require(kinds[3].get("payload") == "starter-position-plus-combining-position-plus-composite-position", "Unicode reverse-composition sidecar changed")
    _require(kinds[4].get("order") == "terminal-singleton" and kinds[4].get("count") == 1, "Unicode root manifest is not a terminal singleton")
    payloads = document.get("payload_contracts", {})
    ducet_position = payloads.get("ducet-position-v1", {})
    _require(ducet_position.get("magic_hex") == "4c554450" and ducet_position.get("header_bytes") == 32, "DUCET position wire header changed")
    _require(ducet_position.get("provenance") == {"explicit": 1, "implicit": 2, "hangul": 3, "lup-surrogate-extension": 4}, "DUCET position provenance enum changed")
    _require(ducet_position.get("collation_element") == ["variable-marker-u8", "reserved-zero-u8", "primary-u16be", "secondary-u16be", "tertiary-u16be"], "DUCET collation element was reduced or reordered")
    _require("no-first-weight-or-truncated-key" in ducet_position.get("rules", []), "DUCET position payload permits an approximation")
    composition = payloads.get("normalization-composition-v1", {})
    _require(composition.get("magic_hex") == "4c554e43" and composition.get("record_bytes") == 32, "normalization composition wire changed")
    _require(composition.get("fields") == ["starter-position-u32le", "combining-position-u32le", "composite-position-u32le"], "normalization composition fields changed")
    contraction = payloads.get("ducet-contraction-v1", {})
    _require(contraction.get("magic_hex") == "4c554352" and contraction.get("header_bytes") == 32, "DUCET contraction wire changed")
    _require(contraction.get("sequence") == "complete-u32le-codepoint-position-sequence" and contraction.get("collation_element") == "same-complete-8-byte-DUCET-element-as-ducet-position-v1", "DUCET contraction structure was reduced")
    manifest = payloads.get("root-manifest-v2", {})
    _require(manifest.get("magic_hex") == "4c55524d" and manifest.get("record_bytes") == 512, "Unicode root manifest wire changed")
    _require(manifest.get("counts") == ["atom", "ducet-position", "ducet-contraction", "normalization-composition", "total-root-frames"], "Unicode root manifest counts changed")
    _require(manifest.get("scalars") == ["physicality-recipe-version-u32le"], "Unicode root manifest physicality recipe version changed")
    _require(manifest.get("bindings") == ["source", "recipe", "canonical-numeric-provider-receipt", "root-stream-contract", "atom-section", "ducet-position-section", "ducet-contraction-section", "normalization-composition-section", "algorithmic-Hangul-rule", "atom-record-contract", "physicality-recipe", "placement-rank-permutation", "coordinate-table", "geometry-epoch"], "Unicode root manifest lost a required binding")
    _require(manifest.get("binding_fingerprints") == {
        "root-stream-contract": "SHA-256(exact-unicode-root-stream.json-bytes)",
        "algorithmic-Hangul-rule": "SHA-256(exact-ducet-totalization.json-bytes)",
        "atom-record-contract": "SHA-256(exact-unicode-atom-record.json-bytes)",
        "physicality-recipe": "SHA-256(exact-unicode-atomic-physicality.json-bytes)",
        "placement-rank-permutation": "BLAKE3-256(exact-position-to-placement-rank-permutation-under-complete-DUCET-equivalence)",
        "coordinate-table": "BLAKE3-256(exact-position-to-rank-to-coordinate-bits)",
        "geometry-epoch": "BLAKE3-256(physicality-recipe-placement-and-coordinate-table)",
    }, "Unicode root manifest binding fingerprint recipes changed")
    _require(document.get("section_fingerprint", {}).get("domain") == "laplace-unicode-root-section-v2", "Unicode root section digest domain was not versioned")
    _require(document.get("stream_validation", {}).get("receipt", "").startswith("BLAKE3-256(laplace-unicode-root-validation-v2"), "Unicode root validation receipt domain was not versioned")
    fanout = document.get("fanout", {})
    _require(fanout.get("calculation_count") == 1 and fanout.get("producer_record_type") == 65536, "Unicode root is no longer one canonical producer calculation")
    _require(fanout.get("consumer_rule") == "PostgreSQL-and-perfcache-sinks-consume-the-identical-canonical-batches-without-recalculation", "Unicode root sinks may independently recalculate")
    _require(fanout.get("activation_rule") == "all-required-sink-artifacts-are-bound-by-one-staged-receipt-and-activate-as-one-root-epoch", "Unicode root sink artifacts may activate incoherently")
    boundaries = document.get("authority_boundaries", {})
    _require(all(boundaries.get(name) is True for name in ["outer-frame-is-not-content-identity", "sidecars-do-not-create-new-atom-identities", "stream-partitioning-does-not-change-stream-fingerprint", "this-contract-does-not-by-itself-activate-unicode"]), "Unicode root stream crossed an authority boundary")


def _validate_ducet(document: dict[str, Any]) -> None:
    _require(document.get("schema") == "laplace.ducet-totalization-contract/v1", "DUCET totalization schema changed")
    _require(document.get("population") == 1114112, "DUCET placement population changed")
    uca = document.get("uca_equivalence", {})
    _require(uca.get("normalization") == "NFD", "UCA normalization changed")
    _require(uca.get("alternate_handling") == "shifted" and uca.get("strength") == "identical", "UCA equivalence parameters changed")
    _require(uca.get("levels") == ["primary", "secondary", "tertiary", "quaternary", "identical-nfd"], "complete UCA levels are not retained")
    _require(uca.get("preserve_complete_collation_element_sequence") is True and uca.get("preserve_variable_marker") is True, "DUCET collation elements were reduced")
    _require(uca.get("retained_ce_mapping_supports_alternate_handling") == ["non-ignorable", "shifted"], "retained DUCET mappings no longer support both official alternate-handling suites")
    _require(uca.get("canonical_equivalents_compare_equal") is True, "canonical equivalence was collapsed into placement identity")
    surrogate = document.get("surrogate_extension", {})
    _require(surrogate.get("domain") == "non-text-Laplace-Unicode-Position-address", "surrogates were treated as Unicode text")
    _require(surrogate.get("range_start") == 0xD800 and surrogate.get("range_end") == 0xDFFF, "surrogate address range changed")
    _require(surrogate.get("is_unicode_text") is False and surrogate.get("is_standard_utf8") is False and surrogate.get("uca_conformance_claim") is False, "surrogate extension overclaims Unicode text conformance")
    _require(surrogate.get("weight_recipe") == "UTS10-53-section-10.1.3-unassigned-implicit-weight", "surrogate implicit-weight extension changed")
    total = document.get("placement_totalization", {})
    _require(total.get("comparison") == ["uca-17-ducet-equivalence-key-v1", "LUP-v1-position-bytes-unsigned-lexicographic-only-when-equivalence-key-equal"], "placement totalization order changed")
    _require(total.get("tie_discriminator_is_ducet_weight") is False and total.get("tie_discriminator_is_uca_level") is False, "Laplace placement discriminator was mislabeled as a DUCET/UCA weight")
    _require(total.get("rank_is_permutation") is True and total.get("rank_is_identity") is False and total.get("rank_is_semantic_equivalence") is False, "placement rank crossed an authority boundary")
    rank_fingerprint = total.get("rank_permutation_fingerprint", {})
    _require(rank_fingerprint.get("algorithm") == "BLAKE3-256" and rank_fingerprint.get("domain") == "laplace-unicode-placement-rank-permutation-v1", "placement-rank fingerprint authority changed")
    _require(rank_fingerprint.get("preimage") == "domain || repeated(codepoint-position-u32le || placement-rank-u32le) in codepoint-position order || population-u64le", "placement-rank fingerprint is not the pure rank map")
    _require(rank_fingerprint.get("excludes") == ["equivalence-receipt", "root-recipe-fingerprint", "physicality-recipe", "numeric-provider-receipt"], "placement-rank fingerprint admitted unrelated receipt state")
    _require(document.get("key_encoding", {}).get("no-truncation-or-first-weight-approximation") is True, "first-weight DUCET approximation was admitted")
    conformance = document.get("conformance", {})
    _require(conformance.get("required_full_suites") == [
        {"path": "CollationTest/CollationTest_NON_IGNORABLE.txt", "alternate_handling": "non-ignorable"},
        {"path": "CollationTest/CollationTest_SHIFTED.txt", "alternate_handling": "shifted"},
    ], "both official full UCA alternate-handling conformance suites are not required")
    _require(conformance.get("short_suites_are_not_substitutes_for_full_suites") is True, "short collation suites may replace required full suites")


def _validate_geometry(document: dict[str, Any]) -> None:
    _require(document.get("schema") == "laplace.super-fibonacci-hopf-contract/v1", "Super-Fibonacci/Hopf schema changed")
    _require(document.get("population") == 1114112 and document.get("rank_domain") == {"minimum": 0, "maximum": 1114111}, "bounded Super-Fibonacci domain changed")
    constants = document.get("constants", {})
    _require(constants.get("phi_equation") == "phi^2=2" and constants.get("phi_binary64_be") == "3ff6a09e667f3bcd", "Super-Fibonacci phi changed")
    _require(constants.get("psi_equation") == "psi^4=psi+4" and constants.get("psi_binary64_be") == "3ff88a3eaa601609", "Super-Fibonacci psi changed")
    _require(constants.get("two_pi_binary64_be") == "401921fb54442d18", "Super-Fibonacci two-pi seed changed")
    _require(document.get("ordered_formula") == [
        "s=binary64(rank)+0.5", "t=s/binary64(1114112)", "d=two_pi*s",
        "r=sqrt(t)", "R=sqrt(1.0-t)", "alpha=d/phi", "beta=d/psi",
        "x=r*sin(alpha)", "y=r*cos(alpha)", "z=R*sin(beta)", "w=R*cos(beta)"
    ], "CVPR Algorithm 1 operation or axis order changed")
    _require(document.get("axis_order") == ["x", "y", "z", "w"], "S3 axis order changed")
    pairs = document.get("complex_pair_convention", {})
    _require(pairs.get("z1") == "x+i*y" and pairs.get("z2") == "z+i*w", "Hopf complex-pair convention changed")
    hopf = document.get("hopf_map", {})
    _require([hopf.get("base_x"), hopf.get("base_y"), hopf.get("base_z")] == ["2*(x*z+y*w)", "2*(y*z-x*w)", "x*x+y*y-z*z-w*w"], "Hopf axis/sign convention changed")
    _require(hopf.get("derived_base_z") == "2*t-1", "Hopf polar-height law changed")
    _require(hopf.get("is_canonical_physicality") is False and hopf.get("is_receipted_view") is True, "Hopf view replaced canonical R4 physicality")
    physicality = document.get("canonical_physicality", {})
    _require(physicality.get("composites_use-arithmetic-centroid-and-are-not-renormalized") is True, "composites were projected back to S3")


def _validate_hilbert_numeric(document: dict[str, Any], verify_provider: bool) -> None:
    _require(document.get("schema") == "laplace.hilbert-numeric-contract/v1", "Hilbert/numeric schema changed")
    _require(document.get("cross_provider_bit_reproduction") == "not-established", "cross-provider bit parity was asserted without evidence")
    builder = document.get("canonical_root_builder", {})
    _require(builder.get("authority") == "single-provider-bit-authority" and builder.get("platform") == "linux-x86_64-avx2", "canonical root-builder authority changed")
    compiler = builder.get("compiler", {})
    _require(compiler.get("version_line") == "Intel(R) oneAPI DPC++/C++ Compiler 2026.1.1 (2026.1.1.20260724)", "canonical compiler version changed")
    _require(compiler.get("flags") == ["-O3", "-march=haswell", "-fp-model=strict", "-fno-fast-math", "-ffp-contract=off"], "canonical floating compiler controls changed")
    math = builder.get("elementary_math", {})
    _require(math.get("name") == "Intel-oneMKL-VML" and math.get("version") == "2026.1.0", "canonical elementary-math provider changed")
    _require(math.get("functions") == ["vmdSqrt", "vmdSinCos"], "canonical elementary operations changed")
    _require(math.get("mode") == ["VML_HA", "VML_FTZDAZ_OFF", "VML_ERRMODE_DEFAULT"], "canonical VML mode changed")
    _require(math.get("instruction_branch") == "AVX2" and math.get("threading") == "sequential-single-thread", "canonical provider branch/threading changed")
    inventory = {
        item.get("path"): item.get("sha256")
        for group in (math.get("runtime_objects", []), math.get("headers", []))
        for item in group
    }
    inventory[compiler.get("driver")] = compiler.get("driver_sha256")
    inventory[compiler.get("frontend")] = compiler.get("frontend_sha256")
    _require(inventory == EXPECTED_PROVIDER_HASHES, "canonical numeric provider inventory changed")
    _require(
        builder.get("installed_provider_lock") == "dependencies/installed-lock.json"
        and builder.get("installed_provider_selection_sha256")
        == "07a9f21bcb81cb669a6be3248eb889ffbceb20bfd05fc957c16373f5ed595137",
        "canonical numeric provider is not bound to the selected installed-provider lock",
    )
    _require("blocked-until" in builder.get("dependency_activation", ""), "unlocked oneAPI assets were treated as activated dependencies")
    validation = document.get("validation_classes", [])
    _require([item.get("id") for item in validation] == ["canonical-bit-replay", "noncanonical-structural", "noncanonical-coordinate-observation"], "numeric validation classes changed")
    _require([item.get("may_publish") for item in validation] == [True, False, False], "a noncanonical numeric provider may publish root state")
    quant = document.get("quantization", {})
    _require(quant.get("formula") == "roundTiesToEven(((exactBinary64(component)+1)*4294967295)/2)", "R4 quantization formula changed")
    _require(quant.get("calculation") == "exact-rational-from-IEEE754-bits-before-final-integer-rounding", "R4 quantization became ambient floating-point")
    _require([quant.get("minus_one"), quant.get("zero"), quant.get("plus_one")] == [0, 2147483648, 4294967295], "R4 quantization boundary changed")
    hilbert = document.get("hilbert", {})
    _require(hilbert.get("doi") == "10.1063/1.1751381", "Skilling source changed")
    _require(hilbert.get("dimensions") == 4 and hilbert.get("bits_per_axis") == 32, "Hilbert dimensionality changed")
    _require(hilbert.get("axis_order") == ["x", "y", "z", "w"], "Hilbert axis order changed")
    _require(hilbert.get("index_bit_order") == "for-bit-31-down-to-0-append-axis-0-through-3", "Hilbert bit-plane orientation changed")
    _require(hilbert.get("key_bytes") == 16 and hilbert.get("key_byte_order") == "most-significant-byte-first", "Hilbert key byte order changed")
    vectors = {(tuple(item["axes"]), item["key_hex"]) for item in hilbert.get("vectors", [])}
    _require(((0, 0, 0, 0), "00000000000000000000000000000000") in vectors, "Hilbert origin vector is missing")
    _require(((4294967295, 0, 0, 0), "ffffffffffffffffffffffffffffffff") in vectors, "Hilbert axis orientation vector is missing")
    boundaries = document.get("authority_boundaries", {})
    _require(all(boundaries.get(name) is True for name in ["coordinate-is-not-identity", "hilbert-is-not-identity", "hilbert-is-locality-not-semantic-standing", "noncanonical-provider-cannot-seed-database-or-perfcache", "this-contract-does-not-implement-or-activate-unicode"]), "Hilbert/numeric authority boundary changed")
    if verify_provider:
        for raw_path, digest in EXPECTED_PROVIDER_HASHES.items():
            path = Path(raw_path)
            _require(path.is_file(), f"canonical numeric provider input is missing: {path}")
            _require(_sha256(path) == digest, f"canonical numeric provider bytes differ: {path}")


def validate_contracts(
    contract_root: Path,
    source_root: Path | None = None,
    verify_numeric_provider: bool = False,
) -> dict[str, int | str]:
    documents = {
        name: _read_json(contract_root / filename)
        for name, filename in CONTRACT_FILES.items()
    }
    _validate_source(documents["source"], source_root)
    _validate_atom(documents["atom"])
    _validate_atomic_physicality(documents["physicality"])
    _validate_stream(documents["stream"])
    _validate_ducet(documents["ducet"])
    _validate_geometry(documents["geometry"])
    _validate_hilbert_numeric(documents["hilbert"], verify_numeric_provider)
    _require(documents["source"]["population"]["count"] == documents["atom"]["population"] == documents["physicality"]["population"] == documents["ducet"]["population"] == documents["geometry"]["population"], "Unicode population differs across contracts")
    _require(documents["ducet"]["ducet_source_sha256"] == EXPECTED_SOURCE_FILES["uca/allkeys.txt"][1], "DUCET contract and source manifest differ")
    return {
        "status": "contracts-verified-canonical-root-stream-implemented-no-persistence-or-activation",
        "contract_count": len(CONTRACT_FILES),
        "source_file_count": len(EXPECTED_SOURCE_FILES),
        "population": 1114112,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contracts", type=Path, required=True)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--verify-numeric-provider", action="store_true")
    arguments = parser.parse_args()
    try:
        report = validate_contracts(
            arguments.contracts,
            arguments.source_root,
            arguments.verify_numeric_provider,
        )
    except ContractError as error:
        print(f"unicode-root contract invalid: {error}")
        return 1
    print(json.dumps(report, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
