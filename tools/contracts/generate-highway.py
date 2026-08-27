#!/usr/bin/env python3
"""Validate the highway authority and generate every declaration mirror."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
from typing import Any


SCHEMA = "laplace.highway-registry-contract/v1"
MIRRORS = {
    "native", "postgresql", "sql", "csharp", "documentation", "diagnostics",
    "perfcache"
}
NAME = re.compile(r"[a-z][a-z0-9-]*\Z")


class HighwayContractError(ValueError):
    pass


def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise HighwayContractError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def canonical(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def sha256(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def exact(value: Any, keys: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != keys:
        raise HighwayContractError(f"{label} fields differ")
    return value


def uint(value: Any, label: str, maximum: int = 0xFFFFFFFF) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < 0
        or value > maximum
    ):
        raise HighwayContractError(f"{label} is not an accepted unsigned integer")
    return value


def name(value: Any, label: str) -> str:
    if not isinstance(value, str) or NAME.fullmatch(value) is None:
        raise HighwayContractError(f"{label} is not a canonical highway name")
    return value


def validate_rows(rows: Any, label: str, aliases: bool) -> list[dict[str, Any]]:
    if not isinstance(rows, list) or not rows:
        raise HighwayContractError(f"{label} must be a nonempty array")
    result: list[dict[str, Any]] = []
    previous = 0
    names: set[str] = set()
    aliases_seen: set[str] = set()
    for index, raw in enumerate(rows):
        keys = {"id", "name"}
        if aliases:
            keys |= {"introduced", "retired", "aliases"}
        row = exact(raw, keys, f"{label}[{index}]")
        identifier = uint(row["id"], f"{label}[{index}].id")
        if identifier == 0 or identifier <= previous:
            raise HighwayContractError(f"{label} identifiers are not append-only")
        previous = identifier
        row_name = name(row["name"], f"{label}[{index}].name")
        if row_name in names or row_name in aliases_seen:
            raise HighwayContractError(f"{label} name or alias is reused")
        names.add(row_name)
        normalized = {"id": identifier, "name": row_name}
        if aliases:
            introduced = uint(row["introduced"], f"{label}[{index}].introduced")
            retired = uint(row["retired"], f"{label}[{index}].retired")
            if introduced == 0 or (retired != 0 and retired < introduced):
                raise HighwayContractError(f"{label} lifecycle is invalid")
            raw_aliases = row["aliases"]
            if not isinstance(raw_aliases, list):
                raise HighwayContractError(f"{label} aliases must be an array")
            normalized_aliases: list[dict[str, Any]] = []
            for alias_index, raw_alias in enumerate(raw_aliases):
                alias = exact(
                    raw_alias,
                    {"name", "introduced", "retired"},
                    f"{label}[{index}].aliases[{alias_index}]",
                )
                alias_name = name(alias["name"], "alias name")
                alias_introduced = uint(alias["introduced"], "alias introduced")
                alias_retired = uint(alias["retired"], "alias retired")
                if (
                    alias_name in names
                    or alias_name in aliases_seen
                    or alias_introduced < introduced
                    or (alias_retired != 0 and alias_retired < alias_introduced)
                ):
                    raise HighwayContractError("highway alias lifecycle or uniqueness differs")
                aliases_seen.add(alias_name)
                normalized_aliases.append(
                    {
                        "name": alias_name,
                        "introduced": alias_introduced,
                        "retired": alias_retired,
                    }
                )
            normalized |= {
                "introduced": introduced,
                "retired": retired,
                "aliases": normalized_aliases,
            }
        result.append(normalized)
    return result


def validate(content: bytes) -> tuple[dict[str, Any], str]:
    try:
        contract = json.loads(content, object_pairs_hook=reject_duplicates)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise HighwayContractError(f"invalid highway JSON: {error}") from error
    contract = exact(
        contract,
        {
            "schema", "version", "coordinate", "kinds", "dispositions",
            "mirrors", "append_only",
        },
        "highway contract",
    )
    if contract["schema"] != SCHEMA or uint(contract["version"], "version") == 0:
        raise HighwayContractError("highway schema or version differs")
    coordinate = exact(
        contract["coordinate"],
        {
            "domain", "fingerprint_domain", "digest", "coordinate_bytes",
            "fingerprint_bytes", "zero_reserved", "encoding", "scope_id_bytes",
            "scope_fields",
        },
        "coordinate",
    )
    if (
        coordinate["domain"] != "laplace-highway-coordinate-v1"
        or coordinate["fingerprint_domain"]
        != "laplace-highway-coordinate-fingerprint-v1"
        or coordinate["digest"] != "BLAKE3-256"
        or coordinate["coordinate_bytes"] != 16
        or coordinate["fingerprint_bytes"] != 32
        or coordinate["zero_reserved"] is not True
        or coordinate["encoding"] != "kind-u32le-four-id128-version-u64le"
        or coordinate["scope_id_bytes"] != 16
        or coordinate["scope_fields"]
        != [
            "kind", "authority", "release", "namespace",
            "local_identifier", "version",
        ]
    ):
        raise HighwayContractError("coordinate identity law differs")
    kinds = validate_rows(contract["kinds"], "kinds", aliases=True)
    dispositions = validate_rows(contract["dispositions"], "dispositions", aliases=False)
    if (
        not isinstance(contract["mirrors"], list)
        or len(contract["mirrors"]) != len(MIRRORS)
        or set(contract["mirrors"]) != MIRRORS
    ):
        raise HighwayContractError("highway mirrors are incomplete or repeated")
    append_only = exact(
        contract["append_only"],
        {
            "ids_never_reused", "entries_never_removed", "names_never_reminted",
            "retirement_preserves_history", "aliases_are_versioned",
        },
        "append-only law",
    )
    if any(value is not True for value in append_only.values()):
        raise HighwayContractError("append-only law was weakened")
    semantic = {
        "schema": SCHEMA,
        "version": contract["version"],
        "coordinate": coordinate,
        "kinds": kinds,
        "dispositions": dispositions,
        "append_only": append_only,
    }
    return semantic, sha256(canonical(semantic))


def validate_append_only(previous: dict[str, Any], candidate: dict[str, Any]) -> None:
    if candidate["version"] < previous["version"]:
        raise HighwayContractError("highway registry version moved backward")
    if candidate["coordinate"] != previous["coordinate"]:
        raise HighwayContractError("highway coordinate law changed")
    previous_dispositions = previous["dispositions"]
    if candidate["dispositions"][:len(previous_dispositions)] != previous_dispositions:
        raise HighwayContractError("highway disposition history was removed or reminted")
    previous_kinds = previous["kinds"]
    candidate_kinds = candidate["kinds"]
    if len(candidate_kinds) < len(previous_kinds):
        raise HighwayContractError("highway kind history was removed")
    for index, old in enumerate(previous_kinds):
        new = candidate_kinds[index]
        for field in ("id", "name", "introduced"):
            if new[field] != old[field]:
                raise HighwayContractError("highway kind history was renumbered or reminted")
        if old["retired"] != 0 and new["retired"] != old["retired"]:
            raise HighwayContractError("retired highway kind history changed")
        if old["retired"] == 0 and new["retired"] != 0:
            if new["retired"] < candidate["version"]:
                raise HighwayContractError("highway retirement was backdated")
        elif new["retired"] != old["retired"]:
            raise HighwayContractError("highway retirement state is invalid")
        old_aliases = old["aliases"]
        if new["aliases"][:len(old_aliases)] != old_aliases:
            raise HighwayContractError("highway alias history was removed or changed")
        for alias in new["aliases"][len(old_aliases):]:
            if alias["introduced"] < candidate["version"]:
                raise HighwayContractError("highway alias introduction was backdated")


def c_name(value: str) -> str:
    return value.upper().replace("-", "_")


def render_native(semantic: dict[str, Any], fingerprint: str) -> bytes:
    lines = [
        "/* Generated by tools/contracts/generate-highway.py. */",
        "#ifndef LAPLACE_CONTRACT_HIGHWAY_H",
        "#define LAPLACE_CONTRACT_HIGHWAY_H",
        "#include <stdint.h>",
        f'#define LAPLACE_HIGHWAY_REGISTRY_FINGERPRINT "{fingerprint}"',
        '#define LAPLACE_HIGHWAY_REGISTRY_FINGERPRINT_ALGORITHM "SHA-256"',
        f"#define LAPLACE_HIGHWAY_REGISTRY_VERSION {semantic['version']}u",
        '#define LAPLACE_HIGHWAY_COORDINATE_DOMAIN "laplace-highway-coordinate-v1"',
        '#define LAPLACE_HIGHWAY_FINGERPRINT_DOMAIN "laplace-highway-coordinate-fingerprint-v1"',
        "#define LAPLACE_HIGHWAY_COORDINATE_BYTES 16u",
        "#define LAPLACE_HIGHWAY_FINGERPRINT_BYTES 32u",
        "#define LAPLACE_HIGHWAY_SCOPE_ID_BYTES 16u",
        f"#define LAPLACE_HIGHWAY_KIND_COUNT {len(semantic['kinds'])}u",
        f"#define LAPLACE_HIGHWAY_DISPOSITION_COUNT {len(semantic['dispositions'])}u",
        "typedef enum laplace_highway_kind_id {",
    ]
    for row in semantic["kinds"]:
        lines.append(f"    LAPLACE_HIGHWAY_KIND_{c_name(row['name'])} = {row['id']}u,")
    lines.extend(["} laplace_highway_kind_id;", "typedef enum laplace_highway_disposition_id {"])
    for row in semantic["dispositions"]:
        lines.append(
            f"    LAPLACE_HIGHWAY_DISPOSITION_{c_name(row['name'])} = {row['id']}u,"
        )
    lines.append("} laplace_highway_disposition_id;")
    lines.append("#define LAPLACE_HIGHWAY_KIND_REGISTRY(X) \\")
    for index, row in enumerate(semantic["kinds"]):
        suffix = " \\" if index + 1 != len(semantic["kinds"]) else ""
        lines.append(
            f"    X({c_name(row['name'])}, UINT32_C({row['id']})){suffix}"
        )
    lines.append("#define LAPLACE_HIGHWAY_KIND_CONTRACT_REGISTRY(X) \\")
    for index, row in enumerate(semantic["kinds"]):
        suffix = " \\" if index + 1 != len(semantic["kinds"]) else ""
        lines.append(
            f"    X(UINT32_C({row['id']}), \"{row['name']}\", "
            f"UINT64_C({row['introduced']}), UINT64_C({row['retired']})){suffix}"
        )
    lines.append("#define LAPLACE_HIGHWAY_DISPOSITION_CONTRACT_REGISTRY(X) \\")
    for index, row in enumerate(semantic["dispositions"]):
        suffix = " \\" if index + 1 != len(semantic["dispositions"]) else ""
        lines.append(
            f"    X(UINT32_C({row['id']}), \"{row['name']}\"){suffix}"
        )
    aliases = [
        (row["id"], alias)
        for row in semantic["kinds"]
        for alias in row["aliases"]
    ]
    lines.append(f"#define LAPLACE_HIGHWAY_ALIAS_COUNT {len(aliases)}u")
    if aliases:
        lines.append("#define LAPLACE_HIGHWAY_ALIAS_CONTRACT_REGISTRY(X) \\")
        for index, (kind_id, alias) in enumerate(aliases):
            suffix = " \\" if index + 1 != len(aliases) else ""
            lines.append(
                f"    X(UINT32_C({kind_id}), \"{alias['name']}\", "
                f"UINT64_C({alias['introduced']}), "
                f"UINT64_C({alias['retired']})){suffix}"
            )
    else:
        lines.append("#define LAPLACE_HIGHWAY_ALIAS_CONTRACT_REGISTRY(X)")
    lines.extend(["#endif", ""])
    return "\n".join(lines).encode("utf-8")


def sql_rows(rows: list[dict[str, Any]]) -> str:
    return ",\n".join(f"    ({row['id']}, '{row['name']}')" for row in rows)


def render_sql(semantic: dict[str, Any], fingerprint: str, target: str) -> bytes:
    lines = [
        "-- Generated by tools/contracts/generate-highway.py.",
        f"-- Mirror: {target}",
        f"-- Semantic fingerprint: {fingerprint}",
        "CREATE OR REPLACE VIEW laplace.highway_kind_contract(id, name) AS",
        "VALUES",
        sql_rows(semantic["kinds"]) + ";",
        "CREATE OR REPLACE VIEW laplace.highway_disposition_contract(id, name) AS",
        "VALUES",
        sql_rows(semantic["dispositions"]) + ";",
        "",
    ]
    return "\n".join(lines).encode("utf-8")


def pascal(value: str) -> str:
    return "".join(part.capitalize() for part in value.split("-"))


def render_csharp(semantic: dict[str, Any], fingerprint: str) -> bytes:
    lines = [
        "// <auto-generated />",
        "// Generator: tools/contracts/generate-highway.py",
        "namespace Laplace.Managed;",
        "public static class LaplaceHighwayContract",
        "{",
        f'    public const string SemanticFingerprint = "{fingerprint}";',
        f"    public const uint Version = {semantic['version']}U;",
    ]
    for row in semantic["kinds"]:
        lines.append(f"    public const uint Kind{pascal(row['name'])} = {row['id']}U;")
    for row in semantic["dispositions"]:
        lines.append(
            f"    public const uint Disposition{pascal(row['name'])} = {row['id']}U;"
        )
    lines.extend(["}", ""])
    return "\n".join(lines).encode("utf-8")


def render_documentation(semantic: dict[str, Any], fingerprint: str) -> bytes:
    lines = [
        "<!-- Generated by tools/contracts/generate-highway.py. -->",
        "# Typed numerical highway registry",
        "",
        f"Semantic fingerprint: `{fingerprint}`",
        "",
        "| Kind | ID | Introduced | Retired |",
        "|---|---:|---:|---:|",
    ]
    lines.extend(
        f"| {row['name']} | {row['id']} | {row['introduced']} | {row['retired']} |"
        for row in semantic["kinds"]
    )
    lines.extend(["", "Zero is reserved. Scope is `(kind, authority, release, namespace, local_identifier, version)`.", ""])
    return "\n".join(lines).encode("utf-8")


def render_diagnostics(semantic: dict[str, Any], fingerprint: str) -> bytes:
    return canonical(
        {
            "schema": "laplace.highway-diagnostics/v1",
            "semantic_fingerprint": fingerprint,
            "registry_version": semantic["version"],
            "coordinate_encoding": semantic["coordinate"]["encoding"],
            "kind_count": len(semantic["kinds"]),
            "disposition_count": len(semantic["dispositions"]),
        }
    )


def artifacts(content: bytes) -> dict[str, bytes]:
    semantic, fingerprint = validate(content)
    perfcache = canonical(
        {
            "schema": "laplace.highway-perfcache-registry/v1",
            "semantic_fingerprint": fingerprint,
            "registry": semantic,
        }
    )
    outputs = {
        "laplace/contract/highway.h": render_native(semantic, fingerprint),
        "postgresql/highway.sql": render_sql(semantic, fingerprint, "postgresql"),
        "sql/highway.sql": render_sql(semantic, fingerprint, "sql"),
        "csharp/HighwayContract.g.cs": render_csharp(semantic, fingerprint),
        "documentation/highway.md": render_documentation(semantic, fingerprint),
        "diagnostics/highway.json": render_diagnostics(semantic, fingerprint),
        "perfcache/highway-registry.json": perfcache,
    }
    manifest = {
        "schema": "laplace.highway-mirror-manifest/v1",
        "semantic_fingerprint": fingerprint,
        "source_sha256": sha256(content),
        "artifacts": [
            {"path": path, "sha256": sha256(value)}
            for path, value in sorted(outputs.items())
        ],
    }
    outputs["manifest.json"] = canonical(manifest)
    return outputs


def write_outputs(root: Path, outputs: dict[str, bytes], verify: bool) -> None:
    for relative, expected in outputs.items():
        if PurePosixPath(relative).is_absolute() or ".." in PurePosixPath(relative).parts:
            raise HighwayContractError("generated highway output path is unsafe")
        path = root.joinpath(*PurePosixPath(relative).parts)
        if verify:
            if not path.is_file() or path.read_bytes() != expected:
                raise HighwayContractError(f"generated highway mirror differs: {relative}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(expected)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--previous-contract", type=Path)
    output = parser.add_mutually_exclusive_group(required=True)
    output.add_argument("--output-root", type=Path)
    output.add_argument("--verify-root", type=Path)
    arguments = parser.parse_args()
    try:
        content = arguments.contract.read_bytes()
        candidate, unused_fingerprint = validate(content)
        if arguments.previous_contract is not None:
            previous, unused_previous_fingerprint = validate(
                arguments.previous_contract.read_bytes()
            )
            validate_append_only(previous, candidate)
        generated = artifacts(content)
        write_outputs(
            arguments.output_root or arguments.verify_root,
            generated,
            arguments.verify_root is not None,
        )
    except (OSError, HighwayContractError) as error:
        print(f"highway generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
