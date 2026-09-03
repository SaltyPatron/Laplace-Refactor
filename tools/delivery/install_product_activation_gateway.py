#!/usr/bin/env python3
"""Install the immutable root side of Laplace product activation exactly once."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import tempfile
from typing import Any, Sequence


MODULE_PATH = Path(__file__).with_name("product_activation.py")
SPEC = importlib.util.spec_from_file_location(
    "laplace_product_activation_installer", MODULE_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product activation implementation")
activation = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = activation
SPEC.loader.exec_module(activation)


class InstallError(RuntimeError):
    """The root gateway could not be installed without weakening its boundary."""


SOURCE_MAP = {
    "bin/laplace-product-activate": "tools/delivery/product_activation_gateway.py",
    "controllers/clusterctl.py": "tools/postgresql/clusterctl.py",
    "controllers/highwayctl.py": "tools/postgresql/highwayctl.py",
    "controllers/hostctl.py": "tools/postgresql/hostctl.py",
    "controllers/product_activation_impl.py": "tools/delivery/product_activation.py",
    "controllers/product_service_state.py": "tools/delivery/product_service_state.py",
    "controllers/unicodectl.py": "tools/postgresql/unicodectl.py",
    "contracts/highway-product-activation.json": "contracts/highway-product-activation.json",
    "contracts/highway.json": "contracts/highway.json",
    "contracts/history/highway-v1.json": "contracts/history/highway-v1.json",
    "contracts/instances/refactor.json": "contracts/instances/refactor.json",
    "contracts/postgresql-cluster.json": "contracts/postgresql-cluster.json",
    "contracts/postgresql-host.json": "contracts/postgresql-host.json",
    "contracts/product-activation-gateway.json": "contracts/product-activation-gateway.json",
    "contracts/unicode-postgresql.json": "contracts/unicode-postgresql.json",
    "contracts/unicode-product-activation.json": "contracts/unicode-product-activation.json",
    "contracts/unicode-source.json": "contracts/unicode-source.json",
}


def prefixed(root: Path, logical: Path) -> Path:
    if not logical.is_absolute():
        raise InstallError("gateway installation path must be absolute")
    return root.joinpath(*logical.parts[1:]) if root != Path("/") else logical


def require_authority(root: Path, authorize_system_root: bool) -> None:
    if not root.is_absolute():
        raise InstallError("installation root must be absolute")
    if root == Path("/") and (not authorize_system_root or os.geteuid() != 0):
        raise InstallError(
            "system gateway installation requires root and --authorize-system-root"
        )


def exact_sources(
    repository: Path, contract: dict[str, Any]
) -> list[tuple[str, Path, int]]:
    expected = contract["trusted_bundle"]["files"]
    if set(expected) != set(SOURCE_MAP):
        raise InstallError("trusted bundle contract and installer source map differ")
    result: list[tuple[str, Path, int]] = []
    for relative in expected:
        source = repository / SOURCE_MAP[relative]
        if not source.is_file() or source.is_symlink():
            raise InstallError(f"gateway source is absent or not physical: {source}")
        mode = 0o555 if relative.startswith(("bin/", "controllers/")) else 0o444
        result.append((relative, source, mode))
    return result


def bundle_manifest(sources: list[tuple[str, Path, int]]) -> dict[str, Any]:
    files = [
        {"path": relative, "sha256": activation.sha256_file(source)}
        for relative, source, _mode in sources
    ]
    core = {
        "schema": "laplace.product-activation-gateway-bundle/v1",
        "files": files,
    }
    core["bundle_id"] = activation.sha256_bytes(activation.canonical_bytes(core))
    return core


def sudoers_content(contract: dict[str, Any]) -> bytes:
    user = contract["repository"]["runner_user"]
    executable = contract["gateway"]["executable"]
    lines = [
        f"{user} ALL=(root) NOPASSWD: {executable} probe",
        f"{user} ALL=(root) NOPASSWD: {executable} execute-request",
    ]
    return ("\n".join(lines) + "\n").encode("utf-8")


def verify_sudoers(path: Path, root: Path) -> None:
    if root != Path("/"):
        content = path.read_text(encoding="utf-8")
        if " ALL=(root) NOPASSWD: " not in content or content.count("\n") != 2:
            raise InstallError("fixture sudoers policy differs")
        return
    visudo = Path("/usr/sbin/visudo")
    if not visudo.is_file():
        raise InstallError("visudo is unavailable")
    completed = subprocess.run(
        [str(visudo), "-c", "-f", str(path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
    )
    if completed.returncode != 0:
        raise InstallError(
            f"generated sudoers policy is invalid: {completed.stderr.strip()}"
        )


def secure_owner(path: Path, root: Path) -> None:
    if root != Path("/"):
        return
    for candidate in [path, *path.rglob("*")]:
        os.chown(candidate, 0, 0, follow_symlinks=False)


def service_state_contract(contract: dict[str, Any]) -> dict[str, Any]:
    state = contract.get("service_state")
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
        raise InstallError("service-state contract fields differ")
    for name in (
        "controller",
        "cluster_unit",
        "enablement_receipt",
        "boot_readback_receipt",
        "boot_id_path",
    ):
        value = Path(state[name])
        if not value.is_absolute() or ".." in PurePosixPath(str(value)).parts:
            raise InstallError(f"service-state path is unsafe: {name}")
    if state["controller"] != (
        f"{contract['gateway']['active_link']}/controllers/product_service_state.py"
    ):
        raise InstallError("service-state controller differs from the active bundle")
    if state["cluster_unit"] != f"/etc/systemd/system/{state['cluster_service']}":
        raise InstallError("service-state cluster unit path differs")
    return state


def lifecycle_units(contract: dict[str, Any]) -> dict[Path, bytes]:
    state = service_state_contract(contract)
    python = contract["gateway"]["python"]
    controller = state["controller"]
    highway_result = contract["product"]["highway_result"]
    receipt_directory = str(Path(state["enablement_receipt"]).parent)
    path_unit = f"""# Generated by the root-owned Laplace product activation gateway.
[Unit]
Description=Observe completed Laplace refactor product activation

[Path]
PathChanged={highway_result}
Unit={state['enable_service_unit']}

[Install]
WantedBy=multi-user.target
"""
    enable_service = f"""# Generated by the root-owned Laplace product activation gateway.
[Unit]
Description=Enable the exact activated Laplace refactor PostgreSQL generation for boot
After=local-fs.target {state['cluster_service']}
Wants={state['cluster_service']}
StartLimitIntervalSec=0
ConditionPathExists={highway_result}

[Service]
Type=oneshot
User=root
Group=root
ExecStart={python} {controller} enable
TimeoutStartSec=900
Restart=on-failure
RestartSec=15
UMask=0077
NoNewPrivileges=yes
PrivateTmp=yes
ProtectHome=yes
ProtectSystem=strict
ReadWritePaths=/etc/systemd/system {receipt_directory}
RestrictAddressFamilies=AF_UNIX

[Install]
WantedBy=multi-user.target
"""
    boot_readback = f"""# Generated by the root-owned Laplace product activation gateway.
[Unit]
Description=Prove the exact Laplace refactor PostgreSQL product after host boot
After=local-fs.target {state['cluster_service']}
Requires={state['cluster_service']}
ConditionPathExists={state['enablement_receipt']}
ConditionPathExists={contract['product']['highway_result']}
ConditionPathExists={contract['product']['unicode_result']}

[Service]
Type=oneshot
User=root
Group=root
ExecStart={python} {controller} boot-readback
TimeoutStartSec=900
RemainAfterExit=yes
UMask=0077
NoNewPrivileges=yes
PrivateTmp=yes
ProtectHome=yes
ProtectSystem=strict
ReadWritePaths={receipt_directory}
RestrictAddressFamilies=AF_UNIX

[Install]
WantedBy=multi-user.target
"""
    return {
        Path(f"/etc/systemd/system/{state['enable_path_unit']}"): path_unit.encode(
            "utf-8"
        ),
        Path(f"/etc/systemd/system/{state['enable_service_unit']}"): enable_service.encode(
            "utf-8"
        ),
        Path(f"/etc/systemd/system/{state['boot_readback_unit']}"): boot_readback.encode(
            "utf-8"
        ),
    }


def install_policy_file(
    path: Path, content: bytes, mode: int, root: Path
) -> bool:
    if path.exists() or path.is_symlink():
        if not path.is_file() or path.is_symlink():
            raise InstallError(f"existing gateway policy path is unsafe: {path}")
        metadata = path.stat()
        if root == Path("/") and (
            metadata.st_uid != 0
            or metadata.st_gid != 0
            or metadata.st_mode & 0o022
        ):
            raise InstallError(f"existing gateway policy ownership is unsafe: {path}")
        if path.read_bytes() == content and stat.S_IMODE(metadata.st_mode) == mode:
            return False
    activation.atomic_write(path, content, mode)
    if root == Path("/"):
        os.chown(path, 0, 0)
    return True


def run_systemctl(label: str, *arguments: str) -> dict[str, Any]:
    command = ["/usr/bin/systemctl", *arguments]
    completed = subprocess.run(
        command,
        check=False,
        cwd="/",
        env={
            "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
            "LANG": "C",
            "LC_ALL": "C",
        },
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=120,
    )
    receipt = {
        "label": label,
        "argv": command,
        "exit_code": completed.returncode,
        "stdout_sha256": activation.sha256_bytes(completed.stdout.encode("utf-8")),
        "stderr_sha256": activation.sha256_bytes(completed.stderr.encode("utf-8")),
    }
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic"
        raise InstallError(f"{label} failed with exit {completed.returncode}: {detail}")
    return receipt


def install_lifecycle_units(
    contract: dict[str, Any], root: Path
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], bool]:
    units = lifecycle_units(contract)
    installed: list[dict[str, Any]] = []
    changed = False
    for logical, content in units.items():
        target = prefixed(root, logical)
        target.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
        changed = install_policy_file(target, content, 0o644, root) or changed
        installed.append(
            {
                "path": str(logical),
                "sha256": activation.sha256_bytes(content),
            }
        )
    commands: list[dict[str, Any]] = []
    if root == Path("/"):
        state = service_state_contract(contract)
        commands.append(run_systemctl("reload-systemd-gateway-policy", "daemon-reload"))
        commands.append(
            run_systemctl(
                "enable-product-service-state-monitor",
                "enable",
                "--now",
                state["enable_path_unit"],
            )
        )
        if Path(contract["product"]["highway_result"]).is_file():
            commands.append(
                run_systemctl(
                    "reconcile-existing-completed-product",
                    "start",
                    state["enable_service_unit"],
                )
            )
        for action, expected in (("is-enabled", "enabled"), ("is-active", "active")):
            command = ["/usr/bin/systemctl", action, state["enable_path_unit"]]
            completed = subprocess.run(
                command,
                check=False,
                cwd="/",
                env={
                    "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
                    "LANG": "C",
                    "LC_ALL": "C",
                },
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=60,
            )
            if completed.returncode != 0 or completed.stdout.strip() != expected:
                raise InstallError(
                    f"product service-state monitor {action} did not report {expected}"
                )
            commands.append(
                {
                    "label": f"verify-product-service-state-monitor-{action}",
                    "argv": command,
                    "exit_code": completed.returncode,
                    "stdout_sha256": activation.sha256_bytes(
                        completed.stdout.encode("utf-8")
                    ),
                    "stderr_sha256": activation.sha256_bytes(
                        completed.stderr.encode("utf-8")
                    ),
                }
            )
    return installed, commands, changed


def install_gateway(
    repository: Path,
    contract_path: Path,
    key_path: Path,
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    require_authority(root, authorize_system_root)
    repository = repository.resolve()
    contract = activation.load_json(contract_path)
    activation.validate_contract(contract)
    service_state_contract(contract)
    sources = exact_sources(repository, contract)
    manifest = bundle_manifest(sources)
    bundle_id = manifest["bundle_id"]
    release_root = prefixed(root, Path(contract["gateway"]["release_root"]))
    release = release_root / bundle_id
    active = prefixed(root, Path(contract["gateway"]["active_link"]))
    secret = prefixed(root, Path(contract["request"]["secret_path"]))
    sudoers = prefixed(root, Path(contract["gateway"]["sudoers_path"]))
    receipt_root = prefixed(root, Path(contract["gateway"]["receipt_root"]))
    key_text = key_path.read_text(encoding="ascii").strip()
    activation.decode_key(key_text)

    release_root.mkdir(parents=True, exist_ok=True, mode=0o755)
    installed_new = False
    if not release.exists():
        temporary = Path(
            tempfile.mkdtemp(prefix=f".{bundle_id}.", dir=release_root)
        )
        try:
            temporary.chmod(0o755)
            for relative, source, mode in sources:
                destination = temporary.joinpath(*PurePosixPath(relative).parts)
                destination.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
                shutil.copyfile(source, destination)
                destination.chmod(mode)
            manifest_path = temporary / "bundle-manifest.json"
            manifest_path.write_bytes(activation.canonical_bytes(manifest))
            manifest_path.chmod(0o444)
            secure_owner(temporary, root)
            os.replace(temporary, release)
            installed_new = True
        except BaseException:
            shutil.rmtree(temporary, ignore_errors=True)
            raise
    executable = release / "bin/laplace-product-activate"
    verified_bundle, verified_manifest = activation.verify_installed_bundle(
        executable, require_root_ownership=root == Path("/")
    )
    if verified_bundle != release or verified_manifest != manifest:
        raise InstallError("installed gateway bundle differs from its source manifest")

    secret.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
    if secret.exists():
        if (
            not secret.is_file()
            or secret.is_symlink()
            or secret.read_text(encoding="ascii").strip() != key_text
        ):
            raise InstallError("existing product activation key differs")
    else:
        activation.atomic_write(secret, (key_text + "\n").encode("ascii"), 0o400)
    if root == Path("/"):
        os.chown(secret, 0, 0)

    sudoers.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
    desired_sudoers = sudoers_content(contract)
    if sudoers.exists() and (
        not sudoers.is_file()
        or sudoers.is_symlink()
        or sudoers.read_bytes() != desired_sudoers
    ):
        raise InstallError("existing product activation sudoers policy differs")
    if not sudoers.exists():
        activation.atomic_write(sudoers, desired_sudoers, 0o440)
    if root == Path("/"):
        os.chown(sudoers, 0, 0)
    verify_sudoers(sudoers, root)

    receipt_root.mkdir(parents=True, exist_ok=True, mode=0o750)
    receipt_root.chmod(0o750)
    if root == Path("/"):
        os.chown(receipt_root, 0, 0)

    relative_target = os.path.relpath(release, active.parent)
    previous_active_target: str | None = None
    pointer_changed = False
    if active.is_symlink() and os.readlink(active) == relative_target:
        pass
    else:
        if active.exists() and not active.is_symlink():
            raise InstallError("existing activation gateway pointer is not a symlink")
        if active.is_symlink():
            previous_active_target = os.readlink(active)
            try:
                existing_release = active.resolve(strict=True)
                existing_release.relative_to(release_root.resolve())
            except (OSError, ValueError) as error:
                raise InstallError(
                    "existing activation gateway pointer escapes its release root"
                ) from error
            activation.verify_installed_bundle(
                existing_release / "bin/laplace-product-activate",
                require_root_ownership=root == Path("/"),
            )
        active.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
        temporary_link = active.parent / f".{active.name}.{bundle_id[:16]}"
        temporary_link.unlink(missing_ok=True)
        os.symlink(relative_target, temporary_link)
        os.replace(temporary_link, active)
        pointer_changed = True

    lifecycle, lifecycle_commands, lifecycle_changed = install_lifecycle_units(
        contract, root
    )
    receipt = {
        "schema": "laplace.product-activation-gateway-installation/v1",
        "bundle_id": bundle_id,
        "release": str(Path(contract["gateway"]["release_root"]) / bundle_id),
        "active_link": contract["gateway"]["active_link"],
        "executable": contract["gateway"]["executable"],
        "sudoers_sha256": activation.sha256_bytes(desired_sudoers),
        "secret_sha256": activation.sha256_bytes(
            (key_text + "\n").encode("ascii")
        ),
        "installed_new": installed_new,
        "previous_active_target": previous_active_target,
        "active_pointer_changed": pointer_changed,
        "root_owned": root == Path("/"),
        "lifecycle_units": lifecycle,
        "lifecycle_units_changed": lifecycle_changed,
        "lifecycle_command_receipts": lifecycle_commands,
        "service_state_monitor_enabled": root == Path("/"),
    }
    receipt["receipt_sha256"] = activation.document_identity(
        receipt, "receipt_sha256"
    )
    return receipt


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repository", default=str(Path(__file__).resolve().parents[2])
    )
    parser.add_argument(
        "--contract", default="contracts/product-activation-gateway.json"
    )
    parser.add_argument("--key-file", required=True)
    parser.add_argument("--root", default="/")
    parser.add_argument("--authorize-system-root", action="store_true")
    parser.add_argument("--output", default="-")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    repository = Path(arguments.repository)
    contract_path = Path(arguments.contract)
    if not contract_path.is_absolute():
        contract_path = repository / contract_path
    receipt = install_gateway(
        repository,
        contract_path,
        Path(arguments.key_file),
        Path(arguments.root),
        arguments.authorize_system_root,
    )
    content = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if arguments.output == "-":
        sys.stdout.write(content)
    else:
        activation.atomic_write(Path(arguments.output), content.encode("utf-8"))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (InstallError, activation.ActivationGatewayError) as error:
        print(f"product activation gateway installation: {error}", file=sys.stderr)
        raise SystemExit(1)
