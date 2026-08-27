#!/usr/bin/env python3
"""Compose PostgreSQL and the native Laplace engine into one immutable product package."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, Sequence


CONTRACT_SCHEMA = "laplace.product-package-contract/v1"
MANIFEST_SCHEMA = "laplace.package-manifest/v1"
RECEIPT_SCHEMA = "laplace.product-package-receipt/v1"
PLAN_SCHEMA = "laplace.product-package-plan/v1"
SELECTION_SCHEMA = "laplace.product-package-selection/v1"
PUBLICATION_SCHEMA = "laplace.postgresql-package-publication-receipt/v1"
QUALIFICATION_SCHEMA = "laplace.runtime-provider-qualification/v1"
HEX_256 = re.compile(r"^[0-9a-f]{64}$")
RUNPATH_PATTERN = re.compile(r"\((?:RUNPATH|RPATH)\).*\[([^]]*)\]")


class ProductPackageError(RuntimeError):
    pass


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    output: dict[str, Any] = {}
    for key, value in pairs:
        if key in output:
            raise ProductPackageError(f"duplicate JSON key: {key}")
        output[key] = value
    return output


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ProductPackageError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ProductPackageError(f"JSON root must be an object: {path}")
    return value


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def canonical_sha256(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def require_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise ProductPackageError(f"{field} must be a non-empty string")
    return value


def require_absolute(value: Any, field: str) -> Path:
    path = Path(require_string(value, field))
    if not path.is_absolute() or ".." in path.parts:
        raise ProductPackageError(f"{field} must be a normalized absolute path")
    return path


def require_relative(value: Any, field: str) -> str:
    text = require_string(value, field)
    path = PurePosixPath(text)
    if path.is_absolute() or ".." in path.parts or str(path) != text:
        raise ProductPackageError(f"{field} must be a normalized relative path")
    return text


def validate_contract(contract: dict[str, Any]) -> None:
    if contract.get("schema") != CONTRACT_SCHEMA:
        raise ProductPackageError(f"contract schema must be {CONTRACT_SCHEMA}")
    postgresql = contract.get("postgresql")
    laplace = contract.get("laplace")
    build = contract.get("build")
    package = contract.get("package")
    closure = contract.get("runtime_closure")
    host_provider = contract.get("host_build_provider")
    activation = contract.get("activation")
    if not all(
        isinstance(item, dict)
        for item in (
            postgresql,
            laplace,
            build,
            package,
            closure,
            host_provider,
            activation,
        )
    ):
        raise ProductPackageError("product package contract sections must be objects")
    if postgresql.get("receipt_schema") != "laplace.postgresql-package-receipt/v2":
        raise ProductPackageError("PostgreSQL package receipt schema is invalid")
    if postgresql.get("publication_receipt_schema") != PUBLICATION_SCHEMA:
        raise ProductPackageError("PostgreSQL publication receipt schema is invalid")
    if postgresql.get("version") != "PostgreSQL 18.6":
        raise ProductPackageError("product package must select exact PostgreSQL 18.6")
    if postgresql.get("logical_prefix") != "/opt/laplace/current/pgsql-18":
        raise ProductPackageError("PostgreSQL logical prefix is invalid")
    if laplace.get("logical_prefix") != "/opt/laplace/current":
        raise ProductPackageError("Laplace logical prefix is invalid")
    providers = laplace.get("required_installed_providers")
    if providers != ["intel-oneapi-runtime", "onetbb", "onemkl"]:
        raise ProductPackageError("complete selected oneAPI provider set is required")
    runtime_dependency_names = laplace.get("required_runtime_dependencies")
    if runtime_dependency_names != ["onetbb-hwloc"]:
        raise ProductPackageError("complete selected runtime dependency set is required")
    runtime_dependencies = contract.get("runtime_dependencies")
    if (
        not isinstance(runtime_dependencies, dict)
        or list(runtime_dependencies) != runtime_dependency_names
    ):
        raise ProductPackageError("runtime dependency map differs from the required set")
    for provider_name, provider in runtime_dependencies.items():
        if not isinstance(provider, dict):
            raise ProductPackageError(f"runtime dependency is invalid: {provider_name}")
        immutable_root = require_absolute(
            provider.get("immutable_root"),
            f"runtime_dependencies.{provider_name}.immutable_root",
        )
        if "latest" in immutable_root.parts:
            raise ProductPackageError(f"runtime dependency is not versioned: {provider_name}")
        for field in ("version", "license_id"):
            require_string(provider.get(field), f"runtime_dependencies.{provider_name}.{field}")
        package_versions = provider.get("package_versions")
        if not isinstance(package_versions, dict) or not package_versions or any(
            not isinstance(name, str)
            or not name
            or not isinstance(version, str)
            or not version
            for name, version in package_versions.items()
        ):
            raise ProductPackageError(
                f"runtime dependency package versions are invalid: {provider_name}"
            )
        files = provider.get("files")
        if not isinstance(files, list) or not files:
            raise ProductPackageError(f"runtime dependency files are absent: {provider_name}")
        roles: set[str] = set()
        paths: set[str] = set()
        classes: set[str] = set()
        for item in files:
            if not isinstance(item, dict):
                raise ProductPackageError(f"runtime dependency file is invalid: {provider_name}")
            role = require_string(item.get("role"), f"runtime dependency {provider_name} role")
            item_class = item.get("class")
            path = require_absolute(item.get("path"), f"runtime dependency {provider_name} path")
            digest = require_string(item.get("sha256"), f"runtime dependency {provider_name} sha256")
            size = item.get("bytes")
            if role in roles or str(path) in paths:
                raise ProductPackageError(f"runtime dependency repeats a role or path: {provider_name}")
            if item_class not in {"runtime-object", "license"}:
                raise ProductPackageError(f"runtime dependency file class is invalid: {provider_name}")
            if (
                not path.is_relative_to(immutable_root)
                or not isinstance(size, int)
                or size <= 0
                or HEX_256.fullmatch(digest) is None
            ):
                raise ProductPackageError(f"runtime dependency file identity is invalid: {path}")
            if item_class == "runtime-object":
                require_string(item.get("soname"), f"runtime dependency {provider_name} SONAME")
            elif "soname" in item:
                raise ProductPackageError(f"runtime dependency license declares a SONAME: {path}")
            roles.add(role)
            paths.add(str(path))
            classes.add(item_class)
        if classes != {"runtime-object", "license"}:
            raise ProductPackageError(
                f"runtime dependency requires runtime and license evidence: {provider_name}"
            )
    capabilities = laplace.get("required_capabilities")
    if not isinstance(capabilities, dict) or any(
        not isinstance(name, str) or not isinstance(version, int) or version <= 0
        for name, version in capabilities.items()
    ):
        raise ProductPackageError("Laplace capabilities are invalid")
    for field in (
        "root",
        "stage_root",
        "c_compiler",
        "cxx_compiler",
        "blake3_root",
        "blake3_source",
    ):
        require_absolute(build.get(field), f"build.{field}")
    if (
        build.get("root") != "/build/laplace/runner/product/build"
        or build.get("stage_root") != "/build/laplace/runner/product/stage"
    ):
        raise ProductPackageError(
            "product build and stage roots must use the declared CI runner workspace"
        )
    blake3_root = Path(build["blake3_root"])
    blake3_source = Path(build["blake3_source"])
    if not blake3_source.is_relative_to(blake3_root) or blake3_source == blake3_root:
        raise ProductPackageError("BLAKE3 source must be contained by its repository root")
    if build.get("configuration") != "Release":
        raise ProductPackageError("product package must use the Release configuration")
    if build.get("install_libdir") != "lib":
        raise ProductPackageError("product package must use the canonical lib directory")
    if build.get("testing") is not False or build.get("dotnet_bindings") is not False:
        raise ProductPackageError("runtime product package cannot include test or SDK builds")
    if not isinstance(build.get("parallel_jobs"), int) or not 1 <= build["parallel_jobs"] <= 64:
        raise ProductPackageError("build.parallel_jobs is invalid")
    if host_provider.get("receipt_schema") != "laplace.postgresql-host-build-provider/v1":
        raise ProductPackageError("host build provider receipt schema is invalid")
    require_relative(host_provider.get("verifier"), "host_build_provider.verifier")
    if host_provider.get("sandbox_executable") != "/usr/bin/bwrap":
        raise ProductPackageError("product build sandbox executable is invalid")
    if host_provider.get("additional_receipted_files") != [
        "/var/lib/dpkg/status"
    ]:
        raise ProductPackageError("additional host build-provider files are invalid")
    if package.get("manifest_schema") != MANIFEST_SCHEMA:
        raise ProductPackageError("product package manifest schema is invalid")
    if package.get("release_root") != "/opt/laplace/releases":
        raise ProductPackageError("product package release root is invalid")
    for field in ("required_loaded_objects", "required_files"):
        values = package.get(field)
        if not isinstance(values, list) or not values or len(values) != len(set(values)):
            raise ProductPackageError(f"package.{field} must be a non-empty unique array")
        for index, value in enumerate(values):
            require_relative(value, f"package.{field}[{index}]")
    if not set(package["required_loaded_objects"]).issubset(package["required_files"]):
        raise ProductPackageError("loaded objects must be required package files")
    if "bin/laplace_resource_observe" not in package["required_files"]:
        raise ProductPackageError("package must contain the native resource observer")
    if "bin/laplace_unicode_activation_identify" not in package["required_files"]:
        raise ProductPackageError(
            "package must contain the native Unicode activation identity provider"
        )
    if closure.get("schema") != "laplace.elf-closure/v1" or closure.get(
        "tool_version"
    ) != "1.0.0":
        raise ProductPackageError("recursive closure verifier contract is invalid")
    require_relative(closure.get("verifier"), "runtime_closure.verifier")
    for field in ("search_subdirectories", "required_zero_summary_fields"):
        values = closure.get(field)
        if not isinstance(values, list) or not values or any(
            not isinstance(item, str) or not item for item in values
        ):
            raise ProductPackageError(f"runtime_closure.{field} is invalid")
    if closure.get("allowed_additional_platform_abi_sonames") != [
        "libdl.so.2",
        "libpthread.so.0",
        "librt.so.1",
    ]:
        raise ProductPackageError("product platform ABI expansion is invalid")
    required_zero = {
        "unresolved_edge_count",
        "parse_error_count",
        "discovery_error_count",
        "resolution_conflict_count",
        "root_abi_family_collision_count",
        "custom_to_external_edge_count",
    }
    if set(closure["required_zero_summary_fields"]) != required_zero:
        raise ProductPackageError("product recursive closure zero gates are incomplete")
    if set(activation) != {
        "requires_postgresql_build_input_closure",
        "requires_postgresql_runtime_provider_qualification",
        "requires_recursive_elf_closure",
        "requires_product_build_input_closure",
        "requires_clean_repository",
    } or any(value is not True for value in activation.values()):
        raise ProductPackageError("activation gates must remain explicit and fail closed")


def git_output(repository: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments], cwd=repository, text=True, capture_output=True
    )
    if result.returncode != 0:
        raise ProductPackageError(result.stderr.strip() or "git command failed")
    return result.stdout.strip()


def repository_identity(repository: Path, require_clean: bool) -> dict[str, Any]:
    status = subprocess.run(
        ["git", "status", "--porcelain=v1", "-z", "--untracked-files=all"],
        cwd=repository,
        check=True,
        stdout=subprocess.PIPE,
    ).stdout
    if require_clean and status:
        raise ProductPackageError("product build requires a clean repository")
    identity: dict[str, Any] = {
        "commit": git_output(repository, "rev-parse", "HEAD"),
        "tree": git_output(repository, "rev-parse", "HEAD^{tree}"),
        "clean": not bool(status),
    }
    if status:
        tracked_patch = subprocess.run(
            ["git", "diff", "--binary", "HEAD", "--"],
            cwd=repository,
            check=True,
            stdout=subprocess.PIPE,
        ).stdout
        untracked: list[dict[str, Any]] = []
        for record in status.split(b"\0"):
            if not record or not record.startswith(b"?? "):
                continue
            relative = record[3:].decode("utf-8", errors="surrogateescape")
            path = repository / relative
            if path.is_symlink():
                target = os.readlink(path)
                untracked.append(
                    {
                        "path": relative,
                        "kind": "symlink",
                        "sha256": hashlib.sha256(target.encode("utf-8")).hexdigest(),
                    }
                )
            elif path.is_file():
                untracked.append(
                    {"path": relative, "kind": "file", "sha256": sha256_file(path)}
                )
            else:
                raise ProductPackageError(
                    f"dirty product build has unsupported untracked object: {relative}"
                )
        dirty_payload = {
            "porcelain_sha256": hashlib.sha256(status).hexdigest(),
            "tracked_patch_sha256": hashlib.sha256(tracked_patch).hexdigest(),
            "untracked": sorted(untracked, key=lambda item: item["path"]),
        }
        identity["dirty_fingerprint"] = dirty_payload
        identity["dirty_sha256"] = canonical_sha256(dirty_payload)
    return identity


def verify_receipted_file(record: Mapping[str, Any], field: str) -> Path:
    path = require_absolute(record.get("path"), f"{field}.path")
    digest = require_string(record.get("sha256"), f"{field}.sha256")
    if (
        HEX_256.fullmatch(digest) is None
        or not path.is_file()
        or path.is_symlink()
        or sha256_file(path) != digest
    ):
        raise ProductPackageError(f"selected {field} bytes differ: {path}")
    return path


def published_tree_receipt(root: Path) -> dict[str, Any]:
    if not root.is_dir() or root.is_symlink():
        raise ProductPackageError(f"published package root must be physical: {root}")
    digest = hashlib.sha256()
    file_count = 0
    symlink_count = 0
    directory_count = 0
    total_file_bytes = 0
    for path in sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix()):
        relative_text = path.relative_to(root).as_posix()
        relative = relative_text.encode("utf-8")
        mode = stat.S_IMODE(path.lstat().st_mode)
        if path.is_symlink():
            kind = b"symlink"
            target = os.readlink(path)
            target_path = Path(target)
            if target_path.is_absolute():
                raise ProductPackageError(
                    f"published package has an absolute symlink: {relative_text}"
                )
            resolved = (path.parent / target_path).resolve(strict=False)
            try:
                resolved.relative_to(root.resolve())
            except ValueError as error:
                raise ProductPackageError(
                    f"published package symlink escapes its root: {relative_text}"
                ) from error
            if not resolved.exists():
                raise ProductPackageError(
                    f"published package symlink is broken: {relative_text}"
                )
            content = target.encode("utf-8")
            symlink_count += 1
        elif path.is_file():
            kind = b"file"
            content = sha256_file(path).encode("ascii")
            file_count += 1
            total_file_bytes += path.stat().st_size
        elif path.is_dir():
            kind = b"directory"
            content = b""
            directory_count += 1
        else:
            raise ProductPackageError(
                f"published package has an unsupported object: {relative_text}"
            )
        for field in (relative, kind, str(mode).encode("ascii"), content):
            digest.update(len(field).to_bytes(8, "big"))
            digest.update(field)
    return {
        "tree_sha256": digest.hexdigest(),
        "file_count": file_count,
        "symlink_count": symlink_count,
        "directory_count": directory_count,
        "total_file_bytes": total_file_bytes,
    }


def verify_postgresql_publication(
    contract: Mapping[str, Any], publication_path: Path
) -> tuple[dict[str, Any], dict[str, Any]]:
    if not publication_path.is_file() or publication_path.is_symlink():
        raise ProductPackageError("PostgreSQL publication receipt must be a physical file")
    publication = load_json(publication_path)
    if publication.get("schema") != contract["postgresql"][
        "publication_receipt_schema"
    ]:
        raise ProductPackageError("PostgreSQL publication receipt schema differs")
    recorded_path = require_absolute(
        publication.get("receipt_path"), "postgresql_publication.receipt_path"
    )
    if recorded_path != publication_path.resolve():
        raise ProductPackageError("PostgreSQL publication receipt path differs")
    self_digest = require_string(
        publication.get("receipt_sha256"), "postgresql_publication.receipt_sha256"
    )
    payload = dict(publication)
    payload.pop("receipt_sha256", None)
    if HEX_256.fullmatch(self_digest) is None or canonical_sha256(payload) != self_digest:
        raise ProductPackageError("PostgreSQL publication receipt self-digest differs")
    root = require_absolute(
        publication.get("publication_root"), "postgresql_publication.publication_root"
    )
    if not root.is_dir() or root.is_symlink():
        raise ProductPackageError("PostgreSQL publication root is absent")
    source_record = publication.get("source_receipt")
    postgresql_record = publication.get("postgresql")
    toolchain_record = publication.get("toolchain")
    host_record = publication.get("host_provider")
    if not all(
        isinstance(item, dict)
        for item in (source_record, postgresql_record, toolchain_record, host_record)
    ):
        raise ProductPackageError("PostgreSQL publication receipt is incomplete")
    source_path = require_absolute(
        source_record.get("path"), "postgresql_publication.source_receipt.path"
    )
    source_sha256 = require_string(
        source_record.get("sha256"), "postgresql_publication.source_receipt.sha256"
    )
    if (
        not source_path.is_relative_to(root)
        or not source_path.is_file()
        or source_path.is_symlink()
        or HEX_256.fullmatch(source_sha256) is None
        or sha256_file(source_path) != source_sha256
    ):
        raise ProductPackageError("published PostgreSQL source receipt bytes differ")
    source = load_json(source_path)
    if (
        source.get("schema") != contract["postgresql"]["receipt_schema"]
        or source.get("version") != contract["postgresql"]["version"]
        or source.get("build_input_id") != publication.get("build_input_id")
        or source.get("tree_sha256") != publication.get("postgresql_tree_sha256")
        or source.get("activation_eligible") is not True
        or source.get("build_input_closure_complete") is not True
        or source.get("runtime_provider_qualification_complete") is not True
        or source.get("recursive_elf_closure_verified") is not True
    ):
        raise ProductPackageError("published PostgreSQL source proof state differs")
    for name, record in (
        ("postgresql", postgresql_record),
        ("toolchain", toolchain_record),
    ):
        prefix = require_absolute(
            record.get("prefix"), f"postgresql_publication.{name}.prefix"
        )
        if not prefix.is_relative_to(root):
            raise ProductPackageError(f"published {name} prefix escaped publication root")
        observed = published_tree_receipt(prefix)
        for field in (
            "tree_sha256",
            "file_count",
            "symlink_count",
            "directory_count",
            "total_file_bytes",
        ):
            if observed[field] != record.get(field):
                raise ProductPackageError(f"published {name} package differs: {field}")
    if postgresql_record.get("tree_sha256") != source.get("tree_sha256"):
        raise ProductPackageError("published PostgreSQL tree differs from source receipt")
    for name, record in (("toolchain", toolchain_record), ("host provider", host_record)):
        receipt = require_absolute(
            record.get("source_receipt"),
            f"postgresql_publication.{name}.source_receipt",
        )
        digest = require_string(
            record.get("source_receipt_sha256"),
            f"postgresql_publication.{name}.source_receipt_sha256",
        )
        if (
            not receipt.is_relative_to(root)
            or not receipt.is_file()
            or receipt.is_symlink()
            or HEX_256.fullmatch(digest) is None
            or sha256_file(receipt) != digest
        ):
            raise ProductPackageError(f"published {name} receipt bytes differ")
    if publication.get("publication_complete") is not True:
        raise ProductPackageError("PostgreSQL publication is incomplete")
    return publication, source


def verify_product_toolchain(
    postgresql: Mapping[str, Any], publication: Mapping[str, Any]
) -> dict[str, Any]:
    selected = postgresql.get("build_toolchain")
    if not isinstance(selected, dict):
        raise ProductPackageError("PostgreSQL receipt omits its selected build toolchain")
    published = publication.get("toolchain")
    if not isinstance(published, dict):
        raise ProductPackageError("PostgreSQL publication omits its toolchain")
    receipt_path = require_absolute(
        published.get("source_receipt"),
        "postgresql_publication.toolchain.source_receipt",
    )
    receipt_sha256 = require_string(
        published.get("source_receipt_sha256"),
        "postgresql_publication.toolchain.source_receipt_sha256",
    )
    if receipt_sha256 != selected.get("receipt_sha256"):
        raise ProductPackageError("published and selected toolchain receipts differ")
    if (
        not receipt_path.is_file()
        or receipt_path.is_symlink()
        or sha256_file(receipt_path) != receipt_sha256
    ):
        raise ProductPackageError("selected build-toolchain receipt bytes differ")
    receipt = load_json(receipt_path)
    if (
        receipt.get("schema") != "laplace.toolchain-package-receipt/v1"
        or receipt.get("build_input_id") != selected.get("build_input_id")
    ):
        raise ProductPackageError("selected build-toolchain receipt identity differs")
    package = receipt.get("package")
    manifest = receipt.get("consumer_manifest")
    if not isinstance(package, dict) or not isinstance(manifest, dict):
        raise ProductPackageError("build-toolchain receipt omits its consumer manifest")
    source_prefix = require_absolute(package.get("prefix"), "build_toolchain.package.prefix")
    if str(source_prefix) != published.get("source_prefix"):
        raise ProductPackageError("published toolchain source prefix differs")
    prefix = require_absolute(published.get("prefix"), "published toolchain prefix")
    if not prefix.is_dir() or prefix.is_symlink():
        raise ProductPackageError("published build-toolchain prefix is absent")
    if (
        manifest.get("schema") != "laplace.toolchain-consumer-manifest/v1"
        or manifest.get("build_input_id") != selected.get("build_input_id")
        or Path(str(manifest.get("prefix", ""))) != source_prefix
    ):
        raise ProductPackageError("build-toolchain consumer manifest identity differs")
    tools = manifest.get("tools")
    required_tools = ("cmake", "ninja", "ar", "ranlib", "ld", "readelf")
    if not isinstance(tools, dict):
        raise ProductPackageError("build-toolchain consumer manifest omits tools")
    verified_tools: dict[str, dict[str, Any]] = {}
    for name in required_tools:
        record = tools.get(name)
        if not isinstance(record, dict):
            raise ProductPackageError(f"build-toolchain omits selected {name}")
        source_path = require_absolute(record.get("path"), f"build_toolchain.tools.{name}.path")
        if not source_path.is_relative_to(source_prefix):
            raise ProductPackageError(f"selected {name} escaped the toolchain prefix")
        path = prefix / source_path.relative_to(source_prefix)
        mapped = {**record, "path": str(path)}
        verify_receipted_file(mapped, f"published build_toolchain.tools.{name}")
        verified_tools[name] = mapped
    selected_readelf = selected.get("tools", {}).get("readelf")
    if not isinstance(selected_readelf, dict) or any(
        selected_readelf.get(field) != verified_tools["readelf"].get(field)
        for field in ("sha256", "version")
    ):
        raise ProductPackageError("PostgreSQL and product toolchain readelf differ")
    return {
        "schema": "laplace.product-build-toolchain/v1",
        "build_input_id": selected["build_input_id"],
        "receipt_path": str(receipt_path),
        "receipt_sha256": receipt_sha256,
        "prefix": str(prefix),
        "source_prefix": str(source_prefix),
        "consumer_manifest_sha256": canonical_sha256(manifest),
        "tools": verified_tools,
    }


def exact_tree_receipt(root: Path) -> dict[str, Any]:
    if not root.is_dir() or root.is_symlink():
        raise ProductPackageError(f"build provider root must be a physical directory: {root}")
    digest = hashlib.sha256()
    file_count = 0
    symlink_count = 0
    directory_count = 0
    total_file_bytes = 0
    for path in sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix()):
        relative = path.relative_to(root).as_posix()
        mode = stat.S_IMODE(path.lstat().st_mode)
        if path.is_symlink():
            kind = "symlink"
            identity = os.readlink(path)
            symlink_count += 1
        elif path.is_file():
            kind = "file"
            identity = sha256_file(path)
            file_count += 1
            total_file_bytes += path.stat().st_size
        elif path.is_dir():
            kind = "directory"
            identity = ""
            directory_count += 1
        else:
            raise ProductPackageError(f"build provider root contains unsupported object: {path}")
        encoded = json.dumps(
            [relative, kind, mode, identity],
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        digest.update(len(encoded).to_bytes(8, "big"))
        digest.update(encoded)
    return {
        "path": str(root),
        "tree_sha256": digest.hexdigest(),
        "file_count": file_count,
        "symlink_count": symlink_count,
        "directory_count": directory_count,
        "total_file_bytes": total_file_bytes,
    }


def exact_file_receipt(path: Path) -> dict[str, Any]:
    if not path.is_file() or path.is_symlink():
        raise ProductPackageError(f"build provider file must be physical: {path}")
    return {
        "path": str(path),
        "mode": stat.S_IMODE(path.stat().st_mode),
        "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def verify_host_build_provider(
    contract: Mapping[str, Any],
    repository: Path,
    postgresql: Mapping[str, Any],
    publication: Mapping[str, Any],
) -> dict[str, Any]:
    selected = postgresql.get("host_build_provider")
    if not isinstance(selected, dict) or selected.get("schema") != contract[
        "host_build_provider"
    ]["receipt_schema"]:
        raise ProductPackageError("PostgreSQL receipt omits its exact host build provider")
    published = publication.get("host_provider")
    if not isinstance(published, dict):
        raise ProductPackageError("PostgreSQL publication omits its host provider")
    receipt_path = require_absolute(
        published.get("source_receipt"),
        "postgresql_publication.host_provider.source_receipt",
    )
    receipt_sha256 = require_string(
        published.get("source_receipt_sha256"),
        "postgresql_publication.host_provider.source_receipt_sha256",
    )
    if receipt_sha256 != selected.get("receipt_sha256"):
        raise ProductPackageError("published and selected host-provider receipts differ")
    verifier = (repository / contract["host_build_provider"]["verifier"]).resolve()
    if (
        not receipt_path.is_file()
        or receipt_path.is_symlink()
        or sha256_file(receipt_path) != receipt_sha256
        or not verifier.is_file()
    ):
        raise ProductPackageError("host build provider receipt or verifier differs")
    result = subprocess.run(
        [sys.executable, str(verifier), "verify", "--receipt", str(receipt_path)],
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        raise ProductPackageError(
            "host build provider verification failed: "
            + (result.stderr.strip() or result.stdout.strip())
        )
    observed = json.loads(result.stdout, object_pairs_hook=reject_duplicate_keys)
    expected = dict(selected)
    expected.pop("receipt_path", None)
    expected.pop("receipt_sha256", None)
    if observed != expected:
        raise ProductPackageError("host build provider differs from the PostgreSQL receipt")
    return {
        **selected,
        "receipt_path": str(receipt_path),
        "receipt_sha256": receipt_sha256,
        "verifier": {
            "path": contract["host_build_provider"]["verifier"],
            "sha256": sha256_file(verifier),
        },
    }


def verify_postgresql_receipt(
    contract: dict[str, Any],
    receipt: dict[str, Any],
    publication: Mapping[str, Any],
) -> tuple[dict[str, Any], Path, Path]:
    if receipt.get("schema") != contract["postgresql"]["receipt_schema"]:
        raise ProductPackageError("PostgreSQL package receipt schema differs")
    if receipt.get("version") != contract["postgresql"]["version"]:
        raise ProductPackageError("PostgreSQL package version differs")
    postgresql_publication = publication.get("postgresql")
    toolchain_publication = publication.get("toolchain")
    if not isinstance(postgresql_publication, dict) or not isinstance(
        toolchain_publication, dict
    ):
        raise ProductPackageError("PostgreSQL publication physical mappings are absent")
    prefix = require_absolute(
        postgresql_publication.get("prefix"), "postgresql_publication.postgresql.prefix"
    )
    source_toolchain_prefix = require_absolute(
        toolchain_publication.get("source_prefix"),
        "postgresql_publication.toolchain.source_prefix",
    )
    published_toolchain_prefix = require_absolute(
        toolchain_publication.get("prefix"), "postgresql_publication.toolchain.prefix"
    )
    source_readelf = require_absolute(
        receipt.get("build_toolchain", {}).get("tools", {}).get("readelf", {}).get("path"),
        "postgresql_receipt.build_toolchain.tools.readelf.path",
    )
    if not source_readelf.is_relative_to(source_toolchain_prefix):
        raise ProductPackageError("selected readelf escaped the source toolchain")
    readelf = published_toolchain_prefix / source_readelf.relative_to(
        source_toolchain_prefix
    )
    if not prefix.is_dir() or prefix.is_symlink() or not readelf.is_file():
        raise ProductPackageError("PostgreSQL package bytes or selected readelf are absent")
    postgres = prefix / "pgsql-18/bin/postgres"
    pg_config = prefix / "pgsql-18/bin/pg_config"
    if not postgres.is_file() or not pg_config.is_file():
        raise ProductPackageError("PostgreSQL package omits server executables")
    version = subprocess.run(
        [str(pg_config), "--version"], text=True, capture_output=True
    )
    if version.returncode != 0 or version.stdout.strip() != contract["postgresql"]["version"]:
        raise ProductPackageError("physical PostgreSQL package version differs")
    for path in sorted(prefix.rglob("*")):
        if path.is_file() and not path.is_symlink():
            try:
                elf_runpath(path, readelf)
            except ProductPackageError as error:
                raise ProductPackageError(
                    f"PostgreSQL input is not relocatable: {error}"
                ) from error
    return receipt, prefix, readelf


def verify_installed_providers(
    contract: dict[str, Any], lock_path: Path
) -> tuple[
    dict[str, Any],
    dict[str, list[dict[str, Any]]],
    dict[str, dict[str, Any]],
]:
    lock = load_json(lock_path)
    if lock.get("schema") != "laplace.installed-provider-lock/v1":
        raise ProductPackageError("installed provider lock schema differs")
    providers = lock.get("providers")
    if not isinstance(providers, dict):
        raise ProductPackageError("installed provider lock omits providers")
    selected: dict[str, list[dict[str, Any]]] = {}
    provider_roots: dict[str, dict[str, Any]] = {}
    for name in contract["laplace"]["required_installed_providers"]:
        provider = providers.get(name)
        if not isinstance(provider, dict) or not isinstance(provider.get("files"), list):
            raise ProductPackageError(f"installed provider is absent: {name}")
        immutable_root = require_absolute(
            provider.get("immutable_root"), f"provider {name} immutable_root"
        )
        provider_roots[name] = exact_tree_receipt(immutable_root)
        selected[name] = []
        for item in provider["files"]:
            if not isinstance(item, dict):
                raise ProductPackageError(f"installed provider file is invalid: {name}")
            path = require_absolute(item.get("path"), f"provider {name} path")
            digest = require_string(item.get("sha256"), f"provider {name} sha256")
            size = item.get("bytes")
            if (
                HEX_256.fullmatch(digest) is None
                or not isinstance(size, int)
                or size < 0
                or not path.is_file()
                or path.is_symlink()
                or path.stat().st_size != size
                or sha256_file(path) != digest
                or not path.is_relative_to(immutable_root)
            ):
                raise ProductPackageError(f"installed provider bytes differ: {path}")
            selected[name].append(dict(item))
    return lock, selected, provider_roots


def verify_runtime_dependencies(
    contract: Mapping[str, Any],
) -> tuple[dict[str, list[dict[str, Any]]], dict[str, dict[str, Any]]]:
    selected: dict[str, list[dict[str, Any]]] = {}
    roots: dict[str, dict[str, Any]] = {}
    for name in contract["laplace"]["required_runtime_dependencies"]:
        provider = contract["runtime_dependencies"][name]
        immutable_root = require_absolute(
            provider["immutable_root"], f"runtime dependency {name} immutable_root"
        )
        roots[f"runtime-dependency:{name}"] = exact_tree_receipt(immutable_root)
        selected[name] = []
        for item in provider["files"]:
            path = require_absolute(item["path"], f"runtime dependency {name} path")
            if (
                not path.is_file()
                or path.is_symlink()
                or path.stat().st_size != item["bytes"]
                or sha256_file(path) != item["sha256"]
                or not path.is_relative_to(immutable_root)
            ):
                raise ProductPackageError(f"runtime dependency bytes differ: {path}")
            selected[name].append(dict(item))
        for package, version in provider["package_versions"].items():
            result = subprocess.run(
                ["/usr/bin/dpkg-query", "-W", "-f=${Version}", package],
                text=True,
                capture_output=True,
            )
            if result.returncode != 0 or result.stdout != version:
                raise ProductPackageError(
                    f"runtime dependency package version differs: {package}"
                )
    return selected, roots


def create_plan(
    contract: dict[str, Any],
    repository: Path,
    postgresql_publication_path: Path,
    installed_lock_path: Path,
    *,
    require_clean: bool = True,
) -> dict[str, Any]:
    validate_contract(contract)
    publication, published_source = verify_postgresql_publication(
        contract, postgresql_publication_path.resolve()
    )
    postgresql, prefix, readelf = verify_postgresql_receipt(
        contract, published_source, publication
    )
    toolchain = verify_product_toolchain(postgresql, publication)
    host_build_provider = verify_host_build_provider(
        contract, repository, postgresql, publication
    )
    lock, providers, provider_roots = verify_installed_providers(
        contract, installed_lock_path
    )
    runtime_dependencies, runtime_dependency_roots = verify_runtime_dependencies(
        contract
    )
    blake3_root = require_absolute(contract["build"]["blake3_root"], "build.blake3_root")
    build_input_roots = {
        "blake3": exact_tree_receipt(blake3_root),
        **provider_roots,
        **runtime_dependency_roots,
    }
    build_input_files: dict[str, dict[str, Any]] = {}
    for file_value in contract["host_build_provider"]["additional_receipted_files"]:
        path = require_absolute(
            file_value, "host_build_provider.additional_receipted_files"
        )
        build_input_files[f"host:{path}"] = exact_file_receipt(path)
    source = repository_identity(repository, require_clean)
    driver = Path(__file__).resolve()
    recipe = {
        "contract_sha256": canonical_sha256(contract),
        "driver_sha256": sha256_file(driver),
        "installed_lock_sha256": sha256_file(installed_lock_path),
        "postgresql_publication_sha256": sha256_file(postgresql_publication_path),
        "postgresql_receipt_sha256": publication["source_receipt"]["sha256"],
        "toolchain_receipt_sha256": toolchain["receipt_sha256"],
        "host_build_provider_receipt_sha256": host_build_provider["receipt_sha256"],
        "build_input_roots": build_input_roots,
        "build_input_files": build_input_files,
        "repository": source,
    }
    identity_payload = {
        "schema": PLAN_SCHEMA,
        "recipe": recipe,
        "postgresql_tree_sha256": postgresql.get("tree_sha256"),
        "postgresql_build_input_id": postgresql.get("build_input_id"),
        "provider_selection_sha256": lock.get("provider_selection_sha256"),
        "product_build_input_closure_complete": True,
    }
    plan_id = canonical_sha256(identity_payload)
    build_directory = Path(contract["build"]["root"]) / plan_id
    stage_directory = Path(contract["build"]["stage_root"]) / plan_id
    logical_prefix = contract["laplace"]["logical_prefix"]
    staged_prefix = stage_directory / "root" / logical_prefix.lstrip("/")
    plan = {
        **identity_payload,
        "plan_id": plan_id,
        "build_directory": str(build_directory),
        "stage_directory": str(stage_directory),
        "staged_prefix": str(staged_prefix),
        "repository_root": str(repository),
        "postgresql": {
            "publication_path": str(postgresql_publication_path.resolve()),
            "publication_sha256": sha256_file(postgresql_publication_path),
            "publication_id": publication["publication_id"],
            "receipt_path": publication["source_receipt"]["path"],
            "receipt_sha256": publication["source_receipt"]["sha256"],
            "prefix": str(prefix),
            "tree_sha256": postgresql.get("tree_sha256"),
            "build_input_id": postgresql.get("build_input_id"),
            "build_input_closure_complete": postgresql.get(
                "build_input_closure_complete"
            ) is True,
            "runtime_provider_qualification_complete": postgresql.get(
                "runtime_provider_qualification_complete"
            ) is True,
            "activation_eligible": postgresql.get("activation_eligible") is True,
        },
        "installed_provider_lock": str(installed_lock_path.resolve()),
        "installed_providers": providers,
        "runtime_dependencies": runtime_dependencies,
        "build_input_roots": build_input_roots,
        "build_input_files": build_input_files,
        "product_toolchain": toolchain,
        "host_build_provider": host_build_provider,
        "product_build_input_closure_complete": True,
        "readelf": str(readelf),
    }
    plan["plan_sha256"] = canonical_sha256(plan)
    return plan


def create_private_directory(path: Path) -> None:
    path.mkdir(parents=True)
    os.chmod(path, 0o700)
    if stat.S_IMODE(path.stat().st_mode) != 0o700:
        raise ProductPackageError(f"private directory mode differs: {path}")


def tree_fingerprint(root: Path) -> dict[str, tuple[Any, ...]]:
    records: dict[str, tuple[Any, ...]] = {}
    for path in sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix()):
        relative = path.relative_to(root).as_posix()
        mode = stat.S_IMODE(path.lstat().st_mode)
        if path.is_symlink():
            records[relative] = ("symlink", mode, os.readlink(path))
        elif path.is_file():
            records[relative] = ("file", mode, path.stat().st_size, sha256_file(path))
        elif path.is_dir():
            records[relative] = ("directory", mode)
        else:
            raise ProductPackageError(f"unsupported package object: {path}")
    return records


def reverify_product_build_inputs(
    contract: Mapping[str, Any], repository: Path, plan: Mapping[str, Any]
) -> dict[str, Any]:
    publication_path = Path(plan["postgresql"]["publication_path"])
    if sha256_file(publication_path) != plan["postgresql"]["publication_sha256"]:
        raise ProductPackageError("PostgreSQL publication receipt changed")
    publication, postgresql = verify_postgresql_publication(
        contract, publication_path
    )
    if publication["publication_id"] != plan["postgresql"]["publication_id"]:
        raise ProductPackageError("PostgreSQL publication identity changed")
    toolchain = verify_product_toolchain(postgresql, publication)
    if toolchain != plan["product_toolchain"]:
        raise ProductPackageError("product build toolchain changed during construction")
    host_provider = verify_host_build_provider(
        contract, repository, postgresql, publication
    )
    if host_provider != plan["host_build_provider"]:
        raise ProductPackageError("host build provider changed during product construction")
    roots = {
        name: exact_tree_receipt(Path(receipt["path"]))
        for name, receipt in plan["build_input_roots"].items()
    }
    if roots != plan["build_input_roots"]:
        raise ProductPackageError("product build provider roots changed during construction")
    files = {
        name: exact_file_receipt(Path(receipt["path"]))
        for name, receipt in plan["build_input_files"].items()
    }
    if files != plan["build_input_files"]:
        raise ProductPackageError("product build provider files changed during construction")
    return {
        "schema": "laplace.product-build-input-closure-receipt/v1",
        "toolchain": toolchain,
        "host_build_provider": host_provider,
        "build_input_roots": roots,
        "build_input_files": files,
        "complete": True,
    }


def sandboxed_build_command(
    contract: Mapping[str, Any],
    plan: Mapping[str, Any],
    command: Sequence[str],
    cwd: Path,
) -> list[str]:
    executable = contract["host_build_provider"]["sandbox_executable"]
    arguments = [executable, "--die-with-parent", "--unshare-all"]
    created: set[str] = set()

    def ensure_parents(path: Path) -> None:
        for parent in reversed(path.parents[:-1]):
            value = str(parent)
            if value != "/" and value not in created:
                arguments.extend(("--dir", value))
                created.add(value)

    arguments.extend(("--ro-bind", "/usr", "/usr"))
    created.add("/usr")
    for target, source in (
        ("/bin", "usr/bin"),
        ("/lib", "usr/lib"),
        ("/lib64", "usr/lib64"),
        ("/sbin", "usr/sbin"),
    ):
        arguments.extend(("--symlink", source, target))
        created.add(target)
    for item in plan["host_build_provider"]["roots"]:
        path = Path(item["path"])
        if path == Path("/usr"):
            continue
        ensure_parents(path)
        arguments.extend(("--ro-bind", str(path), str(path)))
        created.add(str(path))
    for item in plan["host_build_provider"]["files"]:
        path = Path(item["path"])
        ensure_parents(path)
        arguments.extend(("--ro-bind", str(path), str(path)))
    for receipt in plan["build_input_roots"].values():
        path = Path(receipt["path"])
        if str(path) in created:
            continue
        ensure_parents(path)
        arguments.extend(("--ro-bind", str(path), str(path)))
        created.add(str(path))
    for receipt in plan["build_input_files"].values():
        path = Path(receipt["path"])
        ensure_parents(path)
        arguments.extend(("--ro-bind", str(path), str(path)))
    repository = Path(plan["repository_root"])
    for path_value, writable in (
        (plan["product_toolchain"]["prefix"], False),
        (str(repository), False),
        (plan["build_directory"], True),
        (plan["stage_directory"], True),
    ):
        path = Path(path_value)
        ensure_parents(path)
        arguments.extend(
            ("--bind" if writable else "--ro-bind", str(path), str(path))
        )
        created.add(str(path))
    if not cwd.is_relative_to(repository) and not cwd.is_relative_to(
        Path(plan["build_directory"])
    ):
        raise ProductPackageError("sandbox working directory escaped product inputs")
    arguments.extend(("--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp"))
    arguments.extend(("--chdir", str(cwd), "--"))
    arguments.extend(command)
    return arguments


def copy_provider_files(
    providers: Mapping[str, list[dict[str, Any]]], prefix: Path
) -> list[dict[str, Any]]:
    copied: list[dict[str, Any]] = []
    for provider_name, files in providers.items():
        for item in files:
            source = Path(item["path"])
            item_class = item.get("class")
            if item_class == "runtime-object":
                destination = prefix / "lib" / source.name
            elif item_class == "license":
                destination = (
                    prefix
                    / "share/licenses"
                    / provider_name
                    / f"{item['role']}-{source.name}"
                )
            else:
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists():
                if destination.is_symlink() or sha256_file(destination) != item["sha256"]:
                    raise ProductPackageError(
                        f"provider destination conflicts with package: {destination}"
                    )
            else:
                shutil.copy2(source, destination, follow_symlinks=False)
            copied.append(
                {
                    "provider": provider_name,
                    "role": item["role"],
                    "path": str(destination.relative_to(prefix)),
                    "sha256": item["sha256"],
                }
            )
            soname = item.get("soname")
            if item_class == "runtime-object" and isinstance(soname, str) and soname != source.name:
                alias = destination.parent / soname
                target = source.name
                if alias.exists() or alias.is_symlink():
                    if not alias.is_symlink() or os.readlink(alias) != target:
                        raise ProductPackageError(f"provider SONAME alias conflicts: {alias}")
                else:
                    alias.symlink_to(target)
                copied.append(
                    {
                        "provider": provider_name,
                        "role": f"{item['role']}:soname",
                        "path": str(alias.relative_to(prefix)),
                        "target": target,
                        "sha256": hashlib.sha256(target.encode("utf-8")).hexdigest(),
                    }
                )
    return copied


def verify_copied_provider_files(
    receipts: Sequence[Mapping[str, Any]], prefix: Path
) -> None:
    for item in receipts:
        relative = require_string(item.get("path"), "provider receipt path")
        destination = prefix / relative
        target = item.get("target")
        if target is not None:
            if (
                not isinstance(target, str)
                or not destination.is_symlink()
                or os.readlink(destination) != target
                or hashlib.sha256(target.encode("utf-8")).hexdigest()
                != item.get("sha256")
                or not destination.exists()
            ):
                raise ProductPackageError(
                    f"copied provider SONAME alias differs after overlay: {relative}"
                )
        elif (
            not destination.is_file()
            or destination.is_symlink()
            or sha256_file(destination) != item.get("sha256")
        ):
            raise ProductPackageError(
                f"copied provider bytes differ after overlay: {relative}"
            )


def run_logged(
    command: Sequence[str], cwd: Path, environment: Mapping[str, str], log: Path
) -> None:
    with log.open("a", encoding="utf-8") as stream:
        stream.write("$ " + json.dumps(list(command), separators=(",", ":")) + "\n")
        stream.flush()
        process = subprocess.Popen(
            list(command),
            cwd=cwd,
            env=dict(environment),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=1,
        )
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            stream.write(line)
        status = process.wait()
        if status != 0:
            raise ProductPackageError(f"command exited {status}: {command[0]}")


def elf_runpath(path: Path, readelf: Path) -> list[str]:
    with path.open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            return []
    result = subprocess.run([str(readelf), "-d", str(path)], text=True, capture_output=True)
    if result.returncode != 0:
        raise ProductPackageError(f"readelf failed for {path}: {result.stderr.strip()}")
    values: list[str] = []
    for line in result.stdout.splitlines():
        match = RUNPATH_PATTERN.search(line)
        if match:
            values.extend(item for item in match.group(1).split(":") if item)
    for item in values:
        if item != "$ORIGIN" and not item.startswith("$ORIGIN/"):
            raise ProductPackageError(f"package ELF has non-relative RUNPATH: {path}: {item}")
    return values


def manifest_entries(prefix: Path, readelf: Path) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for path in sorted(prefix.rglob("*"), key=lambda item: item.relative_to(prefix).as_posix()):
        relative = path.relative_to(prefix).as_posix()
        if path.is_dir() and not path.is_symlink():
            continue
        if path.is_symlink():
            target = os.readlink(path)
            if target.startswith("/"):
                raise ProductPackageError(f"package symlink is absolute: {relative}")
            resolved = (path.parent / target).resolve()
            try:
                resolved.relative_to(prefix.resolve())
            except ValueError as error:
                raise ProductPackageError(f"package symlink escapes: {relative}") from error
            if not resolved.exists():
                raise ProductPackageError(f"package symlink target is absent: {relative}")
            entries.append(
                {
                    "path": relative,
                    "kind": "symlink",
                    "target": target,
                    "sha256": hashlib.sha256(target.encode("utf-8")).hexdigest(),
                    "runpath": [],
                }
            )
            continue
        if not path.is_file():
            raise ProductPackageError(f"unsupported package object: {path}")
        mode = stat.S_IMODE(path.stat().st_mode)
        if mode not in (0o644, 0o755):
            raise ProductPackageError(f"package file mode must be 0644 or 0755: {relative}")
        entries.append(
            {
                "path": relative,
                "kind": "file",
                "sha256": sha256_file(path),
                "mode": mode,
                "runpath": elf_runpath(path, readelf),
            }
        )
    return entries


def verify_recursive_closure(
    contract: dict[str, Any],
    repository: Path,
    prefix: Path,
    readelf: Path,
    postgresql_receipt: Mapping[str, Any],
    output_path: Path,
) -> dict[str, Any]:
    closure = contract["runtime_closure"]
    platform = postgresql_receipt.get("recursive_elf_closure", {}).get(
        "platform_abi_objects", {}
    )
    loader = platform.get("dynamic-loader")
    if not isinstance(loader, dict) or not Path(str(loader.get("path", ""))).is_file():
        raise ProductPackageError("PostgreSQL receipt omits the selected platform loader")
    command = [
        sys.executable,
        str((repository / closure["verifier"]).resolve()),
        "--root",
        str(prefix),
        "--custom-prefix",
        str(prefix),
        "--process-cwd",
        "/",
        "--readelf",
        str(readelf),
        "--loader",
        loader["path"],
        "--output",
        str(output_path),
        "--strict",
    ]
    for relative in closure["search_subdirectories"]:
        command.extend(("--search-dir", str(prefix / relative)))
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode != 0 or not output_path.is_file():
        raise ProductPackageError(
            "product recursive ELF closure failed: "
            + (result.stderr.strip() or result.stdout.strip())
        )
    report = load_json(output_path)
    if report.get("schema") != closure["schema"] or report.get("tool_version") != closure[
        "tool_version"
    ]:
        raise ProductPackageError("product recursive closure schema differs")
    summary = report.get("summary")
    if not isinstance(summary, dict):
        raise ProductPackageError("product recursive closure summary is absent")
    for field in closure["required_zero_summary_fields"]:
        if summary.get(field) != 0:
            raise ProductPackageError(f"product recursive closure is not clean: {field}")
    expected_host = {
        str(Path(item["path"]).resolve()): item for item in platform.values()
    }
    observed_host = {
        item.get("path"): item
        for item in report.get("objects", [])
        if isinstance(item, dict) and item.get("classification") == "host-system"
    }
    if not set(expected_host).issubset(observed_host):
        raise ProductPackageError("product omitted a PostgreSQL platform ABI object")
    for path, expected in expected_host.items():
        if observed_host[path].get("sha256") != expected.get("sha256"):
            raise ProductPackageError(f"product platform ABI bytes differ: {path}")
    additional_paths = set(observed_host) - set(expected_host)
    allowed_additional = set(closure["allowed_additional_platform_abi_sonames"])
    host_roots = [
        Path(item["path"]).resolve()
        for item in postgresql_receipt.get("host_build_provider", {}).get("roots", [])
        if isinstance(item, dict) and isinstance(item.get("path"), str)
    ]
    additional: dict[str, dict[str, str]] = {}
    for path in sorted(additional_paths):
        item = observed_host[path]
        elf = item.get("elf")
        soname = elf.get("soname") if isinstance(elf, dict) else None
        digest = item.get("sha256")
        resolved = Path(path).resolve()
        if (
            soname not in allowed_additional
            or soname in additional
            or not isinstance(digest, str)
            or HEX_256.fullmatch(digest) is None
            or not any(resolved.is_relative_to(root) for root in host_roots)
        ):
            raise ProductPackageError(f"product selected an undeclared platform ABI object: {path}")
        additional[soname] = {
            "path": path,
            "sha256": digest,
            "soname": soname,
        }
    if set(additional) != allowed_additional:
        raise ProductPackageError("product platform ABI expansion is incomplete")
    return {
        "schema": "laplace.product-recursive-elf-closure-receipt/v1",
        "report": str(output_path),
        "report_sha256": sha256_file(output_path),
        "summary": summary,
        "platform_abi_objects": {
            "postgresql": platform,
            "product_additional": additional,
        },
        "verified": True,
    }


def package_identity(manifest: Mapping[str, Any]) -> str:
    payload = dict(manifest)
    payload.pop("package_id", None)
    payload.pop("root", None)
    return canonical_sha256(payload)


def product_activation_gates(
    plan: Mapping[str, Any], recursive_closure_verified: bool
) -> dict[str, bool]:
    return {
        "clean_repository": plan["recipe"]["repository"]["clean"] is True,
        "postgresql_build_input_closure": plan["postgresql"][
            "build_input_closure_complete"
        ]
        is True,
        "postgresql_runtime_provider_qualification": plan["postgresql"][
            "runtime_provider_qualification_complete"
        ]
        is True,
        "postgresql_package_activation": plan["postgresql"]["activation_eligible"]
        is True,
        "product_build_input_closure": plan["product_build_input_closure_complete"]
        is True,
        "recursive_elf_closure": recursive_closure_verified is True,
    }


def execute_plan(
    contract: dict[str, Any], repository: Path, plan: dict[str, Any]
) -> dict[str, Any]:
    if plan.get("plan_sha256") != canonical_sha256(
        {key: value for key, value in plan.items() if key != "plan_sha256"}
    ):
        raise ProductPackageError("product package plan digest differs")
    build_directory = Path(plan["build_directory"])
    stage_directory = Path(plan["stage_directory"])
    if build_directory.exists() or stage_directory.exists():
        raise ProductPackageError("product build and stage destinations must be absent")
    create_private_directory(build_directory)
    create_private_directory(stage_directory)
    staged_prefix = Path(plan["staged_prefix"])
    staged_prefix.parent.mkdir(parents=True)
    _, postgresql_receipt = verify_postgresql_publication(
        contract, Path(plan["postgresql"]["publication_path"])
    )
    source_prefix = Path(plan["postgresql"]["prefix"])
    source_tree = tree_fingerprint(source_prefix)
    shutil.copytree(source_prefix, staged_prefix, symlinks=True)
    if tree_fingerprint(staged_prefix) != source_tree:
        raise ProductPackageError("PostgreSQL copy differs before Laplace overlay")
    provider_receipts = copy_provider_files(
        {**plan["installed_providers"], **plan["runtime_dependencies"]},
        staged_prefix,
    )
    laplace_build = build_directory / "laplace"
    log = build_directory / "build.log"
    build = contract["build"]
    toolchain = plan["product_toolchain"]["tools"]
    cmake = toolchain["cmake"]["path"]
    ninja = toolchain["ninja"]["path"]
    home = build_directory / ".home"
    home.mkdir()
    environment = {
        key: value
        for key, value in os.environ.items()
        if key not in {"LD_LIBRARY_PATH", "LD_PRELOAD", "CMAKE_PREFIX_PATH"}
    }
    environment.update(
        {
            "HOME": str(home),
            "PATH": f"{plan['product_toolchain']['prefix']}/bin:/usr/bin",
        }
    )
    configure = [
        cmake,
        "-S",
        str(repository),
        "-B",
        str(laplace_build),
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={build['configuration']}",
        f"-DCMAKE_C_COMPILER={build['c_compiler']}",
        f"-DCMAKE_CXX_COMPILER={build['cxx_compiler']}",
        f"-DCMAKE_MAKE_PROGRAM={ninja}",
        f"-DCMAKE_AR={toolchain['ar']['path']}",
        f"-DCMAKE_RANLIB={toolchain['ranlib']['path']}",
        f"-DCMAKE_LINKER={toolchain['ld']['path']}",
        f"-DCMAKE_INSTALL_PREFIX={contract['laplace']['logical_prefix']}",
        f"-DCMAKE_INSTALL_LIBDIR={build['install_libdir']}",
        f"-DBUILD_TESTING={'ON' if build['testing'] else 'OFF'}",
        f"-DLAPLACE_ENABLE_DOTNET_BINDINGS={'ON' if build['dotnet_bindings'] else 'OFF'}",
        f"-DLAPLACE_BLAKE3_SOURCE={build['blake3_source']}",
        f"-DLAPLACE_DEPENDENCY_LOCK={repository / 'dependencies/lock.json'}",
        f"-DLAPLACE_INSTALLED_PROVIDER_LOCK={plan['installed_provider_lock']}",
        "-DLAPLACE_VERIFY_ONEAPI_INSTALLED_PROVIDER=ON",
        f"-DLAPLACE_PG_CONFIG={staged_prefix / 'pgsql-18/bin/pg_config'}",
        f"-DLAPLACE_PG_PHYSICAL_ROOT={stage_directory / 'root'}",
    ]
    run_logged(
        sandboxed_build_command(contract, plan, configure, repository),
        repository,
        environment,
        log,
    )
    run_logged(
        sandboxed_build_command(
            contract,
            plan,
            [cmake, "--build", str(laplace_build), "-j", str(build["parallel_jobs"])],
            repository,
        ),
        repository,
        environment,
        log,
    )
    install_environment = {**environment, "DESTDIR": str(stage_directory / "root")}
    run_logged(
        sandboxed_build_command(
            contract,
            plan,
            [cmake, "--install", str(laplace_build), "--config", build["configuration"]],
            repository,
        ),
        repository,
        install_environment,
        log,
    )
    destination_tree = tree_fingerprint(staged_prefix)
    for relative, identity in source_tree.items():
        if destination_tree.get(relative) != identity:
            raise ProductPackageError(f"Laplace overlay modified PostgreSQL input: {relative}")
    verify_copied_provider_files(provider_receipts, staged_prefix)
    build_input_closure = reverify_product_build_inputs(contract, repository, plan)
    entries = manifest_entries(staged_prefix, Path(plan["readelf"]))
    by_path = {item["path"]: item for item in entries}
    missing = sorted(set(contract["package"]["required_files"]) - set(by_path))
    if missing:
        raise ProductPackageError(f"complete product package omits required file: {missing[0]}")
    closure = verify_recursive_closure(
        contract,
        repository,
        staged_prefix,
        Path(plan["readelf"]),
        postgresql_receipt,
        build_directory / "recursive-elf-closure.json",
    )
    activation_gates = product_activation_gates(plan, closure["verified"])
    activation_eligible = all(activation_gates.values())
    manifest: dict[str, Any] = {
        "schema": MANIFEST_SCHEMA,
        "postgresql": {
            "version": "18.6",
            "pg_config": "pgsql-18/bin/pg_config",
            "build_input_id": plan["postgresql"]["build_input_id"],
            "package_receipt_sha256": plan["postgresql"]["receipt_sha256"],
        },
        "laplace": {
            "version": contract["version"],
            "repository_commit": plan["recipe"]["repository"]["commit"],
            "repository_tree": plan["recipe"]["repository"]["tree"],
        },
        "capabilities": contract["laplace"]["required_capabilities"],
        "loader_environment": {},
        "files": entries,
        "loaded_objects": contract["package"]["required_loaded_objects"],
        "activation_eligible": activation_eligible,
        "activation_gates": activation_gates,
        "provenance": {
            "product_plan_sha256": plan["plan_sha256"],
            "installed_provider_lock_sha256": plan["recipe"][
                "installed_lock_sha256"
            ],
            "provider_files": provider_receipts,
            "build_input_closure": build_input_closure,
            "recursive_elf_closure_sha256": closure["report_sha256"],
        },
    }
    package_id = package_identity(manifest)
    manifest["package_id"] = package_id
    manifest["root"] = f"{contract['package']['release_root']}/{package_id}"
    release_physical = stage_directory / "root" / manifest["root"].lstrip("/")
    release_physical.parent.mkdir(parents=True, exist_ok=True)
    staged_prefix.rename(release_physical)
    manifest_path = build_directory / "package-manifest.json"
    manifest_path.write_bytes(canonical_bytes(manifest))
    receipt = {
        "schema": RECEIPT_SCHEMA,
        "plan_sha256": plan["plan_sha256"],
        "package_id": package_id,
        "manifest": str(manifest_path),
        "manifest_sha256": sha256_file(manifest_path),
        "physical_root": str(release_physical),
        "file_count": sum(item["kind"] == "file" for item in entries),
        "symlink_count": sum(item["kind"] == "symlink" for item in entries),
        "total_file_bytes": sum(
            (release_physical / item["path"]).stat().st_size
            for item in entries
            if item["kind"] == "file"
        ),
        "build_log_sha256": sha256_file(log),
        "recursive_elf_closure": closure,
        "build_input_closure": build_input_closure,
        "build_input_closure_complete": build_input_closure["complete"],
        "activation_eligible": activation_eligible,
        "activation_disposition": (
            "eligible for isolated cluster planning"
            if activation_eligible
            else "inert: PostgreSQL input closure or selected runtime qualification is incomplete"
        ),
        "product_activated": False,
    }
    (build_directory / "package-receipt.json").write_bytes(canonical_bytes(receipt))
    return receipt


def select_or_build_product(
    contract: dict[str, Any], repository: Path, plan: dict[str, Any]
) -> dict[str, Any]:
    build_directory = Path(plan["build_directory"])
    receipt_path = build_directory / "package-receipt.json"
    built_new = False
    if not receipt_path.exists():
        execute_plan(contract, repository, plan)
        built_new = True
    if not receipt_path.is_file() or receipt_path.is_symlink():
        raise ProductPackageError("product package receipt is absent or not physical")
    receipt = load_json(receipt_path)
    if (
        receipt.get("schema") != RECEIPT_SCHEMA
        or receipt.get("plan_sha256") != plan["plan_sha256"]
        or receipt.get("activation_eligible") is not True
        or receipt.get("build_input_closure_complete") is not True
        or receipt.get("product_activated") is not False
    ):
        raise ProductPackageError("selected product package receipt differs from its exact plan")
    manifest_path = Path(require_string(receipt.get("manifest"), "product receipt manifest"))
    expected_manifest_path = build_directory / "package-manifest.json"
    if manifest_path != expected_manifest_path or not manifest_path.is_file() or manifest_path.is_symlink():
        raise ProductPackageError("selected product manifest is absent or not paired with its receipt")
    if sha256_file(manifest_path) != receipt.get("manifest_sha256"):
        raise ProductPackageError("selected product manifest bytes differ from its receipt")
    manifest = load_json(manifest_path)
    package_id = require_string(receipt.get("package_id"), "product receipt package id")
    if HEX_256.fullmatch(package_id) is None or manifest.get("package_id") != package_id:
        raise ProductPackageError("selected product package identity differs")
    expected_physical_root = Path(plan["stage_directory"]) / "root" / str(
        manifest.get("root", "")
    ).lstrip("/")
    if receipt.get("physical_root") != str(expected_physical_root) or not expected_physical_root.is_dir():
        raise ProductPackageError("selected product physical root differs from its plan")
    return {
        "schema": SELECTION_SCHEMA,
        "plan_id": plan["plan_id"],
        "plan_sha256": plan["plan_sha256"],
        "build_directory": plan["build_directory"],
        "stage_directory": plan["stage_directory"],
        "product_receipt": str(receipt_path),
        "package_id": package_id,
        "built_new": built_new,
    }


def write_result(path: str, value: dict[str, Any]) -> None:
    content = canonical_bytes(value)
    if path == "-":
        sys.stdout.buffer.write(content)
        return
    output = Path(path)
    if not output.parent.is_dir():
        raise ProductPackageError("result output parent is absent")
    try:
        with output.open("xb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError as error:
        raise ProductPackageError("result output already exists") from error


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=".")
    parser.add_argument("--contract", default="contracts/product-package.json")
    parser.add_argument(
        "--installed-lock", default="dependencies/installed-lock.json"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("validate-contract")
    for name in ("plan", "build", "compose"):
        command = subparsers.add_parser(name)
        command.add_argument("--postgresql-publication", required=True)
        command.add_argument("--allow-dirty", action="store_true")
        if name == "compose":
            command.add_argument("--output", default="-")
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_args(argv)
    repository = Path(arguments.repository).resolve()
    contract_path = Path(arguments.contract)
    if not contract_path.is_absolute():
        contract_path = repository / contract_path
    installed_lock = Path(arguments.installed_lock)
    if not installed_lock.is_absolute():
        installed_lock = repository / installed_lock
    contract = load_json(contract_path)
    validate_contract(contract)
    if arguments.command == "validate-contract":
        print(json.dumps({"schema": CONTRACT_SCHEMA, "status": "valid"}, sort_keys=True))
        return 0
    plan = create_plan(
        contract,
        repository,
        Path(arguments.postgresql_publication),
        installed_lock,
        require_clean=not arguments.allow_dirty,
    )
    if arguments.command == "plan":
        print(json.dumps(plan, indent=2, sort_keys=True))
    elif arguments.command == "build":
        print(json.dumps(execute_plan(contract, repository, plan), indent=2, sort_keys=True))
    else:
        write_result(
            arguments.output,
            select_or_build_product(contract, repository, plan),
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (ProductPackageError, subprocess.CalledProcessError) as error:
        print(f"product-package: {error}", file=sys.stderr)
        raise SystemExit(1) from error
