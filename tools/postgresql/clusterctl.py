#!/usr/bin/env python3
"""PostgreSQL product-cluster entrypoint with persistent activation transactions.

The implementation core lives in ``cluster_core.py``. This entrypoint preserves the
public clusterctl API while owning lifecycle rules that span host-owned receipt state
and fresh candidate PostgreSQL state:

* the persistent receipt namespace is never treated as a fresh-cluster collision;
* a plan never claims the host-owned receipt directory as candidate database state;
* a failed, uncommitted candidate is rolled back to a retryable boundary only after
  proving the candidate service/postmaster is stopped;
* a committed/active database is never touched by candidate rollback.
"""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Any, Sequence


_CORE_PATH = Path(__file__).with_name("cluster_core.py")
_SPEC = importlib.util.spec_from_file_location("laplace_postgresql_cluster_core", _CORE_PATH)
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("cannot load PostgreSQL cluster core")
_core = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = _core
_SPEC.loader.exec_module(_core)

# Preserve the controller's existing module API for tests and sibling controllers.
for _name, _value in vars(_core).items():
    if _name not in {"collision_target", "build_plan", "activate_product", "main"}:
        globals()[_name] = _value

_ORIGINAL_COLLISION_TARGET = _core.collision_target
_ORIGINAL_BUILD_PLAN = _core.build_plan
_ORIGINAL_MAIN = _core.main


def collision_target(contract: dict[str, Any]) -> dict[str, Any]:
    """Return fresh-cluster collision targets, excluding host-owned receipts."""

    target = _ORIGINAL_COLLISION_TARGET(contract)
    receipt_directory = contract["instance"]["receipt_directory"]
    target = dict(target)
    target["paths"] = [
        path for path in target["paths"] if path != receipt_directory
    ]
    return target


# Core collision validation/inspection must resolve the corrected ownership law too.
_core.collision_target = collision_target


def build_plan(
    contract_path: Path,
    package_path: Path,
    resource_path: Path,
    collision_path: Path,
    physical_root: Path | None,
) -> dict[str, Any]:
    """Build the canonical plan without claiming the persistent receipt root."""

    plan = _ORIGINAL_BUILD_PLAN(
        contract_path,
        package_path,
        resource_path,
        collision_path,
        physical_root,
    )
    contract = _core.load_json(contract_path)
    receipt_directory = contract["instance"]["receipt_directory"]
    state_directories = plan.get("state_directories")
    if not isinstance(state_directories, list):
        raise _core.ClusterError("cluster plan state directories are absent")
    plan["state_directories"] = [
        path for path in state_directories if path != receipt_directory
    ]
    if len(plan["state_directories"]) != len(state_directories) - 1:
        raise _core.ClusterError(
            "cluster plan did not contain exactly one host-owned receipt directory"
        )
    plan.pop("plan_sha256", None)
    plan["plan_sha256"] = _core.sha256_bytes(_core.canonical_bytes(plan))
    _core.validate_plan(plan, contract)
    return plan


_core.build_plan = build_plan


def _remove_empty_directory(path: Path, label: str) -> bool:
    if not (path.exists() or path.is_symlink()):
        return False
    if path.is_symlink() or not path.is_dir():
        raise _core.ClusterError(f"{label} is not one physical directory: {path}")
    try:
        path.rmdir()
    except OSError as error:
        raise _core.ClusterError(
            f"{label} is not empty after candidate stop: {path}: {error}"
        ) from error
    return True


def prove_candidate_stopped(plan: dict[str, Any], root: Path) -> dict[str, Any]:
    """Prove no system candidate process can still own state being rolled back."""

    if root != Path("/"):
        return {
            "schema": "laplace.postgresql-candidate-stopped-proof/v1",
            "source": "laplace_typed_fixture",
            "service": plan["instance"]["service"],
            "active_state": "inactive",
            "main_pid": 0,
            "matching_processes": [],
        }

    service = plan["instance"]["service"]
    command = [
        "/usr/bin/systemctl",
        "show",
        service,
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
        detail = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic"
        raise _core.ClusterError(
            f"candidate stop proof could not inspect systemd state: {detail}"
        )
    fields: dict[str, str] = {}
    for line in completed.stdout.splitlines():
        if "=" not in line:
            continue
        name, value = line.split("=", 1)
        fields[name] = value
    active_state = fields.get("ActiveState")
    main_pid_text = fields.get("MainPID")
    if active_state not in {"inactive", "failed"}:
        raise _core.ClusterError(
            f"candidate service is not stopped before rollback: {active_state!r}"
        )
    if main_pid_text is None or not main_pid_text.isdecimal() or int(main_pid_text) != 0:
        raise _core.ClusterError(
            f"candidate service retains a postmaster before rollback: {main_pid_text!r}"
        )

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
        except OSError as error:
            raise _core.ClusterError(
                f"candidate stop proof cannot inspect process {process.name}: {error}"
            ) from error
        if command_line and any(needle in command_line for needle in needles):
            matches.append(int(process.name))
    if matches:
        raise _core.ClusterError(
            f"candidate PostgreSQL processes remain before rollback: {sorted(matches)}"
        )

    return {
        "schema": "laplace.postgresql-candidate-stopped-proof/v1",
        "source": "laplace_systemd_and_proc_probe",
        "service": service,
        "active_state": active_state,
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
    """Remove only state proven to belong to an uncommitted fresh candidate.

    ``apply_plan`` can create these paths only after a collision-free observation.
    Therefore the staged receipt proves provenance for the candidate state. The
    content-addressed package release and persistent receipt/evidence namespace are
    deliberately retained. Any active pointer to this candidate blocks rollback.
    """

    _core.validate_plan(plan, contract)
    _core.require_fixture_or_root(root, authorize_system_root)
    if (
        staged.get("schema") != _core.ACTIVATION_SCHEMA
        or staged.get("phase") != "staged"
        or staged.get("plan_sha256") != plan["plan_sha256"]
        or staged.get("package_id") != plan["package_id"]
    ):
        raise _core.ClusterError("candidate rollback requires the exact staged receipt")

    active = _core.prefixed(root, plan["active_link"])
    candidate_target = f"releases/{plan['package_id']}"
    if active.is_symlink() and os.readlink(active) == candidate_target:
        raise _core.ClusterError("refusing to roll back a committed active candidate")

    installed = staged.get("installed_files")
    if not isinstance(installed, list):
        raise _core.ClusterError("staged receipt omits generated files")
    for entry in installed:
        if not isinstance(entry, dict) or set(entry) != {"path", "sha256"}:
            raise _core.ClusterError("staged generated-file receipt differs")
        target = _core.prefixed(root, entry["path"])
        if not target.is_file() or target.is_symlink():
            raise _core.ClusterError(
                f"candidate generated file is absent or unsafe: {entry['path']}"
            )
        if _core.sha256_file(target) != entry["sha256"]:
            raise _core.ClusterError(
                f"candidate generated file changed before rollback: {entry['path']}"
            )

    expected_state = list(plan["state_directories"])
    if staged.get("state_directories") != expected_state:
        raise _core.ClusterError("staged candidate state directories differ from the plan")

    stopped = prove_candidate_stopped(plan, root)

    removed_state: list[str] = []
    for directory in reversed(expected_state):
        target = _core.prefixed(root, directory)
        if not (target.exists() or target.is_symlink()):
            continue
        if target.is_symlink() or not target.is_dir():
            raise _core.ClusterError(
                f"candidate state path is not one physical directory: {directory}"
            )
        shutil.rmtree(target)
        removed_state.append(directory)

    removed_files: list[str] = []
    for entry in reversed(installed):
        target = _core.prefixed(root, entry["path"])
        target.unlink()
        removed_files.append(entry["path"])

    # apply_plan creates the configuration parent implicitly for rendered files.
    config_directory = _core.prefixed(root, plan["instance"]["config_directory"])
    removed_config_directory = _remove_empty_directory(
        config_directory, "candidate configuration directory"
    )

    # systemd owns the runtime socket directory while the candidate is running.
    socket_directory = _core.prefixed(root, plan["instance"]["socket_directory"])
    removed_socket_directory = _remove_empty_directory(
        socket_directory, "candidate runtime socket directory"
    )

    daemon_reload = None
    if root == Path("/"):
        command = ["/usr/bin/systemctl", "daemon-reload"]
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
        daemon_reload = _core.command_execution_receipt(
            "reload-service-manager-after-candidate-rollback", command, completed
        )
        if completed.returncode != 0:
            detail = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic"
            raise _core.ClusterError(
                f"candidate rollback could not reload systemd: {detail}"
            )

    return {
        "schema": _core.ACTIVATION_SCHEMA,
        "phase": "uncommitted-candidate-rolled-back",
        "plan_sha256": plan["plan_sha256"],
        "package_id": plan["package_id"],
        "stopped_proof": stopped,
        "removed_state_directories": sorted(removed_state),
        "removed_generated_files": sorted(removed_files),
        "removed_config_directory": removed_config_directory,
        "removed_socket_directory": removed_socket_directory,
        "persistent_receipt_directory": contract["instance"]["receipt_directory"],
        "package_release_preserved": True,
        "active_pointer_unchanged": True,
        "daemon_reload": daemon_reload,
    }


def activate_product(
    contract_path: Path,
    package_path: Path,
    resource_path: Path,
    evidence_directory: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    """Perform fresh activation transactionally and leave retries unwedged."""

    _core.require_fixture_or_root(Path("/"), authorize_system_root)
    contract = _core.load_json(contract_path)
    package = _core.load_json(package_path)
    _core.validate_contract(contract)
    expected_evidence = (
        Path(contract["instance"]["receipt_directory"])
        / "cluster-activation"
        / package.get("package_id", "invalid")
    )
    if evidence_directory != expected_evidence:
        raise _core.ClusterError(
            "system activation evidence directory must be package-addressed"
        )
    evidence_directory.mkdir(parents=True, exist_ok=True, mode=0o750)

    ownership = _core.qualify_package_ownership(package, contract)
    _core.write_evidence_document(evidence_directory, "package-ownership", ownership)

    collision = _core.inspect_collisions(contract, Path("/"))
    _core.validate_collision_observation(collision, contract)
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
    plan_path = _core.write_evidence_document(
        evidence_directory, "cluster-plan", plan
    )
    staged = _core.apply_plan(plan, contract, Path("/"), authorize_system_root)
    _core.write_evidence_document(evidence_directory, "activation-staged", staged)
    command_receipts: list[dict[str, Any]] = []

    def record(stem: str, document: dict[str, Any]) -> None:
        _core.write_evidence_document(evidence_directory, stem, document)

    try:
        activated = _core.execute_cluster_activation(
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
                plan,
                contract,
                staged,
                Path("/"),
                authorize_system_root,
            )
            _core.write_evidence_document(
                evidence_directory, "candidate-rollback", rollback
            )
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
                f"{error}; uncommitted candidate rollback also failed: {rollback_error}"
            ) from error
        raise

    activated["ownership_receipt_sha256"] = ownership["receipt_sha256"]
    activated["collision_observation_sha256"] = collision["observation_sha256"]
    activated["cluster_plan_path"] = str(plan_path)
    activated.pop("activation_receipt_sha256", None)
    activated["activation_receipt_sha256"] = _core.state_observation_identity(activated)
    _core.write_evidence_document(evidence_directory, "activation-complete", activated)
    return activated


_core.activate_product = activate_product

# Re-export the corrected entrypoints after patching the core module namespace.
globals().update(
    {
        "collision_target": collision_target,
        "build_plan": build_plan,
        "prove_candidate_stopped": prove_candidate_stopped,
        "rollback_uncommitted_candidate": rollback_uncommitted_candidate,
        "activate_product": activate_product,
    }
)


def main(argv: Sequence[str]) -> int:
    return _ORIGINAL_MAIN(argv)


_core.main = main


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except _core.ClusterError as error:
        print(f"clusterctl: {error}", file=sys.stderr)
        raise SystemExit(1) from error
