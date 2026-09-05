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

for _name in dir(_core):
    if _name.startswith("__"):
        continue
    globals()[_name] = getattr(_core, _name)

_ORIGINAL_VALIDATE_CONTRACT = _core.validate_contract
_ORIGINAL_VALIDATE_PLAN = _core.validate_plan
_ORIGINAL_BUILD_PLAN = _core.build_plan
_ORIGINAL_COLLISION_TARGET = _core.collision_target
_ORIGINAL_INSTALL_PACKAGE = _core.install_package
_ORIGINAL_REQUIRE_FIXTURE_OR_ROOT = _core.require_fixture_or_root
_ORIGINAL_APPLY_PLAN = _core.apply_plan
_ORIGINAL_COMMIT_PLAN = _core.commit_plan
_ORIGINAL_REMOVE_ACTIVATION = _core.remove_activation
_ORIGINAL_EXECUTE_CLUSTER_ACTIVATION = _core.execute_cluster_activation


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
            f"system product lifecycle requires {RUNNER_USER}, not {actual}"
        )


def require_fixture_or_root(root: Path, authorize_system_root: bool) -> None:
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
        contract = load_json(Path(plan["contract_path"])) if "contract_path" in plan else None
    if contract is None:
        raise _core.ClusterError("runner plan validation requires its cluster contract")
    validate_contract(contract)
    service_path = f"/etc/systemd/system/{plan['instance']['service']}"
    if plan.get("collision_observation", {}).get("source") != "laplace_typed_fixture":
        if any(entry.get("path") == service_path for entry in plan.get("files", [])):
            raise _core.ClusterError("live activation plan cannot rewrite the static bootstrap service")
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


def build_plan(
    contract_path: Path,
    package_path: Path,
    resource_path: Path,
    collision_path: Path,
    package_physical_root: Path | None = None,
) -> dict[str, Any]:
    contract = load_json(contract_path)
    validate_contract(contract)
    projected_contract = _validation_contract(contract)
    temporary_contract = None
    try:
        if projected_contract != contract:
            import tempfile

            descriptor, name = tempfile.mkstemp(prefix="laplace-cluster-contract-")
            os.close(descriptor)
            temporary_contract = Path(name)
            temporary_contract.write_bytes(canonical_bytes(projected_contract))
            source_contract_path = temporary_contract
        else:
            source_contract_path = contract_path
        plan = _ORIGINAL_BUILD_PLAN(
            source_contract_path,
            package_path,
            resource_path,
            collision_path,
            package_physical_root,
        )
    finally:
        if temporary_contract is not None:
            temporary_contract.unlink(missing_ok=True)

    plan["contract_sha256"] = sha256_bytes(canonical_bytes(contract))
    plan["runtime_link"] = RUNTIME_LINK
    service_path = f"/etc/systemd/system/{contract['instance']['service']}"
    fixture = plan.get("collision_observation", {}).get("source") == "laplace_typed_fixture"
    if fixture:
        if not any(entry.get("path") == service_path for entry in plan["files"]):
            service = _core.render_service(
                projected_contract,
                plan["package_root"],
                plan["resource_grant"],
            )
            plan["files"].append(
                {
                    "path": service_path,
                    "mode": 0o644,
                    "sha256": sha256_bytes(service.encode("utf-8")),
                    "content": service,
                }
            )
            plan["files"] = sorted(plan["files"], key=lambda item: item["path"])
    else:
        plan["files"] = [
            entry for entry in plan["files"] if entry.get("path") != service_path
        ]
    ident_path = f"{contract['instance']['config_directory']}/pg_ident.conf"
    _replace_rendered(plan, ident_path, render_ident(contract), 0o640)
    plan.pop("plan_sha256", None)
    plan["plan_sha256"] = sha256_bytes(canonical_bytes(plan))
    validate_plan(plan, contract)
    return plan


def render_ident(contract: dict[str, Any]) -> str:
    security = contract["security"]
    instance = contract["instance"]
    return (
        f"{security['admin_map']} {RUNNER_USER} {instance['admin_role']}\n"
        f"{security['app_map']} {RUNNER_USER} {instance['app_role']}\n"
    )


def _bootstrap_command_for_runner(plan: dict[str, Any]) -> list[str]:
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


def _initdb_command_for_runner(plan: dict[str, Any]) -> list[str]:
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


def _fixture_service_command(plan: dict[str, Any], action: str) -> list[str]:
    return ["systemctl", action, plan["instance"]["service"]]


def _live_service_command(plan: dict[str, Any], action: str) -> list[str]:
    if action not in {"start", "stop", "restart"}:
        raise _core.ClusterError(f"unsupported recurring privileged service action: {action}")
    bootstrap = load_json(BOOTSTRAP_RECEIPT)
    policy = bootstrap.get("sudo_policy")
    envelope = bootstrap.get("service_envelope")
    if not isinstance(policy, dict) or not isinstance(envelope, dict):
        raise _core.ClusterError("host bootstrap receipt omits service authority")
    service = plan["instance"]["service"]
    if (
        bootstrap.get("schema") != "laplace.host-bootstrap/v1"
        or bootstrap.get("phase") != "host-prerequisites-ready"
        or policy.get("service") != service
        or policy.get("allowed_actions") != ["start", "stop", "restart"]
        or envelope.get("enabled") is not True
        or envelope.get("started_by_bootstrap") is not False
    ):
        raise _core.ClusterError("host bootstrap service authority differs")
    systemctl = policy.get("systemctl")
    if not isinstance(systemctl, str) or not systemctl.startswith("/"):
        raise _core.ClusterError("host bootstrap systemctl path is invalid")
    sudo = shutil.which("sudo")
    if sudo is None:
        raise _core.ClusterError("sudo is unavailable for bounded service control")
    return [sudo, "-n", systemctl, action, service]


def _service_command(plan: dict[str, Any], action: str, root: Path) -> list[str]:
    return (
        _live_service_command(plan, action)
        if root == Path("/")
        else _fixture_service_command(plan, action)
    )


def install_package(
    manifest: dict[str, Any],
    contract: dict[str, Any],
    package_physical_root: Path,
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    require_fixture_or_root(root, authorize_system_root)
    receipt = _ORIGINAL_INSTALL_PACKAGE(
        manifest,
        _validation_contract(contract),
        package_physical_root,
        root,
        False,
    )
    if root == Path("/"):
        installed = prefixed(root, manifest["root"])
        expected = _runner_identity()
        for candidate in [installed, *installed.rglob("*")]:
            if candidate.is_symlink():
                continue
            metadata = candidate.stat()
            if metadata.st_uid != expected.pw_uid or metadata.st_gid != expected.pw_gid:
                os.chown(candidate, expected.pw_uid, expected.pw_gid)
    return receipt


def apply_plan(
    plan: dict[str, Any],
    contract: dict[str, Any],
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    require_fixture_or_root(root, authorize_system_root)
    validate_plan(plan, contract)
    receipt = _ORIGINAL_APPLY_PLAN(
        plan,
        _validation_contract(contract),
        root,
        False,
    )
    if root == Path("/"):
        runtime = Path(RUNTIME_LINK)
        runtime.parent.mkdir(parents=True, exist_ok=True, mode=0o750)
        target = f"../releases/{plan['package_id']}"
        temporary = runtime.parent / f".{runtime.name}.{plan['package_id'][:16]}"
        temporary.unlink(missing_ok=True)
        os.symlink(target, temporary)
        os.replace(temporary, runtime)
    return receipt


def execute_activation_command(
    label: str, command: Sequence[str], timeout: int
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
        raise _core.ClusterError(f"{label} failed: {detail}")
    return receipt


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
    instance = plan["instance"]

    if root != Path("/"):
        return _ORIGINAL_EXECUTE_CLUSTER_ACTIVATION(
            plan,
            _validation_contract(contract),
            staged,
            root,
            False,
            command_receipts,
            observer=observer,
            recorder=recorder,
            executor=executor,
            readiness=readiness,
        )

    commands = [
        ("initialize-cluster", _initdb_command_for_runner(plan), 1800),
        ("start-candidate-service", _service_command(plan, "start", root), 300),
    ]
    for label, command, timeout in commands:
        receipt = execute(label, command, timeout)
        command_receipts.append(receipt)
    readiness_receipt = readiness_runner(
        "candidate-readiness", plan["commands"]["probe_readiness"], 300
    )
    command_receipts.append(readiness_receipt)
    bootstrap_receipt = execute(
        "bootstrap-product-database", _bootstrap_command_for_runner(plan), 1800
    )
    command_receipts.append(bootstrap_receipt)

    loaded_initial = observe(plan, contract, root)
    verify_loaded(plan, contract, loaded_initial)
    record("loaded-initial", loaded_initial)

    stop_receipt = execute(
        "stop-candidate-for-restart-proof", _service_command(plan, "stop", root), 300
    )
    command_receipts.append(stop_receipt)
    start_receipt = execute(
        "start-candidate-after-restart", _service_command(plan, "start", root), 300
    )
    command_receipts.append(start_receipt)
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

    committed = commit_plan(
        plan,
        contract,
        staged,
        loaded_restart,
        root,
        False,
    )
    committed.update(
        {
            "phase": "activated",
            "restart_proven": True,
            "boot_enabled": True,
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


def commit_plan(
    plan: dict[str, Any],
    contract: dict[str, Any],
    staged: dict[str, Any],
    loaded: dict[str, Any],
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    require_fixture_or_root(root, authorize_system_root)
    committed = _ORIGINAL_COMMIT_PLAN(
        plan,
        _validation_contract(contract),
        staged,
        loaded,
        root,
        False,
    )
    if root == Path("/"):
        runtime = Path(RUNTIME_LINK)
        if not runtime.is_symlink() or os.readlink(runtime) != f"../releases/{plan['package_id']}":
            raise _core.ClusterError("runtime selection changed before commit")
    return committed


def remove_activation(
    plan: dict[str, Any],
    contract: dict[str, Any],
    receipt: dict[str, Any],
    stopped: dict[str, Any],
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    require_fixture_or_root(root, authorize_system_root)
    result = _ORIGINAL_REMOVE_ACTIVATION(
        plan,
        _validation_contract(contract),
        receipt,
        stopped,
        root,
        False,
    )
    if root == Path("/"):
        runtime = Path(RUNTIME_LINK)
        if runtime.is_symlink() and os.readlink(runtime) == f"../releases/{plan['package_id']}":
            runtime.unlink()
    return result


def _stopped_live(plan: dict[str, Any]) -> dict[str, Any]:
    instance = plan["instance"]
    service = instance["service"]
    systemctl = shutil.which("systemctl") or "/usr/bin/systemctl"
    completed = subprocess.run(
        [systemctl, "show", service, "--property=ActiveState,MainPID", "--value"],
        check=False,
        cwd="/",
        env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C", "LC_ALL": "C"},
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
    )
    values = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    state = values[0] if values else "unknown"
    pid = int(values[1]) if len(values) > 1 and values[1].isdigit() else 0
    if completed.returncode != 0 or state not in {"inactive", "failed"} or pid != 0:
        raise _core.ClusterError("candidate rollback requires an inactive service with no postmaster")
    return {
        "schema": STOPPED_SCHEMA,
        "source": "laplace_live_systemd_state",
        "service": service,
        "data_directory": instance["data_directory"],
        "service_state": state,
        "postmaster_pid": None,
        "observation_sha256": "",
    }


def _rollback_uncommitted_candidate(
    plan: dict[str, Any], contract: dict[str, Any], staged: dict[str, Any]
) -> None:
    try:
        execute_activation_command(
            "stop-candidate-after-failure", _service_command(plan, "stop", Path("/")), 300
        )
    except Exception:
        pass
    stopped = _stopped_live(plan)
    stopped["observation_sha256"] = state_observation_identity(stopped)
    runtime = Path(RUNTIME_LINK)
    if runtime.is_symlink() and os.readlink(runtime) == f"../releases/{plan['package_id']}":
        runtime.unlink()
    for entry in reversed(staged.get("installed_files", [])):
        target = Path(entry["path"])
        if target.exists() and not target.is_symlink():
            target.unlink()
    for directory in sorted(
        (Path(value) for value in staged.get("created_directories", [])),
        key=lambda path: len(path.parts),
        reverse=True,
    ):
        try:
            directory.rmdir()
        except OSError:
            pass


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
    collision = inspect_collisions(contract, Path("/"))
    collision_path = evidence_directory / "collision-observation.json"
    evidence_directory.mkdir(parents=True, exist_ok=True, mode=0o750)
    write_json(collision_path, collision)
    validate_collision_observation(collision, contract)
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
    staged_path = evidence_directory / "activation-staged.json"
    write_json(staged_path, staged)
    try:
        result = execute_cluster_activation(
            plan,
            contract,
            staged,
            Path("/"),
            False,
            [],
        )
    except BaseException as error:
        try:
            _rollback_uncommitted_candidate(plan, contract, staged)
        except BaseException as rollback_error:
            raise _core.ClusterError(
                f"activation failed and candidate rollback could not prove safety: {rollback_error}"
            ) from error
        raise
    result["cluster_plan_path"] = str(plan_path)
    result["activation_receipt_sha256"] = state_observation_identity(result)
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
    return _core.main(sys.argv[1:] if argv is None else list(argv))


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"clusterctl: {error}", file=sys.stderr)
        raise SystemExit(1) from error
