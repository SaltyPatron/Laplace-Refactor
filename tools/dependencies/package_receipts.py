#!/usr/bin/env python3
"""Canonical verification for packaged build-tool and staged-tree receipts."""

from __future__ import annotations

import hashlib
import json
import os
import re
import stat
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, Sequence


class ReceiptError(RuntimeError):
    pass


def reject_duplicate_object_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ReceiptError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_object_keys,
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ReceiptError(f"cannot read JSON document {path}: {error}") from error
    if not isinstance(value, dict):
        raise ReceiptError(f"JSON document must be an object: {path}")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def require_string(value: object, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise ReceiptError(f"{field} must be a non-empty string")
    return value


def path_is_within(path: Path, prefix: Path) -> bool:
    try:
        path.resolve().relative_to(prefix.resolve())
    except ValueError:
        return False
    return True


def verify_toolchain_package_receipt(
    expected: Mapping[str, Any], receipt_path: Path
) -> dict[str, Any]:
    receipt = read_json(receipt_path)
    if receipt.get("schema") != expected.get("receipt_schema"):
        raise ReceiptError("toolchain receipt schema mismatch")
    build_input_id = require_string(receipt.get("build_input_id"), "toolchain.build_input_id")
    if not re.fullmatch(r"[0-9a-f]{64}", build_input_id):
        raise ReceiptError("toolchain build_input_id must be lowercase SHA-256")
    package = receipt.get("package")
    manifest = receipt.get("consumer_manifest")
    activation = receipt.get("activation")
    if not all(isinstance(item, dict) for item in (package, manifest, activation)):
        raise ReceiptError(
            "toolchain receipt package, consumer_manifest, and activation are required"
        )
    prefix = Path(require_string(package.get("prefix"), "toolchain.package.prefix"))
    if not prefix.is_absolute() or not prefix.is_dir() or prefix.is_symlink():
        raise ReceiptError(
            "toolchain package prefix must be an existing physical absolute directory"
        )
    if manifest.get("schema") != expected.get("consumer_manifest_schema"):
        raise ReceiptError("toolchain consumer manifest schema mismatch")
    if manifest.get("build_input_id") != build_input_id:
        raise ReceiptError("toolchain receipt and consumer manifest build_input_id differ")
    if manifest.get("prefix") != str(prefix):
        raise ReceiptError("toolchain receipt and consumer manifest prefix differ")
    if activation.get("scope") != "build-toolchain-only":
        raise ReceiptError("toolchain activation scope must be build-toolchain-only")
    if activation.get("product_runtime_activation_eligible") is not False:
        raise ReceiptError("build toolchain cannot be product-runtime activation eligible")
    tools = manifest.get("tools")
    required_tools = expected.get("required_tools")
    if not isinstance(tools, dict) or not isinstance(required_tools, list):
        raise ReceiptError("toolchain tools and required_tools must be present")
    selected: dict[str, dict[str, str]] = {}
    for name in required_tools:
        if not isinstance(name, str) or not name:
            raise ReceiptError("toolchain required tool name is invalid")
        tool = tools.get(name)
        if not isinstance(tool, dict):
            raise ReceiptError(f"toolchain consumer manifest omits required tool: {name}")
        path = Path(require_string(tool.get("path"), f"toolchain.tools.{name}.path"))
        digest = require_string(tool.get("sha256"), f"toolchain.tools.{name}.sha256")
        version = require_string(tool.get("version"), f"toolchain.tools.{name}.version")
        if not path.is_absolute() or not path_is_within(path, prefix):
            raise ReceiptError(f"toolchain tool is outside its package prefix: {name}")
        if not path.is_file() or not os.access(path, os.X_OK):
            raise ReceiptError(f"toolchain tool is not executable: {name}")
        if not re.fullmatch(r"[0-9a-f]{64}", digest) or sha256_file(path) != digest:
            raise ReceiptError(f"toolchain tool digest mismatch: {name}")
        selected[name] = {"path": str(path), "sha256": digest, "version": version}
    return {
        "receipt_path": str(receipt_path.resolve()),
        "receipt_sha256": sha256_file(receipt_path),
        "build_input_id": build_input_id,
        "prefix": str(prefix),
        "tools": selected,
    }


def safe_relative_path(value: object, field: str) -> str:
    text = require_string(value, field)
    path = PurePosixPath(text)
    if path.is_absolute() or ".." in path.parts or text != path.as_posix():
        raise ReceiptError(f"{field} must be a normalized package-relative path")
    return text


def verify_recorded_package_tree(
    prefix: Path,
    records: Sequence[object],
    expected_tree_sha256: object,
    *,
    allow_additions: bool = False,
) -> dict[str, int | str]:
    if not prefix.is_absolute() or not prefix.is_dir() or prefix.is_symlink():
        raise ReceiptError("recorded package prefix must be an existing physical absolute directory")
    if not isinstance(records, list) or not records:
        raise ReceiptError("recorded package files must be a non-empty array")
    expected_digest = require_string(expected_tree_sha256, "package.tree_sha256")
    if not re.fullmatch(r"[0-9a-f]{64}", expected_digest):
        raise ReceiptError("package.tree_sha256 must be lowercase SHA-256")
    observed_paths = [
        path.relative_to(prefix).as_posix()
        for path in sorted(prefix.rglob("*"), key=lambda item: item.relative_to(prefix).as_posix())
    ]
    recorded_paths: list[str] = []
    digest = hashlib.sha256()
    file_count = 0
    total_file_bytes = 0
    for index, item in enumerate(records):
        if not isinstance(item, dict):
            raise ReceiptError(f"package.files[{index}] must be an object")
        relative = safe_relative_path(item.get("path"), f"package.files[{index}].path")
        recorded_paths.append(relative)
        candidate = prefix / relative
        kind = item.get("kind")
        mode = item.get("mode")
        if not isinstance(mode, str) or not re.fullmatch(r"[0-7]{4}", mode):
            raise ReceiptError(f"package.files[{index}].mode is invalid")
        if not candidate.exists() and not candidate.is_symlink():
            raise ReceiptError(f"recorded package path is absent: {relative}")
        if f"{stat.S_IMODE(candidate.lstat().st_mode):04o}" != mode:
            raise ReceiptError(f"recorded package mode differs: {relative}")
        if kind == "file":
            if not candidate.is_file() or candidate.is_symlink():
                raise ReceiptError(f"recorded package file kind differs: {relative}")
            size = candidate.stat().st_size
            if item.get("size") != size or item.get("sha256") != sha256_file(candidate):
                raise ReceiptError(f"recorded package file bytes differ: {relative}")
            file_count += 1
            total_file_bytes += size
        elif kind == "symlink":
            if not candidate.is_symlink() or item.get("target") != os.readlink(candidate):
                raise ReceiptError(f"recorded package symlink differs: {relative}")
        elif kind == "directory":
            if not candidate.is_dir() or candidate.is_symlink():
                raise ReceiptError(f"recorded package directory kind differs: {relative}")
        else:
            raise ReceiptError(f"recorded package kind is invalid: {relative}")
        encoded = json.dumps(item, sort_keys=True, separators=(",", ":")).encode("utf-8")
        digest.update(len(encoded).to_bytes(8, "big"))
        digest.update(encoded)
    if recorded_paths != sorted(recorded_paths) or len(recorded_paths) != len(set(recorded_paths)):
        raise ReceiptError("recorded package paths must be unique and sorted")
    if allow_additions:
        if not set(recorded_paths).issubset(observed_paths):
            raise ReceiptError("recorded package paths are missing from the physical package tree")
    elif recorded_paths != observed_paths:
        raise ReceiptError("recorded package paths differ from the physical package tree")
    if digest.hexdigest() != expected_digest:
        raise ReceiptError("recorded package tree digest differs")
    return {
        "tree_sha256": expected_digest,
        "entry_count": len(records),
        "file_count": file_count,
        "total_file_bytes": total_file_bytes,
    }
