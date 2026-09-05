#!/usr/bin/env python3
"""PostgreSQL product-cluster lifecycle owned by ``laplace-runner``.

``cluster_core.py`` retains shared package, plan, PostgreSQL, receipt, and verification
machinery. This adapter owns the physical DEV/BAT policy:

* system-root product state is created by ``laplace-runner``, never a root product gateway;
* root bootstrap owns only the static systemd unit and exact start/stop/restart sudo capability;
* package releases, runtime selection, config, PGDATA, WAL, perfcache, logs, receipts,
  Unicode, and Highway remain service-account state;
* static service state is not a fresh-cluster collision and is never rewritten by CI;
* `/opt/laplace/runtime/refactor` selects the candidate before proof while
  `/opt/laplace/current` remains the post-proof committed package identity;
* failed uncommitted candidates roll back only after a stopped-process proof.
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
BOOTSTRAP_RECEIPT = Path("/opt/laplace/receipts/bootstrap/host.json")
CANONICAL_CONTRACT = Path(__file__).resolve().parents[2] / "contracts/postgresql-cluster.json"

_ORIGINAL_VALIDATE_CONTRACT = _core.validate_contract
_ORIGINAL_VALIDATE_PLAN = _core.validate_plan
_ORIGINAL_COLLISION_TARGET = _core.collision_target
_ORIGINAL_INSPECT_COLLISIONS = _core.inspect_collisions
_ORIGINAL_MAIN = _core.main


def _runner_uid() -> int:
    try:
        return pwd.getpwnam(RUNNER_USER).pw_uid
    except KeyError as error:
        raise _core.ClusterError(f"required product owner is absent: {RUNNER_USER}") from error


def _require_runner() -> None:
    expected = _runner_uid()
    if os.geteuid() != expected:
        actual = pwd.getpwuid(os.geteuid()).pw_name
        raise _core.ClusterError(
            f"system product lifecycle requires {RUNNER_USER}, not {actual}"
        )


def require_fixture_or_root(root: Path, authorize_system_root: bool) -> None:
    if not root.is_absolute():
        raise _core.ClusterError("activation root must be absolute")
    if root == Path("/"):
        _require_runner()


def _validation_contract(document: dict[str, Any]) -> dict[str, Any]:
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
        canonical = _core.load_json(CANONICAL_CONTRACT)
        canonical_sha = _core.sha256_bytes(_core.canonical_bytes(canonical))
        if plan.get("contract_sha256") == canonical_sha:
            contract = canonical
        else:
            _ORIGINAL_VALIDATE_PLAN(plan, None)
            return
    validate_contract(contract)
    projected, projected_contract = _project_plan_for_core_validation(plan, contract)
    _ORIGINAL_VALIDATE_PLAN(projected, projected_contract)
    instance = plan.get("instance")
    if not isinstance(instance, dict):
        raise _core.ClusterError("plan instance is required")
    if plan.get("runtime_link") != RUNTIME_LINK:
        raise _core.ClusterError("plan runtime link differs from the runner-owned product link")
    service_path = f"/etc/systemd/system/{instance['service']}"
    if any(entry.get("path") == service_path for entry in plan.get("files", [])):
        raise _core.ClusterError("product plan must not rewrite the bootstrap-owned service unit")
    if instance["receipt_directory"] in plan.get("state_directories", []):
        raise _core.ClusterError("persistent receipt directory cannot be candidate state")
    for name in ("initdb", "bootstrap"):
        command = plan.get("commands", {}).get(name)
        if not isinstance(command, list) or not command:
            raise _core.ClusterError(f"plan command is absent: {name}")
        if any(str(item).endswith("runuser") or str(item) == "runuser" for item in command):
            raise _core.ClusterError(f"runner-owned plan cannot use runuser: {name}")


def collision_target(contract: dict[str, Any]) -> dict[str, Any]:
    target = _ORIGINAL_COLLISION_TARGET(contract)
    instance = contract["instance"]
    persistent = {
        instance["receipt_directory"],
        f"/etc/systemd/system/{instance['service']}",
    }
    result = dict(target)
    result["paths"] = [path for path in target["paths"] if path not in persistent]
    return result


def inspect_collisions(contract: dict[str, Any], root: Path) -> dict[str, Any]:
    validate_contract(contract)
    observation = _ORIGINAL_INSPECT_COLLISIONS(contract, root)
    observation["collisions"] = [
        item
        for item in observation.get("collisions", [])
        if not (
            item.get("kind") == "service"
            and item.get("target") == contract["instance"]["service"]
        )
    ]
    observation["target"] = collision_target(contract)
    observation["observation_sha256"] = _core.collision_observation_identity(observation)
    return observation


def validate_collision_observation(
    observation: dict[str, Any], contract: dict[str, Any]
) -> None:
    _core.validate_collision_observation(observation, contract)


def _bootstrap_service_policy(service: str) -> tuple[str, str]:
    if not BOOTSTRAP_RECEIPT.is_file() or BOOTSTRAP_RECEIPT.is_symlink():
        raise _core.ClusterError(f"host bootstrap receipt is absent: {BOOTSTRAP_RECEIPT}")
    receipt = _core.load_json(BOOTSTRAP_RECEIPT)
    if receipt.get("schema") != "laplace.host-bootstrap/v1":
        raise _core.ClusterError("host bootstrap receipt schema differs")
    identity = receipt.get("service_identity")
    policy = receipt.get("sudo_policy")
    envelope = receipt.get("service_envelope")
    if not isinstance(identity, dict) or identity.get("user") != RUNNER_USER:
        raise _core.ClusterError("host bootstrap product owner differs")
    if not isinstance(policy, dict) or policy.get("service") != service:
        raise _core.ClusterError("host bootstrap service-control policy differs")
    if policy.get("allowed_actions") != ["start", "stop", "restart"]:
        raise _core.ClusterError("host bootstrap sudo actions differ")
    if not isinstance(envelope, dict) or envelope.get("enabled") is not True:
        raise _core.ClusterError("static PostgreSQL service envelope is not enabled")
    systemctl = policy.get("systemctl")
    if not isinstance(systemctl, str) or not Path(systemctl).is_file():
        raise _core.ClusterError("bootstrap systemctl path is absent")
    sudo = shutil.which("sudo")
    if sudo is None:
        raise _core.ClusterError("sudo is unavailable for narrow service control")
    return sudo, systemctl


def _service_command(action: str, service: str, *, live: bool) -> list[str]:
    if action not in {"start", "stop", "restart"}:
        raise _core.ClusterError(f"unsupported privileged service action: {action}")
    if not live:
        return ["systemctl", action, service]
    sudo, systemctl = _bootstrap_service_policy(service)
    return [sudo, "-n", systemctl, action, service]


def build_plan(
    contract_path: Path,
    package_path: Path,
    resource_path: Path,
    collision_path: Path,
    physical_root: Path | None,
) -> dict[str, Any]:
    contract = _core.load_json(contract_path)
    package = _core.load_json(package_path)
    resource_observation = _core.load_json(resource_path)
    collision_observation = _core.load_json(collision_path)
    validate_contract(contract)
    grant = _core.validate_resource_observation(resource_observation, contract)
    validate_collision_observation(collision_observation, contract)
    status = _core.verify_package(package, contract, physical_root)
    if status.verified:
        _core.validate_resource_package_binding(resource_observation, package)
    package_root = package["root"]
    settings = _core.generate_settings(contract, grant)
    instance = contract["instance"]
    postgresql_bin = f"{package_root}/pgsql-{contract['package']['postgresql_major']}/bin"
    files = {
        f"{instance['config_directory']}/postgresql.conf": _core.render_postgresql_conf(
            contract, package_root, settings
        ),
        f"{instance['config_directory']}/pg_hba.conf": _core.render_hba(contract),
        f"{instance['config_directory']}/pg_ident.conf": _core.render_ident(contract),
        f"{instance['config_directory']}/bootstrap.sql": _core.render_bootstrap_sql(contract),
    }
    rendered = [
        {
            "path": path,
            "mode": 0o640,
            "sha256": _core.sha256_bytes(content.encode("utf-8")),
            "content": content,
        }
        for path, content in sorted(files.items())
    ]
    live = (
        collision_observation.get("source") == "laplace_clusterctl_live_probe"
        and collision_observation.get("root") == "/"
    )
    plan: dict[str, Any] = {
        "schema": _core.PLAN_SCHEMA,
        "contract_sha256": _core.sha256_bytes(_core.canonical_bytes(contract)),
        "package_manifest_sha256": status.manifest_sha256,
        "package_id": package["package_id"],
        "package_root": package_root,
        "postgresql_version": contract["package"]["postgresql_version"],
        "postgresql_major": contract["package"]["postgresql_major"],
        "package_verified": status.verified,
        "package_verification": status.reason,
        "resource_observation_sha256": _core.sha256_bytes(
            _core.canonical_bytes(resource_observation)
        ),
        "collision_observation_sha256": _core.sha256_bytes(
            _core.canonical_bytes(collision_observation)
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
            instance["perfcache_directory"],
            instance["log_directory"],
        ],
        "required_loaded_objects": [
            {
                "path": f"{package_root}/{relative}",
                "sha256": status.files[relative]["sha256"],
            }
            for relative in contract["package"]["required_loaded_objects"]
        ],
        "active_link": contract["package"]["active_link"],
        "runtime_link": RUNTIME_LINK,
        "commands": {
            "initdb": [
                f"{postgresql_bin}/initdb",
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
                f"{postgresql_bin}/psql",
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
            "start_candidate_service": _service_command(
                "start", instance["service"], live=live
            ),
            "stop_candidate_service": _service_command(
                "stop", instance["service"], live=live
            ),
            "restart_candidate_service": _service_command(
                "restart", instance["service"], live=live
            ),
            "probe_readiness": [
                f"{postgresql_bin}/pg_isready",
                "--host",
                instance["socket_directory"],
                "--port",
                str(instance["port"]),
                "--dbname",
                "postgres",
            ],
        },
        "activation_blocked": not status.verified,
    }
    plan["plan_sha256"] = _core.sha256_bytes(_core.canonical_bytes(plan))
    validate_plan(plan, contract)
    return plan


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
        raise _core.ClusterError("system activation requires a live collision observation")
    validate_collision_observation(inspect_collisions(contract, root), contract)
    if not plan.get("package_verified") or plan.get("activation_blocked"):
        raise _core.ClusterError("activation is blocked until package bytes verify")
    targets = [(entry, _core.prefixed(root, entry["path"])) for entry in plan["files"]]
    for entry, target in targets:
        if target.exists() or target.is_symlink():
            raise _core.ClusterError(f"activation refuses existing path: {entry['path']}")
    state_targets = [
        (directory, _core.prefixed(root, directory))
        for directory in plan["state_directories"]
    ]
    for directory, target in state_targets:
        if target.exists() or target.is_symlink():
            raise _core.ClusterError(f"activation refuses existing state directory: {directory}")
    installed: list[dict[str, Any]] = []
    created_directories: list[Path] = []
    try:
        for entry, target in targets:
            _core.atomic_write(target, entry["content"].encode("utf-8"), entry["mode"])
            installed.append({"path": entry["path"], "sha256": entry["sha256"]})
        config = _core.prefixed(root, plan["instance"]["config_directory"])
        config.chmod(0o750)
        for _directory, target in state_targets:
            target.mkdir(parents=True, exist_ok=False, mode=0o700)
            target.chmod(0o700)
            created_directories.append(target)
    except BaseException:
        for entry in reversed(installed):
            target = _core.prefixed(root, entry["path"])
            if (
                target.is_file()
                and not target.is_symlink()
                and _core.sha256_file(target) == entry["sha256"]
            ):
                target.unlink()
        for target in reversed(created_directories):
            try:
                target.rmdir()
            except OSError:
                pass
        raise
    return {
        "schema": _core.ACTIVATION_SCHEMA,
        "phase": "staged",
        "plan_sha256": plan["plan_sha256"],
        "package_id": plan["package_id"],
        "installed_files": installed,
        "state_directories": plan["state_directories"],
        "state_preserved_on_remove": True,
        "previous_active_target": None,
    }


def qualify_package_ownership(
    package: dict[str, Any], contract: dict[str, Any]
) -> dict[str, Any]:
    _require_runner()
    status = _core.verify_package(package, contract, Path("/"))
    if not status.verified:
        raise _core.ClusterError(
            f"package bytes failed ownership qualification: {status.reason}"
        )
    owner = pwd.getpwnam(RUNNER_USER)
    release = Path(package["root"])
    observed = 0
    for path in [release, *release.rglob("*")]:
        metadata = path.lstat()
        if metadata.st_uid != owner.pw_uid or metadata.st_gid != owner.pw_gid:
            raise _core.ClusterError(
                f"installed product package is not owned by {RUNNER_USER}: {path}"
            )
        observed += 1
    receipt = {
        "schema": _core.OWNERSHIP_SCHEMA,
        "package_id": package["package_id"],
        "package_root": package["root"],
        "owner_uid": owner.pw_uid,
        "owner_gid": owner.pw_gid,
        "observed_paths": observed,
        "changed_paths": 0,
        "package_manifest_sha256": status.manifest_sha256,
        "package_bytes_verified_after_change": True,
    }
    receipt["receipt_sha256"] = _core.state_observation_identity(receipt)
    return receipt


def _runtime_target(package_id: str) -> str:
    return f"../releases/{package_id}"


def _set_runtime_candidate(plan: dict[str, Any], root: Path) -> str | None:
    link = _core.prefixed(root, plan["runtime_link"])
    link.parent.mkdir(parents=True, exist_ok=True)
    previous: str | None = None
    if link.is_symlink():
        previous = os.readlink(link)
    elif link.exists():
        raise _core.ClusterError("runtime package path exists and is not a symlink")
    temporary = link.parent / f".{link.name}.{plan['package_id'][:12]}"
    temporary.unlink(missing_ok=True)
    os.symlink(_runtime_target(plan["package_id"]), temporary)
    os.replace(temporary, link)
    return previous


def _restore_runtime(plan: dict[str, Any], root: Path, previous: str | None) -> None:
    link = _core.prefixed(root, plan["runtime_link"])
    if previous is None:
        link.unlink(missing_ok=True)
        return
    temporary = link.parent / f".{link.name}.rollback"
    temporary.unlink(missing_ok=True)
    os.symlink(previous, temporary)
    os.replace(temporary, link)


def execute_activation_command(
    label: str, command: Sequence[str], timeout: int = 1800
) -> dict[str, Any]:
    actual = list(command)
    if actual and Path(actual[0]).name == "systemctl":
        if len(actual) != 3:
            raise _core.ClusterError("systemctl product command shape differs")
        actual = _service_command(actual[1], actual[2], live=True)
    completed = subprocess.run(
        actual,
        check=False,
        cwd="/",
        env=_core.activation_environment(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )
    receipt = _core.command_execution_receipt(label, actual, completed)
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        if len(detail) > 1000:
            detail = detail[-1000:]
        raise _core.ActivationCommandError(
            f"{label} failed with exit {completed.returncode}: {detail or 'no diagnostic'}",
            receipt,
        )
    return receipt


def observe_loaded_live(
    plan: dict[str, Any],
    contract: dict[str, Any],
    root: Path,
    proc_root: Path = Path("/proc"),
) -> dict[str, Any]:
    if root != Path("/"):
        raise _core.ClusterError("live loaded-object observation requires system root")
    _require_runner()
    validate_plan(plan, contract)
    state, postmaster_pid, service_receipt = _core.observe_systemd_service(
        plan["instance"]["service"]
    )
    expected_postmaster = (
        f"{plan['package_root']}/pgsql-{contract['package']['postgresql_major']}/bin/postgres"
    )
    postmaster_paths = _core.process_loaded_paths(proc_root, postmaster_pid)
    if expected_postmaster not in postmaster_paths:
        raise _core.ClusterError("service is not executing the planned package postmaster")
    instance = plan["instance"]
    application_name = f"laplace_loaded_{plan['plan_sha256'][:24]}"
    probe_sql = (
        f"SET application_name = '{application_name}'; "
        "LOAD '$libdir/laplace_pg'; LOAD '$libdir/pg_stat_statements'; SELECT pg_sleep(120);"
    )
    psql = f"{plan['package_root']}/pgsql-{contract['package']['postgresql_major']}/bin/psql"
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
        env=_core.activation_environment(),
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
                raise _core.ClusterError(
                    f"loaded-object probe exited before observation: {detail}"
                )
            completed = subprocess.run(
                [*base, "--tuples-only", "--no-align", "--quiet", "--command", lookup_sql],
                check=False,
                cwd="/",
                env=_core.activation_environment(),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=10,
            )
            if completed.returncode == 0:
                rows = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
                if len(rows) == 1 and "|" in rows[0]:
                    pid_text, identifier = rows[0].split("|", 1)
                    if pid_text.isdecimal() and identifier.isdecimal():
                        backend_pid = int(pid_text)
                        system_identifier = identifier
                        break
            time.sleep(0.1)
        if backend_pid is None or system_identifier is None:
            raise _core.ClusterError("timed out locating the loaded-object probe backend")
        paths = _core.process_loaded_paths(proc_root, backend_pid) | postmaster_paths
        return _core.compose_loaded_observation(
            plan,
            contract,
            root,
            state,
            postmaster_pid,
            backend_pid,
            system_identifier,
            paths,
            _core.sha256_bytes(probe_sql.encode("utf-8")),
            service_receipt,
        )
    finally:
        _core.terminate_probe(probe)


def _verify_service_enabled(service: str, root: Path) -> dict[str, Any]:
    if root != Path("/"):
        return {
            "label": "verify-static-service-enabled",
            "argv": ["systemctl", "is-enabled", "--quiet", service],
            "exit_code": 0,
            "stdout_sha256": _core.sha256_bytes(b""),
            "stderr_sha256": _core.sha256_bytes(b""),
        }
    _sudo, systemctl = _bootstrap_service_policy(service)
    command = [systemctl, "is-enabled", "--quiet", service]
    completed = subprocess.run(
        command,
        check=False,
        cwd="/",
        env=_core.activation_environment(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
    )
    receipt = _core.command_execution_receipt("verify-static-service-enabled", command, completed)
    if completed.returncode != 0:
        raise _core.ClusterError("static PostgreSQL service is not enabled")
    return receipt


def execute_cluster_activation(
    plan: dict[str, Any],
    contract: dict[str, Any],
    staged_receipt: dict[str, Any],
    root: Path,
    authorize_system_root: bool,
    command_receipts: list[dict[str, Any]],
    observer: Any = observe_loaded_live,
    recorder: Any | None = None,
    executor: Any = execute_activation_command,
    readiness: Any = _core.await_postgresql_ready,
) -> dict[str, Any]:
    validate_plan(plan, contract)
    require_fixture_or_root(root, authorize_system_root)
    if (
        staged_receipt.get("schema") != _core.ACTIVATION_SCHEMA
        or staged_receipt.get("phase") != "staged"
        or staged_receipt.get("plan_sha256") != plan["plan_sha256"]
    ):
        raise _core.ClusterError("complete activation requires the exact staged receipt")
    started = False
    runtime_previous = _set_runtime_candidate(plan, root)

    def run(label: str, command: Sequence[str], timeout: int = 1800) -> None:
        try:
            receipt = executor(label, command, timeout)
        except _core.ActivationCommandError as error:
            command_receipts.append(error.receipt)
            raise
        command_receipts.append(receipt)

    def wait_ready(label: str) -> None:
        try:
            receipt = readiness(label, plan["commands"]["probe_readiness"], 120)
        except _core.ActivationCommandError as error:
            command_receipts.append(error.receipt)
            raise
        command_receipts.append(receipt)

    try:
        run("initialize-cluster", plan["commands"]["initdb"], 3600)
        run("start-candidate-service", plan["commands"]["start_candidate_service"], 240)
        started = True
        wait_ready("candidate-readiness")
        run("bootstrap-product-database", plan["commands"]["bootstrap"], 1800)
        initial = observer(plan, contract, root)
        if recorder is not None:
            recorder("loaded-initial", initial)
        run("stop-candidate-for-restart-proof", plan["commands"]["stop_candidate_service"], 240)
        started = False
        run("start-candidate-after-restart", plan["commands"]["start_candidate_service"], 240)
        started = True
        wait_ready("restart-readiness")
        restarted = observer(plan, contract, root)
        if recorder is not None:
            recorder("loaded-restart", restarted)
        if initial["system_identifier"] != restarted["system_identifier"]:
            raise _core.ClusterError("cluster system identity changed across restart")
        if initial.get("postmaster_pid") == restarted.get("postmaster_pid"):
            raise _core.ClusterError("restart proof retained the original postmaster process")
        if initial["loaded_objects"] != restarted["loaded_objects"]:
            raise _core.ClusterError("loaded package identity changed across restart")
        if initial["config_files"] != restarted["config_files"]:
            raise _core.ClusterError("generated configuration changed across restart")
        command_receipts.append(_verify_service_enabled(plan["instance"]["service"], root))
        committed = _core.commit_plan(
            plan,
            contract,
            staged_receipt,
            restarted,
            root,
            authorize_system_root,
        )
        result = dict(committed)
        result["phase"] = "activated"
        result["system_identifier"] = restarted["system_identifier"]
        result["initial_loaded_observation_sha256"] = initial["observation_sha256"]
        result["restart_loaded_observation_sha256"] = restarted["observation_sha256"]
        result["restart_proven"] = True
        result["boot_enabled"] = True
        result["runtime_link"] = plan["runtime_link"]
        result["runtime_target"] = _runtime_target(plan["package_id"])
        result["command_receipts"] = list(command_receipts)
        result["activation_receipt_sha256"] = _core.state_observation_identity(result)
        return result
    except BaseException:
        if started:
            try:
                run("stop-candidate-after-failure", plan["commands"]["stop_candidate_service"], 240)
            except BaseException:
                pass
        _restore_runtime(plan, root, runtime_previous)
        raise


def _remove_empty_directory(path: Path, label: str) -> bool:
    if not (path.exists() or path.is_symlink()):
        return False
    if path.is_symlink() or not path.is_dir():
        raise _core.ClusterError(f"{label} is not one physical directory: {path}")
    try:
        path.rmdir()
    except OSError as error:
        raise _core.ClusterError(f"{label} is not empty: {path}: {error}") from error
    return True


def prove_candidate_stopped(plan: dict[str, Any], root: Path) -> dict[str, Any]:
    if root != Path("/"):
        return {
            "schema": "laplace.postgresql-candidate-stopped-proof/v1",
            "source": "laplace_typed_fixture",
            "service": plan["instance"]["service"],
            "active_state": "inactive",
            "main_pid": 0,
            "matching_processes": [],
        }
    _require_runner()
    _sudo, systemctl = _bootstrap_service_policy(plan["instance"]["service"])
    command = [
        systemctl,
        "show",
        plan["instance"]["service"],
        "--property=ActiveState",
        "--property=MainPID",
        "--no-pager",
    ]
    completed = subprocess.run(
        command,
        check=False,
        cwd="/",
        env=_core.activation_environment(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=60,
    )
    receipt = _core.command_execution_receipt(
        "prove-candidate-stopped-before-rollback", command, completed
    )
    if completed.returncode != 0:
        raise _core.ClusterError("candidate stop proof cannot inspect systemd state")
    fields = dict(
        line.split("=", 1)
        for line in completed.stdout.splitlines()
        if "=" in line
    )
    if fields.get("ActiveState") not in {"inactive", "failed"} or fields.get("MainPID") != "0":
        raise _core.ClusterError("candidate service/postmaster is not stopped before rollback")
    needles = (
        plan["instance"]["data_directory"].encode("utf-8"),
        plan["instance"]["socket_directory"].encode("utf-8"),
    )
    matches: list[int] = []
    for process in Path("/proc").iterdir():
        if not process.name.isdigit():
            continue
        try:
            command_line = (process / "cmdline").read_bytes()
        except FileNotFoundError:
            continue
        if command_line and any(needle in command_line for needle in needles):
            matches.append(int(process.name))
    if matches:
        raise _core.ClusterError(
            f"candidate PostgreSQL processes remain before rollback: {sorted(matches)}"
        )
    return {
        "schema": "laplace.postgresql-candidate-stopped-proof/v1",
        "source": "laplace_systemd_and_proc_probe",
        "service": plan["instance"]["service"],
        "active_state": fields["ActiveState"],
        "main_pid": 0,
        "matching_processes": [],
        "systemd_receipt": receipt,
    }


def rollback_uncommitted_candidate(
    plan: dict[str, Any],
    contract: dict[str, Any],
    staged: dict[str, Any],
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    validate_plan(plan, contract)
    require_fixture_or_root(root, authorize_system_root)
    if (
        staged.get("schema") != _core.ACTIVATION_SCHEMA
        or staged.get("phase") != "staged"
        or staged.get("plan_sha256") != plan["plan_sha256"]
        or staged.get("package_id") != plan["package_id"]
    ):
        raise _core.ClusterError("candidate rollback requires the exact staged receipt")
    active = _core.prefixed(root, plan["active_link"])
    if active.is_symlink() and os.readlink(active) == f"releases/{plan['package_id']}":
        raise _core.ClusterError("refusing to roll back a committed active candidate")
    stopped = prove_candidate_stopped(plan, root)
    removed_state: list[str] = []
    for directory in reversed(plan["state_directories"]):
        target = _core.prefixed(root, directory)
        if target.exists():
            if target.is_symlink() or not target.is_dir():
                raise _core.ClusterError(f"unsafe candidate state path: {directory}")
            shutil.rmtree(target)
            removed_state.append(directory)
    removed_files: list[str] = []
    for entry in reversed(staged.get("installed_files", [])):
        target = _core.prefixed(root, entry["path"])
        if (
            not target.is_file()
            or target.is_symlink()
            or _core.sha256_file(target) != entry["sha256"]
        ):
            raise _core.ClusterError(f"candidate generated file changed: {entry['path']}")
        target.unlink()
        removed_files.append(entry["path"])
    config = _core.prefixed(root, plan["instance"]["config_directory"])
    removed_config = _remove_empty_directory(config, "candidate configuration directory")
    socket = _core.prefixed(root, plan["instance"]["socket_directory"])
    removed_socket = _remove_empty_directory(socket, "candidate runtime socket directory")
    return {
        "schema": _core.ACTIVATION_SCHEMA,
        "phase": "uncommitted-candidate-rolled-back",
        "plan_sha256": plan["plan_sha256"],
        "package_id": plan["package_id"],
        "stopped_proof": stopped,
        "removed_state_directories": sorted(removed_state),
        "removed_generated_files": sorted(removed_files),
        "removed_config_directory": removed_config,
        "removed_socket_directory": removed_socket,
        "persistent_receipt_directory": contract["instance"]["receipt_directory"],
        "package_release_preserved": True,
        "active_pointer_unchanged": True,
    }


def activate_product(
    contract_path: Path,
    package_path: Path,
    resource_path: Path,
    evidence_directory: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    require_fixture_or_root(Path("/"), authorize_system_root)
    contract = _core.load_json(contract_path)
    package = _core.load_json(package_path)
    validate_contract(contract)
    expected_evidence = (
        Path(contract["instance"]["receipt_directory"])
        / "cluster-activation"
        / package.get("package_id", "invalid")
    )
    if evidence_directory != expected_evidence:
        raise _core.ClusterError("system activation evidence directory must be package-addressed")
    evidence_directory.mkdir(parents=True, exist_ok=True, mode=0o750)
    ownership = qualify_package_ownership(package, contract)
    _core.write_evidence_document(evidence_directory, "package-ownership", ownership)
    collision = inspect_collisions(contract, Path("/"))
    validate_collision_observation(collision, contract)
    collision_path = _core.write_evidence_document(
        evidence_directory, "collision-observation", collision
    )
    plan = build_plan(
        contract_path,
        package_path,
        resource_path,
        collision_path,
        Path("/"),
    )
    plan_path = _core.write_evidence_document(evidence_directory, "cluster-plan", plan)
    staged = apply_plan(plan, contract, Path("/"), authorize_system_root)
    _core.write_evidence_document(evidence_directory, "activation-staged", staged)
    command_receipts: list[dict[str, Any]] = []

    def record(stem: str, document: dict[str, Any]) -> None:
        _core.write_evidence_document(evidence_directory, stem, document)

    try:
        activated = execute_cluster_activation(
            plan,
            contract,
            staged,
            Path("/"),
            authorize_system_root,
            command_receipts,
            recorder=record,
        )
    except BaseException as error:
        rollback: dict[str, Any] | None = None
        rollback_error: BaseException | None = None
        try:
            rollback = rollback_uncommitted_candidate(
                plan, contract, staged, Path("/"), authorize_system_root
            )
            _core.write_evidence_document(evidence_directory, "candidate-rollback", rollback)
        except BaseException as cleanup_error:
            rollback_error = cleanup_error
        failure = {
            "schema": _core.ACTIVATION_SCHEMA,
            "phase": "failed",
            "package_id": package["package_id"],
            "plan_sha256": plan["plan_sha256"],
            "plan_path": str(plan_path),
            "error_type": type(error).__name__,
            "error": str(error),
            "command_receipts": command_receipts,
            "active_pointer_committed": False,
            "candidate_state_rolled_back": rollback is not None,
            "rollback": rollback,
            "rollback_error_type": (
                type(rollback_error).__name__ if rollback_error is not None else None
            ),
            "rollback_error": str(rollback_error) if rollback_error is not None else None,
            "persistent_failure_evidence_preserved": True,
        }
        failure["activation_receipt_sha256"] = _core.state_observation_identity(failure)
        _core.write_evidence_document(evidence_directory, "activation-failed", failure)
        if rollback_error is not None:
            raise _core.ClusterError(
                f"{error}; candidate rollback also failed: {rollback_error}"
            ) from error
        raise
    activated["ownership_receipt_sha256"] = ownership["receipt_sha256"]
    activated["collision_observation_sha256"] = collision["observation_sha256"]
    activated["cluster_plan_path"] = str(plan_path)
    activated.pop("activation_receipt_sha256", None)
    activated["activation_receipt_sha256"] = _core.state_observation_identity(activated)
    _core.write_evidence_document(evidence_directory, "activation-complete", activated)
    return activated


_core.validate_contract = validate_contract
_core.validate_plan = validate_plan
_core.require_fixture_or_root = require_fixture_or_root
_core.collision_target = collision_target
_core.inspect_collisions = inspect_collisions
_core.build_plan = build_plan
_core.apply_plan = apply_plan
_core.qualify_package_ownership = qualify_package_ownership
_core.execute_activation_command = execute_activation_command
_core.observe_loaded_live = observe_loaded_live
_core.execute_cluster_activation = execute_cluster_activation
_core.activate_product = activate_product

for _name, _value in vars(_core).items():
    if _name not in globals():
        globals()[_name] = _value


def main(argv: Sequence[str]) -> int:
    return _ORIGINAL_MAIN(argv)


_core.main = main


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except _core.ClusterError as error:
        print(f"clusterctl: {error}", file=sys.stderr)
        raise SystemExit(1) from error
