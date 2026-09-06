#!/usr/bin/env python3
"""Upgrade an already-active runner-owned PostgreSQL product generation in place.

Fresh cluster activation and active-generation transition are deliberately distinct.
The fresh path owns initdb and rejects any pre-existing product state. This provider
handles only a proved active generation and preserves its PGDATA/WAL identity while
changing the immutable package/configuration generation.

The transition is fail-closed:

* the current package comes from /opt/laplace/current and must have a durable completed
  cluster receipt and plan;
* the live predecessor must verify against that plan before any mutation;
* all generated configuration replaced by the successor must match predecessor receipts;
* every persistent cluster state directory remains the same physical directory;
* no initdb is executed on the upgrade path;
* the predecessor is stopped before generated configuration/runtime selection changes;
* the successor must load the exact new package, preserve PostgreSQL system_identifier,
  and pass a restart proof before /opt/laplace/current is committed;
* a pre-commit failure restores exact predecessor configuration/runtime selection and
  restarts/reverifies the predecessor before returning failure.
"""

from __future__ import annotations

import copy
import importlib.util
import os
from pathlib import Path
import stat
import sys
from typing import Any


REPOSITORY = Path(__file__).resolve().parents[2]
CLUSTERCTL_PATH = REPOSITORY / "tools/postgresql/clusterctl.py"
SPEC = importlib.util.spec_from_file_location(
    "laplace_generation_upgrade_clusterctl", CLUSTERCTL_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {CLUSTERCTL_PATH}")
clusterctl = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = clusterctl
SPEC.loader.exec_module(clusterctl)

UPGRADE_SCHEMA = "laplace.postgresql-generation-upgrade/v1"
HEX = set("0123456789abcdef")


class UpgradeError(RuntimeError):
    pass


def _activation_identity(document: dict[str, Any]) -> str:
    payload = dict(document)
    payload.pop("activation_receipt_sha256", None)
    return clusterctl.sha256_bytes(clusterctl.canonical_bytes(payload))


def _package_id_from_target(target: str) -> str:
    prefix = "releases/"
    if not target.startswith(prefix):
        raise UpgradeError("active package target is outside the release namespace")
    package_id = target[len(prefix) :]
    if len(package_id) != 64 or any(character not in HEX for character in package_id):
        raise UpgradeError("active package target does not contain a canonical package id")
    return package_id


def active_package_id(contract: dict[str, Any]) -> str:
    active = Path(contract["package"]["active_link"])
    if not active.is_symlink():
        raise UpgradeError("persistent upgrade requires an active package symlink")
    return _package_id_from_target(os.readlink(active))


def _receipt_root(contract: dict[str, Any]) -> Path:
    return Path(contract["instance"]["receipt_directory"])


def _activation_directory(contract: dict[str, Any], package_id: str) -> Path:
    return _receipt_root(contract) / "cluster-activation" / package_id


def _load_completed_activation(
    contract: dict[str, Any], package_id: str
) -> tuple[dict[str, Any], dict[str, Any]]:
    receipt_path = _activation_directory(contract, package_id) / "activation-complete.json"
    if not receipt_path.is_file() or receipt_path.is_symlink():
        raise UpgradeError(
            f"active predecessor lacks durable completed cluster receipt: {receipt_path}"
        )
    receipt = clusterctl.load_json(receipt_path)
    if (
        receipt.get("schema") != clusterctl.ACTIVATION_SCHEMA
        or receipt.get("phase") != "activated"
        or receipt.get("package_id") != package_id
        or receipt.get("restart_proven") is not True
        or receipt.get("lifecycle_provider") != clusterctl.LIFECYCLE_PROVIDER
        or receipt.get("activation_receipt_sha256") != _activation_identity(receipt)
    ):
        raise UpgradeError("active predecessor cluster receipt is incomplete")
    plan_path = Path(str(receipt.get("cluster_plan_path", "")))
    if not plan_path.is_file() or plan_path.is_symlink():
        raise UpgradeError("active predecessor cluster plan is absent")
    plan = clusterctl.load_json(plan_path)
    clusterctl.validate_plan(plan, contract)
    if plan.get("package_id") != package_id:
        raise UpgradeError("active predecessor plan package identity differs")
    return receipt, plan


def _validate_runtime_target(package_id: str) -> None:
    runtime = Path(clusterctl.RUNTIME_LINK)
    expected = f"../releases/{package_id}"
    if not runtime.is_symlink() or os.readlink(runtime) != expected:
        raise UpgradeError("runtime candidate selection does not match active predecessor")


def _verify_existing_generated_files(plan: dict[str, Any]) -> dict[str, dict[str, Any]]:
    backups: dict[str, dict[str, Any]] = {}
    for entry in plan["files"]:
        target = Path(entry["path"])
        if not target.is_file() or target.is_symlink():
            raise UpgradeError(f"predecessor generated file is absent: {target}")
        content = target.read_bytes()
        digest = clusterctl.sha256_bytes(content)
        if digest != entry["sha256"]:
            raise UpgradeError(
                f"predecessor generated file changed outside its receipt: {target}"
            )
        backups[str(target)] = {
            "content": content,
            "mode": stat.S_IMODE(target.stat().st_mode),
            "sha256": digest,
        }
    return backups


def _verify_state_directories(plan: dict[str, Any]) -> dict[str, tuple[int, int]]:
    identities: dict[str, tuple[int, int]] = {}
    for logical in plan["state_directories"]:
        target = Path(logical)
        if target.is_symlink() or not target.is_dir():
            raise UpgradeError(
                f"predecessor state directory is not one physical directory: {target}"
            )
        metadata = target.stat()
        identities[str(target)] = (metadata.st_dev, metadata.st_ino)
    return identities


def _verify_state_identities(identities: dict[str, tuple[int, int]]) -> None:
    for name, expected in identities.items():
        target = Path(name)
        if target.is_symlink() or not target.is_dir():
            raise UpgradeError(
                f"persistent state directory disappeared during upgrade: {target}"
            )
        metadata = target.stat()
        if (metadata.st_dev, metadata.st_ino) != expected:
            raise UpgradeError(
                f"persistent state directory identity changed during upgrade: {target}"
            )


def _validate_active_collisions(
    observation: dict[str, Any], contract: dict[str, Any]
) -> None:
    if observation.get("schema") != clusterctl.COLLISION_SCHEMA:
        raise UpgradeError("active collision observation schema is invalid")
    if (
        observation.get("source") != "laplace_clusterctl_live_probe"
        or observation.get("root") != "/"
    ):
        raise UpgradeError("active upgrade requires one live collision observation")
    if observation.get("target") != clusterctl.collision_target(contract):
        raise UpgradeError(
            "active collision observation target differs from the cluster contract"
        )
    if observation.get("observation_sha256") != clusterctl.collision_observation_identity(
        observation
    ):
        raise UpgradeError("active collision observation identity differs")
    errors = observation.get("inspection_errors")
    if not isinstance(errors, list) or errors:
        raise UpgradeError("active collision observation is incomplete")
    findings = observation.get("collisions")
    if not isinstance(findings, list) or not findings:
        raise UpgradeError("active generation produced no collision evidence")

    target = clusterctl.collision_target(contract)
    allowed_paths = set(target["paths"])
    socket = f"{target['socket_directory']}/.s.PGSQL.{target['port']}"
    for finding in findings:
        if not isinstance(finding, dict):
            raise UpgradeError("active collision finding is malformed")
        kind = finding.get("kind")
        value = finding.get("target")
        if kind == "path" and value in allowed_paths:
            continue
        if kind == "tcp-port" and value == target["port"]:
            continue
        if kind == "unix-socket" and value == socket:
            continue
        if (
            kind == "process"
            and isinstance(value, int)
            and value > 0
            and finding.get("owner_uid") == os.geteuid()
        ):
            continue
        raise UpgradeError(
            f"unrelated collision blocks active-generation upgrade: {kind} {value}"
        )


def _project_upgrade_collision(
    observation: dict[str, Any], predecessor_package_id: str
) -> dict[str, Any]:
    projected = copy.deepcopy(observation)
    projected["collisions"] = []
    projected["upgrade_predecessor_package_id"] = predecessor_package_id
    projected["upgrade_source_observation_sha256"] = observation[
        "observation_sha256"
    ]
    projected.pop("observation_sha256", None)
    projected["observation_sha256"] = clusterctl.collision_observation_identity(
        projected
    )
    return projected


def _replace_runtime(package_id: str) -> None:
    runtime = Path(clusterctl.RUNTIME_LINK)
    runtime.parent.mkdir(parents=True, exist_ok=True, mode=0o750)
    temporary = runtime.parent / f".{runtime.name}.{package_id[:12]}.upgrade"
    temporary.unlink(missing_ok=True)
    os.symlink(f"../releases/{package_id}", temporary)
    os.replace(temporary, runtime)


def _install_generated_files(plan: dict[str, Any]) -> None:
    for entry in plan["files"]:
        content = entry["content"].encode("utf-8")
        if clusterctl.sha256_bytes(content) != entry["sha256"]:
            raise UpgradeError(
                f"successor rendered file digest differs: {entry['path']}"
            )
        clusterctl.atomic_write(Path(entry["path"]), content, entry["mode"])


def _restore_generated_files(backups: dict[str, dict[str, Any]]) -> None:
    for name, backup in backups.items():
        clusterctl.atomic_write(Path(name), backup["content"], backup["mode"])
        if clusterctl.sha256_file(Path(name)) != backup["sha256"]:
            raise UpgradeError(
                f"rollback could not restore predecessor file: {name}"
            )


def _command(label: str, command: list[str], timeout: int) -> dict[str, Any]:
    return clusterctl.execute_activation_command(label, command, timeout)


def _ready(
    label: str, command: list[str], timeout: int = 300
) -> dict[str, Any]:
    return clusterctl.await_postgresql_ready(label, command, timeout)


def _staged_receipt(
    plan: dict[str, Any], predecessor_target: str
) -> dict[str, Any]:
    return {
        "schema": clusterctl.ACTIVATION_SCHEMA,
        "phase": "staged",
        "plan_sha256": plan["plan_sha256"],
        "package_id": plan["package_id"],
        "installed_files": [
            {"path": entry["path"], "sha256": entry["sha256"]}
            for entry in plan["files"]
        ],
        "state_directories": plan["state_directories"],
        "created_directories": [],
        "state_preserved_on_remove": True,
        "previous_active_target": predecessor_target,
        "upgrade": True,
    }


def _same_package_replay(
    contract: dict[str, Any], package_id: str
) -> dict[str, Any]:
    receipt, plan = _load_completed_activation(contract, package_id)
    _validate_runtime_target(package_id)
    loaded = clusterctl.observe_loaded_live(plan, contract, Path("/"))
    clusterctl.verify_loaded(plan, contract, loaded)
    if loaded.get("system_identifier") != receipt.get("system_identifier"):
        raise UpgradeError("same-package replay changed PostgreSQL system identity")
    return receipt


def _persistent_state_set(
    plan: dict[str, Any], contract: dict[str, Any]
) -> set[str]:
    receipt_directory = contract["instance"]["receipt_directory"]
    return {
        str(item)
        for item in plan["state_directories"]
        if str(item) != receipt_directory
    }


def upgrade_product(
    contract_path: Path,
    package_path: Path,
    resource_path: Path,
    evidence_directory: Path,
    _authorize_system_root: bool,
) -> dict[str, Any]:
    clusterctl.require_fixture_or_root(Path("/"), False)
    contract = clusterctl.load_json(contract_path)
    package = clusterctl.load_json(package_path)
    clusterctl.validate_contract(contract)
    successor_package_id = package.get("package_id")
    if not isinstance(successor_package_id, str):
        raise UpgradeError("successor package identity is absent")

    predecessor_package_id = active_package_id(contract)
    if predecessor_package_id == successor_package_id:
        return _same_package_replay(contract, successor_package_id)

    predecessor_receipt, predecessor_plan = _load_completed_activation(
        contract, predecessor_package_id
    )
    _validate_runtime_target(predecessor_package_id)
    predecessor_loaded = clusterctl.observe_loaded_live(
        predecessor_plan, contract, Path("/")
    )
    clusterctl.verify_loaded(predecessor_plan, contract, predecessor_loaded)
    if predecessor_loaded.get("system_identifier") != predecessor_receipt.get(
        "system_identifier"
    ):
        raise UpgradeError(
            "active predecessor PostgreSQL system identity differs from its receipt"
        )

    raw_collision = clusterctl.inspect_collisions(contract, Path("/"))
    _validate_active_collisions(raw_collision, contract)

    expected_evidence = _activation_directory(contract, successor_package_id)
    if evidence_directory != expected_evidence:
        raise UpgradeError(
            "successor upgrade evidence directory must be package-addressed"
        )
    evidence_directory.mkdir(parents=True, exist_ok=True, mode=0o750)
    clusterctl.write_json(
        evidence_directory / "upgrade-predecessor-collisions.json", raw_collision
    )
    projected_collision = _project_upgrade_collision(
        raw_collision, predecessor_package_id
    )
    projected_path = evidence_directory / "upgrade-plan-collision-projection.json"
    clusterctl.write_json(projected_path, projected_collision)

    successor_plan = clusterctl.build_plan(
        contract_path,
        package_path,
        resource_path,
        projected_path,
        Path("/"),
    )
    successor_plan_path = (
        evidence_directory / f"cluster-plan-{successor_plan['plan_sha256']}.json"
    )
    clusterctl.write_json(successor_plan_path, successor_plan)

    if _persistent_state_set(successor_plan, contract) != _persistent_state_set(
        predecessor_plan, contract
    ):
        raise UpgradeError(
            "successor changed the persistent state-directory contract"
        )
    if successor_plan["postgresql_major"] != predecessor_plan["postgresql_major"]:
        raise UpgradeError(
            "in-place product upgrade refuses a PostgreSQL major-version change"
        )

    successor_paths = {entry["path"] for entry in successor_plan["files"]}
    predecessor_files = [
        entry for entry in predecessor_plan["files"] if entry["path"] in successor_paths
    ]
    if {entry["path"] for entry in predecessor_files} != successor_paths:
        raise UpgradeError(
            "successor generated-file set is not covered by predecessor receipts"
        )
    backups = _verify_existing_generated_files({"files": predecessor_files})
    state_identities = _verify_state_directories(
        {"state_directories": sorted(_persistent_state_set(predecessor_plan, contract))}
    )

    predecessor_target = f"releases/{predecessor_package_id}"
    active = Path(contract["package"]["active_link"])
    command_receipts: list[dict[str, Any]] = []
    successor_started = False
    predecessor_stopped = False
    committed = False
    try:
        command_receipts.append(
            _command(
                "stop-predecessor-for-generation-upgrade",
                predecessor_plan["commands"]["stop_candidate"],
                300,
            )
        )
        clusterctl._stopped_live(predecessor_plan)
        predecessor_stopped = True

        if not active.is_symlink() or os.readlink(active) != predecessor_target:
            raise UpgradeError("active package changed after predecessor stop")
        _install_generated_files(successor_plan)
        _replace_runtime(successor_package_id)
        _verify_state_identities(state_identities)

        command_receipts.append(
            _command(
                "start-successor-generation",
                successor_plan["commands"]["start_candidate"],
                300,
            )
        )
        successor_started = True
        command_receipts.append(
            _ready(
                "successor-generation-readiness",
                successor_plan["commands"]["probe_readiness"],
            )
        )
        successor_loaded_initial = clusterctl.observe_loaded_live(
            successor_plan, contract, Path("/")
        )
        clusterctl.verify_loaded(
            successor_plan, contract, successor_loaded_initial
        )
        if successor_loaded_initial["system_identifier"] != predecessor_loaded[
            "system_identifier"
        ]:
            raise UpgradeError("successor changed PostgreSQL system identity")
        clusterctl.write_json(
            evidence_directory / "loaded-upgrade-initial.json",
            successor_loaded_initial,
        )

        command_receipts.append(
            _command(
                "stop-successor-for-restart-proof",
                successor_plan["commands"]["stop_candidate"],
                300,
            )
        )
        successor_started = False
        command_receipts.append(
            _command(
                "start-successor-after-restart",
                successor_plan["commands"]["start_candidate"],
                300,
            )
        )
        successor_started = True
        command_receipts.append(
            _ready(
                "successor-restart-readiness",
                successor_plan["commands"]["probe_readiness"],
            )
        )
        successor_loaded_restart = clusterctl.observe_loaded_live(
            successor_plan, contract, Path("/")
        )
        clusterctl.verify_loaded(
            successor_plan, contract, successor_loaded_restart
        )
        if successor_loaded_restart["system_identifier"] != predecessor_loaded[
            "system_identifier"
        ]:
            raise UpgradeError("restart proof changed PostgreSQL system identity")
        if successor_loaded_restart.get("postmaster_pid") == successor_loaded_initial.get(
            "postmaster_pid"
        ):
            raise UpgradeError(
                "successor restart retained the original postmaster"
            )
        clusterctl.write_json(
            evidence_directory / "loaded-upgrade-restart.json",
            successor_loaded_restart,
        )
        _verify_state_identities(state_identities)
        if not active.is_symlink() or os.readlink(active) != predecessor_target:
            raise UpgradeError("active package changed before successor commit")

        staged = _staged_receipt(successor_plan, predecessor_target)
        result = clusterctl.commit_plan(
            successor_plan,
            contract,
            staged,
            successor_loaded_restart,
            Path("/"),
            False,
        )
        committed = True
        result.update(
            {
                "phase": "activated",
                "restart_proven": True,
                "boot_enabled": False,
                "service_integration_required": False,
                "lifecycle_provider": clusterctl.LIFECYCLE_PROVIDER,
                "runtime_target": f"../releases/{successor_package_id}",
                "cluster_plan_path": str(successor_plan_path),
                "system_identifier": successor_loaded_restart[
                    "system_identifier"
                ],
                "loaded_initial_observation_sha256": successor_loaded_initial[
                    "observation_sha256"
                ],
                "loaded_restart_observation_sha256": successor_loaded_restart[
                    "observation_sha256"
                ],
                "command_receipts": command_receipts,
                "upgrade_schema": UPGRADE_SCHEMA,
                "predecessor_package_id": predecessor_package_id,
                "predecessor_activation_receipt_sha256": predecessor_receipt[
                    "activation_receipt_sha256"
                ],
                "persistent_state_identity_preserved": True,
                "initdb_executed": False,
            }
        )
        result["activation_receipt_sha256"] = _activation_identity(result)
        clusterctl.write_json(
            evidence_directory / "activation-complete.json", result
        )
        return result
    except BaseException as error:
        rollback_error: BaseException | None = None
        if not committed:
            try:
                if successor_started:
                    try:
                        _command(
                            "stop-successor-after-upgrade-failure",
                            successor_plan["commands"]["stop_candidate"],
                            300,
                        )
                    finally:
                        successor_started = False
                _restore_generated_files(backups)
                _replace_runtime(predecessor_package_id)
                _verify_state_identities(state_identities)
                if predecessor_stopped:
                    _command(
                        "restart-predecessor-after-upgrade-failure",
                        predecessor_plan["commands"]["start_candidate"],
                        300,
                    )
                    _ready(
                        "predecessor-rollback-readiness",
                        predecessor_plan["commands"]["probe_readiness"],
                    )
                rollback_loaded = clusterctl.observe_loaded_live(
                    predecessor_plan, contract, Path("/")
                )
                clusterctl.verify_loaded(
                    predecessor_plan, contract, rollback_loaded
                )
                if rollback_loaded["system_identifier"] != predecessor_loaded[
                    "system_identifier"
                ]:
                    raise UpgradeError(
                        "rollback changed PostgreSQL system identity"
                    )
                if (
                    not active.is_symlink()
                    or os.readlink(active) != predecessor_target
                ):
                    raise UpgradeError(
                        "rollback changed the committed predecessor pointer"
                    )
            except BaseException as caught:
                rollback_error = caught

        failure = {
            "schema": UPGRADE_SCHEMA,
            "phase": "failed",
            "predecessor_package_id": predecessor_package_id,
            "successor_package_id": successor_package_id,
            "error_type": type(error).__name__,
            "error": str(error),
            "committed": committed,
            "rollback_proven": rollback_error is None and not committed,
            "rollback_error": (
                None if rollback_error is None else str(rollback_error)
            ),
            "initdb_executed": False,
            "persistent_state_identity_preserved": rollback_error is None,
        }
        failure["receipt_sha256"] = clusterctl.sha256_bytes(
            clusterctl.canonical_bytes(failure)
        )
        clusterctl.write_json(
            evidence_directory / "activation-upgrade-failed.json", failure
        )
        if rollback_error is not None:
            raise UpgradeError(
                "generation upgrade failed and predecessor rollback could not be "
                f"proven: {rollback_error}"
            ) from error
        raise
