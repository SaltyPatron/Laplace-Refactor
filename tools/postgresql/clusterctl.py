#!/usr/bin/env python3
"""Plan and verify an isolated Laplace PostgreSQL product cluster.

This tool deliberately does not start PostgreSQL or invoke systemd.  It renders and
stages only manifest-owned candidate files.  Committing the active package pointer
requires a separately captured, exact loaded-object observation.
"""

from __future__ import annotations

import argparse
import grp
import hashlib
import json
import os
import posixpath
import pwd
import re
import shutil
import stat
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Sequence


CONTRACT_SCHEMA = "laplace.postgresql-cluster-contract/v1"
PACKAGE_SCHEMA = "laplace.package-manifest/v1"
RESOURCE_SCHEMA = "laplace.execution-resource-observation/v1"
COLLISION_SCHEMA = "laplace.postgresql-collision-observation/v2"
PLAN_SCHEMA = "laplace.postgresql-cluster-plan/v1"
LOADED_SCHEMA = "laplace.postgresql-loaded-observation/v1"
STOPPED_SCHEMA = "laplace.postgresql-stopped-observation/v1"
ACTIVATION_SCHEMA = "laplace.postgresql-activation-receipt/v1"
INSTALLATION_SCHEMA = "laplace.product-package-installation-receipt/v1"
HEX_256 = re.compile(r"^[0-9a-f]{64}$")
IDENTIFIER = re.compile(r"^[a-z][a-z0-9_]*$")
OS_IDENTIFIER = re.compile(r"^[a-z_][a-z0-9_-]*$")
SERVICE = re.compile(r"^[a-zA-Z0-9_.@-]+\.service$")


class ClusterError(RuntimeError):
    pass


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    output: dict[str, Any] = {}
    for key, value in pairs:
        if key in output:
            raise ClusterError(f"duplicate JSON object key: {key}")
        output[key] = value
    return output


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ClusterError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ClusterError(f"JSON root must be an object: {path}")
    return value


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def package_identity(manifest: dict[str, Any]) -> str:
    payload = dict(manifest)
    payload.pop("package_id", None)
    payload.pop("root", None)
    return sha256_bytes(canonical_bytes(payload))


def require_string(document: dict[str, Any], key: str) -> str:
    value = document.get(key)
    if not isinstance(value, str) or not value:
        raise ClusterError(f"{key} must be a non-empty string")
    return value


def require_absolute_path(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.startswith("/"):
        raise ClusterError(f"{field} must be an absolute path")
    path = PurePosixPath(value)
    if ".." in path.parts or str(path) != value:
        raise ClusterError(f"{field} must be a normalized absolute path")
    return value


def require_relative_path(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value or value.startswith("/"):
        raise ClusterError(f"{field} must be a relative path")
    path = PurePosixPath(value)
    if ".." in path.parts or str(path) != value:
        raise ClusterError(f"{field} must be a normalized relative path")
    return value


def validate_identifier(value: Any, field: str) -> str:
    if not isinstance(value, str) or IDENTIFIER.fullmatch(value) is None:
        raise ClusterError(f"{field} is not a safe PostgreSQL identifier")
    return value


def prefixed(root: Path, absolute: str) -> Path:
    return root.joinpath(*PurePosixPath(absolute).parts[1:])


def validate_contract(document: dict[str, Any]) -> None:
    if document.get("schema") != CONTRACT_SCHEMA:
        raise ClusterError(f"contract schema must be {CONTRACT_SCHEMA}")
    package = document.get("package")
    instance = document.get("instance")
    security = document.get("security")
    resources = document.get("resource_policy")
    if not all(isinstance(item, dict) for item in (package, instance, security, resources)):
        raise ClusterError(
            "contract package, instance, security, and resource_policy must be objects"
        )

    release_root = require_absolute_path(package.get("release_root"), "package.release_root")
    active_link = require_absolute_path(package.get("active_link"), "package.active_link")
    if PurePosixPath(active_link).parent != PurePosixPath(release_root).parent:
        raise ClusterError("package.active_link must be a sibling of the release root")
    required_files = package.get("required_files")
    required_loaded = package.get("required_loaded_objects")
    if not isinstance(required_files, list) or not required_files:
        raise ClusterError("package.required_files must be a non-empty array")
    if not isinstance(required_loaded, list) or not required_loaded:
        raise ClusterError("package.required_loaded_objects must be a non-empty array")
    for index, relative in enumerate(required_files):
        require_relative_path(relative, f"package.required_files[{index}]")
    for index, relative in enumerate(required_loaded):
        require_relative_path(relative, f"package.required_loaded_objects[{index}]")
    if len(set(required_files)) != len(required_files):
        raise ClusterError("package.required_files contains duplicates")
    if len(set(required_loaded)) != len(required_loaded):
        raise ClusterError("package.required_loaded_objects contains duplicates")
    if not set(required_loaded).issubset(required_files):
        raise ClusterError("every required loaded object must be a required package file")
    if package.get("manifest_schema") != PACKAGE_SCHEMA:
        raise ClusterError("package manifest schema declaration is invalid")
    if package.get("postgresql_version") != "18.6":
        raise ClusterError("first cluster contract must select PostgreSQL 18.6")
    if not isinstance(package.get("required_capabilities"), dict):
        raise ClusterError("package required capabilities must be an object")

    for field in ("id", "database", "admin_role", "app_role"):
        validate_identifier(instance.get(field), f"instance.{field}")
    for field in ("os_user", "os_group"):
        value = instance.get(field)
        if not isinstance(value, str) or OS_IDENTIFIER.fullmatch(value) is None:
            raise ClusterError(f"instance.{field} is invalid")
    service = instance.get("service")
    if not isinstance(service, str) or SERVICE.fullmatch(service) is None:
        raise ClusterError("instance.service is invalid")
    instance_paths = []
    for field in (
        "socket_directory",
        "data_directory",
        "wal_directory",
        "temp_directory",
        "config_directory",
        "log_directory",
        "receipt_directory",
    ):
        value = require_absolute_path(instance.get(field), f"instance.{field}")
        if len(PurePosixPath(value).parts) < 3:
            raise ClusterError(f"instance.{field} is too broad")
        instance_paths.append(value)
    for index, path in enumerate(instance_paths):
        for other in instance_paths[index + 1 :]:
            left = PurePosixPath(path)
            right = PurePosixPath(other)
            if left == right or left in right.parents or right in left.parents:
                raise ClusterError("instance paths must be distinct and non-nested")
    port = instance.get("port")
    if not isinstance(port, int) or not 1024 <= port <= 65535:
        raise ClusterError("instance.port must be an unprivileged TCP port")
    if security.get("listen_addresses") != "":
        raise ClusterError("first product cluster must be Unix-socket-only")
    forbidden_auth = security.get("forbid_auth_methods")
    forbidden_environment = security.get("forbid_environment")
    if not isinstance(forbidden_auth, list) or any(
        not isinstance(item, str) for item in forbidden_auth
    ):
        raise ClusterError("security forbidden authentication methods are invalid")
    if not isinstance(forbidden_environment, list) or any(
        not isinstance(item, str) for item in forbidden_environment
    ):
        raise ClusterError("security forbidden environment list is invalid")
    if "trust" not in forbidden_auth:
        raise ClusterError("security contract must reject trust authentication")
    if security.get("app_os_user") != instance.get("os_user"):
        raise ClusterError("application peer mapping must use the service identity")
    if security.get("admin_os_user") == instance.get("os_user"):
        raise ClusterError("service identity cannot map to database administrator at runtime")
    for field in ("admin_os_user", "app_os_user"):
        value = security.get(field)
        if not isinstance(value, str) or OS_IDENTIFIER.fullmatch(value) is None:
            raise ClusterError(f"security.{field} is invalid")
    for field in ("admin_map", "app_map"):
        validate_identifier(security.get(field), f"security.{field}")
    if security.get("socket_mode") != "0770":
        raise ClusterError("first product socket mode must be 0770")
    if not {"LD_LIBRARY_PATH", "LD_PRELOAD"}.issubset(
        set(forbidden_environment)
    ):
        raise ClusterError("security contract must reject ambient loader state")
    preload = security.get("allowed_preload_libraries")
    if preload != ["pg_stat_statements"]:
        raise ClusterError("first product preload set must contain only pg_stat_statements")

    for field in (
        "minimum_cpu_slots",
        "maximum_cpu_slots",
        "minimum_memory_bytes",
        "maximum_memory_bytes",
        "minimum_io_slots",
        "maximum_io_slots",
        "postgresql_memory_numerator",
        "postgresql_memory_denominator",
        "memory_high_numerator",
        "memory_high_denominator",
        "max_connections_per_cpu_slot",
        "maximum_connections",
        "min_wal_size_bytes",
        "max_wal_size_bytes",
        "wal_headroom_multiplier",
    ):
        if not isinstance(resources.get(field), int) or resources[field] <= 0:
            raise ClusterError(f"resource_policy.{field} must be positive")
    for minimum, maximum in (
        ("minimum_cpu_slots", "maximum_cpu_slots"),
        ("minimum_memory_bytes", "maximum_memory_bytes"),
        ("minimum_io_slots", "maximum_io_slots"),
    ):
        if resources[minimum] > resources[maximum]:
            raise ClusterError(f"resource policy {minimum}/{maximum} order is invalid")
    if resources["postgresql_memory_numerator"] >= resources["postgresql_memory_denominator"]:
        raise ClusterError("PostgreSQL working-memory fraction must leave service headroom")
    if resources["memory_high_numerator"] >= resources["memory_high_denominator"]:
        raise ClusterError("service memory-high fraction must remain below the hard grant")
    if resources["min_wal_size_bytes"] >= resources["max_wal_size_bytes"]:
        raise ClusterError("minimum WAL size must be smaller than maximum")


def resource_observation_identity(observation: dict[str, Any]) -> str:
    payload = dict(observation)
    payload.pop("observation_sha256", None)
    return sha256_bytes(canonical_bytes(payload))


def validate_resource_observation(
    document: dict[str, Any], contract: dict[str, Any]
) -> dict[str, Any]:
    if document.get("schema") != RESOURCE_SCHEMA:
        raise ClusterError(f"resource observation schema must be {RESOURCE_SCHEMA}")
    if document.get("source") != "laplace_native_execution_authority":
        raise ClusterError("resource observation must come from native execution authority")
    if document.get("observation_sha256") != resource_observation_identity(document):
        raise ClusterError("resource observation digest differs from its content")
    for field in (
        "topology_receipt",
        "root_grant_receipt",
        "partition_receipt",
        "processor_allocation_receipt",
        "storage_observation_receipt",
    ):
        if HEX_256.fullmatch(str(document.get(field, ""))) is None:
            raise ClusterError(f"resource observation requires exact {field}")
    grant = document.get("grant")
    storage = document.get("storage")
    if not isinstance(grant, dict) or not isinstance(storage, dict):
        raise ClusterError("native resource grant and storage observation are required")
    cpu_ids = grant.get("processor_ids")
    cpu_slots = grant.get("cpu_slots")
    memory_bytes = grant.get("memory_bytes")
    io_slots = grant.get("io_slots")
    if (
        not isinstance(cpu_ids, list)
        or not cpu_ids
        or any(not isinstance(item, int) or item < 0 for item in cpu_ids)
        or len(set(cpu_ids)) != len(cpu_ids)
        or not isinstance(cpu_slots, int)
        or cpu_slots != len(cpu_ids)
        or not isinstance(memory_bytes, int)
        or not isinstance(io_slots, int)
    ):
        raise ClusterError("native resource grant shape is invalid")
    policy = contract["resource_policy"]
    if not policy["minimum_cpu_slots"] <= cpu_slots <= policy["maximum_cpu_slots"]:
        raise ClusterError("native CPU grant is outside the declared policy")
    if not policy["minimum_memory_bytes"] <= memory_bytes <= policy["maximum_memory_bytes"]:
        raise ClusterError("native memory grant is outside the declared policy")
    if not policy["minimum_io_slots"] <= io_slots <= policy["maximum_io_slots"]:
        raise ClusterError("native I/O grant is outside the declared policy")
    for name, expected_path in (
        ("data", contract["instance"]["data_directory"]),
        ("wal", contract["instance"]["wal_directory"]),
        ("temporary", contract["instance"]["temp_directory"]),
    ):
        volume = storage.get(name)
        if (
            not isinstance(volume, dict)
            or volume.get("path") != expected_path
            or not isinstance(volume.get("available_bytes"), int)
            or volume["available_bytes"] <= 0
        ):
            raise ClusterError(f"resource {name} storage observation is missing or mismatched")
    required_wal = policy["max_wal_size_bytes"] * policy["wal_headroom_multiplier"]
    if required_wal > storage["wal"]["available_bytes"]:
        raise ClusterError("configured WAL budget exceeds observed capacity headroom")
    return grant


def collision_observation_identity(observation: dict[str, Any]) -> str:
    payload = dict(observation)
    payload.pop("observation_sha256", None)
    return sha256_bytes(canonical_bytes(payload))


def state_observation_identity(observation: dict[str, Any]) -> str:
    payload = dict(observation)
    payload.pop("observation_sha256", None)
    return sha256_bytes(canonical_bytes(payload))


def collision_target(contract: dict[str, Any]) -> dict[str, Any]:
    instance = contract["instance"]
    return {
        "service": instance["service"],
        "port": instance["port"],
        "socket_directory": instance["socket_directory"],
        "paths": sorted(
            {
                instance["socket_directory"],
                instance["data_directory"],
                instance["wal_directory"],
                instance["temp_directory"],
                instance["config_directory"],
                instance["log_directory"],
                instance["receipt_directory"],
                f"/etc/systemd/system/{instance['service']}",
            }
        ),
    }


def validate_collision_observation(
    observation: dict[str, Any], contract: dict[str, Any]
) -> None:
    if observation.get("schema") != COLLISION_SCHEMA:
        raise ClusterError(f"collision observation schema must be {COLLISION_SCHEMA}")
    if observation.get("source") not in {
        "laplace_clusterctl_live_probe",
        "laplace_typed_fixture",
    }:
        raise ClusterError("collision observation source is unsupported")
    require_absolute_path(observation.get("root"), "collision.root")
    if observation.get("target") != collision_target(contract):
        raise ClusterError("collision observation targets differ from the cluster contract")
    collisions = observation.get("collisions")
    if not isinstance(collisions, list) or any(not isinstance(item, dict) for item in collisions):
        raise ClusterError("collision observation findings must be an array of objects")
    inspection_errors = observation.get("inspection_errors")
    if not isinstance(inspection_errors, list) or any(
        not isinstance(item, dict) for item in inspection_errors
    ):
        raise ClusterError("collision observation inspection_errors must be an array of objects")
    expected = collision_observation_identity(observation)
    if observation.get("observation_sha256") != expected:
        raise ClusterError("collision observation digest differs from its content")
    if inspection_errors:
        first = inspection_errors[0]
        raise ClusterError(
            "cluster collision inspection is incomplete: "
            f"{first.get('operation', 'unknown')} {first.get('target', '')}: "
            f"{first.get('error', 'unknown error')}"
        )
    if collisions:
        first = collisions[0]
        raise ClusterError(
            f"cluster target collision: {first.get('kind', 'unknown')} {first.get('target', '')}"
        )


def inspect_collisions(contract: dict[str, Any], root: Path) -> dict[str, Any]:
    validate_contract(contract)
    if not root.is_absolute():
        raise ClusterError("collision inspection root must be absolute")
    target = collision_target(contract)
    findings: list[dict[str, Any]] = []
    inspection_errors: list[dict[str, Any]] = []

    def record_inspection_error(operation: str, target_value: Any, error: OSError) -> None:
        inspection_errors.append(
            {
                "operation": operation,
                "target": target_value,
                "errno": error.errno,
                "error": error.strerror or error.__class__.__name__,
            }
        )

    for path in target["paths"]:
        physical = prefixed(root, path)
        try:
            os.lstat(physical)
        except FileNotFoundError:
            continue
        except OSError as error:
            record_inspection_error("lstat", path, error)
        else:
            findings.append({"kind": "path", "target": path})
    if root == Path("/"):
        service = target["service"]
        for directory in (
            "/run/systemd/system",
            "/usr/lib/systemd/system",
            "/lib/systemd/system",
        ):
            candidate = Path(directory) / service
            try:
                os.lstat(candidate)
            except FileNotFoundError:
                continue
            except OSError as error:
                record_inspection_error("lstat", str(candidate), error)
            else:
                findings.append({"kind": "service", "target": service})
                break
        port_hex = f"{target['port']:04X}"
        for network_table in (Path("/proc/net/tcp"), Path("/proc/net/tcp6")):
            try:
                rows = network_table.read_text(encoding="ascii").splitlines()[1:]
            except OSError as error:
                record_inspection_error("read", str(network_table), error)
                rows = []
            if any(
                len(fields := row.split()) > 3
                and fields[1].rsplit(":", 1)[-1] == port_hex
                and fields[3] == "0A"
                for row in rows
            ):
                findings.append({"kind": "tcp-port", "target": target["port"]})
                break
        socket_path = f"{target['socket_directory']}/.s.PGSQL.{target['port']}"
        try:
            unix_rows = Path("/proc/net/unix").read_text(encoding="utf-8").splitlines()[1:]
        except OSError as error:
            record_inspection_error("read", "/proc/net/unix", error)
            unix_rows = []
        if any(row.split()[-1] == socket_path for row in unix_rows if len(row.split()) >= 8):
            findings.append({"kind": "unix-socket", "target": socket_path})
        process_needles = (
            target["socket_directory"].encode("utf-8"),
            contract["instance"]["data_directory"].encode("utf-8"),
        )
        try:
            processes = list(Path("/proc").iterdir())
        except OSError as error:
            record_inspection_error("list", "/proc", error)
            processes = []
        for process in processes:
            if not process.name.isdigit():
                continue
            try:
                command = (process / "cmdline").read_bytes()
            except FileNotFoundError:
                continue
            except OSError as error:
                record_inspection_error("read", str(process / "cmdline"), error)
                continue
            if command and any(needle in command for needle in process_needles):
                try:
                    owner_uid = process.stat().st_uid
                except FileNotFoundError:
                    owner_uid = None
                except OSError as error:
                    record_inspection_error("stat", str(process), error)
                    owner_uid = None
                findings.append(
                    {
                        "kind": "process",
                        "target": int(process.name),
                        "owner_uid": owner_uid,
                    }
                )
    observation = {
        "schema": COLLISION_SCHEMA,
        "source": "laplace_clusterctl_live_probe",
        "root": str(root),
        "target": target,
        "collisions": findings,
        "inspection_errors": inspection_errors,
    }
    observation["observation_sha256"] = collision_observation_identity(observation)
    return observation


@dataclass(frozen=True)
class PackageStatus:
    verified: bool
    reason: str
    manifest_sha256: str
    files: dict[str, dict[str, Any]]


def verify_package(
    manifest: dict[str, Any],
    contract: dict[str, Any],
    physical_root: Path | None,
) -> PackageStatus:
    manifest_sha = sha256_bytes(canonical_bytes(manifest))
    if manifest.get("schema") != PACKAGE_SCHEMA:
        raise ClusterError(f"package manifest schema must be {PACKAGE_SCHEMA}")
    package_id = require_string(manifest, "package_id")
    if HEX_256.fullmatch(package_id) is None:
        raise ClusterError("package_id must be a full lowercase SHA-256 digest")
    if contract["package"].get("identity_algorithm") != "sha256-manifest-payload-v1":
        raise ClusterError("package identity algorithm is unsupported")
    if package_identity(manifest) != package_id:
        raise ClusterError("package_id differs from the canonical manifest payload")
    expected_root = f"{contract['package']['release_root']}/{package_id}"
    root = require_absolute_path(manifest.get("root"), "package.root")
    if root != expected_root:
        raise ClusterError(f"package root must be {expected_root}")
    postgresql = manifest.get("postgresql")
    if not isinstance(postgresql, dict) or postgresql.get("version") != contract["package"]["postgresql_version"]:
        raise ClusterError("package PostgreSQL version does not satisfy the cluster contract")
    pg_config = require_relative_path(postgresql.get("pg_config"), "package.postgresql.pg_config")
    if pg_config != "pgsql-18/bin/pg_config":
        raise ClusterError("package pg_config must resolve inside the immutable package")
    capabilities = manifest.get("capabilities")
    if not isinstance(capabilities, dict):
        raise ClusterError("package capabilities are required")
    for name, version in contract["package"]["required_capabilities"].items():
        if capabilities.get(name) != version:
            raise ClusterError(f"package capability {name}={version} is required")
    if manifest.get("activation_eligible") is not True:
        raise ClusterError("package is not activation eligible")
    gates = manifest.get("activation_gates")
    if not isinstance(gates, dict) or not gates or any(value is not True for value in gates.values()):
        raise ClusterError("every package activation gate must be proven")
    environment = manifest.get("loader_environment")
    if not isinstance(environment, dict):
        raise ClusterError("package loader_environment is required")
    for name in contract["security"]["forbid_environment"]:
        if environment.get(name) not in (None, ""):
            raise ClusterError(f"ambient loader environment {name} is forbidden")

    file_entries = manifest.get("files")
    if not isinstance(file_entries, list):
        raise ClusterError("package files must be an array")
    files: dict[str, dict[str, Any]] = {}
    for index, entry in enumerate(file_entries):
        if not isinstance(entry, dict):
            raise ClusterError("package file entry must be an object")
        relative = require_relative_path(entry.get("path"), f"package.files[{index}].path")
        kind = entry.get("kind", "file")
        if kind not in {"file", "symlink"}:
            raise ClusterError(f"package file {relative} has unsupported kind")
        digest = entry.get("sha256")
        if HEX_256.fullmatch(digest or "") is None:
            raise ClusterError(f"package file {relative} has invalid SHA-256")
        if relative in files:
            raise ClusterError(f"duplicate package file: {relative}")
        runpath = entry.get("runpath", [])
        if not isinstance(runpath, list) or any(not isinstance(item, str) for item in runpath):
            raise ClusterError(f"package file {relative} has invalid RUNPATH")
        for item in runpath:
            if item != "$ORIGIN" and not item.startswith("$ORIGIN/"):
                raise ClusterError(f"package file {relative} has non-package RUNPATH")
            suffix = item.removeprefix("$ORIGIN").lstrip("/")
            resolved = posixpath.normpath(posixpath.join(posixpath.dirname(relative), suffix))
            if resolved == ".." or resolved.startswith("../"):
                raise ClusterError(f"package file {relative} has escaping RUNPATH")
        if kind == "file":
            mode = entry.get("mode")
            if not isinstance(mode, int) or mode & 0o022 or mode not in (0o644, 0o755):
                raise ClusterError(f"package file {relative} has unsafe or invalid mode")
            if entry.get("target") is not None:
                raise ClusterError(f"package file {relative} cannot declare a symlink target")
        else:
            target = entry.get("target")
            if (
                not isinstance(target, str)
                or not target
                or target.startswith("/")
                or PurePosixPath(target).is_absolute()
            ):
                raise ClusterError(f"package symlink {relative} has an unsafe target")
            resolved = posixpath.normpath(
                posixpath.join(posixpath.dirname(relative), target)
            )
            if resolved == ".." or resolved.startswith("../"):
                raise ClusterError(f"package symlink {relative} escapes its package")
            if entry.get("mode") is not None:
                raise ClusterError(f"package symlink {relative} cannot declare a mode")
            if runpath:
                raise ClusterError(f"package symlink {relative} cannot declare RUNPATH")
            if digest != sha256_bytes(target.encode("utf-8")):
                raise ClusterError(f"package symlink {relative} digest differs from target")
        files[relative] = entry
    missing = sorted(set(contract["package"]["required_files"]) - set(files))
    if missing:
        raise ClusterError(f"package manifest omits required file: {missing[0]}")
    loaded = manifest.get("loaded_objects")
    if loaded != contract["package"]["required_loaded_objects"]:
        raise ClusterError("package loaded-object set differs from the contract")

    if physical_root is None:
        return PackageStatus(False, "package bytes were not supplied", manifest_sha, files)
    expected_physical_root = prefixed(physical_root, root)
    try:
        expected_physical_root.resolve().relative_to(physical_root.resolve())
    except (OSError, ValueError):
        return PackageStatus(False, "package root escapes the supplied physical root", manifest_sha, files)
    actual_entries = {
        str(path.relative_to(expected_physical_root))
        for path in expected_physical_root.rglob("*")
        if path.is_file() or path.is_symlink()
    }
    if actual_entries != set(files):
        difference = sorted(actual_entries ^ set(files))
        detail = difference[0] if difference else "unknown"
        return PackageStatus(False, f"package tree differs from manifest: {detail}", manifest_sha, files)
    for relative, entry in files.items():
        candidate = expected_physical_root.joinpath(*PurePosixPath(relative).parts)
        try:
            candidate.resolve().relative_to(expected_physical_root.resolve())
        except (OSError, ValueError):
            return PackageStatus(False, f"package file escapes its root: {relative}", manifest_sha, files)
        if entry.get("kind", "file") == "symlink":
            if not candidate.is_symlink():
                return PackageStatus(False, f"package symlink is absent: {relative}", manifest_sha, files)
            target = os.readlink(candidate)
            if target != entry["target"] or sha256_bytes(target.encode("utf-8")) != entry["sha256"]:
                return PackageStatus(False, f"package symlink differs: {relative}", manifest_sha, files)
            if not candidate.exists():
                return PackageStatus(
                    False,
                    f"package symlink target is absent: {relative}",
                    manifest_sha,
                    files,
                )
        else:
            if not candidate.is_file() or candidate.is_symlink():
                return PackageStatus(False, f"package file is absent: {relative}", manifest_sha, files)
            if sha256_file(candidate) != entry["sha256"]:
                return PackageStatus(False, f"package file digest differs: {relative}", manifest_sha, files)
            mode = entry.get("mode")
            if not isinstance(mode, int) or stat.S_IMODE(candidate.stat().st_mode) != mode:
                return PackageStatus(False, f"package file mode differs: {relative}", manifest_sha, files)
    return PackageStatus(True, "all package files and modes verified", manifest_sha, files)


def require_install_root(root: Path, authorize_system_root: bool) -> None:
    if not root.is_absolute():
        raise ClusterError("package installation root must be absolute")
    if root == Path("/") and not authorize_system_root:
        raise ClusterError(
            "system package installation requires --authorize-system-root"
        )


def package_installation_receipt(
    manifest: dict[str, Any],
    status: PackageStatus,
    source_physical_root: Path,
    root: Path,
    installed_release: Path,
) -> dict[str, Any]:
    file_count = 0
    symlink_count = 0
    total_file_bytes = 0
    for relative, entry in status.files.items():
        candidate = installed_release.joinpath(*PurePosixPath(relative).parts)
        if entry.get("kind", "file") == "symlink":
            symlink_count += 1
        else:
            file_count += 1
            total_file_bytes += candidate.stat().st_size
    core = {
        "schema": INSTALLATION_SCHEMA,
        "phase": "installed",
        "package_id": manifest["package_id"],
        "package_manifest_sha256": status.manifest_sha256,
        "package_root": manifest["root"],
        "installation_root": str(root),
        "installed_release": str(installed_release),
        "source_physical_root": str(source_physical_root.resolve()),
        "file_count": file_count,
        "symlink_count": symlink_count,
        "total_file_bytes": total_file_bytes,
        "source_package_verified": True,
        "installed_package_verified": True,
        "overwrite_performed": False,
    }
    core["installation_receipt_sha256"] = sha256_bytes(canonical_bytes(core))
    return core


def install_package(
    manifest: dict[str, Any],
    contract: dict[str, Any],
    source_physical_root: Path,
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    """Atomically place one exact package at its content-addressed release path."""

    validate_contract(contract)
    require_install_root(root, authorize_system_root)
    source_status = verify_package(manifest, contract, source_physical_root)
    if not source_status.verified:
        raise ClusterError(
            f"source package cannot be installed: {source_status.reason}"
        )
    installed_release = prefixed(root, manifest["root"])
    release_root = prefixed(root, contract["package"]["release_root"])
    if installed_release.parent != release_root:
        raise ClusterError("package installation target is outside the release root")
    release_root.mkdir(parents=True, exist_ok=True, mode=0o2755)

    if installed_release.exists() or installed_release.is_symlink():
        installed_status = verify_package(manifest, contract, root)
        if not installed_status.verified:
            raise ClusterError(
                "immutable package release already exists but differs from the manifest: "
                f"{installed_status.reason}"
            )
        return package_installation_receipt(
            manifest,
            installed_status,
            source_physical_root,
            root,
            installed_release,
        )

    temporary_root = Path(
        tempfile.mkdtemp(prefix=f".{manifest['package_id']}.install.", dir=release_root)
    )
    temporary_release = prefixed(temporary_root, manifest["root"])
    try:
        for relative, entry in sorted(source_status.files.items()):
            source = prefixed(source_physical_root, manifest["root"]).joinpath(
                *PurePosixPath(relative).parts
            )
            destination = temporary_release.joinpath(*PurePosixPath(relative).parts)
            destination.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
            if entry.get("kind", "file") == "symlink":
                destination.symlink_to(entry["target"])
            else:
                with source.open("rb") as input_stream, destination.open("xb") as output_stream:
                    shutil.copyfileobj(input_stream, output_stream, length=1024 * 1024)
                    output_stream.flush()
                    os.fsync(output_stream.fileno())
                destination.chmod(entry["mode"])

        staged_status = verify_package(manifest, contract, temporary_root)
        if not staged_status.verified:
            raise ClusterError(
                f"copied package failed pre-install verification: {staged_status.reason}"
            )
        os.rename(temporary_release, installed_release)
        parent_descriptor = os.open(release_root, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(parent_descriptor)
        finally:
            os.close(parent_descriptor)
    finally:
        shutil.rmtree(temporary_root, ignore_errors=True)

    installed_status = verify_package(manifest, contract, root)
    if not installed_status.verified:
        raise ClusterError(
            f"installed package failed verification: {installed_status.reason}"
        )
    return package_installation_receipt(
        manifest,
        installed_status,
        source_physical_root,
        root,
        installed_release,
    )


def memory_setting(bytes_value: int) -> str:
    if bytes_value % (1024 * 1024) != 0:
        raise ClusterError("generated PostgreSQL memory setting is not MiB-aligned")
    return f"{bytes_value // (1024 * 1024)}MB"


def generate_settings(
    contract: dict[str, Any], grant: dict[str, Any]
) -> dict[str, str]:
    policy = contract["resource_policy"]
    mebibyte = 1024 * 1024
    working_mebibytes = (
        (grant["memory_bytes"] // mebibyte)
        * policy["postgresql_memory_numerator"]
        // policy["postgresql_memory_denominator"]
    )
    cpu_slots = grant["cpu_slots"]
    io_slots = grant["io_slots"]
    shared = (working_mebibytes // 4) * mebibyte
    effective_cache = (working_mebibytes * 3 // 4) * mebibyte
    maintenance = min(512, working_mebibytes // 16) * mebibyte
    autovacuum_work = min(256, working_mebibytes // 32) * mebibyte
    return {
        "archive_mode": "off",
        "autovacuum_max_workers": str(max(1, cpu_slots // 2)),
        "autovacuum_work_mem": memory_setting(autovacuum_work),
        "checkpoint_completion_target": "0.9",
        "checkpoint_timeout": "15min",
        "compute_query_id": "on",
        "effective_cache_size": memory_setting(effective_cache),
        "fsync": "on",
        "full_page_writes": "on",
        "huge_pages": "try",
        "io_method": "io_uring",
        "io_workers": str(min(io_slots, cpu_slots)),
        "jit": "off",
        "maintenance_work_mem": memory_setting(maintenance),
        "max_connections": str(
            min(
                policy["maximum_connections"],
                cpu_slots * policy["max_connections_per_cpu_slot"],
            )
        ),
        "max_parallel_workers": str(max(1, cpu_slots // 2)),
        "max_parallel_workers_per_gather": str(max(1, cpu_slots // 2)),
        "max_wal_size": memory_setting(policy["max_wal_size_bytes"]),
        "max_worker_processes": str(cpu_slots * 2),
        "min_wal_size": memory_setting(policy["min_wal_size_bytes"]),
        "password_encryption": "scram-sha-256",
        "shared_buffers": memory_setting(shared),
        "shared_preload_libraries": "pg_stat_statements",
        "synchronous_commit": "on",
        "track_io_timing": "on",
        "track_wal_io_timing": "on",
        "wal_level": "replica",
        "work_mem": "32MB",
    }


def sql_literal(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def render_postgresql_conf(contract: dict[str, Any], package_root: str, settings: dict[str, str]) -> str:
    instance = contract["instance"]
    security = contract["security"]
    config = {
        "data_directory": instance["data_directory"],
        "dynamic_library_path": "$libdir",
        "extension_control_path": "$system",
        "hba_file": f"{instance['config_directory']}/pg_hba.conf",
        "ident_file": f"{instance['config_directory']}/pg_ident.conf",
        "listen_addresses": security["listen_addresses"],
        "log_directory": instance["log_directory"],
        "logging_collector": "on",
        "port": str(instance["port"]),
        "ssl": "off",
        "unix_socket_directories": instance["socket_directory"],
        "unix_socket_permissions": security["socket_mode"],
    }
    config.update(settings)
    quoted = {
        "data_directory",
        "dynamic_library_path",
        "extension_control_path",
        "hba_file",
        "ident_file",
        "listen_addresses",
        "log_directory",
        "shared_preload_libraries",
        "unix_socket_directories",
        "unix_socket_permissions",
    }
    lines = [
        "# Generated from contracts/postgresql-cluster.json; do not edit.",
        f"# Package root: {package_root}",
    ]
    for name in sorted(config):
        value = config[name]
        lines.append(f"{name} = {sql_literal(value) if name in quoted else value}")
    return "\n".join(lines) + "\n"


def render_hba(contract: dict[str, Any]) -> str:
    instance = contract["instance"]
    security = contract["security"]
    return "\n".join(
        (
            "# Generated runtime authentication. Bootstrap authentication is separate.",
            f"local all {instance['admin_role']} peer map={security['admin_map']}",
            f"local {instance['database']} {instance['app_role']} peer map={security['app_map']}",
            "local all all reject",
            "host all all 127.0.0.1/32 reject",
            "host all all ::1/128 reject",
            "",
        )
    )


def render_ident(contract: dict[str, Any]) -> str:
    instance = contract["instance"]
    security = contract["security"]
    return "\n".join(
        (
            "# Generated runtime peer mappings.",
            f"{security['admin_map']} {security['admin_os_user']} {instance['admin_role']}",
            f"{security['app_map']} {security['app_os_user']} {instance['app_role']}",
            "",
        )
    )


def render_bootstrap_sql(contract: dict[str, Any]) -> str:
    instance = contract["instance"]
    database = instance["database"]
    admin = instance["admin_role"]
    app = instance["app_role"]
    temp_name = f"{database}_temp"
    return f"""-- Generated bootstrap for a new, empty cluster only.
CREATE ROLE {app} LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION NOBYPASSRLS;
CREATE TABLESPACE {temp_name} OWNER {admin} LOCATION {sql_literal(instance['temp_directory'])};
CREATE DATABASE {database} OWNER {admin} TEMPLATE template0 ENCODING 'UTF8';
REVOKE ALL ON DATABASE {database} FROM PUBLIC;
GRANT CONNECT ON DATABASE {database} TO {app};
ALTER DATABASE {database} SET temp_tablespaces = {sql_literal(temp_name)};
\\connect {database}
REVOKE CREATE ON SCHEMA public FROM PUBLIC;
CREATE EXTENSION pg_stat_statements;
CREATE EXTENSION laplace;
GRANT USAGE ON SCHEMA laplace TO {app};
GRANT EXECUTE ON FUNCTION laplace.identity_codepoint_calculate_batch(integer[]) TO {app};
GRANT EXECUTE ON FUNCTION laplace.identity_codepoint_execute_batch(integer[]) TO {app};
GRANT EXECUTE ON FUNCTION laplace.trajectory_composition_decode_calculate_batch(bytea[]) TO {app};
GRANT EXECUTE ON FUNCTION laplace.trajectory_composition_decode_execute_batch(bytea[]) TO {app};
REVOKE ALL ON TABLE laplace.execution_receipt FROM {app};
"""


def render_service(
    contract: dict[str, Any], package_root: str, grant: dict[str, Any]
) -> str:
    instance = contract["instance"]
    policy = contract["resource_policy"]
    postgres = f"{package_root}/pgsql-18/bin/postgres"
    pg_isready = f"{package_root}/pgsql-18/bin/pg_isready"
    cpu_ids = " ".join(str(item) for item in grant["processor_ids"])
    memory_high = (
        grant["memory_bytes"]
        * policy["memory_high_numerator"]
        // policy["memory_high_denominator"]
    )
    return f"""# Generated from the verified Laplace PostgreSQL cluster plan.
[Unit]
Description=Isolated Laplace refactor PostgreSQL 18.6 cluster
After=local-fs.target
RequiresMountsFor={instance['data_directory']} {instance['wal_directory']} {instance['temp_directory']}

[Service]
Type=simple
User={instance['os_user']}
Group={instance['os_group']}
RuntimeDirectory=laplace-refactor-postgresql
RuntimeDirectoryMode=0770
ExecStart={postgres} -D {instance['data_directory']} -c config_file={instance['config_directory']}/postgresql.conf
ExecStartPost=/bin/bash -c 'for i in $(seq 1 120); do {pg_isready} -h {instance['socket_directory']} -p {instance['port']} >/dev/null 2>&1 && exit 0; sleep 1; done; exit 1'
ExecReload=/bin/kill -HUP $MAINPID
KillMode=mixed
KillSignal=SIGINT
TimeoutStartSec=180
TimeoutStopSec=120
Restart=on-failure
RestartSec=2
UMask=0027
LimitNOFILE=65536
AllowedCPUs={cpu_ids}
MemoryHigh={memory_high}
MemoryMax={grant['memory_bytes']}
Environment=OMP_NUM_THREADS=1
Environment=MKL_NUM_THREADS=1
Environment=MKL_DYNAMIC=FALSE
NoNewPrivileges=yes
PrivateTmp=yes
ProtectHome=yes
ProtectSystem=strict
ReadWritePaths={instance['data_directory']} {instance['wal_directory']} {instance['temp_directory']} {instance['log_directory']} {instance['receipt_directory']}
RestrictAddressFamilies=AF_UNIX

[Install]
WantedBy=multi-user.target
"""


def build_plan(
    contract_path: Path,
    package_path: Path,
    resource_path: Path,
    collision_path: Path,
    physical_root: Path | None,
) -> dict[str, Any]:
    contract = load_json(contract_path)
    package = load_json(package_path)
    resource_observation = load_json(resource_path)
    collision_observation = load_json(collision_path)
    validate_contract(contract)
    grant = validate_resource_observation(resource_observation, contract)
    validate_collision_observation(collision_observation, contract)
    status = verify_package(package, contract, physical_root)
    package_root = package["root"]
    settings = generate_settings(contract, grant)
    instance = contract["instance"]
    files = {
        f"{instance['config_directory']}/postgresql.conf": render_postgresql_conf(contract, package_root, settings),
        f"{instance['config_directory']}/pg_hba.conf": render_hba(contract),
        f"{instance['config_directory']}/pg_ident.conf": render_ident(contract),
        f"{instance['config_directory']}/bootstrap.sql": render_bootstrap_sql(contract),
        f"/etc/systemd/system/{instance['service']}": render_service(
            contract, package_root, grant
        ),
    }
    rendered = [
        {
            "path": path,
            "mode": 0o640 if path.endswith((".conf", ".sql")) else 0o644,
            "sha256": sha256_bytes(content.encode("utf-8")),
            "content": content,
        }
        for path, content in sorted(files.items())
    ]
    plan_core = {
        "schema": PLAN_SCHEMA,
        "contract_sha256": sha256_bytes(canonical_bytes(contract)),
        "package_manifest_sha256": status.manifest_sha256,
        "package_id": package["package_id"],
        "package_root": package_root,
        "package_verified": status.verified,
        "package_verification": status.reason,
        "resource_observation_sha256": sha256_bytes(
            canonical_bytes(resource_observation)
        ),
        "collision_observation_sha256": sha256_bytes(
            canonical_bytes(collision_observation)
        ),
        "collision_observation_source": collision_observation["source"],
        "collision_observation_root": collision_observation["root"],
        "instance": instance,
        "settings": settings,
        "resource_grant": grant,
        "resource_policy": contract["resource_policy"],
        "files": rendered,
        "state_directories": [
            instance["data_directory"],
            instance["wal_directory"],
            instance["temp_directory"],
            instance["log_directory"],
            instance["receipt_directory"],
        ],
        "required_loaded_objects": [
            {
                "path": f"{package_root}/{relative}",
                "sha256": status.files[relative]["sha256"],
            }
            for relative in contract["package"]["required_loaded_objects"]
        ],
        "active_link": contract["package"]["active_link"],
        "commands": {
            "initdb": [
                "runuser",
                "--user",
                instance["os_user"],
                "--",
                f"{package_root}/pgsql-18/bin/initdb",
                f"--pgdata={instance['data_directory']}",
                f"--waldir={instance['wal_directory']}",
                "--data-checksums",
                "--encoding=UTF8",
                "--no-locale",
                "--auth-local=peer",
                "--auth-host=reject",
                f"--username={instance['admin_role']}",
            ],
            "bootstrap": [
                "runuser",
                "--user",
                contract["security"]["admin_os_user"],
                "--",
                f"{package_root}/pgsql-18/bin/psql",
                "--host",
                instance["socket_directory"],
                "--port",
                str(instance["port"]),
                "--username",
                instance["admin_role"],
                "--dbname",
                "postgres",
                "--no-psqlrc",
                "--set",
                "ON_ERROR_STOP=1",
                "--file",
                f"{instance['config_directory']}/bootstrap.sql",
            ],
            "start_candidate_service": ["systemctl", "start", instance["service"]],
            "stop_candidate_service": ["systemctl", "stop", instance["service"]],
            "daemon_reload": ["systemctl", "daemon-reload"],
        },
        "activation_blocked": not status.verified,
    }
    plan_core["plan_sha256"] = sha256_bytes(canonical_bytes(plan_core))
    validate_plan(plan_core, contract)
    return plan_core


def validate_plan(plan: dict[str, Any], contract: dict[str, Any] | None = None) -> None:
    if plan.get("schema") != PLAN_SCHEMA:
        raise ClusterError(f"plan schema must be {PLAN_SCHEMA}")
    expected = plan.get("plan_sha256")
    if HEX_256.fullmatch(expected or "") is None:
        raise ClusterError("plan digest is invalid")
    candidate = dict(plan)
    candidate.pop("plan_sha256", None)
    if sha256_bytes(canonical_bytes(candidate)) != expected:
        raise ClusterError("plan digest differs from plan content")
    if contract is not None:
        validate_contract(contract)
        if plan.get("contract_sha256") != sha256_bytes(canonical_bytes(contract)):
            raise ClusterError("plan was not generated from the supplied cluster contract")
    files = plan.get("files")
    if not isinstance(files, list):
        raise ClusterError("plan rendered files are required")
    rendered: dict[str, str] = {}
    for entry in files:
        if not isinstance(entry, dict):
            raise ClusterError("plan rendered-file entry must be an object")
        path = require_absolute_path(entry.get("path"), "plan.files.path")
        content = entry.get("content")
        if not isinstance(content, str):
            raise ClusterError(f"plan rendered file has no content: {path}")
        if path in rendered:
            raise ClusterError(f"plan contains duplicate rendered file: {path}")
        if sha256_bytes(content.encode("utf-8")) != entry.get("sha256"):
            raise ClusterError(f"rendered file digest differs: {path}")
        rendered[path] = content

    instance = plan.get("instance")
    if not isinstance(instance, dict):
        raise ClusterError("plan instance is required")
    hba_path = f"{instance['config_directory']}/pg_hba.conf"
    ident_path = f"{instance['config_directory']}/pg_ident.conf"
    service_path = f"/etc/systemd/system/{instance['service']}"
    for path in (hba_path, ident_path, service_path):
        if path not in rendered:
            raise ClusterError(f"plan omits required generated file: {path}")
    hba = rendered[hba_path]
    if re.search(r"(?m)^\s*(?:local|host\S*)\s+.*\s+trust(?:\s|$)", hba):
        raise ClusterError("generated HBA contains trust authentication")
    if not re.search(
        rf"(?m)^local\s+all\s+{re.escape(instance['admin_role'])}\s+peer\s+map=",
        hba,
    ):
        raise ClusterError("generated HBA omits the administrator peer rule")
    if "local all all reject" not in hba:
        raise ClusterError("generated HBA does not reject unmatched local identities")
    ident = rendered[ident_path]
    service_identity = str(instance["os_user"])
    admin_role = str(instance["admin_role"])
    for line in ident.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1:] == [service_identity, admin_role]:
            raise ClusterError("generated peer map elevates the service identity to administrator")
    service = rendered[service_path]
    if "LD_LIBRARY_PATH" in service or "LD_PRELOAD" in service:
        raise ClusterError("generated service depends on ambient loader environment")
    package_root = str(plan.get("package_root", ""))
    if f"ExecStart={package_root}/pgsql-18/bin/postgres " not in service:
        raise ClusterError("generated service does not execute the immutable package postmaster")


def atomic_write(path: Path, content: bytes, mode: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(mode)
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def require_fixture_or_root(root: Path, authorize_system_root: bool) -> None:
    if root == Path("/"):
        if not authorize_system_root or os.geteuid() != 0:
            raise ClusterError("system-root mutation requires root and --authorize-system-root")
    elif not root.is_absolute():
        raise ClusterError("activation root must be absolute")


def apply_plan(
    plan: dict[str, Any],
    contract: dict[str, Any],
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    validate_plan(plan, contract)
    require_fixture_or_root(root, authorize_system_root)
    if root == Path("/") and (
        plan.get("collision_observation_source") != "laplace_clusterctl_live_probe"
        or plan.get("collision_observation_root") != "/"
    ):
        raise ClusterError("system activation requires a live collision observation")
    validate_collision_observation(inspect_collisions(contract, root), contract)
    if not plan.get("package_verified") or plan.get("activation_blocked"):
        raise ClusterError("activation is blocked until the package manifest and bytes verify")
    targets = [(entry, prefixed(root, entry["path"])) for entry in plan["files"]]
    for entry, target in targets:
        if target.exists() or target.is_symlink():
            raise ClusterError(f"activation refuses existing path: {entry['path']}")
    state_targets = [(directory, prefixed(root, directory)) for directory in plan["state_directories"]]
    for directory, target in state_targets:
        if target.exists() or target.is_symlink():
            raise ClusterError(f"activation refuses existing state directory: {directory}")
    installed: list[dict[str, Any]] = []
    created_directories: list[Path] = []
    try:
        for entry, target in targets:
            content = entry["content"].encode("utf-8")
            if sha256_bytes(content) != entry["sha256"]:
                raise ClusterError(f"rendered file digest differs: {entry['path']}")
            atomic_write(target, content, entry["mode"])
            installed.append({"path": entry["path"], "sha256": entry["sha256"]})
        for _directory, target in state_targets:
            target.mkdir(parents=True, exist_ok=False, mode=0o700)
            created_directories.append(target)
        if root == Path("/"):
            service_user = pwd.getpwnam(plan["instance"]["os_user"])
            service_group = grp.getgrnam(plan["instance"]["os_group"])
            for _directory, target in state_targets:
                target.chmod(0o700)
                os.chown(target, service_user.pw_uid, service_group.gr_gid)
            for entry, target in targets:
                if entry["path"].startswith("/etc/laplace/"):
                    os.chown(target, 0, service_group.gr_gid)
    except BaseException:
        for entry in reversed(installed):
            target = prefixed(root, entry["path"])
            if target.is_file() and not target.is_symlink() and sha256_file(target) == entry["sha256"]:
                target.unlink()
        for target in reversed(created_directories):
            try:
                target.rmdir()
            except OSError:
                pass
        raise
    return {
        "schema": ACTIVATION_SCHEMA,
        "phase": "staged",
        "plan_sha256": plan["plan_sha256"],
        "package_id": plan["package_id"],
        "installed_files": installed,
        "state_directories": plan["state_directories"],
        "state_preserved_on_remove": True,
        "previous_active_target": None,
    }


def verify_loaded(
    plan: dict[str, Any], contract: dict[str, Any], observation: dict[str, Any]
) -> None:
    validate_plan(plan, contract)
    if observation.get("schema") != LOADED_SCHEMA:
        raise ClusterError(f"loaded observation schema must be {LOADED_SCHEMA}")
    if observation.get("source") not in {
        "laplace_postgresql_loaded_probe",
        "laplace_typed_fixture",
    }:
        raise ClusterError("loaded observation source is unsupported")
    if observation.get("observation_sha256") != state_observation_identity(observation):
        raise ClusterError("loaded observation digest differs from its content")
    if observation.get("package_id") != plan["package_id"]:
        raise ClusterError("loaded package identity differs from the plan")
    instance = plan["instance"]
    for field in ("port", "socket_directory", "data_directory", "service"):
        if observation.get(field) != instance[field]:
            raise ClusterError(f"loaded observation {field} differs from the plan")
    system_identifier = str(observation.get("system_identifier", ""))
    if not system_identifier.isdecimal() or int(system_identifier) <= 0:
        raise ClusterError("loaded observation omits a valid positive system identifier")
    objects = observation.get("loaded_objects")
    if not isinstance(objects, list):
        raise ClusterError("loaded object observation is required")
    if any(not isinstance(item, dict) for item in objects):
        raise ClusterError("loaded object observation contains an invalid entry")
    observed = {item.get("path"): item.get("sha256") for item in objects}
    expected = {item["path"]: item["sha256"] for item in plan["required_loaded_objects"]}
    if len(observed) != len(objects) or observed != expected:
        raise ClusterError("loaded object paths or hashes differ from the package manifest")
    config = observation.get("config_files")
    expected_config = {item["path"]: item["sha256"] for item in plan["files"]}
    if not isinstance(config, list):
        raise ClusterError("loaded config observation is required")
    if any(not isinstance(item, dict) for item in config):
        raise ClusterError("loaded config observation contains an invalid entry")
    observed_config = {item.get("path"): item.get("sha256") for item in config}
    if len(observed_config) != len(config) or observed_config != expected_config:
        raise ClusterError("loaded configuration differs from the generated plan")


def commit_plan(
    plan: dict[str, Any],
    contract: dict[str, Any],
    receipt: dict[str, Any],
    observation: dict[str, Any],
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    require_fixture_or_root(root, authorize_system_root)
    validate_plan(plan, contract)
    if receipt.get("schema") != ACTIVATION_SCHEMA or receipt.get("phase") != "staged":
        raise ClusterError("activation receipt is not staged")
    if receipt.get("plan_sha256") != plan["plan_sha256"]:
        raise ClusterError("activation receipt belongs to another plan")
    if root == Path("/") and observation.get("source") != "laplace_postgresql_loaded_probe":
        raise ClusterError("system commit requires a live loaded-state observation")
    verify_loaded(plan, contract, observation)
    active = prefixed(root, plan["active_link"])
    active.parent.mkdir(parents=True, exist_ok=True)
    previous: str | None = None
    if active.is_symlink():
        previous = os.readlink(active)
    elif active.exists():
        raise ClusterError("active package path exists and is not a symbolic link")
    target = f"releases/{plan['package_id']}"
    temporary = active.parent / f".{active.name}.{plan['package_id'][:12]}"
    temporary.unlink(missing_ok=True)
    os.symlink(target, temporary)
    os.replace(temporary, active)
    committed = dict(receipt)
    committed["phase"] = "committed"
    committed["previous_active_target"] = previous
    committed["active_target"] = target
    committed["loaded_observation_sha256"] = sha256_bytes(canonical_bytes(observation))
    return committed


def verify_stopped(plan: dict[str, Any], observation: dict[str, Any]) -> None:
    if observation.get("schema") != STOPPED_SCHEMA:
        raise ClusterError(f"stopped observation schema must be {STOPPED_SCHEMA}")
    if observation.get("source") not in {
        "laplace_postgresql_stopped_probe",
        "laplace_typed_fixture",
    }:
        raise ClusterError("stopped observation source is unsupported")
    if observation.get("observation_sha256") != state_observation_identity(observation):
        raise ClusterError("stopped observation digest differs from its content")
    instance = plan["instance"]
    if observation.get("service") != instance["service"]:
        raise ClusterError("stopped observation belongs to another service")
    if observation.get("data_directory") != instance["data_directory"]:
        raise ClusterError("stopped observation belongs to another data directory")
    if observation.get("service_state") != "inactive":
        raise ClusterError("removal requires an inactive candidate service")
    if observation.get("postmaster_pid") is not None:
        raise ClusterError("removal requires proof that no candidate postmaster remains")


def remove_activation(
    plan: dict[str, Any],
    contract: dict[str, Any],
    receipt: dict[str, Any],
    stopped_observation: dict[str, Any],
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    require_fixture_or_root(root, authorize_system_root)
    validate_plan(plan, contract)
    if receipt.get("schema") != ACTIVATION_SCHEMA or receipt.get("plan_sha256") != plan["plan_sha256"]:
        raise ClusterError("activation receipt does not match the plan")
    verify_stopped(plan, stopped_observation)
    if root == Path("/") and stopped_observation.get("source") != (
        "laplace_postgresql_stopped_probe"
    ):
        raise ClusterError("system removal requires a live stopped-state observation")
    active = prefixed(root, plan["active_link"])
    if receipt.get("phase") == "committed":
        if not active.is_symlink() or os.readlink(active) != receipt.get("active_target"):
            raise ClusterError("active package pointer changed after activation")
    for entry in receipt.get("installed_files", []):
        target = prefixed(root, entry["path"])
        if not target.is_file() or target.is_symlink() or sha256_file(target) != entry["sha256"]:
            raise ClusterError(f"refusing removal of changed or absent file: {entry['path']}")
    for entry in reversed(receipt.get("installed_files", [])):
        target = prefixed(root, entry["path"])
        target.unlink()
    if receipt.get("phase") == "committed":
        previous = receipt.get("previous_active_target")
        if previous is None:
            active.unlink()
        else:
            temporary = active.parent / f".{active.name}.rollback"
            temporary.unlink(missing_ok=True)
            os.symlink(previous, temporary)
            os.replace(temporary, active)
    return {
        "schema": ACTIVATION_SCHEMA,
        "phase": "removed",
        "plan_sha256": plan["plan_sha256"],
        "package_id": plan["package_id"],
        "state_directories": receipt.get("state_directories", []),
        "state_preserved": True,
    }


def write_json(path: Path | None, value: dict[str, Any]) -> None:
    content = json.dumps(value, indent=2, sort_keys=True) + "\n"
    if path is None or str(path) == "-":
        sys.stdout.write(content)
    else:
        atomic_write(path, content.encode("utf-8"), 0o640)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    plan = subparsers.add_parser("plan")
    plan.add_argument("--contract", required=True)
    plan.add_argument("--package-manifest", required=True)
    plan.add_argument("--resource-observation", required=True)
    plan.add_argument("--collision-observation", required=True)
    plan.add_argument("--package-physical-root")
    plan.add_argument("--output", default="-")
    install = subparsers.add_parser("install-package")
    install.add_argument("--contract", required=True)
    install.add_argument("--package-manifest", required=True)
    install.add_argument("--package-physical-root", required=True)
    install.add_argument("--root", required=True)
    install.add_argument("--receipt", default="-")
    install.add_argument("--authorize-system-root", action="store_true")
    inspect = subparsers.add_parser("inspect-collisions")
    inspect.add_argument("--contract", required=True)
    inspect.add_argument("--root", default="/")
    inspect.add_argument("--output", default="-")
    apply = subparsers.add_parser("apply")
    apply.add_argument("--plan", required=True)
    apply.add_argument("--contract", required=True)
    apply.add_argument("--root", required=True)
    apply.add_argument("--receipt", default="-")
    apply.add_argument("--authorize-system-root", action="store_true")
    commit = subparsers.add_parser("commit")
    commit.add_argument("--plan", required=True)
    commit.add_argument("--contract", required=True)
    commit.add_argument("--receipt", required=True)
    commit.add_argument("--loaded-observation", required=True)
    commit.add_argument("--root", required=True)
    commit.add_argument("--output", default="-")
    commit.add_argument("--authorize-system-root", action="store_true")
    verify = subparsers.add_parser("verify-loaded")
    verify.add_argument("--plan", required=True)
    verify.add_argument("--contract", required=True)
    verify.add_argument("--loaded-observation", required=True)
    remove = subparsers.add_parser("remove")
    remove.add_argument("--plan", required=True)
    remove.add_argument("--contract", required=True)
    remove.add_argument("--receipt", required=True)
    remove.add_argument("--stopped-observation", required=True)
    remove.add_argument("--root", required=True)
    remove.add_argument("--output", default="-")
    remove.add_argument("--authorize-system-root", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_args(argv)
    if arguments.command == "plan":
        physical = Path(arguments.package_physical_root) if arguments.package_physical_root else None
        result = build_plan(
            Path(arguments.contract),
            Path(arguments.package_manifest),
            Path(arguments.resource_observation),
            Path(arguments.collision_observation),
            physical,
        )
        write_json(Path(arguments.output), result)
        return 0
    if arguments.command == "install-package":
        result = install_package(
            load_json(Path(arguments.package_manifest)),
            load_json(Path(arguments.contract)),
            Path(arguments.package_physical_root),
            Path(arguments.root),
            arguments.authorize_system_root,
        )
        write_json(Path(arguments.receipt), result)
        return 0
    if arguments.command == "inspect-collisions":
        result = inspect_collisions(
            load_json(Path(arguments.contract)), Path(arguments.root)
        )
        write_json(Path(arguments.output), result)
        return 0
    if arguments.command == "apply":
        result = apply_plan(
            load_json(Path(arguments.plan)),
            load_json(Path(arguments.contract)),
            Path(arguments.root),
            arguments.authorize_system_root,
        )
        write_json(Path(arguments.receipt), result)
        return 0
    if arguments.command == "commit":
        result = commit_plan(
            load_json(Path(arguments.plan)),
            load_json(Path(arguments.contract)),
            load_json(Path(arguments.receipt)),
            load_json(Path(arguments.loaded_observation)),
            Path(arguments.root),
            arguments.authorize_system_root,
        )
        write_json(Path(arguments.output), result)
        return 0
    if arguments.command == "verify-loaded":
        verify_loaded(
            load_json(Path(arguments.plan)),
            load_json(Path(arguments.contract)),
            load_json(Path(arguments.loaded_observation)),
        )
        print("loaded PostgreSQL objects and configuration match the activation plan")
        return 0
    if arguments.command == "remove":
        result = remove_activation(
            load_json(Path(arguments.plan)),
            load_json(Path(arguments.contract)),
            load_json(Path(arguments.receipt)),
            load_json(Path(arguments.stopped_observation)),
            Path(arguments.root),
            arguments.authorize_system_root,
        )
        write_json(Path(arguments.output), result)
        return 0
    raise ClusterError(f"unknown command: {arguments.command}")


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except ClusterError as error:
        print(f"clusterctl: {error}", file=sys.stderr)
        raise SystemExit(1) from error
