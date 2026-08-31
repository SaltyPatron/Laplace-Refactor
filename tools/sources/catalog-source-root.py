#!/usr/bin/env python3
"""Create a deterministic discovery-only catalog for a local source estate.

The catalog records exact observable file/container facts and candidate parser/provider
hints. It does not select source authority, infer semantic truth, admit world state, or
activate generated recipes.
"""

from __future__ import annotations

import argparse
import codecs
from collections import Counter, defaultdict
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import sys
import tarfile
from typing import Any, Iterable, Iterator, Sequence
import xml.etree.ElementTree as ET
import zipfile


CONTRACT_SCHEMA = "laplace.source-discovery-catalog-contract/v1"
CATALOG_SCHEMA = "laplace.source-discovery-catalog/v1"


class CatalogError(RuntimeError):
    """The discovery contract, source root, or generated catalog is invalid."""


def canonical(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CatalogError(message)


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CatalogError(f"cannot read discovery contract: {error}") from error
    require(isinstance(value, dict), "discovery contract must be an object")
    return value


def _string_array(value: Any, field: str, *, allow_empty: bool = False) -> list[str]:
    require(isinstance(value, list), f"{field} must be an array")
    require(allow_empty or bool(value), f"{field} must not be empty")
    result: list[str] = []
    for item in value:
        require(isinstance(item, str) and item, f"{field} contains an invalid string")
        result.append(item)
    require(len(result) == len(set(result)), f"{field} contains duplicates")
    return result


def _validate_signature(signature: dict[str, Any], field: str) -> None:
    require(isinstance(signature, dict), f"{field} must be an object")
    location = signature.get("location")
    require(location in ("prefix", "suffix"), f"{field}.location is invalid")
    offset = signature.get("offset")
    require(isinstance(offset, int) and offset >= 0, f"{field}.offset is invalid")
    payload = signature.get("hex")
    require(isinstance(payload, str) and payload and len(payload) % 2 == 0,
            f"{field}.hex is invalid")
    try:
        bytes.fromhex(payload)
    except ValueError as error:
        raise CatalogError(f"{field}.hex is invalid") from error


def validate_contract(contract: dict[str, Any]) -> None:
    require(contract.get("schema") == CONTRACT_SCHEMA, "unknown discovery contract schema")
    identity = contract.get("identity")
    require(isinstance(identity, dict), "identity contract is absent")
    require(identity.get("algorithm") == "sha256-canonical-json",
            "unsupported catalog identity algorithm")
    includes = _string_array(identity.get("includes"), "identity.includes")
    require(includes == ["contract_sha256", "entries", "summary"],
            "catalog identity inputs differ")
    excluded = set(_string_array(identity.get("excludes"), "identity.excludes"))
    require({"absolute_root", "observation_time", "mtime", "uid", "gid"} <= excluded,
            "catalog identity does not exclude host-local metadata")

    limits = contract.get("limits")
    require(isinstance(limits, dict), "limits are absent")
    for field in (
        "read_block_bytes",
        "strict_structure_probe_max_bytes",
        "archive_probe_max_bytes",
        "archive_member_limit",
        "prefix_probe_bytes",
        "suffix_probe_bytes",
    ):
        require(isinstance(limits.get(field), int) and limits[field] > 0,
                f"limits.{field} is invalid")

    hints = contract.get("format_hints")
    require(isinstance(hints, list) and hints, "format_hints must be non-empty")
    identifiers: set[str] = set()
    for index, hint in enumerate(hints):
        field = f"format_hints[{index}]"
        require(isinstance(hint, dict), f"{field} must be an object")
        identifier = hint.get("id")
        require(isinstance(identifier, str) and identifier and identifier not in identifiers,
                f"{field}.id is absent or duplicated")
        identifiers.add(identifier)
        extensions = _string_array(hint.get("extensions", []), f"{field}.extensions",
                                   allow_empty=True)
        for extension in extensions:
            require(extension.startswith(".") and extension == extension.lower(),
                    f"{field} has a noncanonical extension")
        signatures = hint.get("signatures", [])
        alternatives = hint.get("signature_alternatives", [])
        require(not (signatures and alternatives),
                f"{field} cannot declare signatures and alternatives together")
        if signatures:
            require(isinstance(signatures, list), f"{field}.signatures must be an array")
            for sig_index, signature in enumerate(signatures):
                _validate_signature(signature, f"{field}.signatures[{sig_index}]")
        if alternatives:
            require(isinstance(alternatives, list),
                    f"{field}.signature_alternatives must be an array")
            for alt_index, alternative in enumerate(alternatives):
                require(isinstance(alternative, list) and alternative,
                        f"{field}.signature_alternatives[{alt_index}] is invalid")
                for sig_index, signature in enumerate(alternative):
                    _validate_signature(
                        signature,
                        f"{field}.signature_alternatives[{alt_index}][{sig_index}]",
                    )
        strict_probe = hint.get("strict_probe")
        require(strict_probe in (None, "json-document", "xml-document",
                                 "zip-central-directory", "tar-member-table"),
                f"{field}.strict_probe is invalid")
        providers = hint.get("provider_candidates", [])
        require(isinstance(providers, list), f"{field}.provider_candidates must be an array")
        for provider_index, provider in enumerate(providers):
            provider_field = f"{field}.provider_candidates[{provider_index}]"
            require(isinstance(provider, dict), f"{provider_field} must be an object")
            for name in ("family", "key", "status"):
                require(isinstance(provider.get(name), str) and provider[name],
                        f"{provider_field}.{name} is invalid")

    laws = contract.get("laws")
    require(isinstance(laws, dict), "laws are absent")
    required_laws = {
        "path_is_discovery_only": True,
        "extension_is_authority": False,
        "magic_is_semantics": False,
        "strict_parse_is_truth": False,
        "generated_profile_is_active": False,
        "follow_symlinks": False,
        "omit_unknown_or_unreadable": False,
        "arbitrary_binary_is_utf8": False,
        "source_bytes_uploaded_by_catalog_workflow": False,
    }
    require(all(laws.get(key) is value for key, value in required_laws.items()),
            "discovery laws differ")


def canonical_relative(relative: Path) -> str:
    value = relative.as_posix()
    path = PurePosixPath(value)
    require(value and not path.is_absolute(), f"noncanonical discovery path: {value!r}")
    require(all(part not in ("", ".", "..") for part in path.parts),
            f"noncanonical discovery path: {value!r}")
    require(path.as_posix() == value, f"noncanonical discovery path: {value!r}")
    return value


def extension_chain(path: str) -> list[str]:
    return [suffix.lower() for suffix in PurePosixPath(path).suffixes]


def _signature_sets(hint: dict[str, Any]) -> list[list[dict[str, Any]]]:
    signatures = hint.get("signatures")
    if signatures:
        return [signatures]
    alternatives = hint.get("signature_alternatives")
    return alternatives if alternatives else []


def _signature_matches(
    signature: dict[str, Any], prefix: bytes, suffix: bytes, byte_count: int
) -> bool:
    expected = bytes.fromhex(signature["hex"])
    offset = signature["offset"]
    if signature["location"] == "prefix":
        end = offset + len(expected)
        return end <= len(prefix) and prefix[offset:end] == expected
    end_from_file = offset + len(expected)
    if end_from_file > byte_count or end_from_file > len(suffix):
        return False
    start = len(suffix) - end_from_file
    return suffix[start:start + len(expected)] == expected


def format_indexes(contract: dict[str, Any]) -> tuple[dict[str, list[dict[str, Any]]], list[dict[str, Any]]]:
    extensions: dict[str, list[dict[str, Any]]] = defaultdict(list)
    signed: list[dict[str, Any]] = []
    for hint in contract["format_hints"]:
        for extension in hint.get("extensions", []):
            extensions[extension].append(hint)
        if _signature_sets(hint):
            signed.append(hint)
    return extensions, signed


def _read_observations(path: Path, limits: dict[str, int]) -> dict[str, Any]:
    digest = hashlib.sha256()
    prefix_limit = limits["prefix_probe_bytes"]
    suffix_limit = limits["suffix_probe_bytes"]
    block_bytes = limits["read_block_bytes"]
    prefix = bytearray()
    suffix = bytearray()
    decoder = codecs.getincrementaldecoder("utf-8")("strict")
    utf8_valid = True
    contains_nul = False
    byte_count = 0

    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(block_bytes), b""):
            byte_count += len(block)
            digest.update(block)
            if len(prefix) < prefix_limit:
                prefix.extend(block[:prefix_limit - len(prefix)])
            if suffix_limit:
                suffix.extend(block)
                if len(suffix) > suffix_limit:
                    del suffix[:-suffix_limit]
            if b"\0" in block:
                contains_nul = True
            if utf8_valid:
                try:
                    decoder.decode(block, final=False)
                except UnicodeDecodeError:
                    utf8_valid = False
        if utf8_valid:
            try:
                decoder.decode(b"", final=True)
            except UnicodeDecodeError:
                utf8_valid = False

    encoding: dict[str, Any]
    prefix_bytes = bytes(prefix)
    if prefix_bytes.startswith(codecs.BOM_UTF8):
        encoding = {"kind": "utf-8-bom", "strict_utf8": utf8_valid,
                    "contains_nul": contains_nul}
    elif prefix_bytes.startswith(codecs.BOM_UTF32_LE):
        encoding = {"kind": "utf-32-le-bom", "strict_utf8": False,
                    "contains_nul": contains_nul}
    elif prefix_bytes.startswith(codecs.BOM_UTF32_BE):
        encoding = {"kind": "utf-32-be-bom", "strict_utf8": False,
                    "contains_nul": contains_nul}
    elif prefix_bytes.startswith(codecs.BOM_UTF16_LE):
        encoding = {"kind": "utf-16-le-bom", "strict_utf8": False,
                    "contains_nul": contains_nul}
    elif prefix_bytes.startswith(codecs.BOM_UTF16_BE):
        encoding = {"kind": "utf-16-be-bom", "strict_utf8": False,
                    "contains_nul": contains_nul}
    elif utf8_valid:
        encoding = {"kind": "utf-8", "strict_utf8": True,
                    "contains_nul": contains_nul}
    else:
        encoding = {"kind": "binary-or-other", "strict_utf8": False,
                    "contains_nul": contains_nul}

    return {
        "byte_count": byte_count,
        "sha256": digest.hexdigest(),
        "prefix": prefix_bytes,
        "suffix": bytes(suffix),
        "encoding": encoding,
    }


def _safe_member_name(value: str) -> str:
    path = PurePosixPath(value.replace("\\", "/"))
    if not value or path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        return "unsafe"
    return "relative"


def _zip_probe(path: Path, limits: dict[str, int]) -> dict[str, Any]:
    if path.stat().st_size > limits["archive_probe_max_bytes"]:
        return {"kind": "zip-central-directory", "disposition": "not-probed-size-bound"}
    try:
        with zipfile.ZipFile(path) as archive:
            infos = archive.infolist()
            if len(infos) > limits["archive_member_limit"]:
                return {
                    "kind": "zip-central-directory",
                    "disposition": "not-probed-member-bound",
                    "declared_member_count": len(infos),
                }
            members = []
            for info in infos:
                members.append({
                    "name": info.filename,
                    "path_safety": _safe_member_name(info.filename),
                    "is_directory": info.is_dir(),
                    "encrypted": bool(info.flag_bits & 0x1),
                    "compression_method": info.compress_type,
                    "compressed_bytes": info.compress_size,
                    "uncompressed_bytes": info.file_size,
                    "crc32": f"{info.CRC:08x}",
                })
            return {
                "kind": "zip-central-directory",
                "disposition": "observed",
                "member_count": len(members),
                "members": members,
            }
    except (OSError, zipfile.BadZipFile, RuntimeError, NotImplementedError) as error:
        return {
            "kind": "zip-central-directory",
            "disposition": "invalid-or-unsupported",
            "error_class": type(error).__name__,
        }


def _tar_type(member: tarfile.TarInfo) -> str:
    if member.isfile():
        return "file"
    if member.isdir():
        return "directory"
    if member.issym():
        return "symlink"
    if member.islnk():
        return "hardlink"
    if member.ischr():
        return "character-device"
    if member.isblk():
        return "block-device"
    if member.isfifo():
        return "fifo"
    return "other"


def _tar_probe(path: Path, limits: dict[str, int]) -> dict[str, Any]:
    if path.stat().st_size > limits["archive_probe_max_bytes"]:
        return {"kind": "tar-member-table", "disposition": "not-probed-size-bound"}
    try:
        with tarfile.open(path, mode="r:*") as archive:
            members = archive.getmembers()
            if len(members) > limits["archive_member_limit"]:
                return {
                    "kind": "tar-member-table",
                    "disposition": "not-probed-member-bound",
                    "declared_member_count": len(members),
                }
            rows = []
            for member in members:
                rows.append({
                    "name": member.name,
                    "path_safety": _safe_member_name(member.name),
                    "member_type": _tar_type(member),
                    "byte_count": member.size,
                })
            return {
                "kind": "tar-member-table",
                "disposition": "observed",
                "member_count": len(rows),
                "members": rows,
            }
    except (OSError, tarfile.TarError, EOFError) as error:
        return {
            "kind": "tar-member-table",
            "disposition": "invalid-or-unsupported",
            "error_class": type(error).__name__,
        }


def _strict_text_probe(path: Path, probe: str, byte_count: int, limits: dict[str, int]) -> dict[str, Any]:
    if byte_count > limits["strict_structure_probe_max_bytes"]:
        return {"kind": probe, "disposition": "not-probed-size-bound"}
    try:
        text = path.read_text(encoding="utf-8-sig", errors="strict")
    except (OSError, UnicodeError) as error:
        return {"kind": probe, "disposition": "invalid", "error_class": type(error).__name__}
    try:
        if probe == "json-document":
            value = json.loads(text)
            return {
                "kind": probe,
                "disposition": "valid",
                "root_kind": (
                    "object" if isinstance(value, dict)
                    else "array" if isinstance(value, list)
                    else "scalar"
                ),
            }
        if probe == "xml-document":
            root = ET.fromstring(text)
            return {"kind": probe, "disposition": "valid", "root_tag": root.tag}
    except (json.JSONDecodeError, ET.ParseError) as error:
        return {"kind": probe, "disposition": "invalid", "error_class": type(error).__name__}
    raise CatalogError(f"unknown strict text probe: {probe}")


def _add_candidate(
    candidates: dict[str, dict[str, Any]], hint: dict[str, Any], basis: str
) -> None:
    row = candidates.setdefault(
        hint["id"],
        {
            "id": hint["id"],
            "media_type": hint.get("media_type"),
            "evidence": [],
            "provider_candidates": hint.get("provider_candidates", []),
        },
    )
    if basis not in row["evidence"]:
        row["evidence"].append(basis)


def catalog_file(
    path: Path,
    relative: str,
    contract: dict[str, Any],
    extension_index: dict[str, list[dict[str, Any]]],
    signed_hints: list[dict[str, Any]],
) -> dict[str, Any]:
    limits = contract["limits"]
    try:
        observed = _read_observations(path, limits)
    except OSError as error:
        return {
            "path": relative,
            "entry_kind": "file",
            "disposition": "unreadable",
            "error_class": type(error).__name__,
        }

    candidates: dict[str, dict[str, Any]] = {}
    suffixes = extension_chain(relative)
    for suffix in suffixes:
        for hint in extension_index.get(suffix, []):
            _add_candidate(candidates, hint, f"extension:{suffix}")

    for hint in signed_hints:
        for signature_set in _signature_sets(hint):
            if all(_signature_matches(signature, observed["prefix"], observed["suffix"],
                                      observed["byte_count"])
                   for signature in signature_set):
                _add_candidate(candidates, hint, f"signature:{hint['id']}")
                break

    probes: list[dict[str, Any]] = []
    for hint in contract["format_hints"]:
        probe = hint.get("strict_probe")
        if probe is None:
            continue
        should_probe = hint["id"] in candidates
        if probe in ("json-document", "xml-document") and observed["encoding"]["strict_utf8"]:
            sniff = observed["prefix"]
            if sniff.startswith(codecs.BOM_UTF8):
                sniff = sniff[len(codecs.BOM_UTF8):]
            sniff = sniff.lstrip()
            if probe == "json-document":
                should_probe = should_probe or sniff.startswith((b"{", b"["))
            else:
                should_probe = should_probe or sniff.startswith(b"<")
        if not should_probe:
            continue
        if probe == "zip-central-directory":
            result = _zip_probe(path, limits)
        elif probe == "tar-member-table":
            result = _tar_probe(path, limits)
        else:
            result = _strict_text_probe(path, probe, observed["byte_count"], limits)
        probes.append(result)
        if result["disposition"] in ("valid", "observed"):
            _add_candidate(candidates, hint, f"strict-probe:{probe}")

    candidate_rows = []
    for identifier in sorted(candidates):
        row = candidates[identifier]
        row["evidence"].sort()
        row["provider_candidates"] = sorted(
            row["provider_candidates"],
            key=lambda item: (item["family"], item["key"], item["status"]),
        )
        candidate_rows.append(row)

    exact_candidate_ids = {
        row["id"] for row in candidate_rows
        if any(evidence.startswith("signature:") or evidence.startswith("strict-probe:")
               for evidence in row["evidence"])
    }
    extension_candidate_ids = {
        row["id"] for row in candidate_rows
        if any(evidence.startswith("extension:") for evidence in row["evidence"])
    }
    if not candidate_rows:
        candidate_disposition = "none"
    elif len(candidate_rows) == 1:
        candidate_disposition = "single-candidate"
    else:
        candidate_disposition = "multiple-candidates"
    conflict = bool(exact_candidate_ids and extension_candidate_ids
                    and exact_candidate_ids.isdisjoint(extension_candidate_ids))

    return {
        "path": relative,
        "entry_kind": "file",
        "disposition": "observed",
        "byte_count": observed["byte_count"],
        "sha256": observed["sha256"],
        "extension_chain": suffixes,
        "encoding_observation": observed["encoding"],
        "format_candidate_disposition": candidate_disposition,
        "extension_exact_conflict": conflict,
        "format_candidates": candidate_rows,
        "structure_probes": sorted(probes, key=lambda item: item["kind"]),
    }


def _walk(root: Path) -> Iterator[tuple[Path, str, os.stat_result]]:
    def visit(directory: Path, relative_directory: Path) -> Iterator[tuple[Path, str, os.stat_result]]:
        try:
            with os.scandir(directory) as iterator:
                entries = sorted(iterator, key=lambda item: os.fsencode(item.name))
        except OSError as error:
            if relative_directory.parts:
                synthetic = directory
                yield synthetic, canonical_relative(relative_directory), error  # type: ignore[misc]
            else:
                raise CatalogError(f"cannot enumerate source root: {type(error).__name__}") from error
            return
        for entry in entries:
            relative_path = relative_directory / entry.name
            relative = canonical_relative(relative_path)
            try:
                metadata = entry.stat(follow_symlinks=False)
            except OSError as error:
                yield Path(entry.path), relative, error  # type: ignore[misc]
                continue
            yield Path(entry.path), relative, metadata
            if stat.S_ISDIR(metadata.st_mode) and not stat.S_ISLNK(metadata.st_mode):
                yield from visit(Path(entry.path), relative_path)
    yield from visit(root, Path())


def build_catalog(contract: dict[str, Any], root: Path, label: str) -> dict[str, Any]:
    validate_contract(contract)
    require(isinstance(label, str) and label and "\0" not in label,
            "catalog label is invalid")
    try:
        root_metadata = root.lstat()
    except OSError as error:
        raise CatalogError(f"source root is unavailable: {type(error).__name__}") from error
    require(not stat.S_ISLNK(root_metadata.st_mode), "source root must not be a symlink")
    require(stat.S_ISDIR(root_metadata.st_mode), "source root is not a directory")

    extension_index, signed_hints = format_indexes(contract)
    entries: list[dict[str, Any]] = []
    for path, relative, metadata in _walk(root):
        if isinstance(metadata, OSError):
            entries.append({
                "path": relative,
                "entry_kind": "unknown",
                "disposition": "unreadable",
                "error_class": type(metadata).__name__,
            })
            continue
        mode = metadata.st_mode
        if stat.S_ISLNK(mode):
            try:
                target = os.readlink(path)
                target_sha256 = hashlib.sha256(os.fsencode(target)).hexdigest()
                target_kind = "absolute" if os.path.isabs(target) else "relative"
                disposition = "not-followed"
                error_class = None
            except OSError as error:
                target_sha256 = None
                target_kind = None
                disposition = "unreadable"
                error_class = type(error).__name__
            row: dict[str, Any] = {
                "path": relative,
                "entry_kind": "symlink",
                "disposition": disposition,
            }
            if target_sha256 is not None:
                row["link_target_sha256"] = target_sha256
                row["link_target_kind"] = target_kind
            if error_class is not None:
                row["error_class"] = error_class
            entries.append(row)
        elif stat.S_ISREG(mode):
            entries.append(catalog_file(
                path, relative, contract, extension_index, signed_hints
            ))
        elif stat.S_ISDIR(mode):
            # Readable directories are represented through their descendants. Empty
            # directory identity is deliberately not part of source-content discovery.
            continue
        else:
            entries.append({
                "path": relative,
                "entry_kind": "special",
                "disposition": "not-read",
                "special_kind": (
                    "fifo" if stat.S_ISFIFO(mode)
                    else "socket" if stat.S_ISSOCK(mode)
                    else "character-device" if stat.S_ISCHR(mode)
                    else "block-device" if stat.S_ISBLK(mode)
                    else "other"
                ),
            })

    entries.sort(key=lambda row: row["path"])
    digest_paths: dict[str, list[str]] = defaultdict(list)
    total_bytes = 0
    disposition_counts: Counter[str] = Counter()
    format_counts: Counter[str] = Counter()
    entry_kind_counts: Counter[str] = Counter()
    for row in entries:
        disposition_counts[row["disposition"]] += 1
        entry_kind_counts[row["entry_kind"]] += 1
        if row.get("entry_kind") == "file" and row.get("disposition") == "observed":
            total_bytes += row["byte_count"]
            digest_paths[row["sha256"]].append(row["path"])
            for candidate in row["format_candidates"]:
                format_counts[candidate["id"]] += 1

    duplicate_groups = [
        {"sha256": digest, "paths": sorted(paths), "occurrence_count": len(paths)}
        for digest, paths in sorted(digest_paths.items())
        if len(paths) > 1
    ]
    summary = {
        "entry_count": len(entries),
        "entry_kind_counts": dict(sorted(entry_kind_counts.items())),
        "disposition_counts": dict(sorted(disposition_counts.items())),
        "observed_file_bytes": total_bytes,
        "observed_distinct_file_digests": len(digest_paths),
        "duplicate_content_group_count": len(duplicate_groups),
        "duplicate_content_groups": duplicate_groups,
        "format_candidate_occurrences": dict(sorted(format_counts.items())),
    }
    contract_sha256 = hashlib.sha256(canonical(contract)).hexdigest()
    identity_body = {
        "contract_sha256": contract_sha256,
        "entries": entries,
        "summary": summary,
    }
    return {
        "schema": CATALOG_SCHEMA,
        "status": "discovery-only",
        "catalog_label": label,
        "contract_sha256": contract_sha256,
        "catalog_id": hashlib.sha256(canonical(identity_body)).hexdigest(),
        "entries": entries,
        "summary": summary,
        "nonclaims": [
            "source-authority",
            "release-identity",
            "license-permission",
            "selected-parser",
            "semantic-meaning",
            "trust-or-truth",
            "source-profile-completion",
            "world-admission",
            "foundational-seed-completion",
            "product-activation",
            "release",
        ],
    }


def validate_catalog(catalog: dict[str, Any], contract: dict[str, Any]) -> None:
    validate_contract(contract)
    require(catalog.get("schema") == CATALOG_SCHEMA, "unknown catalog schema")
    require(catalog.get("status") == "discovery-only", "catalog status differs")
    require(isinstance(catalog.get("catalog_label"), str) and catalog["catalog_label"],
            "catalog label is absent")
    contract_sha256 = hashlib.sha256(canonical(contract)).hexdigest()
    require(catalog.get("contract_sha256") == contract_sha256,
            "catalog contract digest differs")
    entries = catalog.get("entries")
    summary = catalog.get("summary")
    require(isinstance(entries, list), "catalog entries are absent")
    require(isinstance(summary, dict), "catalog summary is absent")
    paths = [row.get("path") for row in entries]
    require(all(isinstance(path, str) for path in paths), "catalog path is invalid")
    require(paths == sorted(paths), "catalog paths are not sorted")
    require(len(paths) == len(set(paths)), "catalog paths are duplicated")
    for path in paths:
        canonical_relative(Path(path))
    forbidden_keys = {"absolute_root", "observation_time", "mtime", "uid", "gid"}
    require(not (forbidden_keys & set(catalog)), "catalog contains host-local identity fields")
    identity_body = {
        "contract_sha256": contract_sha256,
        "entries": entries,
        "summary": summary,
    }
    expected = hashlib.sha256(canonical(identity_body)).hexdigest()
    require(catalog.get("catalog_id") == expected, "catalog identity differs")
    nonclaims = catalog.get("nonclaims")
    require(isinstance(nonclaims, list) and "world-admission" in nonclaims
            and "selected-parser" in nonclaims and "trust-or-truth" in nonclaims,
            "catalog nonclaims are incomplete")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path,
                        default=Path("contracts/source-discovery-catalog.json"))
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--label", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--verify", action="store_true",
                        help="verify an existing output instead of rescanning")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    contract = load_json(arguments.contract)
    if arguments.verify:
        try:
            catalog = json.loads(arguments.output.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise CatalogError(f"cannot read catalog: {error}") from error
        require(isinstance(catalog, dict), "catalog must be an object")
        validate_catalog(catalog, contract)
    else:
        catalog = build_catalog(contract, arguments.root, arguments.label)
        validate_catalog(catalog, contract)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(
            json.dumps(catalog, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    print(json.dumps({
        "catalog_id": catalog["catalog_id"],
        "entry_count": catalog["summary"]["entry_count"],
        "observed_file_bytes": catalog["summary"]["observed_file_bytes"],
        "duplicate_content_group_count": catalog["summary"]["duplicate_content_group_count"],
        "status": catalog["status"],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CatalogError as error:
        print(f"source-catalog: {error}", file=sys.stderr)
        raise SystemExit(1) from error
