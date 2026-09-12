#!/usr/bin/env python3
"""Converge obsolete receipt layouts into package-addressed activation evidence.

The current persistent product authority is one package-addressed tree under the
PostgreSQL instance receipt root:

    <receipt_directory>/cluster-activation/<package-id>/

Older delivery generations wrote the same product generation across several competing
namespaces (`/opt/laplace/receipts/packages`, `/opt/laplace/receipts/plans`, the
instance-local `plan`, `release-capacity`, and `product-cognition` trees, plus singleton
Unicode/Highway product receipts). This controller moves only mechanically identified
legacy evidence into the canonical package tree and removes a legacy source only after
byte-exact publication. Unknown files, symlinks, malformed identities, and conflicting
canonical bytes are preserved fail-closed.

The obsolete root-owned `/opt/laplace/receipts/deployments` directory is removed only
when it is a physical empty directory and system-root authority is explicitly granted.
"""

from __future__ import annotations

import argparse
import grp
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import pwd
import re
import stat
import sys
import tempfile
from typing import Any, Sequence


SCHEMA = "laplace.receipt-estate-convergence/v1"
INSTALLATION_SCHEMA = "laplace.product-package-installation-receipt/v1"
HEX_256 = re.compile(r"^[0-9a-f]{64}$")


class ReceiptEstateError(RuntimeError):
    pass


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ReceiptEstateError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ReceiptEstateError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ReceiptEstateError(f"JSON root must be an object: {path}")
    return value


def prefixed(root: Path, logical: Path) -> Path:
    if not logical.is_absolute() or ".." in PurePosixPath(str(logical)).parts:
        raise ReceiptEstateError(f"receipt path is not canonical absolute: {logical}")
    return logical if root == Path("/") else root.joinpath(*logical.parts[1:])


def physical_directory(path: Path, label: str) -> None:
    if path.is_symlink() or not path.is_dir():
        raise ReceiptEstateError(f"{label} is not a physical directory: {path}")


def fsync_directory(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def service_ids(root: Path, user: str, group: str) -> tuple[int, int]:
    if root != Path("/"):
        return os.getuid(), os.getgid()
    try:
        account = pwd.getpwnam(user)
        team = grp.getgrnam(group)
    except KeyError as error:
        raise ReceiptEstateError("receipt service identity is absent") from error
    if account.pw_gid != team.gr_gid:
        raise ReceiptEstateError("receipt service user primary group differs")
    return account.pw_uid, team.gr_gid


def ensure_directory(path: Path, uid: int, gid: int, system_root: bool) -> None:
    path.mkdir(parents=True, exist_ok=True, mode=0o750)
    physical_directory(path, "canonical receipt directory")
    path.chmod(0o750)
    if system_root:
        os.chown(path, uid, gid)


def publish_exact(
    source: Path,
    destination: Path,
    uid: int,
    gid: int,
    system_root: bool,
) -> str:
    if source.is_symlink() or not source.is_file():
        raise ReceiptEstateError(f"legacy receipt is not a physical file: {source}")
    payload = source.read_bytes()
    if not payload:
        raise ReceiptEstateError(f"legacy receipt is empty: {source}")
    ensure_directory(destination.parent, uid, gid, system_root)
    if destination.exists() or destination.is_symlink():
        if destination.is_symlink() or not destination.is_file():
            raise ReceiptEstateError(
                f"canonical receipt destination is unsafe: {destination}"
            )
        if destination.read_bytes() != payload:
            raise ReceiptEstateError(
                f"legacy receipt conflicts with canonical evidence: {source} -> {destination}"
            )
        source.unlink()
        fsync_directory(source.parent)
        return "deduplicated"
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.", dir=destination.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(0o640)
        if system_root:
            os.chown(temporary, uid, gid)
        os.replace(temporary, destination)
        fsync_directory(destination.parent)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    source.unlink()
    fsync_directory(source.parent)
    return "migrated"


def package_id_from_document(path: Path, expected: str | None = None) -> str:
    document = load_json(path)
    package_id = document.get("package_id")
    if not isinstance(package_id, str) or HEX_256.fullmatch(package_id) is None:
        raise ReceiptEstateError(f"receipt has no canonical package identity: {path}")
    if expected is not None and package_id != expected:
        raise ReceiptEstateError(
            f"receipt package identity differs from its legacy address: {path}"
        )
    return package_id


def remove_if_empty(path: Path, removed: list[str]) -> None:
    if not path.exists() and not path.is_symlink():
        return
    physical_directory(path, "legacy receipt directory")
    try:
        next(path.iterdir())
    except StopIteration:
        path.rmdir()
        fsync_directory(path.parent)
        removed.append(str(path))


def migrate_packages(
    receipt_root: Path,
    canonical_root: Path,
    uid: int,
    gid: int,
    system_root: bool,
    migrated: list[dict[str, str]],
    preserved: list[str],
    removed: list[str],
) -> None:
    legacy = receipt_root / "packages"
    if not legacy.exists() and not legacy.is_symlink():
        return
    physical_directory(legacy, "legacy package receipt root")
    pattern = re.compile(r"^([0-9a-f]{64})(\.replay)?\.json$")
    for source in sorted(legacy.iterdir(), key=lambda value: value.name):
        match = pattern.fullmatch(source.name)
        if match is None:
            preserved.append(str(source))
            continue
        package_id = match.group(1)
        document = load_json(source)
        if document.get("schema") != INSTALLATION_SCHEMA:
            preserved.append(str(source))
            continue
        package_id_from_document(source, package_id)
        name = (
            "package-installation-replay.json"
            if match.group(2)
            else "package-installation.json"
        )
        destination = canonical_root / package_id / name
        action = publish_exact(source, destination, uid, gid, system_root)
        migrated.append(
            {"source": str(source), "destination": str(destination), "action": action}
        )
    remove_if_empty(legacy, removed)


def migrate_package_directory_tree(
    source_root: Path,
    canonical_root: Path,
    allowed_names: set[str],
    uid: int,
    gid: int,
    system_root: bool,
    migrated: list[dict[str, str]],
    preserved: list[str],
    removed: list[str],
) -> None:
    if not source_root.exists() and not source_root.is_symlink():
        return
    physical_directory(source_root, "legacy package-addressed receipt root")
    for package_directory in sorted(source_root.iterdir(), key=lambda value: value.name):
        if (
            HEX_256.fullmatch(package_directory.name) is None
            or package_directory.is_symlink()
            or not package_directory.is_dir()
        ):
            preserved.append(str(package_directory))
            continue
        package_id = package_directory.name
        for source in sorted(package_directory.iterdir(), key=lambda value: value.name):
            if source.name not in allowed_names or source.is_symlink() or not source.is_file():
                preserved.append(str(source))
                continue
            if source.name in {
                "resource-observation.json",
                "package-installation.json",
                "package-installation-replay.json",
            }:
                document = load_json(source)
                observed = document.get("package_id")
                if observed is not None and observed != package_id:
                    raise ReceiptEstateError(
                        f"package-addressed receipt belongs to another package: {source}"
                    )
            destination = canonical_root / package_id / source.name
            action = publish_exact(source, destination, uid, gid, system_root)
            migrated.append(
                {"source": str(source), "destination": str(destination), "action": action}
            )
        remove_if_empty(package_directory, removed)
    remove_if_empty(source_root, removed)


def migrate_singleton_product_receipt(
    source: Path,
    canonical_root: Path,
    destination_name: str,
    uid: int,
    gid: int,
    system_root: bool,
    migrated: list[dict[str, str]],
) -> None:
    if not source.exists() and not source.is_symlink():
        return
    package_id = package_id_from_document(source)
    destination = canonical_root / package_id / destination_name
    action = publish_exact(source, destination, uid, gid, system_root)
    migrated.append(
        {"source": str(source), "destination": str(destination), "action": action}
    )


def migrate_named_package_leaf(
    source_root: Path,
    canonical_root: Path,
    source_name: str,
    destination_name: str,
    uid: int,
    gid: int,
    system_root: bool,
    migrated: list[dict[str, str]],
    preserved: list[str],
    removed: list[str],
) -> None:
    if not source_root.exists() and not source_root.is_symlink():
        return
    physical_directory(source_root, "legacy package receipt root")
    for package_directory in sorted(source_root.iterdir(), key=lambda value: value.name):
        if (
            HEX_256.fullmatch(package_directory.name) is None
            or package_directory.is_symlink()
            or not package_directory.is_dir()
        ):
            preserved.append(str(package_directory))
            continue
        entries = sorted(package_directory.iterdir(), key=lambda value: value.name)
        for entry in entries:
            if entry.name != source_name or entry.is_symlink() or not entry.is_file():
                preserved.append(str(entry))
                continue
            destination = canonical_root / package_directory.name / destination_name
            action = publish_exact(entry, destination, uid, gid, system_root)
            migrated.append(
                {"source": str(entry), "destination": str(destination), "action": action}
            )
        remove_if_empty(package_directory, removed)
    remove_if_empty(source_root, removed)


def converge(
    root: Path,
    cluster_contract: dict[str, Any],
    *,
    service_user: str = "laplace-runner",
    service_group: str = "laplace-runner",
    authorize_system_root: bool = False,
) -> dict[str, Any]:
    if not root.is_absolute():
        raise ReceiptEstateError("receipt convergence root must be absolute")
    if root == Path("/") and (not authorize_system_root or os.geteuid() != 0):
        raise ReceiptEstateError(
            "system receipt convergence requires root and --authorize-system-root"
        )
    instance = cluster_contract.get("instance")
    if not isinstance(instance, dict):
        raise ReceiptEstateError("PostgreSQL cluster contract instance is absent")
    logical_instance_root = Path(str(instance.get("receipt_directory", "")))
    if not logical_instance_root.is_absolute():
        raise ReceiptEstateError("PostgreSQL receipt directory is not absolute")
    receipt_root = prefixed(root, Path("/opt/laplace/receipts"))
    instance_root = prefixed(root, logical_instance_root)
    canonical_root = instance_root / "cluster-activation"
    uid, gid = service_ids(root, service_user, service_group)
    ensure_directory(receipt_root, uid, gid, root == Path("/"))
    ensure_directory(instance_root, uid, gid, root == Path("/"))
    ensure_directory(canonical_root, uid, gid, root == Path("/"))

    migrated: list[dict[str, str]] = []
    preserved: list[str] = []
    removed: list[str] = []

    migrate_packages(
        receipt_root, canonical_root, uid, gid, root == Path("/"),
        migrated, preserved, removed
    )
    migrate_package_directory_tree(
        receipt_root / "plans",
        canonical_root,
        {"host-inventory.json", "host-selection.json", "resource-observation.json"},
        uid,
        gid,
        root == Path("/"),
        migrated,
        preserved,
        removed,
    )
    migrate_package_directory_tree(
        instance_root / "plan",
        canonical_root,
        {"host-inventory.json", "host-selection.json", "resource-observation.json"},
        uid,
        gid,
        root == Path("/"),
        migrated,
        preserved,
        removed,
    )
    migrate_named_package_leaf(
        instance_root / "release-capacity",
        canonical_root,
        "capacity.json",
        "release-capacity.json",
        uid,
        gid,
        root == Path("/"),
        migrated,
        preserved,
        removed,
    )
    migrate_named_package_leaf(
        instance_root / "product-cognition",
        canonical_root,
        "installed-proof.json",
        "installed-cognition-proof.json",
        uid,
        gid,
        root == Path("/"),
        migrated,
        preserved,
        removed,
    )
    migrate_singleton_product_receipt(
        instance_root / "unicode-product-activation.json",
        canonical_root,
        "unicode-product-activation.json",
        uid,
        gid,
        root == Path("/"),
        migrated,
    )
    migrate_singleton_product_receipt(
        instance_root / "highway-product-activation.json",
        canonical_root,
        "highway-product-activation.json",
        uid,
        gid,
        root == Path("/"),
        migrated,
    )

    deployments = receipt_root / "deployments"
    if deployments.exists() or deployments.is_symlink():
        if deployments.is_symlink() or not deployments.is_dir():
            preserved.append(str(deployments))
        else:
            try:
                next(deployments.iterdir())
            except StopIteration:
                if root == Path("/") and authorize_system_root:
                    deployments.rmdir()
                    fsync_directory(deployments.parent)
                    removed.append(str(deployments))
                elif root != Path("/"):
                    deployments.rmdir()
                    removed.append(str(deployments))
                else:
                    preserved.append(str(deployments))
            else:
                preserved.append(str(deployments))

    result: dict[str, Any] = {
        "schema": SCHEMA,
        "canonical_package_evidence_root": str(canonical_root),
        "migrated": migrated,
        "removed_empty_legacy_roots": removed,
        "preserved_unknown_or_conflicting": sorted(set(preserved)),
        "migration_count": len(migrated),
    }
    result["receipt_sha256"] = sha256_bytes(canonical_bytes(result))
    return result


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default="/")
    parser.add_argument("--cluster-contract", default="contracts/postgresql-cluster.json")
    parser.add_argument("--service-user", default="laplace-runner")
    parser.add_argument("--service-group", default="laplace-runner")
    parser.add_argument("--authorize-system-root", action="store_true")
    parser.add_argument("--output", default="-")
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_args(argv)
    result = converge(
        Path(arguments.root),
        load_json(Path(arguments.cluster_contract)),
        service_user=arguments.service_user,
        service_group=arguments.service_group,
        authorize_system_root=arguments.authorize_system_root,
    )
    content = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output == "-":
        sys.stdout.write(content)
    else:
        destination = Path(arguments.output)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except ReceiptEstateError as error:
        print(f"receipt-estate: {error}", file=sys.stderr)
        raise SystemExit(1) from error
