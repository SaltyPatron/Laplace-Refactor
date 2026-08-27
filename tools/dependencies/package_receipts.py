#!/usr/bin/env python3
"""Canonical verification for packaged build-tool and staged-tree receipts."""

from __future__ import annotations

import hashlib
import json
import os
import re
import stat
import subprocess
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
    modules = manifest.get("perl_modules")
    required_modules = expected.get("required_perl_modules")
    if not isinstance(modules, dict) or not isinstance(required_modules, dict):
        raise ReceiptError(
            "toolchain Perl modules and required_perl_modules must be present"
        )
    if set(modules) != set(required_modules):
        raise ReceiptError("toolchain consumer manifest Perl module set differs")
    selected_modules: dict[str, dict[str, Any]] = {}
    for name, required_version in required_modules.items():
        if not isinstance(name, str) or not name or not isinstance(required_version, str):
            raise ReceiptError("toolchain required Perl module declaration is invalid")
        module = modules.get(name)
        if not isinstance(module, dict):
            raise ReceiptError(f"toolchain consumer manifest omits Perl module: {name}")
        path = Path(require_string(module.get("path"), f"toolchain.perl_modules.{name}.path"))
        digest = require_string(
            module.get("sha256"), f"toolchain.perl_modules.{name}.sha256"
        )
        version = require_string(
            module.get("version"), f"toolchain.perl_modules.{name}.version"
        )
        if version != required_version:
            raise ReceiptError(f"toolchain Perl module version differs: {name}")
        if not path.is_absolute() or not path_is_within(path, prefix):
            raise ReceiptError(f"toolchain Perl module is outside its package prefix: {name}")
        if not path.is_file() or path.is_symlink():
            raise ReceiptError(f"toolchain Perl module provider is not a file: {name}")
        if not re.fullmatch(r"[0-9a-f]{64}", digest) or sha256_file(path) != digest:
            raise ReceiptError(f"toolchain Perl module digest mismatch: {name}")
        native = module.get("native_providers")
        if not isinstance(native, list):
            raise ReceiptError(f"toolchain Perl module native provider list is invalid: {name}")
        native_receipts: list[dict[str, str]] = []
        for index, provider in enumerate(native):
            if not isinstance(provider, dict):
                raise ReceiptError(
                    f"toolchain Perl module native provider is invalid: {name}[{index}]"
                )
            provider_path = Path(
                require_string(
                    provider.get("path"),
                    f"toolchain.perl_modules.{name}.native_providers[{index}].path",
                )
            )
            provider_digest = require_string(
                provider.get("sha256"),
                f"toolchain.perl_modules.{name}.native_providers[{index}].sha256",
            )
            if not provider_path.is_absolute() or not path_is_within(provider_path, prefix):
                raise ReceiptError(
                    f"toolchain Perl native provider is outside its package prefix: {name}"
                )
            if not provider_path.is_file() or provider_path.is_symlink():
                raise ReceiptError(
                    f"toolchain Perl native provider is not a file: {name}"
                )
            if (
                not re.fullmatch(r"[0-9a-f]{64}", provider_digest)
                or sha256_file(provider_path) != provider_digest
            ):
                raise ReceiptError(
                    f"toolchain Perl native provider digest mismatch: {name}"
                )
            native_receipts.append(
                {"path": str(provider_path), "sha256": provider_digest}
            )
        selected_modules[name] = {
            "path": str(path),
            "sha256": digest,
            "version": version,
            "native_providers": native_receipts,
            "source_component": require_string(
                module.get("source_component"),
                f"toolchain.perl_modules.{name}.source_component",
            ),
        }
    linker_inputs = receipt.get("linker_map_inputs")
    required_linker_inputs = expected.get("required_linker_map_inputs", {})
    if not isinstance(linker_inputs, list) or not isinstance(required_linker_inputs, dict):
        raise ReceiptError("toolchain linker-map inputs and requirements must be present")
    indexed_linker_inputs: dict[str, dict[str, Any]] = {}
    for index, item in enumerate(linker_inputs):
        if not isinstance(item, dict):
            raise ReceiptError(f"toolchain linker_map_inputs[{index}] must be an object")
        raw_path = require_string(
            item.get("path"), f"toolchain.linker_map_inputs[{index}].path"
        )
        path = Path(raw_path)
        digest = require_string(
            item.get("sha256"), f"toolchain.linker_map_inputs[{index}].sha256"
        )
        size = item.get("size_bytes")
        if raw_path in indexed_linker_inputs:
            raise ReceiptError(f"toolchain linker-map input path is duplicated: {raw_path}")
        if not path.is_absolute() or not isinstance(size, int) or size <= 0:
            raise ReceiptError(f"toolchain linker-map input identity is invalid: {raw_path}")
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ReceiptError(f"toolchain linker-map input digest is invalid: {raw_path}")
        indexed_linker_inputs[raw_path] = {
            "path": raw_path,
            "sha256": digest,
            "size_bytes": size,
        }
    selected_linker_inputs: dict[str, dict[str, Any]] = {}
    for role, requirement in required_linker_inputs.items():
        if not isinstance(role, str) or not role or not isinstance(requirement, dict):
            raise ReceiptError("toolchain required linker-map input declaration is invalid")
        raw_path = require_string(
            requirement.get("path"), f"toolchain.required_linker_map_inputs.{role}.path"
        )
        selected_input = indexed_linker_inputs.get(raw_path)
        if selected_input is None:
            raise ReceiptError(f"toolchain receipt omits required linker-map input: {role}")
        path = Path(raw_path)
        if not path.is_file() or path.is_symlink():
            raise ReceiptError(f"toolchain linker-map input is not a physical file: {role}")
        if path.stat().st_size != selected_input["size_bytes"]:
            raise ReceiptError(f"toolchain linker-map input size differs: {role}")
        if sha256_file(path) != selected_input["sha256"]:
            raise ReceiptError(f"toolchain linker-map input digest differs: {role}")
        selected_linker_inputs[role] = {
            **selected_input,
            "soname": require_string(
                requirement.get("soname"),
                f"toolchain.required_linker_map_inputs.{role}.soname",
            ),
            "classification": require_string(
                requirement.get("classification"),
                f"toolchain.required_linker_map_inputs.{role}.classification",
            ),
        }
        package_relative_path = requirement.get("package_relative_path")
        if package_relative_path is not None:
            selected_linker_inputs[role]["package_relative_path"] = safe_relative_path(
                package_relative_path,
                f"toolchain.required_linker_map_inputs.{role}.package_relative_path",
            )
        aliases = requirement.get("package_aliases", [])
        if not isinstance(aliases, list):
            raise ReceiptError(f"toolchain required linker-map aliases are invalid: {role}")
        selected_linker_inputs[role]["package_aliases"] = [
            safe_relative_path(
                alias,
                f"toolchain.required_linker_map_inputs.{role}.package_aliases[{index}]",
            )
            for index, alias in enumerate(aliases)
        ]
    return {
        "receipt_path": str(receipt_path.resolve()),
        "receipt_sha256": sha256_file(receipt_path),
        "build_input_id": build_input_id,
        "prefix": str(prefix),
        "tools": selected,
        "perl_modules": selected_modules,
        "linker_map_inputs": selected_linker_inputs,
    }


def safe_relative_path(value: object, field: str) -> str:
    text = require_string(value, field)
    path = PurePosixPath(text)
    if path.is_absolute() or ".." in path.parts or text != path.as_posix():
        raise ReceiptError(f"{field} must be a normalized package-relative path")
    return text


def verify_installed_provider_selection(
    expected: Mapping[str, Any], lock_path: Path, readelf: Path
) -> dict[str, Any]:
    """Select exact installed-provider roles without promoting installation to authority."""
    document = read_json(lock_path)
    if document.get("schema") != expected.get("lock_schema"):
        raise ReceiptError("installed provider lock schema mismatch")
    if document.get("selection") != "installed-bytes-selected-not-product-runtime-activated":
        raise ReceiptError("installed provider selection crossed the activation boundary")
    providers = document.get("providers")
    if not isinstance(providers, dict):
        raise ReceiptError("installed provider lock omits its provider map")
    selection_sha256 = require_string(
        document.get("provider_selection_sha256"), "installed_provider.provider_selection_sha256"
    )
    observed_selection_sha256 = hashlib.sha256(
        json.dumps(
            providers, ensure_ascii=False, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
    ).hexdigest()
    if (
        selection_sha256 != expected.get("provider_selection_sha256")
        or selection_sha256 != observed_selection_sha256
    ):
        raise ReceiptError("installed provider selection fingerprint differs")
    provider_id = require_string(expected.get("provider"), "installed_provider.provider")
    provider = providers.get(provider_id)
    if not isinstance(provider, dict):
        raise ReceiptError(f"installed provider lock omits selected provider: {provider_id}")
    provider_sha256 = hashlib.sha256(
        json.dumps(
            provider, ensure_ascii=False, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
    ).hexdigest()
    if provider_sha256 != expected.get("provider_sha256"):
        raise ReceiptError(f"installed provider exact identity differs: {provider_id}")
    if provider.get("version") != expected.get("version"):
        raise ReceiptError(f"installed provider version differs: {provider_id}")
    root = Path(require_string(provider.get("immutable_root"), "installed_provider.immutable_root"))
    if not root.is_absolute() or "latest" in root.parts:
        raise ReceiptError("installed provider root must be absolute and immutable")
    files = provider.get("files")
    requirements = expected.get("required_files")
    if not isinstance(files, list) or not isinstance(requirements, dict) or not requirements:
        raise ReceiptError("installed provider file selection is incomplete")
    by_role: dict[str, dict[str, Any]] = {}
    for index, item in enumerate(files):
        if not isinstance(item, dict):
            raise ReceiptError(f"installed provider files[{index}] must be an object")
        role = require_string(item.get("role"), f"installed_provider.files[{index}].role")
        if role in by_role:
            raise ReceiptError(f"installed provider file role is duplicated: {role}")
        by_role[role] = item
    selected: dict[str, dict[str, Any]] = {}
    for role, requirement in requirements.items():
        if not isinstance(role, str) or not role or not isinstance(requirement, dict):
            raise ReceiptError("installed provider required file declaration is invalid")
        item = by_role.get(role)
        if item is None:
            raise ReceiptError(f"installed provider omits required file role: {role}")
        file_class = require_string(item.get("class"), f"installed_provider.{role}.class")
        if file_class != requirement.get("class"):
            raise ReceiptError(f"installed provider file class differs: {role}")
        path = Path(require_string(item.get("path"), f"installed_provider.{role}.path"))
        if not path.is_absolute() or not path_is_within(path, root) or "latest" in path.parts:
            raise ReceiptError(f"installed provider file escapes its immutable root: {role}")
        digest = require_string(item.get("sha256"), f"installed_provider.{role}.sha256")
        size = item.get("bytes")
        if (
            not re.fullmatch(r"[0-9a-f]{64}", digest)
            or not isinstance(size, int)
            or size <= 0
            or not path.is_file()
            or path.is_symlink()
            or path.stat().st_size != size
            or sha256_file(path) != digest
        ):
            raise ReceiptError(f"installed provider file bytes differ: {role}")
        selected_item: dict[str, Any] = {
            "path": str(path),
            "sha256": digest,
            "size_bytes": size,
            "class": file_class,
            "package_relative_path": safe_relative_path(
                requirement.get("package_relative_path"),
                f"installed_provider.required_files.{role}.package_relative_path",
            ),
        }
        if file_class == "runtime-object":
            soname = require_string(item.get("soname"), f"installed_provider.{role}.soname")
            result = subprocess.run(
                [str(readelf), "-d", str(path)], text=True, capture_output=True
            )
            observed_sonames = re.findall(r"\(SONAME\).*\[([^]]+)\]", result.stdout)
            if result.returncode != 0 or observed_sonames != [soname]:
                raise ReceiptError(f"installed provider runtime SONAME differs: {role}")
            selected_item["soname"] = soname
        selected[role] = selected_item
    boundaries = document.get("authority_boundaries")
    if not isinstance(boundaries, dict) or boundaries.get(
        "selection-activates-product-runtime"
    ) is not False or boundaries.get(
        "loaded-runtime-object-closure-requires-package-receipt"
    ) is not True:
        raise ReceiptError("installed provider authority boundary differs")
    return {
        "lock_path": str(lock_path.resolve()),
        "lock_sha256": sha256_file(lock_path),
        "provider_selection_sha256": selection_sha256,
        "provider": provider_id,
        "provider_sha256": provider_sha256,
        "version": provider["version"],
        "files": selected,
        "selection_activates_product_runtime": False,
    }


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
