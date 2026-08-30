#!/usr/bin/env python3
"""Install the immutable root side of Laplace product activation exactly once."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any, Sequence


MODULE_PATH = Path(__file__).with_name("product_activation.py")
SPEC = importlib.util.spec_from_file_location("laplace_product_activation_installer", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product activation implementation")
activation = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = activation
SPEC.loader.exec_module(activation)


class InstallError(RuntimeError):
    """The root gateway could not be installed without weakening its boundary."""


SOURCE_MAP = {
    "bin/laplace-product-activate": "tools/delivery/product_activation.py",
    "controllers/clusterctl.py": "tools/postgresql/clusterctl.py",
    "controllers/highwayctl.py": "tools/postgresql/highwayctl.py",
    "controllers/unicodectl.py": "tools/postgresql/unicodectl.py",
    "contracts/highway-product-activation.json": "contracts/highway-product-activation.json",
    "contracts/highway.json": "contracts/highway.json",
    "contracts/history/highway-v1.json": "contracts/history/highway-v1.json",
    "contracts/postgresql-cluster.json": "contracts/postgresql-cluster.json",
    "contracts/product-activation-gateway.json": "contracts/product-activation-gateway.json",
    "contracts/unicode-postgresql.json": "contracts/unicode-postgresql.json",
    "contracts/unicode-product-activation.json": "contracts/unicode-product-activation.json",
    "contracts/unicode-source.json": "contracts/unicode-source.json",
}
BOOTSTRAP_SOURCE = "tools/delivery/install_product_activation_gateway.py"


def prefixed(root: Path, logical: Path) -> Path:
    if not logical.is_absolute():
        raise InstallError("gateway installation path must be absolute")
    return root.joinpath(*logical.parts[1:]) if root != Path("/") else logical


def require_authority(root: Path, authorize_system_root: bool) -> None:
    if not root.is_absolute():
        raise InstallError("installation root must be absolute")
    if root == Path("/") and (not authorize_system_root or os.geteuid() != 0):
        raise InstallError("system gateway installation requires root and --authorize-system-root")


def git_command(repository: Path, arguments: Sequence[str]) -> str:
    completed = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise InstallError(f"cannot verify gateway source repository: {detail}")
    return completed.stdout.strip()


def verify_repository_binding(repository: Path, expected_commit: str) -> dict[str, Any]:
    expected = expected_commit.strip().lower()
    if re.fullmatch(r"[0-9a-f]{40}", expected) is None:
        raise InstallError("expected repository commit must be a full 40-character Git commit SHA")
    repository = repository.resolve()
    top_level = Path(git_command(repository, ["rev-parse", "--show-toplevel"])).resolve()
    if top_level != repository:
        raise InstallError("gateway source repository must be the exact Git worktree root")
    commit = git_command(repository, ["rev-parse", "--verify", "HEAD^{commit}"]).lower()
    if commit != expected:
        raise InstallError(f"expected repository commit {expected} but checkout is {commit}")
    tree = git_command(repository, ["rev-parse", "HEAD^{tree}"]).lower()
    trusted_paths = sorted(set(SOURCE_MAP.values()) | {BOOTSTRAP_SOURCE})
    for relative in trusted_paths:
        committed_blob = git_command(repository, ["rev-parse", f"HEAD:{relative}"]).lower()
        source = repository / relative
        if not source.is_file() or source.is_symlink():
            raise InstallError(f"trusted gateway source is absent or not physical: {relative}")
        working_blob = git_command(repository, ["hash-object", "--no-filters", relative]).lower()
        if working_blob != committed_blob:
            raise InstallError(
                f"trusted gateway source bytes differ from reviewed commit: {relative}"
            )
    status = git_command(
        repository,
        ["status", "--porcelain=v1", "--untracked-files=all", "--", *trusted_paths],
    )
    if status:
        raise InstallError("trusted gateway source checkout is not clean at the expected repository commit")
    return {
        "commit": commit,
        "tree": tree,
        "trusted_paths": trusted_paths,
    }


def exact_sources(repository: Path, contract: dict[str, Any]) -> list[tuple[str, Path, int]]:
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
        [str(visudo), "-c", "-f", str(path)], check=False,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=30,
    )
    if completed.returncode != 0:
        raise InstallError(f"generated sudoers policy is invalid: {completed.stderr.strip()}")


def secure_owner(path: Path, root: Path) -> None:
    if root != Path("/"):
        return
    for candidate in [path, *path.rglob("*")]:
        os.chown(candidate, 0, 0, follow_symlinks=False)


def install_gateway(
    repository: Path,
    contract_path: Path,
    key_path: Path,
    root: Path,
    authorize_system_root: bool,
    expected_commit: str | None = None,
) -> dict[str, Any]:
    require_authority(root, authorize_system_root)
    repository = repository.resolve()
    contract_path = contract_path.resolve()
    canonical_contract_path = (repository / SOURCE_MAP["contracts/product-activation-gateway.json"]).resolve()
    if contract_path != canonical_contract_path:
        raise InstallError("gateway installation contract must be the canonical repository contract")
    source_binding: dict[str, Any] | None = None
    if root == Path("/"):
        if expected_commit is None:
            raise InstallError("system gateway installation requires --expected-commit")
        source_binding = verify_repository_binding(repository, expected_commit)
    elif expected_commit is not None:
        source_binding = verify_repository_binding(repository, expected_commit)
    contract = activation.load_json(contract_path)
    activation.validate_contract(contract)
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
        temporary = Path(tempfile.mkdtemp(prefix=f".{bundle_id}.", dir=release_root))
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
        if not secret.is_file() or secret.is_symlink() or secret.read_text(encoding="ascii").strip() != key_text:
            raise InstallError("existing product activation key differs")
    else:
        activation.atomic_write(secret, (key_text + "\n").encode("ascii"), 0o400)
    if root == Path("/"):
        os.chown(secret, 0, 0)

    sudoers.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
    desired_sudoers = sudoers_content(contract)
    if sudoers.exists() and (not sudoers.is_file() or sudoers.is_symlink() or sudoers.read_bytes() != desired_sudoers):
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
    if active.is_symlink() and os.readlink(active) == relative_target:
        pass
    elif active.exists() or active.is_symlink():
        raise InstallError("existing activation gateway pointer differs")
    else:
        active.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
        temporary_link = active.parent / f".{active.name}.{bundle_id[:16]}"
        os.symlink(relative_target, temporary_link)
        os.replace(temporary_link, active)
    receipt = {
        "schema": "laplace.product-activation-gateway-installation/v1",
        "bundle_id": bundle_id,
        "release": str(Path(contract["gateway"]["release_root"]) / bundle_id),
        "active_link": contract["gateway"]["active_link"],
        "executable": contract["gateway"]["executable"],
        "sudoers_sha256": activation.sha256_bytes(desired_sudoers),
        "secret_sha256": activation.sha256_bytes((key_text + "\n").encode("ascii")),
        "installed_new": installed_new,
        "root_owned": root == Path("/"),
        "source_verified": source_binding is not None,
        "source_commit": None if source_binding is None else source_binding["commit"],
        "source_tree": None if source_binding is None else source_binding["tree"],
    }
    receipt["receipt_sha256"] = activation.document_identity(receipt, "receipt_sha256")
    return receipt


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument("--contract", default="contracts/product-activation-gateway.json")
    parser.add_argument("--key-file", required=True)
    parser.add_argument("--root", default="/")
    parser.add_argument("--authorize-system-root", action="store_true")
    parser.add_argument("--expected-commit")
    parser.add_argument("--output", default="-")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    repository = Path(arguments.repository)
    contract_path = Path(arguments.contract)
    if not contract_path.is_absolute():
        contract_path = repository / contract_path
    receipt = install_gateway(
        repository, contract_path, Path(arguments.key_file), Path(arguments.root),
        arguments.authorize_system_root, arguments.expected_commit,
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
