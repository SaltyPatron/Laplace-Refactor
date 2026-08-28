#!/usr/bin/env python3
"""Compile one declarative tabular source profile into typed C++ data.

Generated output is mechanism-neutral profile data. Runtime parsing, identity,
composition, evidence, persistence, and receipts remain owned by the native engine.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


SCHEMA = "laplace.tabular-source-profile/v1"
SCOPE_DOMAIN = b"laplace-source-profile-scope-v1\0"
OUTCOME_TYPES = {"mapping": 5}
MODES = {"raw_octets": 1, "utf8_delimited": 2}
TERMINATORS = {None: 0, "lf": 1, "crlf": 2}
RECONSTRUCTION = {"exact": 1, "semantic": 2, "none": 3}
REFERENCE_ROLE_FLAGS = {
    "endpoint": 1,
    "present-declaration": 2,
    "retired-declaration": 4,
}


def canonical(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def digest(value: Any) -> bytes:
    return hashlib.sha256(canonical(value)).digest()


def scope_id(field: str, value: str) -> bytes:
    payload = SCOPE_DOMAIN + field.encode("utf-8") + b"\0" + value.encode("utf-8")
    return hashlib.sha256(payload).digest()[:16]


def byte_list(value: bytes) -> str:
    return ", ".join(f"0x{item:02x}u" for item in value)


def cxx_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def validate(document: dict[str, Any]) -> None:
    require(document.get("schema") == SCHEMA, "unknown source-profile schema")
    profile = document["profile"]
    coordinate = document["coordinate"]
    artifacts = document["artifacts"]
    denominators = document["denominators"]
    license_record = document["license"]
    require(profile["version"] > 0, "profile version must be positive")
    require(profile["reconstruction_class"] in RECONSTRUCTION,
            "unknown reconstruction class")
    require(coordinate["kind"] == 17, "source profile must use Highway kind 17")
    require(coordinate["version"] == profile["version"],
            "coordinate and profile versions differ")
    notice = license_record["exact_notice_utf8"].encode("utf-8")
    require(hashlib.sha256(notice).hexdigest() == license_record["exact_notice_sha256"],
            "license notice digest mismatch")
    require(isinstance(artifacts, list) and artifacts,
            "profile needs at least one artifact")
    names: list[str] = []
    by_name: dict[str, dict[str, Any]] = {}
    for artifact in artifacts:
        name = artifact["name"]
        require(name not in by_name, f"duplicate artifact name: {name}")
        require(len(artifact["sha256"]) == 64, f"invalid SHA-256: {name}")
        bytes.fromhex(artifact["sha256"])
        require(artifact["byte_count"] > 0, f"empty artifact: {name}")
        require(artifact["mode"] in MODES, f"unknown mode: {name}")
        parent = artifact["parent"]
        if parent is None:
            require("container" in artifact["roles"],
                    f"root artifact is not a container: {name}")
            acquisition = artifact.get("acquisition", {})
            require(
                acquisition.get("transport") == "https"
                and acquisition.get("url", "").startswith("https://")
                and acquisition.get("retry_attempts") in range(1, 6),
                f"root artifact lacks bounded locked HTTPS acquisition: {name}",
            )
        else:
            require(parent in by_name, f"parent must precede member: {name}")
            require("container" in by_name[parent]["roles"],
                    f"parent is not a container: {name}")
            require("member" in artifact["roles"],
                    f"child is not marked as a member: {name}")
        if artifact["mode"] == "raw_octets":
            require(parent is None and not artifact["exact_distribution"],
                    "this profile must not promote the ZIP wrapper to selected content")
        else:
            require(artifact["exact_distribution"],
                    f"selected table is not exact: {name}")
            require(artifact["record_count"] > 1 and artifact["field_count"] > 0,
                    f"invalid table denominators: {name}")
            require(
                artifact["record_count"] * len(artifact["columns"])
                == artifact["field_count"],
                f"record/field equation failed: {name}",
            )
            require(artifact["claim_count"] == artifact["record_count"] - 1,
                    f"claim/header equation failed: {name}")
            require(all(0 <= value < len(artifact["columns"])
                        for value in artifact["reference_columns"]),
                    f"reference column outside row: {name}")
            bindings = artifact.get("reference_bindings", [])
            require(
                [binding["column"] for binding in bindings]
                == artifact["reference_columns"],
                f"reference bindings do not exactly cover marked columns: {name}",
            )
            for binding in bindings:
                require(binding["kind"] == 7,
                        f"ISO references must use external-reference kind: {name}")
                require(isinstance(binding["namespace"], str)
                        and binding["namespace"],
                        f"reference namespace missing: {name}")
                require(binding["roles"] and
                        all(role in REFERENCE_ROLE_FLAGS
                            for role in binding["roles"]),
                        f"unknown reference role: {name}")
                flags = sum(REFERENCE_ROLE_FLAGS[role]
                            for role in binding["roles"])
                require(flags & REFERENCE_ROLE_FLAGS["endpoint"],
                        f"reference binding is not an endpoint: {name}")
                require(not (
                    flags & REFERENCE_ROLE_FLAGS["present-declaration"] and
                    flags & REFERENCE_ROLE_FLAGS["retired-declaration"]),
                    f"reference cannot be present and retired: {name}")
            require(artifact["outcome_type"] in OUTCOME_TYPES,
                    f"unknown outcome type: {name}")
        names.append(name)
        by_name[name] = artifact
    require(names == sorted(names), "artifact graph is not in canonical name order")
    tables = [value for value in artifacts if value["mode"] == "utf8_delimited"]
    require(sum(value["byte_count"] for value in artifacts) == denominators["bytes"],
            "byte denominator mismatch")
    require(len(artifacts) == denominators["files"], "file denominator mismatch")
    require(sum("container" in value["roles"] for value in artifacts)
            == denominators["containers"], "container denominator mismatch")
    require(sum("member" in value["roles"] for value in artifacts)
            == denominators["members"], "member denominator mismatch")
    require(sum(value["record_count"] for value in tables)
            == denominators["records"], "record denominator mismatch")
    require(sum(value["field_count"] for value in tables)
            == denominators["fields"], "field denominator mismatch")
    require(sum(value["claim_count"] for value in tables)
            == denominators["claims"] == denominators["mappings"],
            "claim denominator mismatch")
    reference_disposition = document["highway_and_references"][
        "initial_disposition"
    ]
    require(
        reference_disposition["kind"] == "unresolved"
        and reference_disposition["count"] == denominators["references"]
        and reference_disposition["count"] > 0
        and reference_disposition["until_operation"],
        "reference carriers must remain explicitly unresolved until typed topology",
    )


def generate(document: dict[str, Any], source: Path) -> str:
    validate(document)
    coordinate = document["coordinate"]
    profile = document["profile"]
    artifacts = document["artifacts"]
    section_names = [
        "authority_and_release",
        "license",
        "syntax_authority",
        "recipe_program",
        "universal_ast_mapping",
        "highway_and_references",
        "epistemic_witnessing",
        "denominators",
        "conformance",
        "completion",
    ]
    section_digests = {name: digest(document[name]) for name in section_names}
    selected_boundary = digest({
        "coordinate": coordinate,
        "artifacts": [
            {
                "name": value["name"],
                "parent": value["parent"],
                "byte_count": value["byte_count"],
                "sha256": value["sha256"],
            }
            for value in artifacts
        ],
        "completion": document["completion"],
    })
    occurrence_context = digest(document["execution"]["occurrence_context"])
    native_geometry_fixture = digest({
        "purpose": "native profile compilation fixture",
        "profile": profile["name"],
    })
    lines = [
        "// Generated by tools/contracts/generate-tabular-source-profile.py",
        f"// Source contract: contracts/sources/{source.name}",
        "#ifndef LAPLACE_GENERATED_ISO_639_3_20260415_PROFILE_H",
        "#define LAPLACE_GENERATED_ISO_639_3_20260415_PROFILE_H",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "namespace laplace::generated::iso_639_3_20260415 {",
        "struct Artifact {",
        "  const char* name;",
        "  const char* local_discovery_path;",
        "  const char* archive_member;",
        "  std::array<std::uint8_t, 32> sha256;",
        "  std::array<std::uint8_t, 32> parent_id;",
        "  std::uint64_t byte_count;",
        "  std::uint64_t record_count;",
        "  std::uint64_t field_count;",
        "  std::uint64_t reference_column_mask;",
        "  std::uint32_t mode;",
        "  std::uint32_t delimiter;",
        "  std::uint32_t line_terminator;",
        "  std::uint32_t column_count;",
        "  std::uint32_t outcome_type;",
        "  std::uint32_t flags;",
        "};",
        "struct ReferenceRule {",
        "  std::array<std::uint8_t, 16> namespace_id;",
        "  std::uint64_t artifact_index;",
        "  std::uint64_t column_index;",
        "  std::uint32_t kind;",
        "  std::uint32_t flags;",
        "};",
    ]
    for field in ("authority", "release", "namespace", "local_identifier"):
        value = scope_id(field, coordinate[field])
        lines.append(
            f"inline constexpr std::array<std::uint8_t, 16> {field}_id"
            f"{{{{{byte_list(value)}}}}};"
        )
    mapping = {
        "authority_release": "authority_and_release",
        "license": "license",
        "syntax_authority": "syntax_authority",
        "recipe_program": "recipe_program",
        "universal_ast_mapping": "universal_ast_mapping",
        "highway_references": "highway_and_references",
        "epistemic_witnessing": "epistemic_witnessing",
        "denominator_declaration": "denominators",
        "conformance": "conformance",
        "completion_law": "completion",
    }
    for output_name, section_name in mapping.items():
        lines.append(
            f"inline constexpr std::array<std::uint8_t, 32> {output_name}_fingerprint"
            f"{{{{{byte_list(section_digests[section_name])}}}}};"
        )
    lines.append(
        "inline constexpr std::array<std::uint8_t, 32> selected_boundary_fingerprint"
        f"{{{{{byte_list(selected_boundary)}}}}};"
    )
    lines.append(
        "inline constexpr std::array<std::uint8_t, 32> occurrence_context_fingerprint"
        f"{{{{{byte_list(occurrence_context)}}}}};"
    )
    lines.append(
        "inline constexpr std::array<std::uint8_t, 32> native_geometry_fixture_fingerprint"
        f"{{{{{byte_list(native_geometry_fixture)}}}}};"
    )
    lines.extend([
        f"inline constexpr std::uint32_t kind = {coordinate['kind']}u;",
        f"inline constexpr std::uint64_t version = {coordinate['version']}u;",
        "inline constexpr std::uint32_t reconstruction_class = "
        f"{RECONSTRUCTION[profile['reconstruction_class']]}u;",
        f"inline constexpr std::uint64_t expected_bytes = {document['denominators']['bytes']}u;",
        f"inline constexpr std::uint64_t expected_records = {document['denominators']['records']}u;",
        f"inline constexpr std::uint64_t expected_fields = {document['denominators']['fields']}u;",
        f"inline constexpr std::uint64_t expected_claims = {document['denominators']['claims']}u;",
        f"inline constexpr std::uint64_t expected_references = {document['denominators']['references']}u;",
        f"inline constexpr std::uint64_t preferred_batch_bytes = {document['execution']['preferred_batch_bytes']}u;",
        "inline constexpr Artifact artifacts[] = {",
    ])
    by_name = {value["name"]: value for value in artifacts}
    for artifact in artifacts:
        parent = artifact["parent"]
        parent_id = bytes(32) if parent is None else bytes.fromhex(by_name[parent]["sha256"])
        roles = artifact["roles"]
        flags = (1 if "container" in roles else 0) | (2 if "member" in roles else 0)
        if artifact["exact_distribution"]:
            flags |= 4
        columns = artifact.get("columns", [])
        reference_mask = sum(1 << value for value in artifact.get("reference_columns", []))
        archive_member = artifact.get("archive_member", "")
        lines.extend([
            "  {",
            f"    {cxx_string(artifact['name'])},",
            f"    {cxx_string(artifact['local_discovery_path'])},",
            f"    {cxx_string(archive_member)},",
            f"    {{{{{byte_list(bytes.fromhex(artifact['sha256']))}}}}},",
            f"    {{{{{byte_list(parent_id)}}}}},",
            f"    {artifact['byte_count']}u,",
            f"    {artifact.get('record_count', 0)}u,",
            f"    {artifact.get('field_count', 0)}u,",
            f"    {reference_mask}u,",
            f"    {MODES[artifact['mode']]}u,",
            f"    {artifact.get('delimiter', 0)}u,",
            f"    {TERMINATORS[artifact.get('line_terminator')]}u,",
            f"    {len(columns)}u,",
            f"    {OUTCOME_TYPES.get(artifact.get('outcome_type'), 0)}u,",
            f"    {flags}u",
            "  },",
        ])
    lines.extend([
        "};",
        "inline constexpr std::size_t artifact_count =",
        "    sizeof(artifacts) / sizeof(artifacts[0]);",
        "inline constexpr ReferenceRule reference_rules[] = {",
    ])
    for artifact_index, artifact in enumerate(artifacts):
        for binding in artifact.get("reference_bindings", []):
            namespace_id = scope_id("namespace", binding["namespace"])
            flags = sum(REFERENCE_ROLE_FLAGS[role]
                        for role in binding["roles"])
            lines.append(
                "  {{{" + byte_list(namespace_id) + "}}, "
                + f"{artifact_index}u, {binding['column']}u, "
                + str(binding["kind"]) + "u, " + str(flags) + "u},"
            )
    lines.extend([
        "};",
        "inline constexpr std::size_t reference_rule_count =",
        "    sizeof(reference_rules) / sizeof(reference_rules[0]);",
        "}  // namespace laplace::generated::iso_639_3_20260415",
        "#endif",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    document = json.loads(arguments.contract.read_text(encoding="utf-8"))
    output = generate(document, arguments.contract)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(output, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
