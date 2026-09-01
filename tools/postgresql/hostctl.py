#!/usr/bin/env python3
"""Inventory PostgreSQL infrastructure and compile an exact Laplace host selection."""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
from typing import Any, Iterable, Sequence


CONTRACT_SCHEMA = "laplace.postgresql-host-contract/v1"
INVENTORY_SCHEMA = "laplace.postgresql-host-inventory/v1"
REQUEST_SCHEMA = "laplace.postgresql-instance-request/v1"
SELECTION_SCHEMA = "laplace.postgresql-host-selection/v1"
PACKAGE_SCHEMA = "laplace.package-manifest/v1"
HEX_256 = re.compile(r"^[0-9a-f]{64}$")
VERSION = re.compile(r"(?:PostgreSQL\)?\s+)([0-9]+(?:\.[0-9]+)+)")
IDENTIFIER = re.compile(r"^[a-z][a-z0-9_]*$")
SERVICE = re.compile(r"^[A-Za-z0-9_.@-]+\.service$")


class HostControlError(RuntimeError):
    """The host topology cannot be observed or selected exactly."""


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise HostControlError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise HostControlError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise HostControlError(f"JSON root must be an object: {path}")
    return value


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def document_identity(value: dict[str, Any], field: str) -> str:
    payload = dict(value)
    payload.pop(field, None)
    return hashlib.sha256(canonical_bytes(payload)).hexdigest()


def require_absolute(value: Any, field: str) -> str:
    if not isinstance(value, str):
        raise HostControlError(f"{field} must be an absolute path")
    path = PurePosixPath(value)
    if not path.is_absolute() or ".." in path.parts or str(path) != value:
        raise HostControlError(f"{field} must be a normalized absolute path")
    return value


def prefixed(root: Path, logical: str) -> Path:
    path = PurePosixPath(require_absolute(logical, "logical path"))
    return Path(logical) if root == Path("/") else root.joinpath(*path.parts[1:])


def validate_contract(contract: dict[str, Any]) -> None:
    if contract.get("schema") != CONTRACT_SCHEMA or contract.get("version") != "1.0.0":
        raise HostControlError("PostgreSQL host contract schema or version differs")
    postgresql = contract.get("postgresql")
    discovery = contract.get("discovery")
    topology = contract.get("topology")
    if not all(isinstance(item, dict) for item in (postgresql, discovery, topology)):
        raise HostControlError("PostgreSQL host contract sections are incomplete")
    majors = postgresql.get("supported_major_versions")
    if (
        not isinstance(majors, list)
        or not majors
        or majors != sorted(set(majors))
        or any(not isinstance(item, int) or item <= 0 for item in majors)
    ):
        raise HostControlError("supported PostgreSQL majors are invalid")
    if postgresql.get("exact_package_version_required") is not True:
        raise HostControlError("exact PostgreSQL package versions must be required")
    if postgresql.get("patch_binary_compatibility_assumed") is not False:
        raise HostControlError("PostgreSQL patch compatibility cannot be assumed")
    if postgresql.get("package_layout") != "pgsql-{major}":
        raise HostControlError("PostgreSQL package layout differs")
    patterns = postgresql.get("binary_candidates")
    if not isinstance(patterns, list) or not patterns:
        raise HostControlError("PostgreSQL binary discovery roots are absent")
    for index, pattern in enumerate(patterns):
        require_absolute(pattern, f"postgresql.binary_candidates[{index}]")
    require_absolute(discovery.get("process_root"), "discovery.process_root")
    require_absolute(discovery.get("mountinfo"), "discovery.mountinfo")
    roots = discovery.get("systemd_unit_roots")
    fragments = discovery.get("postgresql_service_name_fragments")
    providers = discovery.get("routing_providers")
    if not isinstance(roots, list) or not roots or not isinstance(fragments, list):
        raise HostControlError("systemd discovery contract is incomplete")
    if not isinstance(providers, dict) or not providers:
        raise HostControlError("routing provider discovery contract is incomplete")
    for index, root in enumerate(roots):
        require_absolute(root, f"discovery.systemd_unit_roots[{index}]")
    for name, provider in providers.items():
        if not IDENTIFIER.fullmatch(name) or not isinstance(provider, dict):
            raise HostControlError("routing provider declaration is invalid")
        if set(provider) != {"binary_candidates", "service_fragments"}:
            raise HostControlError(f"routing provider {name} fields differ")
        for index, path in enumerate(provider["binary_candidates"]):
            require_absolute(path, f"routing provider {name} binary[{index}]")
    if topology.get("modes") != ["adopt", "create", "external"]:
        raise HostControlError("cluster topology modes differ")
    if topology.get("routing_modes") != [
        "direct-tcp",
        "direct-unix",
        "external-haproxy",
        "external-pgbouncer",
    ]:
        raise HostControlError("routing topology modes differ")
    for field in (
        "create_requires_unclaimed_targets",
        "adopt_requires_exact_system_identifier",
        "adopt_requires_exact_server_version",
        "adopt_requires_loaded_extension_proof",
        "external_requires_authority_receipt",
    ):
        if topology.get(field) is not True:
            raise HostControlError(f"topology gate is disabled: {field}")


def parse_version(text: str, field: str) -> tuple[str, int]:
    match = VERSION.search(text.strip())
    if match is None:
        raise HostControlError(f"cannot parse PostgreSQL version from {field}: {text!r}")
    version = match.group(1)
    return version, int(version.split(".", 1)[0])


def physical_candidates(root: Path, patterns: Iterable[str]) -> list[Path]:
    result: set[Path] = set()
    for pattern in patterns:
        physical_pattern = str(prefixed(root, pattern))
        for match in glob.glob(physical_pattern):
            candidate = Path(match)
            try:
                metadata = candidate.lstat()
            except OSError:
                continue
            if stat.S_ISDIR(metadata.st_mode) and not candidate.is_symlink():
                result.add(candidate)
    return sorted(result)


def logical_path(root: Path, physical: Path) -> str:
    if root == Path("/"):
        return str(physical)
    try:
        relative = physical.relative_to(root)
    except ValueError as error:
        raise HostControlError(f"physical path escaped inventory root: {physical}") from error
    return "/" + relative.as_posix()


def run_version(executable: Path, argument: str) -> str:
    completed = subprocess.run(
        [str(executable), argument],
        check=False,
        cwd="/",
        env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C", "LC_ALL": "C"},
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=15,
    )
    if completed.returncode != 0:
        raise HostControlError(
            f"version query failed for {executable}: "
            + (completed.stderr.strip() or f"exit {completed.returncode}")
        )
    return completed.stdout.strip()


def discover_installations(contract: dict[str, Any], root: Path) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    installations: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    for binary_root in physical_candidates(root, contract["postgresql"]["binary_candidates"]):
        pg_config = binary_root / "pg_config"
        postgres = binary_root / "postgres"
        if not pg_config.is_file() or not postgres.is_file():
            continue
        try:
            version_text = run_version(pg_config, "--version")
            server_text = run_version(postgres, "--version")
            version, major = parse_version(version_text, str(pg_config))
            server_version, server_major = parse_version(server_text, str(postgres))
            if (server_version, server_major) != (version, major):
                raise HostControlError("pg_config and postgres versions differ")
            installations.append(
                {
                    "binary_directory": logical_path(root, binary_root),
                    "pg_config": logical_path(root, pg_config),
                    "postgres": logical_path(root, postgres),
                    "version": version,
                    "major": major,
                    "supported_major": major in contract["postgresql"]["supported_major_versions"],
                }
            )
        except (HostControlError, OSError) as error:
            errors.append({"operation": "query-installation", "target": logical_path(root, binary_root), "error": str(error)})
    unique = {item["postgres"]: item for item in installations}
    return [unique[key] for key in sorted(unique)], errors


def parse_postmaster_pid(path: Path) -> dict[str, Any]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        return {"read_error": str(error)}
    result: dict[str, Any] = {}
    if len(lines) >= 1 and lines[0].isdigit():
        result["pid"] = int(lines[0])
    if len(lines) >= 2 and lines[1].startswith("/"):
        result["data_directory"] = lines[1]
    if len(lines) >= 4 and lines[3].isdigit():
        result["port"] = int(lines[3])
    if len(lines) >= 5:
        result["socket_directories"] = [item for item in lines[4].split(",") if item]
    if len(lines) >= 6:
        result["listen_addresses"] = lines[5]
    if len(lines) >= 8:
        result["status"] = lines[7]
    return result


def process_data_directory(arguments: list[str]) -> str | None:
    for index, argument in enumerate(arguments):
        if argument == "-D" and index + 1 < len(arguments):
            return arguments[index + 1]
        if argument.startswith("--pgdata="):
            return argument.split("=", 1)[1]
    return None


def discover_processes(contract: dict[str, Any], root: Path) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    proc = prefixed(root, contract["discovery"]["process_root"])
    processes: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    try:
        candidates = sorted((item for item in proc.iterdir() if item.name.isdigit()), key=lambda item: int(item.name))
    except OSError as error:
        return [], [{"operation": "list-processes", "target": contract["discovery"]["process_root"], "error": str(error)}]
    for process in candidates:
        try:
            raw = (process / "cmdline").read_bytes()
        except (FileNotFoundError, ProcessLookupError):
            continue
        except OSError as error:
            errors.append({"operation": "read-cmdline", "target": int(process.name), "error": str(error)})
            continue
        arguments = [part.decode("utf-8", "surrogateescape") for part in raw.split(b"\0") if part]
        if not arguments or Path(arguments[0]).name != "postgres":
            continue
        data_directory = process_data_directory(arguments)
        record: dict[str, Any] = {
            "pid": int(process.name),
            "executable": arguments[0],
            "argv": arguments,
            "data_directory": data_directory,
        }
        try:
            record["uid"] = process.stat().st_uid
        except OSError as error:
            record["uid_error"] = str(error)
            errors.append(
                {"operation": "stat-process", "target": int(process.name), "error": str(error)}
            )
        try:
            executable = os.readlink(process / "exe")
            record["loaded_executable"] = executable.removesuffix(" (deleted)")
            record["loaded_executable_deleted"] = executable.endswith(" (deleted)")
        except OSError as error:
            record["executable_error"] = str(error)
            errors.append(
                {"operation": "read-executable", "target": int(process.name), "error": str(error)}
            )
        if data_directory is not None and data_directory.startswith("/"):
            pid_record = parse_postmaster_pid(prefixed(root, data_directory) / "postmaster.pid")
            record["postmaster"] = pid_record
            if "read_error" in pid_record:
                errors.append(
                    {
                        "operation": "read-postmaster-pid",
                        "target": data_directory,
                        "error": pid_record["read_error"],
                    }
                )
        processes.append(record)
    return processes, errors


def discover_services(contract: dict[str, Any], root: Path) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    fragments = tuple(contract["discovery"]["postgresql_service_name_fragments"])
    provider_fragments = tuple(
        fragment
        for provider in contract["discovery"]["routing_providers"].values()
        for fragment in provider["service_fragments"]
    )
    services: dict[str, dict[str, Any]] = {}
    errors: list[dict[str, Any]] = []
    for logical_root in contract["discovery"]["systemd_unit_roots"]:
        directory = prefixed(root, logical_root)
        try:
            entries = list(directory.glob("*.service"))
        except OSError as error:
            errors.append({"operation": "list-units", "target": logical_root, "error": str(error)})
            continue
        for path in entries:
            lowered = path.name.lower()
            if not any(fragment in lowered for fragment in (*fragments, *provider_fragments)):
                continue
            if path.name not in services:
                services[path.name] = {
                    "name": path.name,
                    "path": logical_path(root, path),
                    "symlink": path.is_symlink(),
                }
    return [services[key] for key in sorted(services)], errors


def decode_mount_path(value: str) -> str:
    return re.sub(
        r"\\([0-7]{3})",
        lambda match: chr(int(match.group(1), 8)),
        value,
    )


def discover_mounts(contract: dict[str, Any], root: Path) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    path = prefixed(root, contract["discovery"]["mountinfo"])
    try:
        rows = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        return [], [{"operation": "read-mountinfo", "target": contract["discovery"]["mountinfo"], "error": str(error)}]
    mounts: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    for row in rows:
        fields = row.split()
        try:
            separator = fields.index("-")
            mounts.append(
                {
                    "mount_id": int(fields[0]),
                    "parent_id": int(fields[1]),
                    "major_minor": fields[2],
                    "root": decode_mount_path(fields[3]),
                    "mount_point": decode_mount_path(fields[4]),
                    "mount_options": fields[5].split(","),
                    "filesystem": fields[separator + 1],
                    "source": decode_mount_path(fields[separator + 2]),
                }
            )
        except (ValueError, IndexError) as error:
            errors.append({"operation": "parse-mountinfo", "target": row, "error": str(error)})
    return sorted(mounts, key=lambda item: item["mount_point"]), errors


def discover_routing(contract: dict[str, Any], root: Path, services: list[dict[str, Any]]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for name, provider in sorted(contract["discovery"]["routing_providers"].items()):
        binaries = []
        for logical in provider["binary_candidates"]:
            physical = prefixed(root, logical)
            if physical.is_file() and not physical.is_symlink():
                binaries.append(logical)
        matched_services = sorted(
            service["name"]
            for service in services
            if any(fragment in service["name"].lower() for fragment in provider["service_fragments"])
        )
        result.append(
            {
                "provider": name,
                "available": bool(binaries or matched_services),
                "binaries": binaries,
                "services": matched_services,
            }
        )
    return result


def inventory(contract: dict[str, Any], root: Path) -> dict[str, Any]:
    validate_contract(contract)
    if not root.is_absolute():
        raise HostControlError("inventory root must be absolute")
    installations, installation_errors = discover_installations(contract, root)
    processes, process_errors = discover_processes(contract, root)
    services, service_errors = discover_services(contract, root)
    mounts, mount_errors = discover_mounts(contract, root)
    result = {
        "schema": INVENTORY_SCHEMA,
        "root": str(root),
        "postgresql_installations": installations,
        "postgresql_processes": processes,
        "systemd_services": services,
        "routing_providers": discover_routing(contract, root, services),
        "mounts": mounts,
        "inspection_errors": [
            *installation_errors,
            *process_errors,
            *service_errors,
            *mount_errors,
        ],
    }
    result["inventory_sha256"] = document_identity(result, "inventory_sha256")
    return result


def validate_inventory(value: dict[str, Any]) -> None:
    if value.get("schema") != INVENTORY_SCHEMA:
        raise HostControlError("host inventory schema differs")
    if value.get("inventory_sha256") != document_identity(value, "inventory_sha256"):
        raise HostControlError("host inventory digest differs")
    for field in (
        "postgresql_installations",
        "postgresql_processes",
        "systemd_services",
        "routing_providers",
        "mounts",
        "inspection_errors",
    ):
        if not isinstance(value.get(field), list):
            raise HostControlError(f"host inventory {field} is absent")


def validate_request(request: dict[str, Any], contract: dict[str, Any]) -> None:
    if request.get("schema") != REQUEST_SCHEMA:
        raise HostControlError("instance request schema differs")
    if request.get("mode") not in contract["topology"]["modes"]:
        raise HostControlError("instance request mode is unsupported")
    instance = request.get("instance")
    postgresql = request.get("postgresql")
    routing = request.get("routing")
    if not all(isinstance(item, dict) for item in (instance, postgresql, routing)):
        raise HostControlError("instance request sections are incomplete")
    if not IDENTIFIER.fullmatch(str(instance.get("id", ""))):
        raise HostControlError("instance id is invalid")
    if not SERVICE.fullmatch(str(instance.get("service", ""))):
        raise HostControlError("instance service is invalid")
    if not isinstance(instance.get("port"), int) or not 1024 <= instance["port"] <= 65535:
        raise HostControlError("instance port is invalid")
    for field in (
        "data_directory",
        "wal_directory",
        "temp_directory",
        "perfcache_directory",
        "config_directory",
        "log_directory",
        "receipt_directory",
        "socket_directory",
    ):
        require_absolute(instance.get(field), f"instance.{field}")
    version, major = parse_version(f"PostgreSQL {postgresql.get('version', '')}", "request PostgreSQL version")
    if postgresql.get("version") != version or postgresql.get("major") != major:
        raise HostControlError("requested PostgreSQL version and major differ")
    if major not in contract["postgresql"]["supported_major_versions"]:
        raise HostControlError("requested PostgreSQL major is unsupported")
    if routing.get("mode") not in contract["topology"]["routing_modes"]:
        raise HostControlError("routing mode is unsupported")
    if routing["mode"] == "direct-unix":
        if routing.get("endpoint") != {
            "socket_directory": instance["socket_directory"],
            "port": instance["port"],
        }:
            raise HostControlError("direct Unix route differs from selected instance")
    else:
        endpoint = routing.get("endpoint")
        if (
            not isinstance(endpoint, dict)
            or not isinstance(endpoint.get("host"), str)
            or not endpoint["host"]
            or not isinstance(endpoint.get("port"), int)
            or not 1 <= endpoint["port"] <= 65535
        ):
            raise HostControlError("TCP routing endpoint is invalid")


def target_collisions(inventory_value: dict[str, Any], request: dict[str, Any]) -> list[dict[str, Any]]:
    instance = request["instance"]
    paths = {
        value
        for key, value in instance.items()
        if key.endswith("_directory") and isinstance(value, str)
    }
    collisions: list[dict[str, Any]] = []
    for process in inventory_value["postgresql_processes"]:
        postmaster = process.get("postmaster", {})
        process_paths = {process.get("data_directory"), postmaster.get("data_directory")}
        if paths.intersection(process_paths):
            collisions.append({"kind": "postgresql-process-path", "pid": process["pid"]})
        if postmaster.get("port") == instance["port"]:
            collisions.append({"kind": "postgresql-process-port", "pid": process["pid"], "port": instance["port"]})
    if any(service.get("name") == instance["service"] for service in inventory_value["systemd_services"]):
        collisions.append({"kind": "systemd-service", "service": instance["service"]})
    return collisions


def select_host(
    contract: dict[str, Any],
    inventory_value: dict[str, Any],
    request: dict[str, Any],
    package: dict[str, Any],
) -> dict[str, Any]:
    validate_contract(contract)
    validate_inventory(inventory_value)
    validate_request(request, contract)
    if inventory_value["inspection_errors"]:
        raise HostControlError("host inventory is incomplete")
    if package.get("schema") != PACKAGE_SCHEMA or package.get("activation_eligible") is not True:
        raise HostControlError("selected product package is not activation eligible")
    package_id = package.get("package_id")
    if not isinstance(package_id, str) or HEX_256.fullmatch(package_id) is None:
        raise HostControlError("selected product package identity is invalid")
    package_postgresql = package.get("postgresql")
    if not isinstance(package_postgresql, dict) or package_postgresql.get("version") != request["postgresql"]["version"]:
        raise HostControlError("requested and packaged PostgreSQL versions differ")
    collisions = target_collisions(inventory_value, request)
    mode = request["mode"]
    if mode == "create" and collisions:
        raise HostControlError("create topology collides with existing infrastructure")
    selected_existing: dict[str, Any] | None = None
    adoption_gates: dict[str, bool] | None = None
    if mode == "adopt":
        system_identifier = request.get("existing_system_identifier")
        if not isinstance(system_identifier, str) or not system_identifier.isdigit():
            raise HostControlError("adoption requires an exact PostgreSQL system identifier")
        matches = [
            item
            for item in inventory_value["postgresql_processes"]
            if item.get("data_directory") == request["instance"]["data_directory"]
        ]
        if len(matches) != 1:
            raise HostControlError("adoption must resolve exactly one running cluster")
        selected_existing = matches[0]
        installation = next(
            (
                item
                for item in inventory_value["postgresql_installations"]
                if item["postgres"] == selected_existing.get("loaded_executable")
            ),
            None,
        )
        exact_version = installation is not None and installation["version"] == request["postgresql"]["version"]
        adoption_gates = {
            "exact_system_identifier": False,
            "exact_server_version": exact_version,
            "loaded_extension_proof": False,
        }
    route_mode = request["routing"]["mode"]
    provider_name = {
        "external-haproxy": "haproxy",
        "external-pgbouncer": "pgbouncer",
    }.get(route_mode)
    provider = None
    if provider_name is not None:
        provider = next(
            item for item in inventory_value["routing_providers"] if item["provider"] == provider_name
        )
        if not provider["available"]:
            raise HostControlError(f"selected routing provider is unavailable: {provider_name}")
        authority = request["routing"].get("authority_receipt_sha256")
        if not isinstance(authority, str) or HEX_256.fullmatch(authority) is None:
            raise HostControlError("external routing requires an exact authority receipt")
    result = {
        "schema": SELECTION_SCHEMA,
        "mode": mode,
        "package_id": package_id,
        "postgresql": {
            "version": request["postgresql"]["version"],
            "major": request["postgresql"]["major"],
            "exact_package_bound": True,
            "patch_binary_compatibility_assumed": False,
        },
        "instance": request["instance"],
        "routing": request["routing"],
        "routing_provider_observation": provider,
        "existing_cluster": selected_existing,
        "adoption_gates": adoption_gates,
        "collisions": collisions,
        "inventory_sha256": inventory_value["inventory_sha256"],
        "activation_disposition": (
            "eligible-for-new-cluster-plan"
            if mode == "create"
            else "requires-controlled-adoption-proof"
            if mode == "adopt"
            else "requires-external-cluster-authority-proof"
        ),
    }
    result["selection_sha256"] = document_identity(result, "selection_sha256")
    return result


def write_json(path: str, value: dict[str, Any]) -> None:
    content = json.dumps(value, indent=2, sort_keys=True) + "\n"
    if path == "-":
        sys.stdout.write(content)
        return
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f".{output.name}.{os.getpid()}")
    with temporary.open("xb") as stream:
        stream.write(content.encode("utf-8"))
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, output)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", default=str(Path(__file__).resolve().parents[2] / "contracts/postgresql-host.json"))
    subparsers = parser.add_subparsers(dest="command", required=True)
    inspect = subparsers.add_parser("inspect")
    inspect.add_argument("--root", default="/")
    inspect.add_argument("--output", default="-")
    select = subparsers.add_parser("select")
    select.add_argument("--inventory", required=True)
    select.add_argument("--request", required=True)
    select.add_argument("--package-manifest", required=True)
    select.add_argument("--output", default="-")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    contract = load_json(Path(arguments.contract))
    if arguments.command == "inspect":
        write_json(arguments.output, inventory(contract, Path(arguments.root)))
    else:
        write_json(
            arguments.output,
            select_host(
                contract,
                load_json(Path(arguments.inventory)),
                load_json(Path(arguments.request)),
                load_json(Path(arguments.package_manifest)),
            ),
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except HostControlError as error:
        print(f"postgresql host control: {error}", file=sys.stderr)
        raise SystemExit(1)
