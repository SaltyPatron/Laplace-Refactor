#!/usr/bin/env python3
"""Enable and cold-boot verify the durable Laplace refactor PostgreSQL product.

This controller is installed inside the immutable root-owned activation gateway.
It does not create the product cluster.  It accepts only the exact completed
cluster, Unicode, and Highway receipts, enables the selected systemd service for
boot, and later proves the same product identity after a different host boot.
"""

from __future__ import annotations

import argparse
import grp
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import tempfile
from typing import Any, Callable, Mapping, Sequence


HEX_128 = re.compile(r"^[0-9a-f]{32}$")
HEX_256 = re.compile(r"^[0-9a-f]{64}$")
BOOT_ID = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"
)
SERVICE = re.compile(r"^[A-Za-z0-9_.@-]+\.(?:service|path)$")
ENABLEMENT_SCHEMA = "laplace.product-service-enablement/v1"
BOOT_READBACK_SCHEMA = "laplace.product-cold-boot-readback/v1"
ENVIRONMENT = {
    "LANG": "C",
    "LC_ALL": "C",
    "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
}


class ServiceStateError(RuntimeError):
    """The durable product or its boot state differs from the accepted identity."""


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ServiceStateError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def document_identity(document: Mapping[str, Any], field: str) -> str:
    return sha256_bytes(
        canonical_bytes({key: value for key, value in document.items() if key != field})
    )


def producer_document_identity(document: Mapping[str, Any], field: str) -> str:
    """Identity law used by clusterctl, unicodectl, and highwayctl."""

    return sha256_bytes(
        canonical_bytes({key: value for key, value in document.items() if key != field})
        + b"\n"
    )


def prefixed(root: Path, logical: str | Path) -> Path:
    path = Path(logical)
    if not path.is_absolute() or ".." in PurePosixPath(str(path)).parts:
        raise ServiceStateError(f"unsafe absolute path: {logical}")
    return path if root == Path("/") else root.joinpath(*path.parts[1:])


def load_json(path: Path) -> dict[str, Any]:
    if not path.is_file() or path.is_symlink():
        raise ServiceStateError(f"required JSON is absent or not physical: {path}")
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ServiceStateError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ServiceStateError(f"JSON root must be an object: {path}")
    return value


def require_hex(value: Any, pattern: re.Pattern[str], label: str) -> str:
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        raise ServiceStateError(f"{label} is not an exact lowercase hexadecimal identity")
    return value


def require_service(value: Any, label: str, suffix: str) -> str:
    if (
        not isinstance(value, str)
        or SERVICE.fullmatch(value) is None
        or not value.endswith(suffix)
    ):
        raise ServiceStateError(f"{label} is not a safe {suffix} unit")
    return value


def validate_contracts(gateway: dict[str, Any], cluster: dict[str, Any]) -> dict[str, Any]:
    state = gateway.get("service_state")
    expected = {
        "controller",
        "cluster_service",
        "cluster_unit",
        "enable_path_unit",
        "enable_service_unit",
        "boot_readback_unit",
        "enablement_receipt",
        "boot_readback_receipt",
        "boot_id_path",
    }
    if not isinstance(state, dict) or set(state) != expected:
        raise ServiceStateError("service-state contract fields differ")
    for name in (
        "controller",
        "cluster_unit",
        "enablement_receipt",
        "boot_readback_receipt",
        "boot_id_path",
    ):
        prefixed(Path("/"), state[name])
    require_service(state["cluster_service"], "cluster service", ".service")
    require_service(state["enable_path_unit"], "enable path", ".path")
    require_service(state["enable_service_unit"], "enable service", ".service")
    require_service(state["boot_readback_unit"], "boot readback", ".service")
    instance = cluster.get("instance")
    package = cluster.get("package")
    if not isinstance(instance, dict) or not isinstance(package, dict):
        raise ServiceStateError("PostgreSQL cluster contract is incomplete")
    if state["cluster_service"] != instance.get("service"):
        raise ServiceStateError("service-state and cluster service identities differ")
    if state["cluster_unit"] != f"/etc/systemd/system/{instance['service']}":
        raise ServiceStateError("service-state unit path differs from the cluster contract")
    if gateway.get("product", {}).get("package_release_root") != package.get(
        "release_root"
    ):
        raise ServiceStateError("gateway and cluster release roots differ")
    if gateway.get("product", {}).get("cluster_activation_root") != (
        f"{instance['receipt_directory']}/cluster-activation"
    ):
        raise ServiceStateError("cluster activation receipt root differs")
    return state


def implementation_clusterctl_path(bundle: Path | None = None) -> Path:
    if bundle is not None:
        installed = bundle / "controllers/clusterctl.py"
        if installed.is_file():
            return installed
    source = Path(__file__).resolve().parents[1] / "postgresql/clusterctl.py"
    if source.is_file():
        return source
    raise ServiceStateError("canonical PostgreSQL cluster controller is absent")


def load_clusterctl(bundle: Path | None = None) -> Any:
    path = implementation_clusterctl_path(bundle)
    specification = importlib.util.spec_from_file_location(
        "laplace_product_service_clusterctl", path
    )
    if specification is None or specification.loader is None:
        raise ServiceStateError("cannot load the PostgreSQL cluster controller")
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


def verify_installed_bundle(controller: Path) -> tuple[Path, dict[str, Any]]:
    resolved = controller.resolve()
    bundle = resolved.parent.parent
    manifest_path = bundle / "bundle-manifest.json"
    manifest = load_json(manifest_path)
    if manifest.get("schema") != "laplace.product-activation-gateway-bundle/v1":
        raise ServiceStateError("gateway bundle schema differs")
    bundle_id = require_hex(manifest.get("bundle_id"), HEX_256, "gateway bundle id")
    if bundle.name != bundle_id or bundle_id != document_identity(manifest, "bundle_id"):
        raise ServiceStateError("gateway bundle identity differs")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise ServiceStateError("gateway bundle manifest omits files")
    observed: set[str] = set()
    for entry in files:
        if not isinstance(entry, dict) or set(entry) != {"path", "sha256"}:
            raise ServiceStateError("gateway bundle manifest entry differs")
        relative = entry.get("path")
        digest = entry.get("sha256")
        if (
            not isinstance(relative, str)
            or relative in observed
            or PurePosixPath(relative).is_absolute()
            or ".." in PurePosixPath(relative).parts
            or HEX_256.fullmatch(str(digest)) is None
        ):
            raise ServiceStateError("gateway bundle path or digest is invalid")
        observed.add(relative)
        candidate = bundle.joinpath(*PurePosixPath(relative).parts)
        if (
            not candidate.is_file()
            or candidate.is_symlink()
            or sha256_file(candidate) != digest
        ):
            raise ServiceStateError(f"gateway bundle file differs: {relative}")
        metadata = candidate.stat()
        if metadata.st_uid != 0 or metadata.st_gid != 0 or metadata.st_mode & 0o022:
            raise ServiceStateError(f"gateway bundle file ownership is unsafe: {relative}")
    for directory in (bundle, bundle / "bin", bundle / "controllers", bundle / "contracts"):
        metadata = directory.stat()
        if (
            not stat.S_ISDIR(metadata.st_mode)
            or metadata.st_uid != 0
            or metadata.st_gid != 0
            or metadata.st_mode & 0o022
        ):
            raise ServiceStateError(f"gateway bundle directory ownership is unsafe: {directory}")
    if resolved != bundle / "controllers/product_service_state.py":
        raise ServiceStateError("service-state controller is not running from its bundle path")
    return bundle, manifest


def require_owned_file(path: Path, root: Path, label: str) -> None:
    if not path.is_file() or path.is_symlink():
        raise ServiceStateError(f"{label} is absent or not a physical file: {path}")
    if root == Path("/"):
        metadata = path.stat()
        if metadata.st_uid != 0 or metadata.st_gid != 0 or metadata.st_mode & 0o022:
            raise ServiceStateError(f"{label} ownership is unsafe: {path}")


def require_receipt_identity(
    document: dict[str, Any], field: str, label: str, *, producer: bool
) -> None:
    expected = (
        producer_document_identity(document, field)
        if producer
        else document_identity(document, field)
    )
    if document.get(field) != expected:
        raise ServiceStateError(f"{label} identity differs from its content")


def validate_unit_file(
    state: dict[str, Any], cluster: dict[str, Any], package_id: str, root: Path
) -> tuple[Path, str]:
    unit = prefixed(root, state["cluster_unit"])
    require_owned_file(unit, root, "cluster systemd unit")
    content = unit.read_text(encoding="utf-8")
    instance = cluster["instance"]
    release = f"{cluster['package']['release_root']}/{package_id}"
    required = (
        f"User={instance['os_user']}\n",
        f"Group={instance['os_group']}\n",
        f"ExecStart={release}/pgsql-18/bin/postgres -D {instance['data_directory']} -c config_file={instance['config_directory']}/postgresql.conf\n",
        f"RequiresMountsFor={instance['data_directory']} {instance['wal_directory']} {instance['temp_directory']}\n",
        "WantedBy=multi-user.target\n",
    )
    missing = [entry.rstrip() for entry in required if entry not in content]
    if missing:
        raise ServiceStateError(
            "cluster systemd unit differs from the accepted plan: " + missing[0]
        )
    return unit, sha256_bytes(content.encode("utf-8"))


def inspect_product_state(
    gateway: dict[str, Any],
    cluster: dict[str, Any],
    root: Path,
    cluster_module: Any,
    loaded_observer: Callable[[dict[str, Any], dict[str, Any], Path], dict[str, Any]]
    | None = None,
) -> dict[str, Any]:
    state = validate_contracts(gateway, cluster)
    product = gateway["product"]
    highway_path = prefixed(root, product["highway_result"])
    highway = load_json(highway_path)
    if (
        highway.get("schema") != "laplace.highway-product-activation-receipt/v1"
        or highway.get("phase") != "product-activated"
        or highway.get("restart_proven") is not True
        or highway.get("cold_application_readback_proven") is not True
    ):
        raise ServiceStateError("Highway product receipt is incomplete")
    require_receipt_identity(highway, "receipt_sha256", "Highway product receipt", producer=True)
    package_id = require_hex(highway.get("package_id"), HEX_256, "package id")

    unicode_path = prefixed(root, product["unicode_result"])
    unicode = load_json(unicode_path)
    if (
        unicode.get("schema") != "laplace.unicode-product-activation-receipt/v1"
        or unicode.get("phase") != "product-activated"
        or unicode.get("package_id") != package_id
        or unicode.get("restart_proven") is not True
        or unicode.get("cold_public_readback_proven") is not True
        or unicode.get("reverse_inversion_proven") is not True
    ):
        raise ServiceStateError("Unicode product receipt is incomplete")
    require_receipt_identity(unicode, "receipt_sha256", "Unicode product receipt", producer=True)

    cluster_result_logical = (
        Path(product["cluster_activation_root"]) / package_id / "activation-result.json"
    )
    cluster_result_path = prefixed(root, cluster_result_logical)
    cluster_result = load_json(cluster_result_path)
    system_identifier = str(cluster_result.get("system_identifier", ""))
    if (
        cluster_result.get("schema") != "laplace.postgresql-activation-receipt/v1"
        or cluster_result.get("phase") != "activated"
        or cluster_result.get("package_id") != package_id
        or cluster_result.get("restart_proven") is not True
        or cluster_result.get("active_target") != f"releases/{package_id}"
        or not system_identifier.isdecimal()
        or int(system_identifier) <= 0
    ):
        raise ServiceStateError("cluster activation receipt is incomplete")
    require_receipt_identity(
        cluster_result,
        "activation_receipt_sha256",
        "cluster activation receipt",
        producer=True,
    )
    if (
        unicode.get("system_identifier") != system_identifier
        or highway.get("system_identifier") != system_identifier
        or unicode.get("cluster_activation_receipt_sha256")
        != cluster_result["activation_receipt_sha256"]
        or highway.get("cluster_activation_receipt_sha256")
        != cluster_result["activation_receipt_sha256"]
        or highway.get("unicode_activation_receipt_sha256")
        != unicode["receipt_sha256"]
    ):
        raise ServiceStateError("product receipts do not name one cluster generation")

    active = prefixed(root, cluster["package"]["active_link"])
    expected_target = f"releases/{package_id}"
    if not active.is_symlink() or os.readlink(active) != expected_target:
        raise ServiceStateError("active product pointer differs from the accepted package")
    release = prefixed(root, f"{cluster['package']['release_root']}/{package_id}")
    if not release.is_dir() or release.is_symlink() or active.resolve() != release.resolve():
        raise ServiceStateError("active product pointer does not resolve to the accepted release")

    unit, unit_sha256 = validate_unit_file(state, cluster, package_id, root)
    plan_logical = Path(str(cluster_result.get("cluster_plan_path", "")))
    if not plan_logical.is_absolute():
        raise ServiceStateError("cluster activation receipt omits its absolute plan path")
    evidence_root = Path(product["cluster_activation_root"]) / package_id
    try:
        plan_logical.relative_to(evidence_root)
    except ValueError as error:
        raise ServiceStateError("cluster plan escapes its package-addressed evidence root") from error
    plan_path = prefixed(root, plan_logical)
    plan = load_json(plan_path)
    cluster_module.validate_plan(plan, cluster)
    if plan.get("package_id") != package_id:
        raise ServiceStateError("cluster plan names another package")
    observer = loaded_observer or cluster_module.observe_loaded_live
    loaded = observer(plan, cluster, root)
    cluster_module.verify_loaded(plan, cluster, loaded)
    if loaded.get("system_identifier") != system_identifier:
        raise ServiceStateError("live PostgreSQL system identity differs from activation")

    activation = highway.get("activation")
    if not isinstance(activation, dict):
        raise ServiceStateError("Highway receipt omits its activation identity")
    unicode_epoch_id = require_hex(
        unicode.get("activation_epoch_id"), HEX_128, "Unicode activation epoch"
    )
    unicode_epoch_fingerprint = require_hex(
        unicode.get("activation_epoch_fingerprint"),
        HEX_256,
        "Unicode activation epoch fingerprint",
    )
    registry_epoch_id = require_hex(
        activation.get("registry_epoch_id"), HEX_128, "Highway registry epoch"
    )
    registry_epoch_fingerprint = require_hex(
        activation.get("registry_epoch_fingerprint"),
        HEX_256,
        "Highway registry epoch fingerprint",
    )
    stable_identity = {
        "package_id": package_id,
        "system_identifier": system_identifier,
        "active_target": expected_target,
        "cluster_plan_sha256": cluster_result.get("plan_sha256"),
        "cluster_activation_receipt_sha256": cluster_result[
            "activation_receipt_sha256"
        ],
        "unicode_activation_receipt_sha256": unicode["receipt_sha256"],
        "highway_activation_receipt_sha256": highway["receipt_sha256"],
        "unicode_activation_epoch_id": unicode_epoch_id,
        "unicode_activation_epoch_fingerprint": unicode_epoch_fingerprint,
        "highway_registry_epoch_id": registry_epoch_id,
        "highway_registry_epoch_fingerprint": registry_epoch_fingerprint,
        "unit_sha256": unit_sha256,
        "loaded_objects": loaded.get("loaded_objects"),
        "config_files": loaded.get("config_files"),
    }
    if HEX_256.fullmatch(str(stable_identity["cluster_plan_sha256"])) is None:
        raise ServiceStateError("cluster activation receipt omits its exact plan identity")
    if not isinstance(stable_identity["loaded_objects"], list) or not isinstance(
        stable_identity["config_files"], list
    ):
        raise ServiceStateError("live product observation omits package/configuration identity")
    return {
        "stable_identity": stable_identity,
        "live_observation_sha256": loaded.get("observation_sha256"),
        "postmaster_pid": loaded.get("postmaster_pid"),
        "cluster_result": str(cluster_result_logical),
        "unicode_result": product["unicode_result"],
        "highway_result": product["highway_result"],
        "cluster_unit": state["cluster_unit"],
        "cluster_unit_physical": str(unit),
    }


def command_receipt(
    label: str, command: Sequence[str], completed: subprocess.CompletedProcess[str]
) -> dict[str, Any]:
    return {
        "label": label,
        "argv": list(command),
        "exit_code": completed.returncode,
        "stdout_sha256": sha256_bytes(completed.stdout.encode("utf-8")),
        "stderr_sha256": sha256_bytes(completed.stderr.encode("utf-8")),
    }


def run_fixed(
    label: str, command: Sequence[str], timeout: int = 120
) -> tuple[str, dict[str, Any]]:
    completed = subprocess.run(
        list(command),
        check=False,
        cwd="/",
        env=ENVIRONMENT,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )
    receipt = command_receipt(label, command, completed)
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic"
        raise ServiceStateError(f"{label} failed with exit {completed.returncode}: {detail}")
    return completed.stdout.strip(), receipt


def parse_systemd_show(output: str, unit: str) -> dict[str, Any]:
    values: dict[str, str] = {}
    for line in output.splitlines():
        if "=" not in line:
            continue
        name, value = line.split("=", 1)
        if name in values:
            raise ServiceStateError(f"systemd observation repeats {name}: {unit}")
        values[name] = value
    required = {"LoadState", "UnitFileState", "ActiveState", "SubState", "MainPID"}
    if set(values) != required:
        raise ServiceStateError(f"systemd observation fields differ: {unit}")
    pid_text = values.pop("MainPID")
    if not pid_text.isdecimal():
        raise ServiceStateError(f"systemd MainPID is invalid: {unit}")
    return {"unit": unit, **values, "MainPID": int(pid_text)}


def observe_unit(
    unit: str,
    runner: Callable[[str, Sequence[str], int], tuple[str, dict[str, Any]]] = run_fixed,
) -> tuple[dict[str, Any], dict[str, Any]]:
    output, receipt = runner(
        f"observe-{unit}",
        [
            "/usr/bin/systemctl",
            "show",
            "--property=LoadState",
            "--property=UnitFileState",
            "--property=ActiveState",
            "--property=SubState",
            "--property=MainPID",
            unit,
        ],
        60,
    )
    return parse_systemd_show(output, unit), receipt


def read_boot_id(path: Path) -> str:
    try:
        value = path.read_text(encoding="ascii").strip()
    except OSError as error:
        raise ServiceStateError(f"cannot read host boot identity: {error}") from error
    if BOOT_ID.fullmatch(value) is None:
        raise ServiceStateError("host boot identity is invalid")
    return value


def atomic_write(
    path: Path, content: bytes, mode: int, group_name: str, root: Path
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o750)
    if path.parent.is_symlink():
        raise ServiceStateError(f"receipt directory is a symlink: {path.parent}")
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(mode)
        if root == Path("/"):
            group = grp.getgrnam(group_name)
            os.chown(temporary, 0, group.gr_gid)
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def write_receipt(
    logical_pointer: str,
    receipt: dict[str, Any],
    stem: str,
    group_name: str,
    root: Path,
) -> Path:
    pointer = prefixed(root, logical_pointer)
    history = pointer.parent / "service-state" / f"{stem}-{receipt['receipt_sha256']}.json"
    content = canonical_bytes(receipt) + b"\n"
    history.parent.mkdir(parents=True, exist_ok=True, mode=0o750)
    if root == Path("/"):
        group = grp.getgrnam(group_name)
        os.chown(history.parent, 0, group.gr_gid)
        history.parent.chmod(0o2750)
    if history.exists():
        if not history.is_file() or history.is_symlink() or history.read_bytes() != content:
            raise ServiceStateError(f"existing durable receipt differs: {history}")
    else:
        atomic_write(history, content, 0o640, group_name, root)
    atomic_write(pointer, content, 0o640, group_name, root)
    return history


def validate_enablement_receipt(receipt: dict[str, Any]) -> None:
    if (
        receipt.get("schema") != ENABLEMENT_SCHEMA
        or receipt.get("phase") != "enabled-for-cold-boot"
        or receipt.get("enabled_for_boot") is not True
        or receipt.get("cold_boot_proven") is not False
        or BOOT_ID.fullmatch(str(receipt.get("activation_boot_id", ""))) is None
        or HEX_256.fullmatch(str(receipt.get("gateway_bundle_id", ""))) is None
        or not isinstance(receipt.get("product_identity"), dict)
    ):
        raise ServiceStateError("service enablement receipt is incomplete")
    require_receipt_identity(
        receipt, "receipt_sha256", "service enablement receipt", producer=False
    )


def enable_product_service(
    gateway: dict[str, Any],
    cluster: dict[str, Any],
    bundle_id: str,
    root: Path,
    cluster_module: Any,
    loaded_observer: Callable[[dict[str, Any], dict[str, Any], Path], dict[str, Any]]
    | None = None,
    runner: Callable[[str, Sequence[str], int], tuple[str, dict[str, Any]]] = run_fixed,
) -> dict[str, Any]:
    state_contract = validate_contracts(gateway, cluster)
    snapshot = inspect_product_state(
        gateway, cluster, root, cluster_module, loaded_observer
    )
    commands: list[dict[str, Any]] = []
    _output, receipt = runner(
        "reload-systemd-for-product-enablement",
        ["/usr/bin/systemctl", "daemon-reload"],
        60,
    )
    commands.append(receipt)
    _output, receipt = runner(
        "enable-and-start-refactor-postgresql",
        [
            "/usr/bin/systemctl",
            "enable",
            "--now",
            state_contract["cluster_service"],
        ],
        240,
    )
    commands.append(receipt)
    _output, receipt = runner(
        "enable-product-cold-boot-readback",
        [
            "/usr/bin/systemctl",
            "enable",
            state_contract["boot_readback_unit"],
        ],
        120,
    )
    commands.append(receipt)
    service_observation, observation_receipt = observe_unit(
        state_contract["cluster_service"], runner
    )
    commands.append(observation_receipt)
    boot_unit_observation, observation_receipt = observe_unit(
        state_contract["boot_readback_unit"], runner
    )
    commands.append(observation_receipt)
    if (
        service_observation["LoadState"] != "loaded"
        or service_observation["UnitFileState"] != "enabled"
        or service_observation["ActiveState"] != "active"
        or service_observation["MainPID"] <= 0
    ):
        raise ServiceStateError("refactor PostgreSQL service is not enabled and active")
    if (
        boot_unit_observation["LoadState"] != "loaded"
        or boot_unit_observation["UnitFileState"] != "enabled"
    ):
        raise ServiceStateError("cold-boot readback service is not enabled")
    receipt_document: dict[str, Any] = {
        "schema": ENABLEMENT_SCHEMA,
        "phase": "enabled-for-cold-boot",
        "gateway_bundle_id": bundle_id,
        "activation_boot_id": read_boot_id(
            prefixed(root, state_contract["boot_id_path"])
        ),
        "cluster_service": state_contract["cluster_service"],
        "boot_readback_unit": state_contract["boot_readback_unit"],
        "enabled_for_boot": True,
        "cold_boot_proven": False,
        "product_identity": snapshot["stable_identity"],
        "live_observation_sha256": snapshot["live_observation_sha256"],
        "service_observation": service_observation,
        "boot_unit_observation": boot_unit_observation,
        "command_receipts": commands,
    }
    receipt_document["receipt_sha256"] = document_identity(
        receipt_document, "receipt_sha256"
    )
    write_receipt(
        state_contract["enablement_receipt"],
        receipt_document,
        "enablement",
        cluster["instance"]["os_group"],
        root,
    )
    return receipt_document


def cold_boot_readback(
    gateway: dict[str, Any],
    cluster: dict[str, Any],
    bundle_id: str,
    root: Path,
    cluster_module: Any,
    loaded_observer: Callable[[dict[str, Any], dict[str, Any], Path], dict[str, Any]]
    | None = None,
    runner: Callable[[str, Sequence[str], int], tuple[str, dict[str, Any]]] = run_fixed,
) -> dict[str, Any]:
    state_contract = validate_contracts(gateway, cluster)
    enablement = load_json(prefixed(root, state_contract["enablement_receipt"]))
    validate_enablement_receipt(enablement)
    if enablement["gateway_bundle_id"] != bundle_id:
        raise ServiceStateError("gateway bundle identity changed after product enablement")
    current_boot = read_boot_id(prefixed(root, state_contract["boot_id_path"]))
    if current_boot == enablement["activation_boot_id"]:
        raise ServiceStateError(
            "cold-boot readback ran in the activation boot and cannot claim host persistence"
        )
    snapshot = inspect_product_state(
        gateway, cluster, root, cluster_module, loaded_observer
    )
    if snapshot["stable_identity"] != enablement["product_identity"]:
        raise ServiceStateError("product identity changed across the host boot boundary")
    commands: list[dict[str, Any]] = []
    service_observation, observation_receipt = observe_unit(
        state_contract["cluster_service"], runner
    )
    commands.append(observation_receipt)
    boot_unit_observation, observation_receipt = observe_unit(
        state_contract["boot_readback_unit"], runner
    )
    commands.append(observation_receipt)
    if (
        service_observation["LoadState"] != "loaded"
        or service_observation["UnitFileState"] != "enabled"
        or service_observation["ActiveState"] != "active"
        or service_observation["MainPID"] <= 0
    ):
        raise ServiceStateError("refactor PostgreSQL did not return enabled and active after boot")
    if boot_unit_observation["UnitFileState"] != "enabled":
        raise ServiceStateError("cold-boot readback unit lost boot enablement")
    receipt_document: dict[str, Any] = {
        "schema": BOOT_READBACK_SCHEMA,
        "phase": "cold-boot-readback-proven",
        "gateway_bundle_id": bundle_id,
        "enablement_receipt_sha256": enablement["receipt_sha256"],
        "activation_boot_id": enablement["activation_boot_id"],
        "observed_boot_id": current_boot,
        "cluster_service": state_contract["cluster_service"],
        "boot_readback_unit": state_contract["boot_readback_unit"],
        "enabled_for_boot": True,
        "cold_boot_proven": True,
        "product_identity": snapshot["stable_identity"],
        "live_observation_sha256": snapshot["live_observation_sha256"],
        "service_observation": service_observation,
        "boot_unit_observation": boot_unit_observation,
        "command_receipts": commands,
    }
    receipt_document["receipt_sha256"] = document_identity(
        receipt_document, "receipt_sha256"
    )
    write_receipt(
        state_contract["boot_readback_receipt"],
        receipt_document,
        "boot-readback",
        cluster["instance"]["os_group"],
        root,
    )
    return receipt_document


def probe_product_service(
    gateway: dict[str, Any],
    cluster: dict[str, Any],
    bundle_id: str,
    root: Path,
    cluster_module: Any,
) -> dict[str, Any]:
    state_contract = validate_contracts(gateway, cluster)
    snapshot = inspect_product_state(gateway, cluster, root, cluster_module)
    service, _service_receipt = observe_unit(state_contract["cluster_service"])
    enablement_path = prefixed(root, state_contract["enablement_receipt"])
    boot_path = prefixed(root, state_contract["boot_readback_receipt"])
    enablement = load_json(enablement_path) if enablement_path.exists() else None
    if enablement is not None:
        validate_enablement_receipt(enablement)
    boot = load_json(boot_path) if boot_path.exists() else None
    if boot is not None:
        if (
            boot.get("schema") != BOOT_READBACK_SCHEMA
            or boot.get("phase") != "cold-boot-readback-proven"
            or boot.get("cold_boot_proven") is not True
        ):
            raise ServiceStateError("cold-boot readback receipt is incomplete")
        require_receipt_identity(
            boot, "receipt_sha256", "cold-boot readback receipt", producer=False
        )
    return {
        "schema": "laplace.product-service-state-probe/v1",
        "gateway_bundle_id": bundle_id,
        "product_identity": snapshot["stable_identity"],
        "service_observation": service,
        "enablement_receipt_sha256": enablement.get("receipt_sha256")
        if enablement
        else None,
        "cold_boot_receipt_sha256": boot.get("receipt_sha256") if boot else None,
        "cold_boot_proven": bool(boot and boot.get("cold_boot_proven") is True),
    }


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command", choices=("enable", "boot-readback", "probe")
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    if os.geteuid() != 0:
        raise ServiceStateError("product service-state controller requires root")
    controller = Path(__file__).resolve()
    bundle, manifest = verify_installed_bundle(controller)
    gateway = load_json(bundle / "contracts/product-activation-gateway.json")
    cluster = load_json(bundle / "contracts/postgresql-cluster.json")
    state = validate_contracts(gateway, cluster)
    current = prefixed(Path("/"), gateway["gateway"]["active_link"])
    if not current.is_symlink() or current.resolve() != bundle:
        raise ServiceStateError("active gateway pointer does not select this controller bundle")
    if controller != prefixed(Path("/"), state["controller"]).resolve():
        raise ServiceStateError("service-state controller path differs from the contract")
    cluster_module = load_clusterctl(bundle)
    if arguments.command == "enable":
        result = enable_product_service(
            gateway, cluster, manifest["bundle_id"], Path("/"), cluster_module
        )
    elif arguments.command == "boot-readback":
        result = cold_boot_readback(
            gateway, cluster, manifest["bundle_id"], Path("/"), cluster_module
        )
    else:
        result = probe_product_service(
            gateway, cluster, manifest["bundle_id"], Path("/"), cluster_module
        )
    sys.stdout.write(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ServiceStateError as error:
        print(f"product service state: {error}", file=sys.stderr)
        raise SystemExit(1)
