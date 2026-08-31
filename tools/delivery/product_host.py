#!/usr/bin/env python3
"""Converge the Laplace host boundary and dispatch an exact product request."""

from __future__ import annotations

import argparse
import base64
import grp
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import pwd
import secrets
import stat
import subprocess
import sys
from typing import Any, Callable, Sequence


REPOSITORY = Path(__file__).resolve().parents[2]
INSTALLER_PATH = Path(__file__).with_name("install_product_activation_gateway.py")
SPEC = importlib.util.spec_from_file_location("laplace_product_host_gateway", INSTALLER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product activation gateway installer")
gateway = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = gateway
SPEC.loader.exec_module(gateway)

SCHEMA = "laplace.product-host-contract/v1"
RECEIPT_SCHEMA = "laplace.product-host-convergence-receipt/v1"


class HostError(RuntimeError):
    """The host cannot be converged without weakening the declared boundary."""


def load_json(path: Path) -> dict[str, Any]:
    try:
        return gateway.activation.load_json(path)
    except gateway.activation.ActivationGatewayError as error:
        raise HostError(str(error)) from error


def parse_mode(value: Any, label: str) -> int:
    if (
        not isinstance(value, str)
        or len(value) != 4
        or any(character not in "01234567" for character in value)
    ):
        raise HostError(f"{label} must be a four-digit octal mode")
    return int(value, 8)


def require_logical_path(value: Any, label: str) -> Path:
    if not isinstance(value, str):
        raise HostError(f"{label} must be an absolute path")
    path = Path(value)
    if (
        not path.is_absolute()
        or ".." in PurePosixPath(value).parts
        or len(path.parts) < 3
    ):
        raise HostError(f"{label} must be a bounded absolute path")
    return path


def validate_contract(contract: dict[str, Any], repository: Path) -> None:
    if contract.get("schema") != SCHEMA or contract.get("version") != "1.0.0":
        raise HostError("product host contract schema or version differs")
    identity = contract.get("service_identity")
    modules = contract.get("modules")
    lifecycle = contract.get("lifecycle")
    directories = contract.get("directories")
    if not all(isinstance(value, dict) for value in (identity, modules, lifecycle)):
        raise HostError("product host contract sections are incomplete")
    if identity != {
        "user": "laplace-runner",
        "group": "laplace-runner",
        "home": "/var/lib/agents/laplace-runner",
        "shell": "/usr/sbin/nologin",
        "system": True,
    }:
        raise HostError("product service identity differs")
    if not isinstance(directories, list) or not directories:
        raise HostError("product host directories are absent")
    paths: list[str] = []
    for index, item in enumerate(directories):
        if not isinstance(item, dict) or set(item) != {
            "path", "owner", "group", "mode", "purpose"
        }:
            raise HostError(f"directories[{index}] fields differ")
        logical = require_logical_path(item["path"], f"directories[{index}].path")
        if item["owner"] not in {"root", identity["user"]}:
            raise HostError("host directory owner escapes declared identities")
        if item["group"] not in {"root", identity["group"]}:
            raise HostError("host directory group escapes declared identities")
        parse_mode(item["mode"], f"directories[{index}].mode")
        if not isinstance(item["purpose"], str) or not item["purpose"]:
            raise HostError("host directory purpose is absent")
        paths.append(str(logical))
    if paths != sorted(set(paths)):
        raise HostError("host directories must be sorted and unique")
    expected_modules = {
        "gateway_contract": "contracts/product-activation-gateway.json",
        "cluster_contract": "contracts/postgresql-cluster.json",
        "gateway_installer": "tools/delivery/install_product_activation_gateway.py",
        "gateway_executable": "/opt/laplace/deployment/current/bin/laplace-product-activate",
        "product_builder": "tools/product/build-package.py",
        "cluster_controller": "tools/postgresql/clusterctl.py",
    }
    if modules != expected_modules:
        raise HostError("product host module selection differs")
    gateway_contract = load_json(repository / modules["gateway_contract"])
    cluster_contract = load_json(repository / modules["cluster_contract"])
    gateway.activation.validate_contract(gateway_contract)
    if gateway_contract["repository"]["runner_user"] != identity["user"]:
        raise HostError("gateway runner identity differs from host identity")
    if cluster_contract.get("instance", {}).get("os_user") != identity["user"]:
        raise HostError("cluster service identity differs from host identity")
    if gateway_contract["gateway"]["executable"] != modules["gateway_executable"]:
        raise HostError("gateway executable differs across contracts")
    required_lifecycle = {
        "install", "initial_run", "repair", "verify",
        "instance_state_owner", "manual_hotfixes",
    }
    if set(lifecycle) != required_lifecycle or lifecycle["manual_hotfixes"] is not False:
        raise HostError("product host lifecycle is incomplete")


def prefixed(root: Path, logical: Path) -> Path:
    return logical if root == Path("/") else root.joinpath(*logical.parts[1:])


def require_authority(root: Path, authorize_system_root: bool) -> None:
    if not root.is_absolute():
        raise HostError("host root must be absolute")
    if root == Path("/") and (not authorize_system_root or os.geteuid() != 0):
        raise HostError("system host convergence requires root and --authorize-system-root")


def run_command(command: Sequence[str]) -> None:
    completed = subprocess.run(
        list(command),
        check=False,
        cwd="/",
        env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C", "LC_ALL": "C"},
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=60,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or f"exit {completed.returncode}"
        raise HostError(f"host identity command failed: {detail}")


def converge_identity(
    identity: dict[str, Any],
    runner: Callable[[Sequence[str]], None] = run_command,
) -> tuple[int, int, bool]:
    changed = False
    try:
        group_entry = grp.getgrnam(identity["group"])
    except KeyError:
        runner(["/usr/sbin/groupadd", "--system", identity["group"]])
        changed = True
        group_entry = grp.getgrnam(identity["group"])
    try:
        user_entry = pwd.getpwnam(identity["user"])
    except KeyError:
        runner([
            "/usr/sbin/useradd", "--system", "--gid", identity["group"],
            "--home-dir", identity["home"], "--shell", identity["shell"],
            "--create-home", identity["user"],
        ])
        changed = True
        user_entry = pwd.getpwnam(identity["user"])
    if user_entry.pw_gid != group_entry.gr_gid:
        raise HostError("product service user primary group differs")
    if user_entry.pw_dir != identity["home"] or user_entry.pw_shell != identity["shell"]:
        raise HostError("product service user home or shell differs")
    return user_entry.pw_uid, group_entry.gr_gid, changed


def physical_directory(path: Path) -> None:
    metadata = path.lstat()
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
        raise HostError(f"host path is not a physical directory: {path}")


def ensure_directory(
    root: Path,
    logical: Path,
    mode: int,
    uid: int,
    gid: int,
    enforce_owner: bool,
) -> dict[str, Any]:
    target = prefixed(root, logical)
    current = root if root != Path("/") else Path("/")
    for part in logical.parts[1:]:
        current = current / part
        if current.exists() or current.is_symlink():
            physical_directory(current)
        else:
            current.mkdir(mode=0o755)
    metadata = target.stat()
    changed = stat.S_IMODE(metadata.st_mode) != mode
    target.chmod(mode)
    if enforce_owner:
        changed = changed or metadata.st_uid != uid or metadata.st_gid != gid
        os.chown(target, uid, gid)
    metadata = target.stat()
    if stat.S_IMODE(metadata.st_mode) != mode:
        raise HostError(f"host directory mode did not converge: {logical}")
    if enforce_owner and (metadata.st_uid != uid or metadata.st_gid != gid):
        raise HostError(f"host directory ownership did not converge: {logical}")
    return {
        "path": str(logical),
        "mode": f"{mode:04o}",
        "uid": metadata.st_uid,
        "gid": metadata.st_gid,
        "changed": changed,
    }


def converge_host(
    repository: Path,
    contract_path: Path,
    key_path: Path,
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    require_authority(root, authorize_system_root)
    contract = load_json(contract_path)
    validate_contract(contract, repository)
    identity = contract["service_identity"]
    if root == Path("/"):
        service_uid, service_gid, identity_changed = converge_identity(identity)
    else:
        service_uid, service_gid, identity_changed = os.getuid(), os.getgid(), False
    observations: list[dict[str, Any]] = []
    for item in contract["directories"]:
        owner_uid = 0 if item["owner"] == "root" else service_uid
        group_gid = 0 if item["group"] == "root" else service_gid
        if root != Path("/"):
            owner_uid, group_gid = os.getuid(), os.getgid()
        observations.append(
            ensure_directory(
                root,
                Path(item["path"]),
                parse_mode(item["mode"], item["path"]),
                owner_uid,
                group_gid,
                root == Path("/"),
            )
        )
    gateway_receipt = gateway.install_gateway(
        repository,
        repository / contract["modules"]["gateway_contract"],
        key_path,
        root,
        authorize_system_root,
    )
    receipt = {
        "schema": RECEIPT_SCHEMA,
        "phase": "host-ready",
        "service_identity": identity,
        "identity_changed": identity_changed,
        "directories": observations,
        "gateway": gateway_receipt,
        "system_root": root == Path("/"),
        "product_activated": False,
    }
    receipt["receipt_sha256"] = gateway.activation.document_identity(
        receipt, "receipt_sha256"
    )
    return receipt


def execute_product_request(executable: Path, request_path: Path) -> dict[str, Any]:
    if not request_path.is_file() or request_path.is_symlink():
        raise HostError("signed product request is absent or not physical")
    completed = subprocess.run(
        [str(executable), "execute-request"],
        check=False,
        cwd="/",
        env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C", "LC_ALL": "C"},
        input=request_path.read_bytes(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=21600,
    )
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", "replace").strip() or f"exit {completed.returncode}"
        raise HostError(f"whole-product activation failed: {detail}")
    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise HostError("whole-product activation returned invalid JSON") from error
    if result.get("phase") != "product-unicode-and-highway-activated":
        raise HostError("whole-product activation did not reach its terminal phase")
    return result


def ensure_activation_key(key_path: Path, generate: bool) -> bool:
    if key_path.exists() or key_path.is_symlink():
        if not key_path.is_file() or key_path.is_symlink():
            raise HostError("activation key is not one physical file")
        gateway.activation.decode_key(key_path.read_text(encoding="ascii").strip())
        return False
    if not generate:
        raise HostError("activation key is absent; use --generate-key for first install")
    key_path.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
    physical_directory(key_path.parent)
    encoded = base64.b64encode(secrets.token_bytes(32)) + b"\n"
    gateway.activation.atomic_write(key_path, encoded, 0o400)
    gateway.activation.decode_key(key_path.read_text(encoding="ascii").strip())
    return True


def run_service(
    identity: dict[str, Any], command: Sequence[str], timeout: int
) -> bytes:
    completed = subprocess.run(
        ["/usr/sbin/runuser", "--user", identity["user"], "--", *command],
        check=False,
        cwd="/",
        env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C", "LC_ALL": "C"},
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
    )
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", "replace").strip() or f"exit {completed.returncode}"
        raise HostError(f"runner product command failed: {detail}")
    return completed.stdout


def run_service_json(
    identity: dict[str, Any], command: Sequence[str], timeout: int
) -> dict[str, Any]:
    output = run_service(identity, command, timeout)
    try:
        result = json.loads(output)
    except json.JSONDecodeError as error:
        raise HostError("runner product command returned invalid JSON") from error
    if not isinstance(result, dict):
        raise HostError("runner product command did not return an object")
    return result


def prepare_product(
    repository: Path,
    contract: dict[str, Any],
    postgresql_publication: Path,
) -> tuple[Path, Path, dict[str, Any]]:
    if os.geteuid() != 0:
        raise HostError("local product composition requires the administrator entrypoint")
    modules = contract["modules"]
    identity = contract["service_identity"]
    selection = run_service_json(
        identity,
        [
            "/usr/bin/python3",
            str(repository / modules["product_builder"]),
            "--repository",
            str(repository),
            "compose",
            "--postgresql-publication",
            str(postgresql_publication),
        ],
        21600,
    )
    required = {
        "schema", "plan_id", "plan_sha256", "build_directory",
        "stage_directory", "product_receipt", "package_id", "built_new",
    }
    if set(selection) != required:
        raise HostError("product package selection fields differ")
    package_id = gateway.activation.require_hex(
        selection.get("package_id"), gateway.activation.HEX_64, "selected package id"
    )
    product_receipt = Path(selection["product_receipt"])
    package_manifest = Path(selection["build_directory"]) / "package-manifest.json"
    package_source_root = Path(selection["stage_directory"]) / "root"
    gateway_contract = load_json(repository / modules["gateway_contract"])
    receipt_root = Path(gateway_contract["product"]["plan_receipt_root"])
    resource_directory = receipt_root / package_id
    resource_observation = resource_directory / "resource-observation.json"
    run_service(
        identity,
        ["/usr/bin/install", "-d", "-m", "2750", str(resource_directory)],
        60,
    )
    run_service(
        identity,
        [
            "/usr/bin/python3",
            str(repository / modules["cluster_controller"]),
            "observe-resources",
            "--contract",
            str(repository / modules["cluster_contract"]),
            "--package-manifest",
            str(package_manifest),
            "--package-physical-root",
            str(package_source_root),
            "--output",
            str(resource_observation),
        ],
        3600,
    )
    if not product_receipt.is_file() or product_receipt.is_symlink():
        raise HostError("selected product receipt is absent")
    if not resource_observation.is_file() or resource_observation.is_symlink():
        raise HostError("selected resource observation is absent")
    return product_receipt, resource_observation, selection


def execute_local_selection(
    executable: Path,
    product_receipt: Path,
    resource_observation: Path,
) -> dict[str, Any]:
    completed = subprocess.run(
        [
            str(executable),
            "execute-local-selection",
            "--product-receipt",
            str(product_receipt),
            "--resource-observation",
            str(resource_observation),
        ],
        check=False,
        cwd="/",
        env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C", "LC_ALL": "C"},
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=21600,
    )
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", "replace").strip() or f"exit {completed.returncode}"
        raise HostError(f"local whole-product activation failed: {detail}")
    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise HostError("local whole-product activation returned invalid JSON") from error
    if result.get("phase") != "product-unicode-and-highway-activated":
        raise HostError("local whole-product activation did not reach its terminal phase")
    return result


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("converge", "install", "initial-run", "repair"))
    parser.add_argument("--repository", default=str(REPOSITORY))
    parser.add_argument("--contract", default="contracts/product-host.json")
    parser.add_argument("--key-file")
    parser.add_argument("--generate-key", action="store_true")
    parser.add_argument("--request")
    parser.add_argument("--postgresql-publication")
    parser.add_argument("--root", default="/")
    parser.add_argument("--authorize-system-root", action="store_true")
    parser.add_argument("--output", default="-")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    repository = Path(arguments.repository).resolve()
    contract_path = Path(arguments.contract)
    if not contract_path.is_absolute():
        contract_path = repository / contract_path
    root = Path(arguments.root)
    contract = load_json(contract_path)
    gateway_contract = load_json(
        repository / contract["modules"]["gateway_contract"]
    )
    logical_key = Path(gateway_contract["request"]["secret_path"])
    key_path = (
        Path(arguments.key_file)
        if arguments.key_file is not None
        else prefixed(root, logical_key)
    )
    generated_key = ensure_activation_key(key_path, arguments.generate_key)
    receipt = converge_host(
        repository,
        contract_path,
        key_path,
        root,
        arguments.authorize_system_root,
    )
    receipt["activation_key_generated"] = generated_key
    receipt["receipt_sha256"] = gateway.activation.document_identity(
        receipt, "receipt_sha256"
    )
    if arguments.command in {"install", "initial-run", "repair"}:
        if (arguments.request is None) == (arguments.postgresql_publication is None):
            raise HostError(
                f"{arguments.command} requires exactly one --request or --postgresql-publication"
            )
        executable = prefixed(
            root, Path(load_json(contract_path)["modules"]["gateway_executable"])
        )
        if arguments.request is not None:
            receipt["activation"] = execute_product_request(
                executable, Path(arguments.request)
            )
        else:
            product_receipt, resource_observation, selection = prepare_product(
                repository,
                load_json(contract_path),
                Path(arguments.postgresql_publication),
            )
            receipt["package_selection"] = selection
            receipt["activation"] = execute_local_selection(
                executable, product_receipt, resource_observation
            )
        receipt["phase"] = "product-activation-complete"
        receipt["product_activated"] = True
        receipt["receipt_sha256"] = gateway.activation.document_identity(
            receipt, "receipt_sha256"
        )
    content = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if arguments.output == "-":
        sys.stdout.write(content)
    else:
        gateway.activation.atomic_write(
            Path(arguments.output), content.encode("utf-8"), 0o640
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        HostError,
        gateway.InstallError,
        gateway.activation.ActivationGatewayError,
    ) as error:
        print(f"product host: {error}", file=sys.stderr)
        raise SystemExit(1)
