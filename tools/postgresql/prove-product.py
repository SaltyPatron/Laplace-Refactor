#!/usr/bin/env python3
"""Prove the exact current-change PostgreSQL 18.6 product runtime without activation.

The proof composes the current source into the immutable product package, installs that
package below a fresh proof root, and executes the packaged PostgreSQL under the exact
logical /opt/laplace/current prefix inside a bubblewrap mount namespace.  The live
postmaster/backend file identities must equal the package manifest.  This is a merge
proof surface; it never mutates or claims the system product cluster.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import pwd
import re
import signal
import stat
import subprocess
import sys
import time
from typing import Any, Mapping, Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CLUSTERCTL_PATH = Path(__file__).with_name("clusterctl.py")
SPEC = importlib.util.spec_from_file_location(
    "laplace_postgresql_product_clusterctl", CLUSTERCTL_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load PostgreSQL cluster controller")
clusterctl = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = clusterctl
SPEC.loader.exec_module(clusterctl)

PROOF_SCHEMA = "laplace.postgresql-product-proof/v1"
RUNTIME_SCHEMA = "laplace.postgresql-product-runtime-receipt/v1"
BINDING_SCHEMA = "laplace.postgresql-product-proof-binding/v1"
PACKAGE_PROOF_SCHEMA = "laplace.package-product-proof/v1"
HEX_64 = re.compile(r"^[0-9a-f]{64}$")
GIT_OBJECT = re.compile(r"^[0-9a-f]{40}$")


class PostgreSQLProductProofError(RuntimeError):
    """The exact PostgreSQL product-path proof could not be established."""


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise PostgreSQLProductProofError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise PostgreSQLProductProofError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise PostgreSQLProductProofError(f"JSON root must be an object: {path}")
    return value


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def document_identity(value: Mapping[str, Any], field: str) -> str:
    payload = {key: item for key, item in value.items() if key != field}
    return hashlib.sha256(canonical_bytes(payload)).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_hex(value: Any, label: str) -> str:
    if not isinstance(value, str) or HEX_64.fullmatch(value) is None:
        raise PostgreSQLProductProofError(
            f"{label} is not a lowercase 256-bit hexadecimal identity"
        )
    return value


def require_git_object(value: Any, label: str) -> str:
    if not isinstance(value, str) or GIT_OBJECT.fullmatch(value) is None:
        raise PostgreSQLProductProofError(f"{label} is not an exact Git object identity")
    return value


def require_physical_file(path: Path, label: str) -> None:
    if not path.is_file() or path.is_symlink():
        raise PostgreSQLProductProofError(
            f"{label} is absent or not a physical file: {path}"
        )


def require_physical_directory(path: Path, label: str) -> None:
    if not path.is_absolute() or not path.is_dir() or path.is_symlink():
        raise PostgreSQLProductProofError(
            f"{label} is absent or not a physical absolute directory: {path}"
        )


def command_receipt(
    label: str,
    command: Sequence[str],
    completed: subprocess.CompletedProcess[str],
    elapsed_ns: int,
) -> dict[str, Any]:
    return {
        "label": label,
        "argv": list(command),
        "exit_code": completed.returncode,
        "elapsed_ns": elapsed_ns,
        "stdout_sha256": hashlib.sha256(completed.stdout.encode("utf-8")).hexdigest(),
        "stderr_sha256": hashlib.sha256(completed.stderr.encode("utf-8")).hexdigest(),
    }


def run(
    command: Sequence[str],
    label: str,
    *,
    timeout: int = 1800,
    cwd: Path | None = None,
) -> tuple[subprocess.CompletedProcess[str], dict[str, Any]]:
    started = time.monotonic_ns()
    completed = subprocess.run(
        list(command),
        check=False,
        cwd=str(cwd) if cwd is not None else None,
        env=os.environ.copy(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )
    receipt = command_receipt(label, command, completed, time.monotonic_ns() - started)
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        if len(detail) > 2000:
            detail = detail[-2000:]
        raise PostgreSQLProductProofError(
            f"{label} failed with exit {completed.returncode}: {detail or 'no diagnostic'}"
        )
    return completed, receipt


def repository_identity(repository: Path) -> tuple[str, str]:
    commit, _ = run(
        ["git", "-C", str(repository), "rev-parse", "HEAD"],
        "repository commit identity",
        timeout=30,
    )
    tree, _ = run(
        ["git", "-C", str(repository), "rev-parse", "HEAD^{tree}"],
        "repository tree identity",
        timeout=30,
    )
    return (
        require_git_object(commit.stdout.strip(), "repository commit"),
        require_git_object(tree.stdout.strip(), "repository tree"),
    )


def validate_source_identity(
    package_proof: Mapping[str, Any], expected_commit: str, expected_tree: str
) -> None:
    if (
        package_proof.get("schema") != PACKAGE_PROOF_SCHEMA
        or package_proof.get("phase") != "composed-installed-retained"
        or package_proof.get("repository_commit") != expected_commit
        or package_proof.get("repository_tree") != expected_tree
    ):
        raise PostgreSQLProductProofError(
            "package proof is not bound to the checked-out source identity"
        )


def validate_loader_environment(
    manifest: Mapping[str, Any], cluster_contract: Mapping[str, Any]
) -> None:
    loader = manifest.get("loader_environment")
    if not isinstance(loader, Mapping):
        raise PostgreSQLProductProofError("package loader environment is absent")
    security = cluster_contract.get("security")
    if not isinstance(security, Mapping):
        raise PostgreSQLProductProofError("cluster security contract is absent")
    forbidden = security.get("forbid_environment")
    if not isinstance(forbidden, list):
        raise PostgreSQLProductProofError("forbidden loader environment is absent")
    for name in forbidden:
        if not isinstance(name, str) or not name:
            raise PostgreSQLProductProofError("forbidden loader variable is invalid")
        if loader.get(name) not in (None, ""):
            raise PostgreSQLProductProofError(
                f"package admits forbidden ambient loader state: {name}"
            )


def validate_postgresql_selection(
    manifest: Mapping[str, Any], cluster_contract: Mapping[str, Any]
) -> list[str]:
    package = cluster_contract.get("package")
    if not isinstance(package, Mapping):
        raise PostgreSQLProductProofError("cluster package contract is absent")
    if package.get("postgresql_version") != "18.6":
        raise PostgreSQLProductProofError("cluster contract does not select PostgreSQL 18.6")
    postgresql = manifest.get("postgresql")
    if not isinstance(postgresql, Mapping) or postgresql.get("version") != "18.6":
        raise PostgreSQLProductProofError(
            "product package is not exact PostgreSQL 18.6"
        )
    expected = package.get("required_loaded_objects")
    loaded = manifest.get("loaded_objects")
    if (
        not isinstance(expected, list)
        or not expected
        or any(not isinstance(item, str) or not item for item in expected)
        or loaded != expected
    ):
        raise PostgreSQLProductProofError(
            "package loaded-object set differs from the cluster contract"
        )
    files = manifest.get("files")
    if not isinstance(files, list):
        raise PostgreSQLProductProofError("package manifest files are absent")
    file_by_path: dict[str, Mapping[str, Any]] = {}
    for entry in files:
        if not isinstance(entry, Mapping):
            raise PostgreSQLProductProofError("package manifest file entry is invalid")
        path = entry.get("path")
        if not isinstance(path, str) or not path or path in file_by_path:
            raise PostgreSQLProductProofError("package manifest path is invalid or repeated")
        file_by_path[path] = entry
    for relative in expected:
        entry = file_by_path.get(relative)
        if (
            entry is None
            or entry.get("kind", "file") != "file"
            or HEX_64.fullmatch(str(entry.get("sha256", ""))) is None
        ):
            raise PostgreSQLProductProofError(
                f"required loaded object is absent from package manifest: {relative}"
            )
    validate_loader_environment(manifest, cluster_contract)
    return list(expected)


def hba_content(cluster_contract: Mapping[str, Any], runner_user: str) -> str:
    instance = cluster_contract["instance"]
    security = cluster_contract["security"]
    admin_role = instance["admin_role"]
    app_role = instance["app_role"]
    admin_map = security["admin_map"]
    app_map = security["app_map"]
    lines = [
        f"local all {admin_role} peer map={admin_map}",
        f"local all {app_role} peer map={app_map}",
        "local all all reject",
        "host all all 0.0.0.0/0 reject",
        "host all all ::0/0 reject",
    ]
    content = "\n".join(lines) + "\n"
    if "trust" in content.lower():
        raise PostgreSQLProductProofError("isolated product proof cannot admit trust")
    if runner_user != instance["os_user"]:
        raise PostgreSQLProductProofError("proof runner differs from product service identity")
    return content


def ident_content(cluster_contract: Mapping[str, Any], runner_user: str) -> str:
    instance = cluster_contract["instance"]
    security = cluster_contract["security"]
    if runner_user != instance["os_user"]:
        raise PostgreSQLProductProofError("proof runner differs from product service identity")
    return (
        f"{security['admin_map']} {runner_user} {instance['admin_role']}\n"
        f"{security['app_map']} {runner_user} {instance['app_role']}\n"
    )


def postgresql_conf_content(cluster_contract: Mapping[str, Any]) -> str:
    instance = cluster_contract["instance"]
    security = cluster_contract["security"]
    if security.get("listen_addresses") != "":
        raise PostgreSQLProductProofError("product proof requires Unix-socket-only PostgreSQL")
    preload = security.get("allowed_preload_libraries")
    if preload != ["pg_stat_statements"]:
        raise PostgreSQLProductProofError("product preload contract differs")
    return "\n".join(
        [
            "listen_addresses = ''",
            f"port = {instance['port']}",
            f"unix_socket_directories = '{instance['socket_directory']}'",
            f"unix_socket_permissions = '{security['socket_mode']}'",
            "shared_preload_libraries = 'pg_stat_statements'",
            "dynamic_library_path = '$libdir'",
            "max_connections = 16",
            "shared_buffers = '256MB'",
            "fsync = on",
            "full_page_writes = on",
            "synchronous_commit = on",
            "logging_collector = off",
            "log_destination = 'stderr'",
            "",
        ]
    )


def prepare_sandbox(work_root: Path, installed_release: Path) -> dict[str, Path]:
    sandbox = work_root / "runtime"
    if sandbox.exists():
        raise PostgreSQLProductProofError("runtime sandbox already exists")
    laplace_root = sandbox / "laplace"
    run_root = sandbox / "run"
    temp_root = sandbox / "tmp"
    current_mount = laplace_root / "current"
    data = laplace_root / "pgdata" / "refactor" / "data"
    socket = run_root / "laplace-refactor-postgresql"
    for path, mode in (
        (current_mount, 0o700),
        (data, 0o700),
        (socket, 0o770),
        (temp_root, 0o700),
    ):
        path.mkdir(parents=True, mode=mode, exist_ok=False)
        path.chmod(mode)
    require_physical_directory(installed_release, "installed immutable release")
    return {
        "root": sandbox,
        "laplace": laplace_root,
        "run": run_root,
        "tmp": temp_root,
        "data": data,
        "socket": socket,
        "release": installed_release,
    }


def sandbox_prefix(paths: Mapping[str, Path], bwrap: Path) -> list[str]:
    require_physical_file(bwrap, "bubblewrap executable")
    if not os.access(bwrap, os.X_OK):
        raise PostgreSQLProductProofError("bubblewrap executable is not executable")
    return [
        str(bwrap),
        "--die-with-parent",
        "--ro-bind",
        "/",
        "/",
        "--bind",
        str(paths["laplace"]),
        "/opt/laplace",
        "--ro-bind",
        str(paths["release"]),
        "/opt/laplace/current",
        "--bind",
        str(paths["run"]),
        "/run",
        "--bind",
        str(paths["tmp"]),
        "/tmp",
        "--proc",
        "/proc",
        "--dev-bind",
        "/dev",
        "/dev",
        "--clearenv",
        "--setenv",
        "PATH",
        "/opt/laplace/current/pgsql-18/bin:/usr/sbin:/usr/bin:/sbin:/bin",
        "--setenv",
        "LANG",
        "C",
        "--setenv",
        "LC_ALL",
        "C",
        "--setenv",
        "HOME",
        "/tmp",
        "--chdir",
        "/tmp",
    ]


def sandbox_command(
    prefix: Sequence[str], relative_executable: str, arguments: Sequence[str]
) -> list[str]:
    path = PurePosixPath(relative_executable)
    if path.is_absolute() or ".." in path.parts or not relative_executable:
        raise PostgreSQLProductProofError("sandbox executable path is unsafe")
    return [
        *prefix,
        f"/opt/laplace/current/{relative_executable}",
        *arguments,
    ]


def write_runtime_configuration(
    paths: Mapping[str, Path], cluster_contract: Mapping[str, Any], runner_user: str
) -> None:
    data = paths["data"]
    (data / "pg_hba.conf").write_text(
        hba_content(cluster_contract, runner_user), encoding="utf-8"
    )
    (data / "pg_ident.conf").write_text(
        ident_content(cluster_contract, runner_user), encoding="utf-8"
    )
    (data / "postgresql.conf").write_text(
        postgresql_conf_content(cluster_contract), encoding="utf-8"
    )
    for name in ("pg_hba.conf", "pg_ident.conf", "postgresql.conf"):
        (data / name).chmod(0o600)


def psql_command(
    prefix: Sequence[str],
    cluster_contract: Mapping[str, Any],
    database: str,
    sql: str,
    *,
    tuples: bool = False,
) -> list[str]:
    instance = cluster_contract["instance"]
    arguments = [
        "--host",
        instance["socket_directory"],
        "--port",
        str(instance["port"]),
        "--username",
        instance["admin_role"],
        "--dbname",
        database,
        "--no-psqlrc",
        "--set",
        "ON_ERROR_STOP=1",
    ]
    if tuples:
        arguments.extend(["--tuples-only", "--no-align", "--quiet"])
    arguments.extend(["--command", sql])
    return sandbox_command(prefix, "pgsql-18/bin/psql", arguments)


def wait_ready(
    prefix: Sequence[str],
    cluster_contract: Mapping[str, Any],
    *,
    timeout_seconds: float = 60.0,
) -> dict[str, Any]:
    instance = cluster_contract["instance"]
    command = sandbox_command(
        prefix,
        "pgsql-18/bin/pg_isready",
        [
            "--host",
            instance["socket_directory"],
            "--port",
            str(instance["port"]),
            "--username",
            instance["admin_role"],
            "--dbname",
            "postgres",
        ],
    )
    deadline = time.monotonic() + timeout_seconds
    attempts = 0
    last: subprocess.CompletedProcess[str] | None = None
    started = time.monotonic_ns()
    while time.monotonic() < deadline:
        attempts += 1
        last = subprocess.run(
            command,
            check=False,
            env=os.environ.copy(),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=10,
        )
        if last.returncode == 0:
            receipt = command_receipt(
                "isolated PostgreSQL readiness",
                command,
                last,
                time.monotonic_ns() - started,
            )
            receipt["attempts"] = attempts
            return receipt
        time.sleep(0.2)
    detail = ""
    if last is not None:
        detail = last.stderr.strip() or last.stdout.strip()
    raise PostgreSQLProductProofError(
        f"isolated PostgreSQL did not become ready: {detail or 'timeout'}"
    )


def validate_runtime_metadata(
    metadata: Mapping[str, Any], cluster_contract: Mapping[str, Any]
) -> None:
    instance = cluster_contract["instance"]
    security = cluster_contract["security"]
    if metadata.get("server_version") != "18.6":
        raise PostgreSQLProductProofError("live server is not exact PostgreSQL 18.6")
    if metadata.get("port") != str(instance["port"]):
        raise PostgreSQLProductProofError("live PostgreSQL port differs from product contract")
    if metadata.get("socket_directory") != instance["socket_directory"]:
        raise PostgreSQLProductProofError(
            "live PostgreSQL socket directory differs from product contract"
        )
    if metadata.get("database") != instance["database"]:
        raise PostgreSQLProductProofError("live database differs from product contract")
    if metadata.get("admin_role_count") != 1 or metadata.get("app_role_count") != 1:
        raise PostgreSQLProductProofError("live product roles are not exactly present")
    if metadata.get("schema_count") != 1:
        raise PostgreSQLProductProofError("Laplace schema is not exactly present")
    if metadata.get("active_epoch_count") != 0:
        raise PostgreSQLProductProofError(
            "fresh product-path proof unexpectedly contains active product epochs"
        )
    if metadata.get("data_checksums") != "on":
        raise PostgreSQLProductProofError("isolated product cluster lacks data checksums")
    if metadata.get("listen_addresses") != security["listen_addresses"]:
        raise PostgreSQLProductProofError("live listen_addresses differs from contract")
    if metadata.get("shared_preload_libraries") != "pg_stat_statements":
        raise PostgreSQLProductProofError(
            "live shared_preload_libraries differs from product contract"
        )
    system_identifier = str(metadata.get("system_identifier", ""))
    if not system_identifier.isdecimal() or int(system_identifier) <= 0:
        raise PostgreSQLProductProofError("live cluster has no positive system identifier")
    for extension in ("laplace_extension_version", "pg_stat_statements_extension_version"):
        value = metadata.get(extension)
        if not isinstance(value, str) or not value:
            raise PostgreSQLProductProofError(f"live cluster omits {extension}")


def parse_runtime_metadata(line: str) -> dict[str, Any]:
    fields = line.strip().split("|")
    if len(fields) != 13:
        raise PostgreSQLProductProofError(
            f"live metadata query returned {len(fields)} fields instead of 13"
        )
    try:
        admin_role_count = int(fields[7])
        app_role_count = int(fields[8])
        schema_count = int(fields[9])
        active_epoch_count = int(fields[10])
    except ValueError as error:
        raise PostgreSQLProductProofError("live metadata counts are not integers") from error
    return {
        "server_version": fields[0],
        "port": fields[1],
        "socket_directory": fields[2],
        "database": fields[3],
        "system_identifier": fields[4],
        "laplace_extension_version": fields[5],
        "pg_stat_statements_extension_version": fields[6],
        "admin_role_count": admin_role_count,
        "app_role_count": app_role_count,
        "schema_count": schema_count,
        "active_epoch_count": active_epoch_count,
        "data_checksums": fields[11],
        "listen_addresses": fields[12].split(";", 1)[0],
        "shared_preload_libraries": fields[12].split(";", 1)[1]
        if ";" in fields[12]
        else "",
    }


def runtime_metadata_sql(cluster_contract: Mapping[str, Any]) -> str:
    instance = cluster_contract["instance"]
    admin_role = str(instance["admin_role"])
    app_role = str(instance["app_role"])
    if not re.fullmatch(r"[a-z][a-z0-9_]*", admin_role) or not re.fullmatch(
        r"[a-z][a-z0-9_]*", app_role
    ):
        raise PostgreSQLProductProofError("product role identifiers are not SQL-safe")
    return (
        "SELECT current_setting('server_version') || '|' || "
        "current_setting('port') || '|' || "
        "current_setting('unix_socket_directories') || '|' || "
        "current_database() || '|' || "
        "(SELECT system_identifier::text FROM pg_catalog.pg_control_system()) || '|' || "
        "(SELECT extversion FROM pg_catalog.pg_extension WHERE extname='laplace') || '|' || "
        "(SELECT extversion FROM pg_catalog.pg_extension WHERE extname='pg_stat_statements') || '|' || "
        f"(SELECT count(*)::text FROM pg_catalog.pg_roles WHERE rolname='{admin_role}') || '|' || "
        f"(SELECT count(*)::text FROM pg_catalog.pg_roles WHERE rolname='{app_role}') || '|' || "
        "(SELECT count(*)::text FROM pg_catalog.pg_namespace WHERE nspname='laplace') || '|' || "
        "(SELECT count(*)::text FROM laplace.perfcache_active_control WHERE active_present) || '|' || "
        "current_setting('data_checksums') || '|' || "
        "current_setting('listen_addresses') || ';' || "
        "current_setting('shared_preload_libraries');"
    )


def process_file_identities(pid: int) -> set[tuple[int, int, int]]:
    identities: set[tuple[int, int, int]] = set()
    try:
        executable = os.stat(f"/proc/{pid}/exe")
        identities.add((os.major(executable.st_dev), os.minor(executable.st_dev), executable.st_ino))
        lines = Path(f"/proc/{pid}/maps").read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise PostgreSQLProductProofError(
            f"cannot inspect live PostgreSQL process {pid}: {error}"
        ) from error
    for line in lines:
        fields = line.split(maxsplit=5)
        if len(fields) < 5:
            continue
        device = fields[3]
        inode = fields[4]
        if ":" not in device or not inode.isdecimal() or int(inode) == 0:
            continue
        major_text, minor_text = device.split(":", 1)
        try:
            identities.add((int(major_text, 16), int(minor_text, 16), int(inode)))
        except ValueError:
            continue
    return identities


def validate_loaded_observations(
    expected: Sequence[Mapping[str, Any]],
    observed: Sequence[Mapping[str, Any]],
) -> None:
    expected_by_path = {str(item["path"]): item for item in expected}
    observed_by_path = {str(item["path"]): item for item in observed}
    if len(expected_by_path) != len(expected) or len(observed_by_path) != len(observed):
        raise PostgreSQLProductProofError("loaded-object proof contains duplicate paths")
    if set(expected_by_path) != set(observed_by_path):
        raise PostgreSQLProductProofError("loaded-object proof omits or adds product objects")
    for path, wanted in expected_by_path.items():
        actual = observed_by_path[path]
        for field in ("sha256", "device_major", "device_minor", "inode"):
            if actual.get(field) != wanted.get(field):
                raise PostgreSQLProductProofError(
                    f"loaded-object identity differs for {path}: {field}"
                )
        processes = actual.get("processes")
        if not isinstance(processes, list) or not processes:
            raise PostgreSQLProductProofError(
                f"loaded-object proof has no live process for {path}"
            )


def observe_required_loaded_objects(
    installed_release: Path,
    manifest: Mapping[str, Any],
    required: Sequence[str],
    postmaster_pid: int,
    backend_pid: int,
) -> list[dict[str, Any]]:
    file_by_path = {
        str(entry["path"]): entry
        for entry in manifest["files"]
        if isinstance(entry, Mapping) and isinstance(entry.get("path"), str)
    }
    process_identities = {
        "postmaster": process_file_identities(postmaster_pid),
        "backend": process_file_identities(backend_pid),
    }
    expected: list[dict[str, Any]] = []
    observed: list[dict[str, Any]] = []
    for relative in required:
        physical = installed_release.joinpath(*PurePosixPath(relative).parts)
        require_physical_file(physical, f"required loaded object {relative}")
        metadata = physical.stat()
        manifest_entry = file_by_path.get(relative)
        if manifest_entry is None:
            raise PostgreSQLProductProofError(
                f"loaded object is not present in package manifest: {relative}"
            )
        identity = (
            os.major(metadata.st_dev), os.minor(metadata.st_dev), metadata.st_ino
        )
        wanted = {
            "path": relative,
            "sha256": str(manifest_entry["sha256"]),
            "device_major": identity[0],
            "device_minor": identity[1],
            "inode": identity[2],
            "processes": [],
        }
        expected.append(wanted)
        processes = [
            name for name, identities in process_identities.items() if identity in identities
        ]
        actual = dict(wanted)
        actual["sha256"] = sha256_file(physical)
        actual["processes"] = processes
        observed.append(actual)
    validate_loaded_observations(expected, observed)
    return observed


def read_postmaster_pid(data_directory: Path) -> int:
    try:
        text = (data_directory / "postmaster.pid").read_text(encoding="ascii")
        first = text.splitlines()[0]
    except (OSError, IndexError) as error:
        raise PostgreSQLProductProofError("postmaster PID file is absent") from error
    if not first.isdecimal() or int(first) <= 0:
        raise PostgreSQLProductProofError("postmaster PID is invalid")
    return int(first)


def read_backend_pid(
    prefix: Sequence[str],
    cluster_contract: Mapping[str, Any],
    application_name: str,
    *,
    timeout_seconds: float = 30.0,
) -> int:
    if not re.fullmatch(r"[a-z0-9_]+", application_name):
        raise PostgreSQLProductProofError("proof application_name is unsafe")
    sql = (
        "SELECT pid::text FROM pg_catalog.pg_stat_activity "
        f"WHERE application_name='{application_name}' "
        "AND pid <> pg_catalog.pg_backend_pid();"
    )
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        completed = subprocess.run(
            psql_command(
                prefix,
                cluster_contract,
                cluster_contract["instance"]["database"],
                sql,
                tuples=True,
            ),
            check=False,
            env=os.environ.copy(),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=10,
        )
        if completed.returncode == 0:
            rows = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
            if len(rows) == 1 and rows[0].isdecimal() and int(rows[0]) > 0:
                return int(rows[0])
        time.sleep(0.1)
    raise PostgreSQLProductProofError("timed out locating loaded-object proof backend")


def stop_process(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        process.communicate()
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.communicate(timeout=10)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate()


def physical_prefix(installed_release: Path, logical_root: str) -> Path:
    logical = PurePosixPath(logical_root)
    if not logical.is_absolute() or ".." in logical.parts:
        raise PostgreSQLProductProofError("package logical root is invalid")
    result = installed_release
    for _part in reversed(logical.parts[1:]):
        result = result.parent
    if result == installed_release or not result.is_absolute():
        raise PostgreSQLProductProofError("cannot reconstruct package physical prefix")
    return result


def store_receipt(store: Path, receipt: Path, durable_root: Path) -> str:
    require_physical_file(store, "native receipt-store executable")
    if not os.access(store, os.X_OK):
        raise PostgreSQLProductProofError("native receipt-store executable is not executable")
    require_physical_file(receipt, "receipt to retain")
    completed, _ = run(
        [str(store), "put", "--receipt", str(receipt), "--root", str(durable_root)],
        "durable BLAKE3 PostgreSQL-product receipt publication",
        timeout=60,
    )
    digest = require_hex(completed.stdout.strip(), "durable receipt BLAKE3")
    run(
        [str(store), "verify", "--digest", digest, "--root", str(durable_root)],
        "durable BLAKE3 PostgreSQL-product receipt replay",
        timeout=60,
    )
    return digest


def prove(
    repository: Path,
    postgresql_publication: Path,
    work_root: Path,
    output: Path,
) -> dict[str, Any]:
    repository = repository.resolve()
    require_physical_directory(repository, "repository")
    if work_root.exists() or not work_root.is_absolute():
        raise PostgreSQLProductProofError("proof work root must be a fresh absolute path")
    work_root.mkdir(parents=True, mode=0o700)
    if output.exists() or not output.is_absolute() or not output.parent.is_dir():
        raise PostgreSQLProductProofError(
            "proof output must be a fresh absolute file under an existing directory"
        )
    require_physical_file(postgresql_publication, "PostgreSQL publication receipt")
    source_commit, source_tree = repository_identity(repository)

    command_receipts: list[dict[str, Any]] = []
    tests, receipt = run(
        [sys.executable, str(repository / "tests/postgresql_product_proof_tests.py")],
        "PostgreSQL-product deliberate-defect tests",
        timeout=120,
        cwd=repository,
    )
    del tests
    command_receipts.append(receipt)

    package_work = work_root / "package-proof"
    package_result = work_root / "package-proof.json"
    _completed, package_receipt = run(
        [
            sys.executable,
            str(repository / "tools/product/prove-package.py"),
            "--repository",
            str(repository),
            "--postgresql-publication",
            str(postgresql_publication),
            "--work-root",
            str(package_work),
            "--output",
            str(package_result),
        ],
        "exact current-change product package proof",
        timeout=10800,
        cwd=repository,
    )
    command_receipts.append(package_receipt)
    package_proof = load_json(package_result)
    validate_source_identity(package_proof, source_commit, source_tree)
    package_id = require_hex(package_proof.get("package_id"), "package id")
    manifest_path = Path(str(package_proof.get("package_manifest", "")))
    require_physical_file(manifest_path, "package manifest")
    manifest = load_json(manifest_path)
    if manifest.get("package_id") != package_id:
        raise PostgreSQLProductProofError("package proof and manifest identity differ")
    installed_release = Path(str(package_proof.get("installed_release", "")))
    require_physical_directory(installed_release, "installed immutable release")

    cluster_contract_path = repository / "contracts/postgresql-cluster.json"
    product_contract_path = repository / "contracts/product-package.json"
    gateway_contract_path = repository / "contracts/product-activation-gateway.json"
    cluster_contract = load_json(cluster_contract_path)
    product_contract = load_json(product_contract_path)
    gateway_contract = load_json(gateway_contract_path)
    clusterctl.validate_contract(cluster_contract)
    required_loaded = validate_postgresql_selection(manifest, cluster_contract)
    if (
        product_contract.get("postgresql", {}).get("version") != "PostgreSQL 18.6"
        or product_contract.get("package", {}).get("required_loaded_objects")
        != required_loaded
    ):
        raise PostgreSQLProductProofError(
            "product-package and PostgreSQL-cluster runtime selection differ"
        )
    prefix_root = physical_prefix(installed_release, str(manifest.get("root", "")))
    status = clusterctl.verify_package(manifest, cluster_contract, prefix_root)
    if not status.verified:
        raise PostgreSQLProductProofError(
            f"installed package does not satisfy cluster contract: {status.reason}"
        )

    bwrap = Path(str(product_contract.get("host_build_provider", {}).get("sandbox_executable", "")))
    paths = prepare_sandbox(work_root, installed_release)
    prefix = sandbox_prefix(paths, bwrap)
    runner_user = pwd.getpwuid(os.getuid()).pw_name
    if runner_user != cluster_contract["instance"]["os_user"]:
        raise PostgreSQLProductProofError(
            "PostgreSQL product proof must execute as laplace-runner"
        )

    initdb_command = sandbox_command(
        prefix,
        "pgsql-18/bin/initdb",
        [
            "--pgdata",
            cluster_contract["instance"]["data_directory"],
            "--username",
            cluster_contract["instance"]["admin_role"],
            "--auth-local",
            "peer",
            "--auth-host",
            "reject",
            "--data-checksums",
            "--no-locale",
            "--encoding",
            "UTF8",
        ],
    )
    _, initdb_receipt = run(
        initdb_command, "initialize isolated PostgreSQL 18.6 product cluster", timeout=600
    )
    command_receipts.append(initdb_receipt)
    write_runtime_configuration(paths, cluster_contract, runner_user)

    pg_config, pg_config_receipt = run(
        sandbox_command(prefix, "pgsql-18/bin/pg_config", ["--version"]),
        "query packaged pg_config version",
        timeout=30,
    )
    command_receipts.append(pg_config_receipt)
    if pg_config.stdout.strip() != "PostgreSQL 18.6":
        raise PostgreSQLProductProofError("packaged pg_config is not PostgreSQL 18.6")
    postgres_version, postgres_version_receipt = run(
        sandbox_command(prefix, "pgsql-18/bin/postgres", ["--version"]),
        "query packaged postgres version",
        timeout=30,
    )
    command_receipts.append(postgres_version_receipt)
    if postgres_version.stdout.strip() != "postgres (PostgreSQL) 18.6":
        raise PostgreSQLProductProofError("packaged postgres is not PostgreSQL 18.6")

    server_command = sandbox_command(
        prefix,
        "pgsql-18/bin/postgres",
        ["-D", cluster_contract["instance"]["data_directory"]],
    )
    server_started = time.monotonic_ns()
    server = subprocess.Popen(
        server_command,
        cwd="/",
        env=os.environ.copy(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
    )
    hold: subprocess.Popen[str] | None = None
    server_receipt: dict[str, Any] | None = None
    try:
        command_receipts.append(wait_ready(prefix, cluster_contract))
        instance = cluster_contract["instance"]
        for label, database, sql in (
            (
                "create product application role",
                "postgres",
                f"CREATE ROLE {instance['app_role']} LOGIN;",
            ),
            (
                "create product database",
                "postgres",
                f"CREATE DATABASE {instance['database']} OWNER {instance['admin_role']};",
            ),
            (
                "create pg_stat_statements extension",
                instance["database"],
                "CREATE EXTENSION pg_stat_statements;",
            ),
            (
                "create Laplace extension",
                instance["database"],
                "CREATE EXTENSION laplace;",
            ),
        ):
            _, receipt = run(
                psql_command(prefix, cluster_contract, database, sql),
                label,
                timeout=300,
            )
            command_receipts.append(receipt)

        metadata_result, metadata_receipt = run(
            psql_command(
                prefix,
                cluster_contract,
                instance["database"],
                runtime_metadata_sql(cluster_contract),
                tuples=True,
            ),
            "read exact live PostgreSQL product metadata",
            timeout=60,
        )
        command_receipts.append(metadata_receipt)
        rows = [line.strip() for line in metadata_result.stdout.splitlines() if line.strip()]
        if len(rows) != 1:
            raise PostgreSQLProductProofError(
                f"live metadata query returned {len(rows)} rows"
            )
        metadata = parse_runtime_metadata(rows[0])
        validate_runtime_metadata(metadata, cluster_contract)

        application_name = f"laplace_product_proof_{package_id[:16]}"
        hold_sql = (
            f"SET application_name = '{application_name}'; "
            "LOAD '$libdir/laplace_pg'; "
            "LOAD '$libdir/pg_stat_statements'; "
            "SELECT pg_sleep(120);"
        )
        hold_command = psql_command(
            prefix, cluster_contract, instance["database"], hold_sql
        )
        hold = subprocess.Popen(
            hold_command,
            cwd="/",
            env=os.environ.copy(),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
        backend_pid = read_backend_pid(
            prefix, cluster_contract, application_name, timeout_seconds=30.0
        )
        postmaster_pid = read_postmaster_pid(paths["data"])
        loaded_objects = observe_required_loaded_objects(
            installed_release,
            manifest,
            required_loaded,
            postmaster_pid,
            backend_pid,
        )

        pg_ctl_stop, stop_receipt = run(
            sandbox_command(
                prefix,
                "pgsql-18/bin/pg_ctl",
                [
                    "--pgdata",
                    cluster_contract["instance"]["data_directory"],
                    "--mode",
                    "fast",
                    "--wait",
                    "stop",
                ],
            ),
            "stop isolated PostgreSQL product cluster",
            timeout=120,
        )
        del pg_ctl_stop
        command_receipts.append(stop_receipt)
        if hold is not None:
            stop_process(hold)
            hold = None
        try:
            server_stdout, server_stderr = server.communicate(timeout=30)
        except subprocess.TimeoutExpired as error:
            raise PostgreSQLProductProofError(
                "isolated PostgreSQL server did not exit after pg_ctl stop"
            ) from error
        server_completed = subprocess.CompletedProcess(
            server_command, server.returncode or 0, server_stdout, server_stderr
        )
        server_receipt = command_receipt(
            "isolated PostgreSQL server lifetime",
            server_command,
            server_completed,
            time.monotonic_ns() - server_started,
        )
        if server.returncode != 0:
            detail = server_stderr.strip() or server_stdout.strip()
            raise PostgreSQLProductProofError(
                f"isolated PostgreSQL server exited nonzero: {detail[-2000:]}"
            )
        command_receipts.append(server_receipt)
    finally:
        if hold is not None:
            stop_process(hold)
        if server.poll() is None:
            try:
                subprocess.run(
                    sandbox_command(
                        prefix,
                        "pgsql-18/bin/pg_ctl",
                        [
                            "--pgdata",
                            cluster_contract["instance"]["data_directory"],
                            "--mode",
                            "immediate",
                            "--wait",
                            "stop",
                        ],
                    ),
                    check=False,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=30,
                )
            except (OSError, subprocess.SubprocessError):
                pass
            if server.poll() is None:
                stop_process(server)

    runtime_receipt = {
        "schema": RUNTIME_SCHEMA,
        "scope": "isolated-current-change-product-package-not-system-activation",
        "product_activated": False,
        "repository_commit": source_commit,
        "repository_tree": source_tree,
        "package_id": package_id,
        "package_manifest_sha256": sha256_file(manifest_path),
        "cluster_contract_sha256": sha256_file(cluster_contract_path),
        "product_package_contract_sha256": sha256_file(product_contract_path),
        "postgresql_version": metadata["server_version"],
        "system_identifier": metadata["system_identifier"],
        "port": metadata["port"],
        "socket_directory": metadata["socket_directory"],
        "database": metadata["database"],
        "laplace_extension_version": metadata["laplace_extension_version"],
        "pg_stat_statements_extension_version": metadata[
            "pg_stat_statements_extension_version"
        ],
        "active_epoch_count": metadata["active_epoch_count"],
        "data_checksums": metadata["data_checksums"],
        "listen_addresses": metadata["listen_addresses"],
        "shared_preload_libraries": metadata["shared_preload_libraries"],
        "required_loaded_objects": loaded_objects,
        "forbidden_loader_environment_absent": True,
        "hba_trust_absent": True,
        "command_receipts": command_receipts,
    }
    runtime_receipt["receipt_sha256"] = document_identity(
        runtime_receipt, "receipt_sha256"
    )
    runtime_path = work_root / "runtime-receipt.json"
    with runtime_path.open("xb") as stream:
        stream.write(canonical_bytes(runtime_receipt))
        stream.flush()
        os.fsync(stream.fileno())

    plan_receipt_root = Path(
        str(gateway_contract.get("product", {}).get("plan_receipt_root", ""))
    )
    if not plan_receipt_root.is_absolute() or plan_receipt_root.name != "plans":
        raise PostgreSQLProductProofError(
            "product plan receipt authority is invalid"
        )
    durable_root = plan_receipt_root.parent / "postgresql-product"
    durable_root.mkdir(parents=True, exist_ok=True, mode=0o2750)
    require_physical_directory(durable_root, "durable PostgreSQL-product receipt root")
    receipt_store = installed_release / "bin/laplace_receipt_store"
    runtime_blake3 = store_receipt(receipt_store, runtime_path, durable_root)

    binding = {
        "schema": BINDING_SCHEMA,
        "repository_commit": source_commit,
        "repository_tree": source_tree,
        "package_id": package_id,
        "package_manifest_sha256": sha256_file(manifest_path),
        "package_proof_sha256": sha256_file(package_result),
        "package_proof_binding_blake3": require_hex(
            package_proof.get("binding_receipt_blake3"),
            "package proof binding BLAKE3",
        ),
        "runtime_receipt_sha256": runtime_receipt["receipt_sha256"],
        "runtime_receipt_blake3": runtime_blake3,
        "cluster_contract_sha256": sha256_file(cluster_contract_path),
    }
    binding_path = work_root / "binding.json"
    with binding_path.open("xb") as stream:
        stream.write(canonical_bytes(binding))
        stream.flush()
        os.fsync(stream.fileno())
    binding_blake3 = store_receipt(receipt_store, binding_path, durable_root)

    result = {
        "schema": PROOF_SCHEMA,
        "phase": "exact-postgresql-18.6-package-runtime-retained",
        "repository_commit": source_commit,
        "repository_tree": source_tree,
        "package_id": package_id,
        "package_manifest_sha256": sha256_file(manifest_path),
        "postgresql_version": metadata["server_version"],
        "system_identifier": metadata["system_identifier"],
        "runtime_scope": runtime_receipt["scope"],
        "product_activated": False,
        "laplace_schema_present": True,
        "active_epoch_count": metadata["active_epoch_count"],
        "loaded_object_count": len(loaded_objects),
        "required_loaded_object_count": len(required_loaded),
        "forbidden_loader_environment_absent": True,
        "hba_trust_absent": True,
        "package_proof_binding_blake3": binding["package_proof_binding_blake3"],
        "runtime_receipt_blake3": runtime_blake3,
        "binding_receipt_blake3": binding_blake3,
        "durable_receipt_root": str(durable_root),
    }
    result["proof_sha256"] = document_identity(result, "proof_sha256")
    with output.open("xb") as stream:
        stream.write(canonical_bytes(result))
        stream.flush()
        os.fsync(stream.fileno())
    return result


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=str(REPOSITORY_ROOT))
    parser.add_argument("--postgresql-publication", required=True)
    parser.add_argument("--work-root", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    prove(
        Path(arguments.repository),
        Path(arguments.postgresql_publication),
        Path(arguments.work_root),
        Path(arguments.output),
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        PostgreSQLProductProofError,
        clusterctl.ClusterError,
        OSError,
        subprocess.SubprocessError,
    ) as error:
        print(f"postgresql-product-proof: {error}", file=sys.stderr)
        raise SystemExit(1) from error
