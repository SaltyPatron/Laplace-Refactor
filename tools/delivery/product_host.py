#!/usr/bin/env python3
"""Converge the Laplace host boundary and dispatch an exact product request."""

from __future__ import annotations

import argparse
import grp
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import pwd
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


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("converge", "install", "initial-run", "repair"))
    parser.add_argument("--repository", default=str(REPOSITORY))
    parser.add_argument("--contract", default="contracts/product-host.json")
    parser.add_argument("--key-file", required=True)
    parser.add_argument("--request")
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
    receipt = converge_host(
        repository,
        contract_path,
        Path(arguments.key_file),
        root,
        arguments.authorize_system_root,
    )
    if arguments.command in {"install", "initial-run", "repair"}:
        if arguments.request is None:
            raise HostError(
                f"{arguments.command} requires one signed whole-product --request"
            )
        executable = prefixed(
            root, Path(load_json(contract_path)["modules"]["gateway_executable"])
        )
        receipt["activation"] = execute_product_request(
            executable, Path(arguments.request)
        )
        receipt["phase"] = "product-ready"
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
