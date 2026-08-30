#!/usr/bin/env python3
"""Prove one current-change Laplace product package without activating the live cluster."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from typing import Any, Mapping, Sequence


PROOF_SCHEMA = "laplace.package-product-proof/v1"
SELECTION_SCHEMA = "laplace.product-package-selection/v1"
PACKAGE_RECEIPT_SCHEMA = "laplace.product-package-receipt/v1"
INSTALLATION_SCHEMA = "laplace.product-package-installation-receipt/v1"
BINDING_SCHEMA = "laplace.package-product-proof-binding/v1"
HEX_64 = re.compile(r"^[0-9a-f]{64}$")
GIT_OBJECT = re.compile(r"^[0-9a-f]{40}$")


class PackageProductProofError(RuntimeError):
    pass


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    output: dict[str, Any] = {}
    for key, value in pairs:
        if key in output:
            raise PackageProductProofError(f"duplicate JSON key: {key}")
        output[key] = value
    return output


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise PackageProductProofError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise PackageProductProofError(f"JSON root must be an object: {path}")
    return value


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def document_identity(document: Mapping[str, Any], field: str) -> str:
    payload = {key: value for key, value in document.items() if key != field}
    return hashlib.sha256(canonical_bytes(payload)).hexdigest()


def require_hex(value: Any, label: str) -> str:
    if not isinstance(value, str) or HEX_64.fullmatch(value) is None:
        raise PackageProductProofError(f"{label} is not a lowercase 256-bit identity")
    return value


def require_git_object(value: Any, label: str) -> str:
    if not isinstance(value, str) or GIT_OBJECT.fullmatch(value) is None:
        raise PackageProductProofError(f"{label} is not an exact Git object identity")
    return value


def require_physical_file(path: Path, label: str) -> None:
    if not path.is_file() or path.is_symlink():
        raise PackageProductProofError(f"{label} is absent or not a physical file: {path}")


def require_absolute_directory(path: Path, label: str) -> None:
    if not path.is_absolute() or not path.is_dir() or path.is_symlink():
        raise PackageProductProofError(f"{label} is not a physical absolute directory: {path}")


def prefixed(root: Path, logical: str) -> Path:
    pure = PurePosixPath(logical)
    if not pure.is_absolute() or ".." in pure.parts:
        raise PackageProductProofError("package logical root is not canonical absolute")
    return root.joinpath(*pure.parts[1:])


def validate_manifest_source(
    manifest: Mapping[str, Any], expected_commit: str, expected_tree: str
) -> None:
    expected_commit = require_git_object(expected_commit, "checked-out repository commit")
    expected_tree = require_git_object(expected_tree, "checked-out repository tree")
    laplace = manifest.get("laplace")
    if (
        not isinstance(laplace, Mapping)
        or laplace.get("repository_commit") != expected_commit
        or laplace.get("repository_tree") != expected_tree
    ):
        raise PackageProductProofError(
            "product package manifest is not bound to the checked-out source identity"
        )


def validate_package_receipt(
    selection: Mapping[str, Any], receipt: Mapping[str, Any], manifest: Mapping[str, Any]
) -> None:
    if selection.get("schema") != SELECTION_SCHEMA:
        raise PackageProductProofError("product selection schema differs")
    package_id = require_hex(selection.get("package_id"), "selection package id")
    require_hex(selection.get("plan_sha256"), "selection plan identity")
    if (
        receipt.get("schema") != PACKAGE_RECEIPT_SCHEMA
        or receipt.get("package_id") != package_id
        or receipt.get("plan_sha256") != selection.get("plan_sha256")
        or receipt.get("activation_eligible") is not True
        or receipt.get("build_input_closure_complete") is not True
        or receipt.get("product_activated") is not False
        or manifest.get("package_id") != package_id
    ):
        raise PackageProductProofError("product package is not exact and activation eligible")
    manifest_path = Path(str(receipt.get("manifest", "")))
    require_physical_file(manifest_path, "product manifest")
    if sha256_file(manifest_path) != receipt.get("manifest_sha256"):
        raise PackageProductProofError("product manifest bytes differ from package receipt")
    physical_root = Path(str(receipt.get("physical_root", "")))
    if not physical_root.is_absolute() or not physical_root.is_dir() or physical_root.is_symlink():
        raise PackageProductProofError("product physical release is absent or unsafe")
    logical_root = manifest.get("root")
    if not isinstance(logical_root, str) or not str(physical_root).endswith(logical_root):
        raise PackageProductProofError("product physical release differs from manifest root")
    if selection.get("product_receipt") != str(receipt.get("_path", selection.get("product_receipt"))):
        raise PackageProductProofError("product selection receipt path differs")


def validate_installation(
    installation: Mapping[str, Any],
    manifest: Mapping[str, Any],
    manifest_sha256: str,
    source_root: Path,
    installation_root: Path,
) -> None:
    package_id = require_hex(manifest.get("package_id"), "manifest package id")
    logical_root = manifest.get("root")
    if not isinstance(logical_root, str):
        raise PackageProductProofError("manifest package root is absent")
    expected_release = prefixed(installation_root, logical_root)
    if (
        installation.get("schema") != INSTALLATION_SCHEMA
        or installation.get("phase") != "installed"
        or installation.get("package_id") != package_id
        or installation.get("package_manifest_sha256") != manifest_sha256
        or installation.get("package_root") != logical_root
        or installation.get("installation_root") != str(installation_root)
        or installation.get("installed_release") != str(expected_release)
        or installation.get("source_physical_root") != str(source_root.resolve())
        or installation.get("source_package_verified") is not True
        or installation.get("installed_package_verified") is not True
        or installation.get("overwrite_performed") is not False
        or installation.get("installation_receipt_sha256")
        != document_identity(installation, "installation_receipt_sha256")
    ):
        raise PackageProductProofError("package installation receipt differs from exact package state")
    require_absolute_directory(expected_release, "installed immutable release")


def run(command: Sequence[str], label: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        list(command),
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or f"exit {completed.returncode}"
        raise PackageProductProofError(f"{label} failed: {detail}")
    return completed


def repository_identity(repository: Path) -> tuple[str, str]:
    commit = run(
        ["git", "-C", str(repository), "rev-parse", "HEAD"],
        "repository commit identity",
    ).stdout.strip()
    tree = run(
        ["git", "-C", str(repository), "rev-parse", "HEAD^{tree}"],
        "repository tree identity",
    ).stdout.strip()
    return (
        require_git_object(commit, "repository commit"),
        require_git_object(tree, "repository tree"),
    )


def store_receipt(store: Path, receipt: Path, durable_root: Path) -> str:
    require_physical_file(store, "native receipt-store executable")
    if not os.access(store, os.X_OK):
        raise PackageProductProofError("native receipt-store executable is not executable")
    require_physical_file(receipt, "receipt to retain")
    completed = run(
        [str(store), "put", "--receipt", str(receipt), "--root", str(durable_root)],
        "durable BLAKE3 receipt publication",
    )
    digest = completed.stdout.strip()
    require_hex(digest, "durable receipt BLAKE3")
    run(
        [str(store), "verify", "--digest", digest, "--root", str(durable_root)],
        "durable BLAKE3 receipt replay",
    )
    return digest


def prove(
    repository: Path,
    postgresql_publication: Path,
    work_root: Path,
    output: Path,
) -> dict[str, Any]:
    repository = repository.resolve()
    require_absolute_directory(repository, "repository")
    source_commit, source_tree = repository_identity(repository)
    require_physical_file(postgresql_publication, "PostgreSQL publication receipt")
    if not work_root.is_absolute() or work_root.exists():
        raise PackageProductProofError("proof work root must be a fresh absolute path")
    work_root.mkdir(parents=True, mode=0o700)
    if not output.is_absolute() or not output.parent.is_dir() or output.exists():
        raise PackageProductProofError("proof output must be a fresh file under an existing absolute directory")

    selection_path = work_root / "selection.json"
    run(
        [
            sys.executable,
            str(repository / "tools/product/build-package.py"),
            "--repository",
            str(repository),
            "compose",
            "--postgresql-publication",
            str(postgresql_publication),
            "--output",
            str(selection_path),
        ],
        "product package composition",
    )
    selection = load_json(selection_path)
    receipt_path = Path(str(selection.get("product_receipt", "")))
    require_physical_file(receipt_path, "product package receipt")
    receipt = load_json(receipt_path)
    receipt_with_path = dict(receipt)
    receipt_with_path["_path"] = str(receipt_path)
    manifest_path = Path(str(receipt.get("manifest", "")))
    require_physical_file(manifest_path, "product package manifest")
    manifest = load_json(manifest_path)
    validate_package_receipt(selection, receipt_with_path, manifest)
    validate_manifest_source(manifest, source_commit, source_tree)

    stage_directory = Path(str(selection.get("stage_directory", "")))
    source_root = stage_directory / "root"
    require_absolute_directory(source_root, "product package source root")
    expected_physical = prefixed(source_root, str(manifest["root"]))
    if Path(str(receipt.get("physical_root", ""))) != expected_physical:
        raise PackageProductProofError("package source root does not reconstruct its physical release")

    installation_root = work_root / "installation-root"
    installation_root.mkdir(mode=0o700)
    installation_receipt = work_root / "installation.json"
    clusterctl = repository / "tools/postgresql/clusterctl.py"
    cluster_contract = repository / "contracts/postgresql-cluster.json"
    install_command = [
        sys.executable,
        str(clusterctl),
        "install-package",
        "--contract",
        str(cluster_contract),
        "--package-manifest",
        str(manifest_path),
        "--package-physical-root",
        str(source_root),
        "--root",
        str(installation_root),
        "--receipt",
        str(installation_receipt),
    ]
    run(install_command, "isolated immutable package installation")
    installation = load_json(installation_receipt)
    manifest_sha = sha256_file(manifest_path)
    validate_installation(
        installation, manifest, manifest_sha, source_root, installation_root
    )

    replay_receipt = work_root / "installation-replay.json"
    replay_command = list(install_command)
    replay_command[-1] = str(replay_receipt)
    run(replay_command, "idempotent isolated package installation replay")
    replay = load_json(replay_receipt)
    validate_installation(replay, manifest, manifest_sha, source_root, installation_root)
    if installation_receipt.read_bytes() != replay_receipt.read_bytes():
        raise PackageProductProofError("idempotent installation replay changed receipt bytes")

    package_id = require_hex(manifest.get("package_id"), "package id")
    store = expected_physical / "bin/laplace_receipt_store"
    gateway = load_json(repository / "contracts/product-activation-gateway.json")
    plan_receipt_root = Path(str(gateway.get("product", {}).get("plan_receipt_root", "")))
    if not plan_receipt_root.is_absolute() or plan_receipt_root.name != "plans":
        raise PackageProductProofError("product plan receipt authority is invalid")
    durable_root = plan_receipt_root.parent / "package-product"
    durable_root.mkdir(parents=True, exist_ok=True, mode=0o2750)
    require_absolute_directory(durable_root, "durable package-product receipt root")

    package_blake3 = store_receipt(store, receipt_path, durable_root)
    installation_blake3 = store_receipt(store, installation_receipt, durable_root)
    binding = {
        "schema": BINDING_SCHEMA,
        "repository_commit": source_commit,
        "repository_tree": source_tree,
        "package_id": package_id,
        "plan_sha256": require_hex(selection.get("plan_sha256"), "product plan id"),
        "package_manifest_sha256": manifest_sha,
        "package_receipt_sha256": sha256_file(receipt_path),
        "package_receipt_blake3": package_blake3,
        "installation_receipt_sha256": sha256_file(installation_receipt),
        "installation_receipt_blake3": installation_blake3,
        "installation_identity_sha256": installation["installation_receipt_sha256"],
    }
    binding_path = work_root / "binding.json"
    with binding_path.open("xb") as stream:
        stream.write(canonical_bytes(binding))
        stream.flush()
        os.fsync(stream.fileno())
    binding_blake3 = store_receipt(store, binding_path, durable_root)

    result = {
        "schema": PROOF_SCHEMA,
        "phase": "composed-installed-retained",
        "repository_commit": source_commit,
        "repository_tree": source_tree,
        "package_id": package_id,
        "plan_sha256": selection["plan_sha256"],
        "built_new": selection.get("built_new") is True,
        "product_receipt": str(receipt_path),
        "package_manifest": str(manifest_path),
        "package_manifest_sha256": manifest_sha,
        "package_receipt_blake3": package_blake3,
        "installation_receipt_blake3": installation_blake3,
        "binding_receipt_blake3": binding_blake3,
        "durable_receipt_root": str(durable_root),
        "source_package_verified": installation["source_package_verified"],
        "installed_package_verified": installation["installed_package_verified"],
        "installation_replay_identical": True,
        "installed_release": installation["installed_release"],
    }
    with output.open("xb") as stream:
        stream.write(canonical_bytes(result))
        stream.flush()
        os.fsync(stream.fileno())
    return result


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=".")
    parser.add_argument("--postgresql-publication", required=True)
    parser.add_argument("--work-root", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_args(argv)
    prove(
        Path(arguments.repository),
        Path(arguments.postgresql_publication),
        Path(arguments.work_root),
        Path(arguments.output),
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except PackageProductProofError as error:
        print(f"package-product-proof: {error}", file=sys.stderr)
        raise SystemExit(1) from error
