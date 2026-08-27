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
    if postgresql.get("version") != "PostgreSQL 18.6":
        raise ProductPackageError("product package must select exact PostgreSQL 18.6")
    if postgresql.get("logical_prefix") != "/opt/laplace/current/pgsql-18":
        raise ProductPackageError("PostgreSQL logical prefix is invalid")
    if laplace.get("logical_prefix") != "/opt/laplace/current":
        raise ProductPackageError("Laplace logical prefix is invalid")
    providers = laplace.get("required_installed_providers")
    if providers != ["intel-oneapi-runtime", "onetbb", "onemkl"]:
        raise ProductPackageError("complete selected oneAPI provider set is required")
    capabilities = laplace.get("required_capabilities")
    if not isinstance(capabilities, dict) or any(
        not isinstance(name, str) or not isinstance(version, int) or version <= 0
        for name, version in capabilities.items()
    ):
        raise ProductPackageError("Laplace capabilities are invalid")
    for field in ("root", "stage_root", "c_compiler", "cxx_compiler", "blake3_source"):
        require_absolute(build.get(field), f"build.{field}")
    if build.get("configuration") != "Release":
        raise ProductPackageError("product package must use the Release configuration")
    if build.get("testing") is not False or build.get("dotnet_bindings") is not False:
        raise ProductPackageError("runtime product package cannot include test or SDK builds")
    if not isinstance(build.get("parallel_jobs"), int) or not 1 <= build["parallel_jobs"] <= 64:
        raise ProductPackageError("build.parallel_jobs is invalid")
    if host_provider.get("receipt_schema") != "laplace.postgresql-host-build-provider/v1":
        raise ProductPackageError("host build provider receipt schema is invalid")
    require_relative(host_provider.get("verifier"), "host_build_provider.verifier")
    if host_provider.get("sandbox_executable") != "/usr/bin/bwrap":
        raise ProductPackageError("product build sandbox executable is invalid")
    if host_provider.get("additional_receipted_roots") != ["/var/lib/dpkg"]:
        raise ProductPackageError("additional host build-provider roots are invalid")
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


def verify_product_toolchain(postgresql: Mapping[str, Any]) -> dict[str, Any]:
    selected = postgresql.get("build_toolchain")
    if not isinstance(selected, dict):
        raise ProductPackageError("PostgreSQL receipt omits its selected build toolchain")
    receipt_path = require_absolute(
        selected.get("receipt_path"), "postgresql_receipt.build_toolchain.receipt_path"
    )
    receipt_sha256 = require_string(
        selected.get("receipt_sha256"),
        "postgresql_receipt.build_toolchain.receipt_sha256",
    )
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
    if not isinstance(package, dict):
        raise ProductPackageError("build-toolchain receipt omits its consumer manifest")
    manifest_path = require_absolute(
        package.get("consumer_manifest_path"),
        "build_toolchain.package.consumer_manifest_path",
    )
    manifest_sha256 = require_string(
        package.get("consumer_manifest_sha256"),
        "build_toolchain.package.consumer_manifest_sha256",
    )
    prefix = require_absolute(package.get("prefix"), "build_toolchain.package.prefix")
    if (
        not manifest_path.is_file()
        or manifest_path.is_symlink()
        or sha256_file(manifest_path) != manifest_sha256
        or not prefix.is_dir()
        or prefix.is_symlink()
    ):
        raise ProductPackageError("build-toolchain consumer manifest or prefix differs")
    manifest = load_json(manifest_path)
    if (
        manifest.get("schema") != "laplace.toolchain-consumer-manifest/v1"
        or manifest.get("build_input_id") != selected.get("build_input_id")
        or Path(str(manifest.get("prefix", ""))) != prefix
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
        path = verify_receipted_file(record, f"build_toolchain.tools.{name}")
        if not path.is_relative_to(prefix):
            raise ProductPackageError(f"selected {name} escaped the toolchain prefix")
        verified_tools[name] = dict(record)
    selected_readelf = selected.get("tools", {}).get("readelf")
    if selected_readelf != verified_tools["readelf"]:
        raise ProductPackageError("PostgreSQL and product toolchain readelf differ")
    return {
        "schema": "laplace.product-build-toolchain/v1",
        "build_input_id": selected["build_input_id"],
        "receipt_path": str(receipt_path),
        "receipt_sha256": receipt_sha256,
        "prefix": str(prefix),
        "consumer_manifest_path": str(manifest_path),
        "consumer_manifest_sha256": manifest_sha256,
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


def verify_host_build_provider(
    contract: Mapping[str, Any], repository: Path, postgresql: Mapping[str, Any]
) -> dict[str, Any]:
    selected = postgresql.get("host_build_provider")
    if not isinstance(selected, dict) or selected.get("schema") != contract[
        "host_build_provider"
    ]["receipt_schema"]:
        raise ProductPackageError("PostgreSQL receipt omits its exact host build provider")
    receipt_path = require_absolute(
        selected.get("receipt_path"), "postgresql_receipt.host_build_provider.receipt_path"
    )
    receipt_sha256 = require_string(
        selected.get("receipt_sha256"),
        "postgresql_receipt.host_build_provider.receipt_sha256",
    )
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
        "verifier": {
            "path": contract["host_build_provider"]["verifier"],
            "sha256": sha256_file(verifier),
        },
    }


def verify_postgresql_receipt(
    contract: dict[str, Any], receipt_path: Path
) -> tuple[dict[str, Any], Path, Path]:
    receipt = load_json(receipt_path)
    if receipt.get("schema") != contract["postgresql"]["receipt_schema"]:
        raise ProductPackageError("PostgreSQL package receipt schema differs")
    if receipt.get("version") != contract["postgresql"]["version"]:
        raise ProductPackageError("PostgreSQL package version differs")
    prefix = require_absolute(receipt.get("prefix"), "postgresql_receipt.prefix")
    readelf = require_absolute(
        receipt.get("build_toolchain", {}).get("tools", {}).get("readelf", {}).get("path"),
        "postgresql_receipt.build_toolchain.tools.readelf.path",
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


def create_plan(
    contract: dict[str, Any],
    repository: Path,
    postgresql_receipt_path: Path,
    installed_lock_path: Path,
    *,
    require_clean: bool = True,
) -> dict[str, Any]:
    validate_contract(contract)
    postgresql, prefix, readelf = verify_postgresql_receipt(
        contract, postgresql_receipt_path
    )
    toolchain = verify_product_toolchain(postgresql)
    host_build_provider = verify_host_build_provider(contract, repository, postgresql)
    lock, providers, provider_roots = verify_installed_providers(
        contract, installed_lock_path
    )
    blake3_root = require_absolute(contract["build"]["blake3_source"], "build.blake3_source")
    build_input_roots = {
        "blake3": exact_tree_receipt(blake3_root),
        **provider_roots,
    }
    for root_value in contract["host_build_provider"]["additional_receipted_roots"]:
        root = require_absolute(root_value, "host_build_provider.additional_receipted_roots")
        build_input_roots[f"host:{root}"] = exact_tree_receipt(root)
    source = repository_identity(repository, require_clean)
    driver = Path(__file__).resolve()
    recipe = {
        "contract_sha256": canonical_sha256(contract),
        "driver_sha256": sha256_file(driver),
        "installed_lock_sha256": sha256_file(installed_lock_path),
        "postgresql_receipt_sha256": sha256_file(postgresql_receipt_path),
        "toolchain_receipt_sha256": toolchain["receipt_sha256"],
        "host_build_provider_receipt_sha256": host_build_provider["receipt_sha256"],
        "build_input_roots": build_input_roots,
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
            "receipt_path": str(postgresql_receipt_path.resolve()),
            "receipt_sha256": sha256_file(postgresql_receipt_path),
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
        "build_input_roots": build_input_roots,
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
    postgresql = load_json(Path(plan["postgresql"]["receipt_path"]))
    toolchain = verify_product_toolchain(postgresql)
    if toolchain != plan["product_toolchain"]:
        raise ProductPackageError("product build toolchain changed during construction")
    host_provider = verify_host_build_provider(contract, repository, postgresql)
    if host_provider != plan["host_build_provider"]:
        raise ProductPackageError("host build provider changed during product construction")
    roots = {
        name: exact_tree_receipt(Path(receipt["path"]))
        for name, receipt in plan["build_input_roots"].items()
    }
    if roots != plan["build_input_roots"]:
        raise ProductPackageError("product build provider roots changed during construction")
    return {
        "schema": "laplace.product-build-input-closure-receipt/v1",
        "toolchain": toolchain,
        "host_build_provider": host_provider,
        "build_input_roots": roots,
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
    if set(observed_host) != set(expected_host):
        raise ProductPackageError("product selected a different platform ABI object set")
    for path, expected in expected_host.items():
        if observed_host[path].get("sha256") != expected.get("sha256"):
            raise ProductPackageError(f"product platform ABI bytes differ: {path}")
    return {
        "schema": "laplace.product-recursive-elf-closure-receipt/v1",
        "report": str(output_path),
        "report_sha256": sha256_file(output_path),
        "summary": summary,
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
    postgresql_receipt = load_json(Path(plan["postgresql"]["receipt_path"]))
    source_prefix = Path(plan["postgresql"]["prefix"])
    source_tree = tree_fingerprint(source_prefix)
    shutil.copytree(source_prefix, staged_prefix, symlinks=True)
    if tree_fingerprint(staged_prefix) != source_tree:
        raise ProductPackageError("PostgreSQL copy differs before Laplace overlay")
    provider_receipts = copy_provider_files(plan["installed_providers"], staged_prefix)
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


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=".")
    parser.add_argument("--contract", default="contracts/product-package.json")
    parser.add_argument(
        "--installed-lock", default="dependencies/installed-lock.json"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("validate-contract")
    for name in ("plan", "build"):
        command = subparsers.add_parser(name)
        command.add_argument("--postgresql-receipt", required=True)
        command.add_argument("--allow-dirty", action="store_true")
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
        Path(arguments.postgresql_receipt),
        installed_lock,
        require_clean=not arguments.allow_dirty,
    )
    if arguments.command == "plan":
        print(json.dumps(plan, indent=2, sort_keys=True))
    else:
        print(json.dumps(execute_plan(contract, repository, plan), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (ProductPackageError, subprocess.CalledProcessError) as error:
        print(f"product-package: {error}", file=sys.stderr)
        raise SystemExit(1) from error
