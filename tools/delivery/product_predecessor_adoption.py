#!/usr/bin/env python3
"""Re-prove an older active PostgreSQL generation before current in-place upgrade.

A durable active package can predate the current runner-owned activation receipt shape.
That historical receipt is evidence, but missing current proof fields must never be filled
by assumption. This provider preserves the historical receipt byte-for-byte, verifies its
exact plan and live package/configuration state, performs a real pg_ctl restart proof, and
only then publishes a current canonical activation-complete receipt for the same package.

No active state is deleted and no receipt-validation requirement is relaxed. If re-proof
fails after lifecycle mutation begins, the provider must re-establish and reverify the
predecessor before returning failure.
"""

from __future__ import annotations

import importlib.util
import os
import subprocess
from pathlib import Path
import sys
from typing import Any


REPOSITORY = Path(__file__).resolve().parents[2]
CLUSTERCTL_PATH = REPOSITORY / "tools/postgresql/clusterctl.py"
SPEC = importlib.util.spec_from_file_location(
    "laplace_predecessor_adoption_clusterctl", CLUSTERCTL_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {CLUSTERCTL_PATH}")
clusterctl = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = clusterctl
SPEC.loader.exec_module(clusterctl)

ADOPTION_SCHEMA = "laplace.postgresql-predecessor-adoption/v1"
HEX = set("0123456789abcdef")


class AdoptionError(RuntimeError):
    pass


def _activation_identity(document: dict[str, Any]) -> str:
    payload = dict(document)
    payload.pop("activation_receipt_sha256", None)
    return clusterctl.sha256_bytes(clusterctl.canonical_bytes(payload))


def _package_id_from_target(target: str) -> str:
    prefix = "releases/"
    if not target.startswith(prefix):
        raise AdoptionError("active predecessor is outside the release namespace")
    package_id = target[len(prefix) :]
    if len(package_id) != 64 or any(character not in HEX for character in package_id):
        raise AdoptionError("active predecessor target does not contain a canonical package id")
    return package_id


def _active_package_id(contract: dict[str, Any]) -> str:
    active = Path(contract["package"]["active_link"])
    if not active.is_symlink():
        raise AdoptionError("predecessor adoption requires an active package symlink")
    return _package_id_from_target(os.readlink(active))


def _activation_directory(contract: dict[str, Any], package_id: str) -> Path:
    return (
        Path(contract["instance"]["receipt_directory"])
        / "cluster-activation"
        / package_id
    )


def _load_plan(
    receipt: dict[str, Any], contract: dict[str, Any], package_id: str
) -> tuple[Path, dict[str, Any]]:
    plan_path = Path(str(receipt.get("cluster_plan_path", "")))
    if not plan_path.is_file() or plan_path.is_symlink():
        raise AdoptionError("active predecessor cluster plan is absent")
    plan = clusterctl.load_json(plan_path)
    clusterctl.validate_plan(plan, contract)
    if plan.get("package_id") != package_id:
        raise AdoptionError("active predecessor plan package identity differs")
    return plan_path, plan


def _receipt_is_current(receipt: dict[str, Any], package_id: str) -> bool:
    return (
        receipt.get("schema") == clusterctl.ACTIVATION_SCHEMA
        and receipt.get("phase") == "activated"
        and receipt.get("package_id") == package_id
        and receipt.get("restart_proven") is True
        and receipt.get("lifecycle_provider") == clusterctl.LIFECYCLE_PROVIDER
        and receipt.get("activation_receipt_sha256") == _activation_identity(receipt)
    )


def _require_selection(contract: dict[str, Any], package_id: str) -> None:
    active = Path(contract["package"]["active_link"])
    expected_active = f"releases/{package_id}"
    if not active.is_symlink() or os.readlink(active) != expected_active:
        raise AdoptionError("active predecessor selection changed during re-proof")
    runtime = Path(clusterctl.RUNTIME_LINK)
    expected_runtime = f"../releases/{package_id}"
    if not runtime.is_symlink() or os.readlink(runtime) != expected_runtime:
        raise AdoptionError("runtime selection does not match the active predecessor")


def _same_loaded_state(left: dict[str, Any], right: dict[str, Any]) -> bool:
    return (
        left.get("system_identifier") == right.get("system_identifier")
        and left.get("loaded_objects") == right.get("loaded_objects")
        and left.get("config_files") == right.get("config_files")
    )


def _archive_original(receipt_path: Path, receipt_digest: str) -> Path:
    directory = receipt_path.parent
    archive = directory / f"activation-complete.pre-adoption-{receipt_digest}.json"
    original = receipt_path.read_bytes()
    if clusterctl.sha256_bytes(original) != receipt_digest:
        raise AdoptionError("historical predecessor receipt changed while archiving")
    if archive.exists() or archive.is_symlink():
        if archive.is_symlink() or not archive.is_file():
            raise AdoptionError("predecessor receipt archive target is unsafe")
        if clusterctl.sha256_file(archive) != receipt_digest:
            raise AdoptionError("predecessor receipt archive differs from historical bytes")
        return archive
    clusterctl.atomic_write(
        archive,
        original,
        receipt_path.stat().st_mode & 0o777,
    )
    return archive


def _live_predecessor(
    plan: dict[str, Any], contract: dict[str, Any], package_id: str
) -> dict[str, Any]:
    loaded = clusterctl.observe_loaded_live(plan, contract, Path("/"))
    clusterctl.verify_loaded(plan, contract, loaded)
    _require_selection(contract, package_id)
    return loaded


def _ensure_predecessor_running(
    plan: dict[str, Any], contract: dict[str, Any], package_id: str
) -> None:
    """Resume the selected durable cluster when pg_ctl proves it is stopped."""
    _require_selection(contract, package_id)
    status = subprocess.run(
        clusterctl._pg_ctl_command(plan, "status"),
        check=False, cwd="/", env=clusterctl.activation_environment(),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=30,
    )
    if status.returncode == 0:
        return
    # pg_ctl uses 3 specifically for an existing, stopped cluster. Other errors
    # (including inaccessible state) must never be interpreted as permission to start.
    if status.returncode != 3:
        raise AdoptionError(
            "cannot establish selected predecessor lifecycle state: "
            + (status.stderr.strip() or status.stdout.strip())
        )
    clusterctl._stopped_live(plan)
    for entry in plan["files"]:
        path = Path(entry["path"])
        if (path.is_symlink() or not path.is_file()
                or clusterctl.sha256_file(path) != entry["sha256"]):
            raise AdoptionError(f"stopped predecessor configuration differs: {path}")
    _require_selection(contract, package_id)
    started = clusterctl.execute_activation_command(
        "resume-stopped-selected-predecessor", plan["commands"]["start_candidate"], 300
    )
    ready = clusterctl.await_postgresql_ready(
        "resumed-selected-predecessor-readiness", plan["commands"]["probe_readiness"], 300
    )
    loaded = _live_predecessor(plan, contract, package_id)
    clusterctl.write_json(
        _activation_directory(contract, package_id) / "predecessor-resume.json",
        {"schema": "laplace.postgresql-predecessor-resume/v1",
         "package_id": package_id, "command_receipts": [started, ready],
         "loaded_observation": loaded},
    )


def _restore_predecessor_after_failure(
    plan: dict[str, Any],
    contract: dict[str, Any],
    package_id: str,
    initial: dict[str, Any],
) -> dict[str, Any]:
    """Leave failure only after the selected predecessor is live and exact again."""

    try:
        current = _live_predecessor(plan, contract, package_id)
        if current.get("system_identifier") == initial.get("system_identifier"):
            return current
    except BaseException:
        pass

    # It is either stopped or not verifiably healthy. Force one bounded stopped/start
    # cycle. A failed stop may simply mean it was already stopped, so stopped-state
    # observation remains the authority before the restore start.
    try:
        clusterctl.execute_activation_command(
            "stop-predecessor-for-adoption-restore",
            plan["commands"]["stop_candidate"],
            300,
        )
    except BaseException:
        pass
    clusterctl._stopped_live(plan)
    clusterctl.execute_activation_command(
        "restore-predecessor-after-adoption-failure",
        plan["commands"]["start_candidate"],
        300,
    )
    clusterctl.await_postgresql_ready(
        "restored-predecessor-readiness",
        plan["commands"]["probe_readiness"],
        300,
    )
    restored = _live_predecessor(plan, contract, package_id)
    if restored.get("system_identifier") != initial.get("system_identifier"):
        raise AdoptionError("adoption rollback changed PostgreSQL system identity")
    return restored


def ensure_current_predecessor_receipt(contract_path: Path) -> dict[str, Any]:
    """Return a current predecessor receipt, re-proving an older one when necessary."""

    clusterctl.require_fixture_or_root(Path("/"), False)
    contract = clusterctl.load_json(contract_path)
    clusterctl.validate_contract(contract)
    package_id = _active_package_id(contract)
    directory = _activation_directory(contract, package_id)
    receipt_path = directory / "activation-complete.json"
    if not receipt_path.is_file() or receipt_path.is_symlink():
        raise AdoptionError(
            f"active predecessor lacks durable completed cluster receipt: {receipt_path}"
        )
    receipt = clusterctl.load_json(receipt_path)
    if (
        receipt.get("schema") != clusterctl.ACTIVATION_SCHEMA
        or receipt.get("phase") != "activated"
        or receipt.get("package_id") != package_id
    ):
        raise AdoptionError("active predecessor historical receipt identity is invalid")
    plan_path, plan = _load_plan(receipt, contract, package_id)
    _require_selection(contract, package_id)
    _ensure_predecessor_running(plan, contract, package_id)
    if _receipt_is_current(receipt, package_id):
        return receipt

    historical_digest = clusterctl.sha256_file(receipt_path)
    archive_path = _archive_original(receipt_path, historical_digest)
    initial = _live_predecessor(plan, contract, package_id)

    lifecycle_mutation_started = False
    try:
        lifecycle_mutation_started = True
        stop_receipt = clusterctl.execute_activation_command(
            "stop-predecessor-for-receipt-adoption",
            plan["commands"]["stop_candidate"],
            300,
        )
        clusterctl._stopped_live(plan)
        start_receipt = clusterctl.execute_activation_command(
            "start-predecessor-for-receipt-adoption",
            plan["commands"]["start_candidate"],
            300,
        )
        ready_receipt = clusterctl.await_postgresql_ready(
            "predecessor-receipt-adoption-readiness",
            plan["commands"]["probe_readiness"],
            300,
        )
        restarted = _live_predecessor(plan, contract, package_id)
        if restarted.get("postmaster_pid") == initial.get("postmaster_pid"):
            raise AdoptionError("predecessor restart proof retained the original postmaster")
        if not _same_loaded_state(initial, restarted):
            raise AdoptionError("predecessor restart proof changed durable or loaded identity")
    except BaseException as error:
        rollback_error: BaseException | None = None
        if lifecycle_mutation_started:
            try:
                _restore_predecessor_after_failure(
                    plan, contract, package_id, initial
                )
            except BaseException as caught:
                rollback_error = caught
        if rollback_error is not None:
            raise AdoptionError(
                "predecessor receipt re-proof failed and live predecessor restoration "
                f"could not be proven: {rollback_error}"
            ) from error
        raise

    clusterctl.write_json(directory / "adoption-loaded-initial.json", initial)
    clusterctl.write_json(directory / "adoption-loaded-restart.json", restarted)
    normalized = dict(receipt)
    normalized.update(
        {
            "schema": clusterctl.ACTIVATION_SCHEMA,
            "phase": "activated",
            "package_id": package_id,
            "restart_proven": True,
            "boot_enabled": False,
            "service_integration_required": False,
            "lifecycle_provider": clusterctl.LIFECYCLE_PROVIDER,
            "active_target": f"releases/{package_id}",
            "runtime_target": f"../releases/{package_id}",
            "cluster_plan_path": str(plan_path),
            "system_identifier": restarted["system_identifier"],
            "loaded_initial_observation_sha256": initial["observation_sha256"],
            "loaded_restart_observation_sha256": restarted["observation_sha256"],
            "command_receipts": [stop_receipt, start_receipt, ready_receipt],
            "predecessor_adoption_schema": ADOPTION_SCHEMA,
            "predecessor_historical_receipt_sha256": historical_digest,
            "predecessor_historical_receipt_archive": str(archive_path),
            "predecessor_live_reproof": True,
        }
    )
    normalized.pop("activation_receipt_sha256", None)
    normalized["activation_receipt_sha256"] = _activation_identity(normalized)
    clusterctl.write_json(receipt_path, normalized)
    return normalized
