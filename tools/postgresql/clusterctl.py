#!/usr/bin/env python3
"""PostgreSQL product lifecycle owned entirely by ``laplace-runner``.

``cluster_core.py`` remains the isolated/reference implementation used by fixture
coverage.  This adapter owns the physical DEV/BAT policy:

* the recurring product executor is ``laplace-runner``;
* package releases, runtime selection, config, PGDATA, WAL, sockets, logs, receipts,
  Unicode, and Highway are all runner-owned state;
* PostgreSQL is started/stopped with the PostgreSQL package's own ``pg_ctl``;
* live process identity comes from PGDATA/postmaster.pid plus /proc, not systemd;
* systemd is optional boot integration installed by setup-host, never a delivery
  dependency and never a recurring privilege boundary;
* ``/opt/laplace/runtime/refactor`` selects the candidate being proved while
  ``/opt/laplace/current`` is committed only after exact process/restart proof;
* failed uncommitted candidate state is removed only after a stopped-postmaster proof;
* durable receipt state is never treated as fresh-cluster candidate state.
"""

from __future__ import annotations

import copy
import importlib.util
import os
from pathlib import Path
import pwd
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any, Sequence


_CORE_PATH = Path(__file__).with_name("cluster_core.py")
_SPEC = importlib.util.spec_from_file_location("laplace_postgresql_cluster_core", _CORE_PATH)
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("cannot load PostgreSQL cluster core")
_core = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = _core
_SPEC.loader.exec_module(_core)

RUNNER_USER = "laplace-runner"
RUNTIME_LINK = "/opt/laplace/runtime/refactor"
LIFECYCLE_PROVIDER = "pg_ctl"

# Keep every shared contract/package/receipt utility available through clusterctl.
for _name in dir(_core):
    if _name.startswith("__"):
        continue
    globals()[_name] = getattr(_core, _name)

_ORIGINAL_VALIDATE_CONTRACT = _core.validate_contract
_ORIGINAL_VALIDATE_PLAN = _core.validate_plan
_ORIGINAL_BUILD_PLAN = _core.build_plan
_ORIGINAL_COLLISION_TARGET = _core.collision_target
_ORIGINAL_INSTALL_PACKAGE = _core.install_package


def _runner_identity() -> pwd.struct_passwd:
    try:
        return pwd.getpwnam(RUNNER_USER)
    except KeyError as error:
        raise _core.ClusterError(f"required service identity is absent: {RUNNER_USER}") from error


def _require_runner() -> None:
    expected = _runner_identity()
    if os.geteuid() != expected.pw_uid:
        actual = pwd.getpwuid(os.geteuid()).pw_name
        raise _core.ClusterError(
            f"persistent product lifecycle requires {RUNNER_USER}, not {actual}"
        )


def require_fixture_or_root(root: Path, authorize_system_root: bool) -> None:
    if not root.is_absolute():
        raise _core.ClusterError("activation root must be absolute")
    if root == Path("/"):
        _require_runner()


def _validation_contract(document: dict[str, Any]) -> dict[str, Any]:
    """Project only the old core's service->admin prohibition for compatibility.

    Production deliberately uses one OS identity (laplace-runner) with distinct
    PostgreSQL roles.  The projection is used only to reuse legacy invariant checks;
    it is never written, executed, or treated as product authority.
    """
    projected = copy.deepcopy(document)
    security = projected.get("security")
    instance = projected.get("instance")
    if isinstance(security, dict) and isinstance(instance, dict):
        if security.get("admin_os_user") == instance.get("os_user"):
            security["admin_os_user"] = "laplace_admin_owner"
    return projected


def validate_contract(document: dict[str, Any]) -> None:
    _ORIGINAL_VALIDATE_CONTRACT(_validation_contract(document))
    instance = document["instance"]
    security = document["security"]
    if instance["os_user"] != RUNNER_USER or instance["os_group"] != RUNNER_USER:
        raise _core.ClusterError("persistent product cluster owner must be laplace-runner")
    if security.get("admin_os_user") != RUNNER_USER:
        raise _core.ClusterError("product database administrator must be laplace-runner")
    if security.get("app_os_user") != RUNNER_USER:
        raise _core.ClusterError("product application peer must be laplace-runner")
    socket_directory = str(instance.get("socket_directory", ""))
    if not socket_directory.startswith("/opt/laplace/runtime/"):
        raise _core.ClusterError("product socket directory must be runner-owned runtime state")


def _rendered_entry(plan: dict[str, Any], path: str) -> dict[str, Any]:
    for entry in plan.get("files", []):
        if isinstance(entry, dict) and entry.get("path") == path:
            return entry
    raise _core.ClusterError(f"plan omits required generated file: {path}")


def _replace_rendered(plan: dict[str, Any], path: str, content: str, mode: int) -> None:
    entry = _rendered_entry(plan, path)
    entry["content"] = content
    entry["mode"] = mode
    entry["sha256"] = _core.sha256_bytes(content.encode("utf-8"))


def _project_plan_for_core_validation(
    plan: dict[str, Any], contract: dict[str, Any]
) -> tuple[dict[str, Any], dict[str, Any]]:
    projected_contract = _validation_contract(contract)
    projected = copy.deepcopy(plan)
    instance = projected["instance"]
    service_path = f"/etc/systemd/system/{instance['service']}"
    if not any(entry.get("path") == service_path for entry in projected["files"]):
        service = _core.render_service(
            projected_contract,
            projected["package_root"],
            projected["resource_grant"],
        )
        projected["files"].append(
            {
                "path": service_path,
                "mode": 0o644,
                "sha256": _core.sha256_bytes(service.encode("utf-8")),
                "content": service,
            }
        )
        projected["files"] = sorted(projected["files"], key=lambda item: item["path"])
    ident_path = f"{instance['config_directory']}/pg_ident.conf"
    _replace_rendered(
        projected,
        ident_path,
        _core.render_ident(projected_contract),
        0o640,
    )
    projected["contract_sha256"] = _core.sha256_bytes(
        _core.canonical_bytes(projected_contract)
    )
    projected.pop("plan_sha256", None)
    projected["plan_sha256"] = _core.sha256_bytes(_core.canonical_bytes(projected))
    return projected, projected_contract


def validate_plan(plan: dict[str, Any], contract: dict[str, Any] | None = None) -> None:
    if contract is None:
        raise _core.ClusterError("runner plan validation requires its cluster contract")
    validate_contract(contract)
    live = plan.get("collision_observation_source") != "laplace_typed_fixture"
    service_path = f"/etc/systemd/system/{plan['instance']['service']}"
    if live and any(entry.get("path") == service_path for entry in plan.get("files", [])):
        raise _core.ClusterError("live activation plan cannot rewrite optional systemd integration")
    if live and plan["instance"]["receipt_directory"] in plan.get("state_directories", []):
        raise _core.ClusterError("durable receipt namespace cannot be fresh candidate state")
    if live and plan["instance"]["socket_directory"] not in plan.get("state_directories", []):
        raise _core.ClusterError("runner-owned socket directory must be candidate state")
    projected, projected_contract = _project_plan_for_core_validation(plan, contract)
    _ORIGINAL_VALIDATE_PLAN(projected, projected_contract)


def collision_target(contract: dict[str, Any]) -> dict[str, Any]:
    target = _ORIGINAL_COLLISION_TARGET(_validation_contract(contract))
    target["paths"] = [
        path
        for path in target["paths"]
        if path != f"/etc/systemd/system/{contract['instance']['service']}"
        and path != contract["instance"]["receipt_directory"]
    ]
    return target


def validate_collision_observation(
    observation: dict[str, Any], contract: dict[str, Any]
) -> None:
    if observation.get("schema") != COLLISION_SCHEMA:
        raise _core.ClusterError(f"collision observation schema must be {COLLISION_SCHEMA}")
    if observation.get("source") not in {
        "laplace_clusterctl_live_probe",
        "laplace_typed_fixture",
    }:
        raise _core.ClusterError("collision observation source is unsupported")
    require_absolute_path(observation.get("root"), "collision.root")
    if observation.get("target") != collision_target(contract):
        raise _core.ClusterError("collision observation targets differ from the cluster contract")
    collisions = observation.get("collisions")
    errors = observation.get("inspection_errors")
    if not isinstance(collisions, list) or any(not isinstance(item, dict) for item in collisions):
        raise _core.ClusterError("collision observation findings must be an array of objects")
    if not isinstance(errors, list) or any(not isinstance(item, dict) for item in errors):
        raise _core.ClusterError("collision observation inspection_errors must be an array of objects")
    if observation.get("observation_sha256") != collision_observation_identity(observation):
        raise _core.ClusterError("collision observation digest differs from its content")
    if errors:
        first = errors[0]
        raise _core.ClusterError(
            "cluster collision inspection is incomplete: "
            f"{first.get('operation', 'unknown')} {first.get('target', '')}: "
            f"{first.get('error', 'unknown error')}"
        )
    if collisions:
        first = collisions[0]
        raise _core.ClusterError(
            f"cluster target collision: {first.get('kind', 'unknown')} {first.get('target', '')}"
        )


def inspect_collisions(contract: dict[str, Any], root: Path) -> dict[str, Any]:
    """Observe product collisions without treating optional systemd as product state."""
    validate_contract(contract)
    if not root.is_absolute():
        raise _core.ClusterError("collision inspection root must be absolute")
    target = collision_target(contract)
    findings: list[dict[str, Any]] = []
    inspection_errors: list[dict[str, Any]] = []

    def record_error(operation: str, target_value: Any, error: OSError) -> None:
        inspection_errors.append(
            {
                "operation": operation,
                "target": target_value,
                "errno": error.errno,
                "error": error.strerror or error.__class__.__name__,
            }
        )

    for logical in target["paths"]:
        physical = prefixed(root, logical)
        try:
            os.lstat(physical)
        except FileNotFoundError:
            continue
        except OSError as error:
            record_error("lstat", logical, error)
        else:
            findings.append({"kind": "path", "target": logical})

    if root == Path("/"):
        port_hex = f"{target['port']:04X}"
        for network_table in (Path("/proc/net/tcp"), Path("/proc/net/tcp6")):
            try:
                rows = network_table.read_text(encoding="ascii").splitlines()[1:]
            except OSError as error:
                record_error("read", str(network_table), error)
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
            record_error("read", "/proc/net/unix", error)
            unix_rows = []
        if any(row.split()[-1] == socket_path for row in unix_rows if len(row.split()) >= 8):
            findings.append({"kind": "unix-socket", "target": socket_path})

        needles = (
            target["socket_directory"].encode("utf-8"),
            contract["instance"]["data_directory"].encode("utf-8"),
        )
        try:
            processes = list(Path("/proc").iterdir())
        except OSError as error:
            record_error("list", "/proc", error)
            processes = []
        for process in processes:
            if not process.name.isdigit():
                continue
            try:
                command = (process / "cmdline").read_bytes()
            except FileNotFoundError:
                continue
            except OSError as error:
                record_error("read", str(process / "cmdline"), error)
                continue
            if command and any(needle in command for needle in needles):
                try:
                    owner_uid = process.stat().st_uid
                except FileNotFoundError:
                    owner_uid = None
                except OSError as error:
                    record_error("stat", str(process), error)
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
        "source": (
            "laplace_clusterctl_live_probe" if root == Path("/") else "laplace_typed_fixture"
        ),
        "root": str(root),
        "target": target,
        "collisions": findings,
        "inspection_errors": inspection_errors,
    }
    observation["observation_sha256"] = collision_observation_identity(observation)
    return observation


def render_ident(contract: dict[str, Any]) -> str:
    security = contract["security"]
    instance = contract["instance"]
    return (
        f"{security['admin_map']} {RUNNER_USER} {instance['admin_role']}\n"
        f"{security['app_map']} {RUNNER_USER} {instance['app_role']}\n"
    )


def _bootstrap_command(plan: dict[str, Any]) -> list[str]:
    instance = plan["instance"]
    psql = f"{plan['package_root']}/pgsql-{plan['postgresql_major']}/bin/psql"
    return [
        psql,
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
    ]


def _initdb_command(plan: dict[str, Any]) -> list[str]:
    package_root = plan["package_root"]
    instance = plan["instance"]
    return [
        f"{package_root}/pgsql-{plan['postgresql_major']}/bin/initdb",
        "--pgdata",
        instance["data_directory"],
        "--waldir",
        instance["wal_directory"],
        "--encoding",
        "UTF8",
        "--locale-provider",
        "icu",
        "--icu-locale",
        "und",
        "--data-checksums",
        "--auth-local",
        "peer",
        "--auth-host",
        "reject",
        "--username",
        instance["admin_role"],
    ]


def _pg_ctl_command(plan: dict[str, Any], action: str) -> list[str]:
    instance = plan["instance"]
    pg_ctl = f"{plan['package_root']}/pgsql-{plan['postgresql_major']}/bin/pg_ctl"
    common = [pg_ctl, "-D", instance["data_directory"], "-s", "-w", "-t", "300"]
    if action == "start":
        return [
            *common,
            "-l",
            f"{instance['log_directory']}/pg_ctl.log",
            "-o",
            f"-c config_file={instance['config_directory']}/postgresql.conf",
            "start",
        ]
    if action == "stop":
        return [*common, "-m", "fast", "stop"]
    if action == "status":
        return [pg_ctl, "-D", instance["data_directory"], "status"]
    raise _core.ClusterError(f"unsupported pg_ctl product action: {action}")


def build_plan(
    contract_path: Path,
    package_path: Path,
    resource_path: Path,
    collision_path: Path,
    package_physical_root: Path | None = None,
) -> dict[str, Any]:
    contract = load_json(contract_path)
    validate_contract(contract)
    collision = load_json(collision_path)
    validate_collision_observation(collision, contract)
    projected_contract = _validation_contract(contract)

    temporary_contract: Path | None = None
    temporary_collision: Path | None = None
    try:
        descriptor, name = tempfile.mkstemp(prefix="laplace-cluster-contract-")
        os.close(descriptor)
        temporary_contract = Path(name)
        temporary_contract.write_bytes(canonical_bytes(projected_contract))

        projected_collision = copy.deepcopy(collision)
        projected_collision["target"] = _core.collision_target(projected_contract)
        projected_collision["observation_sha256"] = _core.collision_observation_identity(
            projected_collision
        )
        descriptor, name = tempfile.mkstemp(prefix="laplace-cluster-collision-")
        os.close(descriptor)
        temporary_collision = Path(name)
        temporary_collision.write_bytes(canonical_bytes(projected_collision))

        plan = _ORIGINAL_BUILD_PLAN(
            temporary_contract,
            package_path,
            resource_path,
            temporary_collision,
            package_physical_root,
        )
    finally:
        if temporary_contract is not None:
            temporary_contract.unlink(missing_ok=True)
        if temporary_collision is not None:
            temporary_collision.unlink(missing_ok=True)

    fixture = collision["source"] == "laplace_typed_fixture"
    instance = contract["instance"]
    service_path = f"/etc/systemd/system/{instance['service']}"
    plan["contract_sha256"] = sha256_bytes(canonical_bytes(contract))
    plan["collision_observation_sha256"] = sha256_bytes(canonical_bytes(collision))
    plan["collision_observation_source"] = collision["source"]
    plan["collision_observation_root"] = collision["root"]
    plan["runtime_link"] = RUNTIME_LINK
    plan["lifecycle_provider"] = LIFECYCLE_PROVIDER

    if not fixture:
        plan["files"] = [
            entry for entry in plan["files"] if entry.get("path") != service_path
        ]
        plan["state_directories"] = [
            value
            for value in plan["state_directories"]
            if value != instance["receipt_directory"]
        ]
        if instance["socket_directory"] not in plan["state_directories"]:
            plan["state_directories"].append(instance["socket_directory"])
        plan["state_directories"] = sorted(set(plan["state_directories"]))

    ident_path = f"{instance['config_directory']}/pg_ident.conf"
    _replace_rendered(plan, ident_path, render_ident(contract), 0o640)
    plan["commands"]["initdb"] = _initdb_command(plan)
    plan["commands"]["bootstrap"] = _bootstrap_command(plan)
    plan["commands"]["start_candidate"] = _pg_ctl_command(plan, "start")
    plan["commands"]["stop_candidate"] = _pg_ctl_command(plan, "stop")
    plan["commands"]["status_candidate"] = _pg_ctl_command(plan, "status")
    if not fixture:
        for obsolete in (
            "daemon_reload",
            "start_candidate_service",
            "stop_candidate_service",
            "enable_candidate_service",
            "disable_candidate_service",
            "verify_candidate_service_enabled",
        ):
            plan["commands"].pop(obsolete, None)

    plan.pop("plan_sha256", None)
    plan["plan_sha256"] = sha256_bytes(canonical_bytes(plan))
    validate_plan(plan, contract)
    return plan


def install_package(
    manifest: dict[str, Any],
    contract: dict[str, Any],
    package_physical_root: Path,
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    require_fixture_or_root(root, authorize_system_root)
    return _ORIGINAL_INSTALL_PACKAGE(
        manifest,
        _validation_contract(contract),
        package_physical_root,
        root,
        root == Path("/"),
    )


def apply_plan(
    plan: dict[str, Any],
    contract: dict[str, Any],
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    require_fixture_or_root(root, authorize_system_root)
    validate_plan(plan, contract)
    if root == Path("/") and (
        plan.get("collision_observation_source") != "laplace_clusterctl_live_probe"
        or plan.get("collision_observation_root") != "/"
    ):
        raise _core.ClusterError("system activation requires a live collision observation")
    validate_collision_observation(inspect_collisions(contract, root), contract)
    if not plan.get("package_verified") or plan.get("activation_blocked"):
        raise _core.ClusterError("activation is blocked until package bytes verify")

    targets = [(entry, prefixed(root, entry["path"])) for entry in plan["files"]]
    state_targets = [
        (logical, prefixed(root, logical)) for logical in plan["state_directories"]
    ]
    for entry, target in targets:
        if target.exists() or target.is_symlink():
            raise _core.ClusterError(f"activation refuses existing path: {entry['path']}")
    for logical, target in state_targets:
        if target.exists() or target.is_symlink():
            raise _core.ClusterError(f"activation refuses existing state directory: {logical}")

    runtime = prefixed(root, RUNTIME_LINK)
    if root == Path("/") and (runtime.exists() or runtime.is_symlink()):
        raise _core.ClusterError("fresh activation refuses an existing runtime candidate link")

    installed: list[dict[str, Any]] = []
    created: list[str] = []
    try:
        for entry, target in targets:
            content = entry["content"].encode("utf-8")
            if sha256_bytes(content) != entry["sha256"]:
                raise _core.ClusterError(f"rendered file digest differs: {entry['path']}")
            atomic_write(target, content, entry["mode"])
            installed.append({"path": entry["path"], "sha256": entry["sha256"]})
        for logical, target in state_targets:
            target.mkdir(parents=True, exist_ok=False, mode=0o700)
            created.append(logical)
        if root == Path("/"):
            runtime.parent.mkdir(parents=True, exist_ok=True, mode=0o750)
            temporary = runtime.parent / f".{runtime.name}.{plan['package_id'][:16]}"
            temporary.unlink(missing_ok=True)
            os.symlink(f"../releases/{plan['package_id']}", temporary)
            os.replace(temporary, runtime)
    except BaseException:
        if root == Path("/") and runtime.is_symlink() and os.readlink(runtime) == f"../releases/{plan['package_id']}":
            runtime.unlink()
        for entry in reversed(installed):
            target = prefixed(root, entry["path"])
            if target.is_file() and not target.is_symlink() and sha256_file(target) == entry["sha256"]:
                target.unlink()
        for logical in sorted(created, key=lambda value: len(Path(value).parts), reverse=True):
            try:
                prefixed(root, logical).rmdir()
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
        "created_directories": created,
        "state_preserved_on_remove": True,
        "previous_active_target": None,
    }


def execute_activation_command(
    label: str, command: Sequence[str], timeout: int = 1800
) -> dict[str, Any]:
    completed = subprocess.run(
        list(command),
        check=False,
        cwd="/",
        env=activation_environment(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )
    receipt = command_execution_receipt(label, command, completed)
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or f"exit {completed.returncode}"
        raise _core.ClusterError(f"{label} failed: {detail[-1000:]}")
    return receipt


def verify_loaded(
    plan: dict[str, Any], contract: dict[str, Any], observation: dict[str, Any]
) -> None:
    validate_plan(plan, contract)
    if observation.get("schema") != LOADED_SCHEMA:
        raise _core.ClusterError(f"loaded observation schema must be {LOADED_SCHEMA}")
    if observation.get("source") not in {
        "laplace_postgresql_loaded_probe",
        "laplace_typed_fixture",
    }:
        raise _core.ClusterError("loaded observation source is unsupported")
    if observation.get("observation_sha256") != state_observation_identity(observation):
        raise _core.ClusterError("loaded observation digest differs from its content")
    if observation.get("package_id") != plan["package_id"]:
        raise _core.ClusterError("loaded package identity differs from the plan")
    instance = plan["instance"]
    for field in ("port", "socket_directory", "data_directory", "service"):
        if observation.get(field) != instance[field]:
            raise _core.ClusterError(f"loaded observation {field} differs from the plan")
    identifier = str(observation.get("system_identifier", ""))
    if not identifier.isdecimal() or int(identifier) <= 0:
        raise _core.ClusterError("loaded observation omits a valid positive system identifier")
    if observation.get("service_state") not in {None, "active"}:
        raise _core.ClusterError("loaded observation does not describe an active product")
    for field in ("postmaster_pid", "backend_pid"):
        value = observation.get(field)
        if value is not None and (not isinstance(value, int) or value <= 0):
            raise _core.ClusterError(f"loaded observation has an invalid {field}")

    if observation.get("source") == "laplace_postgresql_loaded_probe":
        if observation.get("service_state") != "active":
            raise _core.ClusterError("live loaded observation requires an active postmaster")
        if not all(
            isinstance(observation.get(field), int) and observation[field] > 0
            for field in ("postmaster_pid", "backend_pid")
        ):
            raise _core.ClusterError("live loaded observation requires postmaster and backend PIDs")
        if observation["postmaster_pid"] == observation["backend_pid"]:
            raise _core.ClusterError("live loaded observation collapsed postmaster and backend identity")
        if HEX_256.fullmatch(str(observation.get("probe_sql_sha256", ""))) is None:
            raise _core.ClusterError("live loaded observation omits the exact probe program")
        lifecycle = observation.get("service_observation")
        if (
            not isinstance(lifecycle, dict)
            or lifecycle.get("exit_code") != 0
            or lifecycle.get("label") != "observe-postmaster-state"
        ):
            raise _core.ClusterError("live loaded observation omits pg_ctl state evidence")

    objects = observation.get("loaded_objects")
    if not isinstance(objects, list) or any(not isinstance(item, dict) for item in objects):
        raise _core.ClusterError("loaded object observation is required")
    observed = {item.get("path"): item.get("sha256") for item in objects}
    expected = {item["path"]: item["sha256"] for item in plan["required_loaded_objects"]}
    if len(observed) != len(objects) or observed != expected:
        raise _core.ClusterError("loaded object paths or hashes differ from the package manifest")

    config = observation.get("config_files")
    expected_config = {item["path"]: item["sha256"] for item in plan["files"]}
    if not isinstance(config, list) or any(not isinstance(item, dict) for item in config):
        raise _core.ClusterError("loaded config observation is required")
    observed_config = {item.get("path"): item.get("sha256") for item in config}
    if len(observed_config) != len(config) or observed_config != expected_config:
        raise _core.ClusterError("loaded configuration differs from the generated plan")


def compose_loaded_observation(
    plan: dict[str, Any],
    contract: dict[str, Any],
    root: Path,
    postmaster_pid: int,
    backend_pid: int,
    system_identifier: str,
    process_paths: set[str],
    probe_sql_sha256: str,
    lifecycle_receipt: dict[str, Any],
) -> dict[str, Any]:
    validate_plan(plan, contract)
    expected_paths = {item["path"] for item in plan["required_loaded_objects"]}
    missing = sorted(expected_paths - process_paths)
    if missing:
        raise _core.ClusterError(
            "live PostgreSQL backend omits required package objects: " + ", ".join(missing)
        )
    loaded_objects = []
    for expected in plan["required_loaded_objects"]:
        target = prefixed(root, expected["path"])
        if not target.is_file() or target.is_symlink():
            raise _core.ClusterError(f"loaded package object is absent: {expected['path']}")
        digest = sha256_file(target)
        if digest != expected["sha256"]:
            raise _core.ClusterError(f"loaded package object bytes differ: {expected['path']}")
        loaded_objects.append({"path": expected["path"], "sha256": digest})
    config_files = []
    for expected in plan["files"]:
        target = prefixed(root, expected["path"])
        if not target.is_file() or target.is_symlink():
            raise _core.ClusterError(f"generated configuration is absent: {expected['path']}")
        digest = sha256_file(target)
        if digest != expected["sha256"]:
            raise _core.ClusterError(f"generated configuration bytes differ: {expected['path']}")
        config_files.append({"path": expected["path"], "sha256": digest})
    observation = {
        "schema": LOADED_SCHEMA,
        "source": "laplace_postgresql_loaded_probe",
        "package_id": plan["package_id"],
        "port": plan["instance"]["port"],
        "socket_directory": plan["instance"]["socket_directory"],
        "data_directory": plan["instance"]["data_directory"],
        "service": plan["instance"]["service"],
        "service_state": "active",
        "postmaster_pid": postmaster_pid,
        "backend_pid": backend_pid,
        "system_identifier": system_identifier,
        "probe_sql_sha256": probe_sql_sha256,
        "service_observation": lifecycle_receipt,
        "loaded_objects": loaded_objects,
        "config_files": config_files,
    }
    observation["observation_sha256"] = state_observation_identity(observation)
    verify_loaded(plan, contract, observation)
    return observation


def observe_loaded_live(
    plan: dict[str, Any], contract: dict[str, Any], root: Path, proc_root: Path = Path("/proc")
) -> dict[str, Any]:
    if root != Path("/"):
        raise _core.ClusterError("live loaded-object observation requires the product root")
    _require_runner()
    validate_plan(plan, contract)

    status_command = _pg_ctl_command(plan, "status")
    completed = subprocess.run(
        status_command,
        check=False,
        cwd="/",
        env=activation_environment(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
    )
    lifecycle_receipt = command_execution_receipt(
        "observe-postmaster-state", status_command, completed
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or "pg_ctl status failed"
        raise _core.ClusterError(f"candidate PostgreSQL is not running: {detail}")

    pid_file = Path(plan["instance"]["data_directory"]) / "postmaster.pid"
    try:
        pid_text = pid_file.read_text(encoding="ascii").splitlines()[0].strip()
    except (OSError, IndexError) as error:
        raise _core.ClusterError(f"cannot read postmaster PID from {pid_file}: {error}") from error
    if not pid_text.isdecimal() or int(pid_text) <= 0:
        raise _core.ClusterError("postmaster.pid does not contain a positive PID")
    postmaster_pid = int(pid_text)
    expected_postmaster = (
        f"{plan['package_root']}/pgsql-{plan['postgresql_major']}/bin/postgres"
    )
    postmaster_paths = process_loaded_paths(proc_root, postmaster_pid)
    if expected_postmaster not in postmaster_paths:
        raise _core.ClusterError("running postmaster is not the planned package binary")

    instance = plan["instance"]
    application_name = f"laplace_loaded_{plan['plan_sha256'][:24]}"
    probe_sql = (
        f"SET application_name = '{application_name}'; "
        "LOAD '$libdir/laplace_pg'; "
        "LOAD '$libdir/pg_stat_statements'; "
        "SELECT pg_sleep(120);"
    )
    psql = f"{plan['package_root']}/pgsql-{plan['postgresql_major']}/bin/psql"
    base = [
        psql,
        "--host",
        instance["socket_directory"],
        "--port",
        str(instance["port"]),
        "--username",
        instance["admin_role"],
        "--dbname",
        instance["database"],
        "--no-psqlrc",
        "--set",
        "ON_ERROR_STOP=1",
    ]
    probe = subprocess.Popen(
        [*base, "--command", probe_sql],
        cwd="/",
        env=activation_environment(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
    )
    lookup_sql = (
        "SELECT a.pid::text || '|' || c.system_identifier::text "
        "FROM pg_catalog.pg_stat_activity AS a "
        "CROSS JOIN pg_catalog.pg_control_system() AS c "
        f"WHERE a.application_name = '{application_name}' "
        "AND a.pid <> pg_catalog.pg_backend_pid() AND a.state = 'active';"
    )
    backend_pid: int | None = None
    system_identifier: str | None = None
    try:
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            if probe.poll() is not None:
                stdout, stderr = probe.communicate()
                detail = stderr.strip() or stdout.strip() or f"exit {probe.returncode}"
                raise _core.ClusterError(f"loaded-object probe exited early: {detail}")
            lookup = subprocess.run(
                [*base, "--tuples-only", "--no-align", "--quiet", "--command", lookup_sql],
                check=False,
                cwd="/",
                env=activation_environment(),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=10,
            )
            if lookup.returncode == 0:
                rows = [line.strip() for line in lookup.stdout.splitlines() if line.strip()]
                if len(rows) == 1 and "|" in rows[0]:
                    candidate_pid, candidate_identifier = rows[0].split("|", 1)
                    if candidate_pid.isdecimal() and candidate_identifier.isdecimal():
                        backend_pid = int(candidate_pid)
                        system_identifier = candidate_identifier
                        break
            time.sleep(0.1)
        if backend_pid is None or system_identifier is None:
            raise _core.ClusterError("timed out locating loaded-object probe backend")
        paths = process_loaded_paths(proc_root, backend_pid) | postmaster_paths
        return compose_loaded_observation(
            plan,
            contract,
            root,
            postmaster_pid,
            backend_pid,
            system_identifier,
            paths,
            sha256_bytes(probe_sql.encode("utf-8")),
            lifecycle_receipt,
        )
    finally:
        terminate_probe(probe)


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
        raise _core.ClusterError("activation receipt is not staged")
    if receipt.get("plan_sha256") != plan["plan_sha256"]:
        raise _core.ClusterError("activation receipt belongs to another plan")
    if root == Path("/") and observation.get("source") != "laplace_postgresql_loaded_probe":
        raise _core.ClusterError("system commit requires a live loaded-state observation")
    verify_loaded(plan, contract, observation)

    active = prefixed(root, plan["active_link"])
    active.parent.mkdir(parents=True, exist_ok=True)
    previous: str | None = None
    if active.is_symlink():
        previous = os.readlink(active)
    elif active.exists():
        raise _core.ClusterError("active package path exists and is not a symlink")
    target = f"releases/{plan['package_id']}"
    temporary = active.parent / f".{active.name}.{plan['package_id'][:12]}"
    temporary.unlink(missing_ok=True)
    os.symlink(target, temporary)
    os.replace(temporary, active)

    if root == Path("/"):
        runtime = Path(RUNTIME_LINK)
        if not runtime.is_symlink() or os.readlink(runtime) != f"../releases/{plan['package_id']}":
            raise _core.ClusterError("runtime selection changed before commit")

    committed = dict(receipt)
    committed["phase"] = "committed"
    committed["previous_active_target"] = previous
    committed["active_target"] = target
    committed["loaded_observation_sha256"] = sha256_bytes(canonical_bytes(observation))
    return committed


def verify_stopped(plan: dict[str, Any], observation: dict[str, Any]) -> None:
    if observation.get("schema") != STOPPED_SCHEMA:
        raise _core.ClusterError(f"stopped observation schema must be {STOPPED_SCHEMA}")
    if observation.get("source") not in {
        "laplace_postgresql_stopped_probe",
        "laplace_typed_fixture",
    }:
        raise _core.ClusterError("stopped observation source is unsupported")
    if observation.get("observation_sha256") != state_observation_identity(observation):
        raise _core.ClusterError("stopped observation digest differs from its content")
    instance = plan["instance"]
    if observation.get("service") != instance["service"]:
        raise _core.ClusterError("stopped observation belongs to another product instance")
    if observation.get("data_directory") != instance["data_directory"]:
        raise _core.ClusterError("stopped observation belongs to another data directory")
    if observation.get("service_state") != "inactive":
        raise _core.ClusterError("removal requires an inactive postmaster")
    if observation.get("postmaster_pid") is not None:
        raise _core.ClusterError("removal requires proof that no postmaster remains")


def _stopped_live(plan: dict[str, Any]) -> dict[str, Any]:
    status_command = _pg_ctl_command(plan, "status")
    completed = subprocess.run(
        status_command,
        check=False,
        cwd="/",
        env=activation_environment(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
    )
    pid_file = Path(plan["instance"]["data_directory"]) / "postmaster.pid"
    if completed.returncode == 0 or pid_file.exists():
        raise _core.ClusterError("candidate rollback requires a stopped postmaster")
    observation = {
        "schema": STOPPED_SCHEMA,
        "source": "laplace_postgresql_stopped_probe",
        "service": plan["instance"]["service"],
        "data_directory": plan["instance"]["data_directory"],
        "service_state": "inactive",
        "postmaster_pid": None,
    }
    observation["observation_sha256"] = state_observation_identity(observation)
    verify_stopped(plan, observation)
    return observation


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
        raise _core.ClusterError("activation receipt does not match the plan")
    verify_stopped(plan, stopped_observation)
    if root == Path("/") and stopped_observation.get("source") != "laplace_postgresql_stopped_probe":
        raise _core.ClusterError("system removal requires a live stopped-state observation")

    active = prefixed(root, plan["active_link"])
    if receipt.get("phase") == "committed":
        if not active.is_symlink() or os.readlink(active) != receipt.get("active_target"):
            raise _core.ClusterError("active package pointer changed after activation")
    for entry in receipt.get("installed_files", []):
        target = prefixed(root, entry["path"])
        if not target.is_file() or target.is_symlink() or sha256_file(target) != entry["sha256"]:
            raise _core.ClusterError(f"refusing removal of changed or absent file: {entry['path']}")
    for entry in reversed(receipt.get("installed_files", [])):
        prefixed(root, entry["path"]).unlink()
    if receipt.get("phase") == "committed":
        previous = receipt.get("previous_active_target")
        if previous is None:
            active.unlink()
        else:
            temporary = active.parent / f".{active.name}.rollback"
            temporary.unlink(missing_ok=True)
            os.symlink(previous, temporary)
            os.replace(temporary, active)
    if root == Path("/"):
        runtime = Path(RUNTIME_LINK)
        if runtime.is_symlink() and os.readlink(runtime) == f"../releases/{plan['package_id']}":
            runtime.unlink()
    return {
        "schema": ACTIVATION_SCHEMA,
        "phase": "removed",
        "plan_sha256": plan["plan_sha256"],
        "package_id": plan["package_id"],
        "state_directories": receipt.get("state_directories", []),
        "state_preserved": True,
    }


def execute_cluster_activation(
    plan: dict[str, Any],
    contract: dict[str, Any],
    staged: dict[str, Any],
    root: Path,
    authorize_system_root: bool,
    command_receipts: list[dict[str, Any]],
    *,
    observer: Any = None,
    recorder: Any = None,
    executor: Any = None,
    readiness: Any = None,
) -> dict[str, Any]:
    require_fixture_or_root(root, authorize_system_root)
    validate_plan(plan, contract)
    execute = executor or execute_activation_command
    readiness_runner = readiness or await_postgresql_ready
    observe = observer or observe_loaded_live
    record = recorder or (lambda _stem, _document: None)

    for label, command, timeout in (
        ("initialize-cluster", _initdb_command(plan), 1800),
        ("start-candidate-postmaster", _pg_ctl_command(plan, "start"), 300),
    ):
        receipt = execute(label, command, timeout)
        command_receipts.append(receipt)

    ready = readiness_runner("candidate-readiness", plan["commands"]["probe_readiness"], 300)
    command_receipts.append(ready)
    bootstrap = execute("bootstrap-product-database", _bootstrap_command(plan), 1800)
    command_receipts.append(bootstrap)

    loaded_initial = observe(plan, contract, root)
    verify_loaded(plan, contract, loaded_initial)
    record("loaded-initial", loaded_initial)

    stopped = execute(
        "stop-candidate-for-restart-proof", _pg_ctl_command(plan, "stop"), 300
    )
    command_receipts.append(stopped)
    started = execute(
        "start-candidate-after-restart", _pg_ctl_command(plan, "start"), 300
    )
    command_receipts.append(started)
    restart_ready = readiness_runner(
        "restart-readiness", plan["commands"]["probe_readiness"], 300
    )
    command_receipts.append(restart_ready)

    loaded_restart = observe(plan, contract, root)
    verify_loaded(plan, contract, loaded_restart)
    record("loaded-restart", loaded_restart)
    if loaded_restart.get("postmaster_pid") == loaded_initial.get("postmaster_pid"):
        raise _core.ClusterError("restart proof retained the original postmaster")
    if loaded_restart["system_identifier"] != loaded_initial["system_identifier"]:
        raise _core.ClusterError("restart proof changed PostgreSQL system identity")
    if loaded_restart["loaded_objects"] != loaded_initial["loaded_objects"]:
        raise _core.ClusterError("restart proof changed loaded package objects")
    if loaded_restart["config_files"] != loaded_initial["config_files"]:
        raise _core.ClusterError("restart proof changed generated configuration")

    committed = commit_plan(plan, contract, staged, loaded_restart, root, False)
    committed.update(
        {
            "phase": "activated",
            "restart_proven": True,
            "boot_enabled": False,
            "service_integration_required": False,
            "lifecycle_provider": LIFECYCLE_PROVIDER,
            "runtime_target": f"../releases/{plan['package_id']}",
            "cluster_plan_path": staged.get("cluster_plan_path"),
            "system_identifier": loaded_restart["system_identifier"],
            "loaded_initial_observation_sha256": loaded_initial["observation_sha256"],
            "loaded_restart_observation_sha256": loaded_restart["observation_sha256"],
            "command_receipts": command_receipts,
        }
    )
    committed["activation_receipt_sha256"] = state_observation_identity(committed)
    return committed


def _rollback_uncommitted_candidate(
    plan: dict[str, Any], contract: dict[str, Any], staged: dict[str, Any]
) -> None:
    active = Path(plan["active_link"])
    if active.exists() or active.is_symlink():
        raise _core.ClusterError("rollback refuses to delete state while a committed product exists")

    data = Path(plan["instance"]["data_directory"])
    if (data / "PG_VERSION").exists() or (data / "postmaster.pid").exists():
        try:
            execute_activation_command(
                "stop-candidate-after-failure", _pg_ctl_command(plan, "stop"), 300
            )
        except Exception:
            pass
        _stopped_live(plan)

    runtime = Path(RUNTIME_LINK)
    if runtime.is_symlink() and os.readlink(runtime) == f"../releases/{plan['package_id']}":
        runtime.unlink()

    for entry in reversed(staged.get("installed_files", [])):
        target = Path(entry["path"])
        if target.is_file() and not target.is_symlink() and sha256_file(target) == entry["sha256"]:
            target.unlink()
    config = Path(plan["instance"]["config_directory"])
    try:
        config.rmdir()
    except OSError:
        pass

    for logical in sorted(
        staged.get("created_directories", []),
        key=lambda value: len(Path(value).parts),
        reverse=True,
    ):
        target = Path(logical)
        if target.exists() and not target.is_symlink():
            shutil.rmtree(target)


def activate_product(
    contract_path: Path,
    package_path: Path,
    resource_path: Path,
    evidence_directory: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    _require_runner()
    contract = load_json(contract_path)
    package = load_json(package_path)
    validate_contract(contract)
    expected_evidence = (
        Path(contract["instance"]["receipt_directory"])
        / "cluster-activation"
        / package.get("package_id", "invalid")
    )
    if evidence_directory != expected_evidence:
        raise _core.ClusterError("activation evidence directory must be package-addressed")
    evidence_directory.mkdir(parents=True, exist_ok=True, mode=0o750)

    collision = inspect_collisions(contract, Path("/"))
    validate_collision_observation(collision, contract)
    collision_path = evidence_directory / "collision-observation.json"
    write_json(collision_path, collision)

    plan = build_plan(
        contract_path,
        package_path,
        resource_path,
        collision_path,
        Path("/"),
    )
    plan_path = evidence_directory / f"cluster-plan-{plan['plan_sha256']}.json"
    write_json(plan_path, plan)
    staged = apply_plan(plan, contract, Path("/"), False)
    staged["cluster_plan_path"] = str(plan_path)
    write_json(evidence_directory / "activation-staged.json", staged)

    command_receipts: list[dict[str, Any]] = []

    def record(stem: str, document: dict[str, Any]) -> None:
        write_json(evidence_directory / f"{stem}.json", document)

    try:
        result = execute_cluster_activation(
            plan,
            contract,
            staged,
            Path("/"),
            False,
            command_receipts,
            recorder=record,
        )
    except BaseException as error:
        failure = {
            "schema": ACTIVATION_SCHEMA,
            "phase": "failed",
            "package_id": package["package_id"],
            "plan_sha256": plan["plan_sha256"],
            "error_type": type(error).__name__,
            "error": str(error),
            "command_receipts": command_receipts,
            "active_pointer_committed": False,
            "lifecycle_provider": LIFECYCLE_PROVIDER,
        }
        try:
            _rollback_uncommitted_candidate(plan, contract, staged)
            failure["candidate_rolled_back"] = True
        except BaseException as rollback_error:
            failure["candidate_rolled_back"] = False
            failure["rollback_error"] = str(rollback_error)
            failure["activation_receipt_sha256"] = state_observation_identity(failure)
            write_json(evidence_directory / "activation-failed.json", failure)
            raise _core.ClusterError(
                f"activation failed and candidate rollback could not prove safety: {rollback_error}"
            ) from error
        failure["activation_receipt_sha256"] = state_observation_identity(failure)
        write_json(evidence_directory / "activation-failed.json", failure)
        raise

    result["cluster_plan_path"] = str(plan_path)
    result["activation_receipt_sha256"] = state_observation_identity(result)
    write_json(evidence_directory / "activation-complete.json", result)
    return result


def parse_args(argv: Sequence[str]) -> Any:
    return _core.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    repository = Path(__file__).resolve().parents[2]

    def resolve(value: str) -> Path:
        path = Path(value)
        return path if path.is_absolute() else repository / path

    if args.command == "activate-product":
        result = activate_product(
            resolve(args.contract),
            resolve(args.package_manifest),
            resolve(args.resource_observation),
            resolve(args.evidence_directory),
            bool(args.authorize_system_root),
        )
        write_json(resolve(args.output), result)
        return 0
    if args.command == "inspect-collisions":
        result = inspect_collisions(resolve(args.contract), Path(args.root))
        write_json(resolve(args.output), result)
        return 0
    if args.command == "observe-loaded":
        plan = load_json(resolve(args.plan))
        contract = load_json(resolve(args.contract))
        result = observe_loaded_live(plan, contract, Path("/"))
        write_json(resolve(args.output), result)
        return 0
    return _core.main(sys.argv[1:] if argv is None else list(argv))


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"clusterctl: {error}", file=sys.stderr)
        raise SystemExit(1) from error
