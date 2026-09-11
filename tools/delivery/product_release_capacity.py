#!/usr/bin/env python3
"""Prove product-release capacity and enforce bounded immutable-release retention.

Persistent activation copies an immutable content-addressed product package into
``/opt/laplace/releases`` before PostgreSQL generation transition begins. The native
resource observer measures PGDATA/WAL/temp capacity, but release-package storage has a
separate lifecycle and must be measured separately.

The capacity proof follows the physical install implementation. ``install_package``
creates one temporary tree on the release filesystem, copies each manifest file once,
verifies that tree, and atomically renames it into the final content-addressed path.
This module measures the target filesystem allocation unit, rounds every copied regular
file to that unit, accounts conservatively for directories/symlinks, and checks
available inodes. If the exact successor already exists, no copy capacity is required.

Release retention is deliberately bounded. The current/runtime-selected generations
are always protected. Among other successfully activated generations, only the newest
rollback window is retained. Older activated generations are verified against their
package-addressed installation receipts, checked for live runner process references,
and reclaimed even when the filesystem still has enough space for the next copy. That
prevents successful deployments from becoming an append-only archive on the product LV.
Never-activated verified residue remains capacity-driven cleanup.

Unknown, current, runtime-selected, live-referenced, or otherwise ambiguous releases
are preserved. Old interrupted ``.<package>.install.*`` staging directories may also
be removed after the minimum-age gate because they were never published release paths.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import time
from typing import Any, Sequence


SCHEMA = "laplace.product-release-capacity/v1"
INSTALLATION_SCHEMA = "laplace.product-package-installation-receipt/v1"
PACKAGE_ID = re.compile(r"^[0-9a-f]{64}$")
INSTALL_TEMP = re.compile(r"^\.([0-9a-f]{64})\.install\.[A-Za-z0-9._-]+$")
MIN_HEADROOM_BYTES = 512 * 1024 * 1024
DEFAULT_MINIMUM_TEMP_AGE_SECONDS = 300
DEFAULT_ROLLBACK_RELEASES = 2


class CapacityError(RuntimeError):
    pass


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def document_identity(value: dict[str, Any], field: str) -> str:
    payload = dict(value)
    payload.pop(field, None)
    return hashlib.sha256(canonical_bytes(payload)).hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CapacityError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise CapacityError(f"JSON root must be an object: {path}")
    return value


def _package_id_from_link(link: Path, release_root: Path) -> str | None:
    if not link.is_symlink():
        return None
    raw = os.readlink(link)
    target = (link.parent / raw).resolve(strict=False)
    try:
        relative = target.relative_to(release_root.resolve())
    except ValueError:
        raise CapacityError(f"product pointer escapes release root: {link} -> {raw}")
    if len(relative.parts) != 1 or PACKAGE_ID.fullmatch(relative.name) is None:
        raise CapacityError(
            f"product pointer is not one canonical release: {link} -> {raw}"
        )
    return relative.name


def _disk_free(path: Path) -> int:
    observation = os.statvfs(path)
    return observation.f_frsize * observation.f_bavail


def _disk_free_inodes(path: Path) -> int:
    observation = os.statvfs(path)
    return int(observation.f_favail)


def _allocation_unit(path: Path) -> int:
    observation = os.statvfs(path)
    unit = int(observation.f_frsize or observation.f_bsize)
    if unit <= 0:
        raise CapacityError("release filesystem reports no allocation unit")
    return unit


def _manifest_source(
    product_receipt: dict[str, Any], manifest: dict[str, Any]
) -> tuple[Path, str, list[dict[str, Any]]]:
    physical_release = Path(str(product_receipt.get("physical_root", "")))
    logical_release = str(manifest.get("root", ""))
    if not physical_release.is_absolute() or not logical_release.startswith("/"):
        raise CapacityError("product receipt/package root is not absolute")
    if not str(physical_release).endswith(logical_release):
        raise CapacityError(
            "product receipt physical root does not end in its logical release root"
        )
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise CapacityError("product manifest file set is absent")
    typed: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, entry in enumerate(files):
        if not isinstance(entry, dict):
            raise CapacityError(f"product manifest file {index} is invalid")
        relative = entry.get("path")
        if (
            not isinstance(relative, str)
            or not relative
            or PurePosixPath(relative).is_absolute()
        ):
            raise CapacityError(f"product manifest path {index} is invalid")
        path = PurePosixPath(relative)
        if ".." in path.parts or str(path) != relative or relative in seen:
            raise CapacityError(
                f"product manifest path is unsafe or repeated: {relative}"
            )
        seen.add(relative)
        candidate = physical_release.joinpath(*path.parts)
        kind = entry.get("kind", "file")
        if kind == "symlink":
            if not candidate.is_symlink():
                raise CapacityError(f"source package symlink is absent: {relative}")
        elif kind != "file" or not candidate.is_file() or candidate.is_symlink():
            raise CapacityError(f"source package file is absent: {relative}")
        typed.append(entry)
    return physical_release, logical_release, typed


def _source_package_bytes(
    product_receipt: dict[str, Any], manifest: dict[str, Any]
) -> int:
    physical_release, _logical_release, files = _manifest_source(
        product_receipt, manifest
    )
    total = 0
    for entry in files:
        if entry.get("kind", "file") == "symlink":
            continue
        path = PurePosixPath(entry["path"])
        total += physical_release.joinpath(*path.parts).stat().st_size
    if total <= 0:
        raise CapacityError("source product package has no regular-file bytes")
    return total


def _target_copy_footprint(
    product_receipt: dict[str, Any],
    manifest: dict[str, Any],
    release_root: Path,
) -> dict[str, int]:
    """Bound the allocation made by ``cluster_core.install_package``."""

    physical_release, logical_release, files = _manifest_source(
        product_receipt, manifest
    )
    unit = _allocation_unit(release_root)
    regular_allocation = 0
    relative_directories: set[str] = set()
    symlinks = 0
    for entry in files:
        relative = PurePosixPath(entry["path"])
        for parent in relative.parents:
            if str(parent) == ".":
                break
            relative_directories.add(str(parent))
        if entry.get("kind", "file") == "symlink":
            symlinks += 1
            continue
        size = physical_release.joinpath(*relative.parts).stat().st_size
        regular_allocation += ((size + unit - 1) // unit) * unit

    logical_components = sum(
        1 for part in PurePosixPath(logical_release).parts if part != "/"
    )
    directory_count = 1 + logical_components + len(relative_directories)
    metadata_allocation = (directory_count + symlinks) * unit
    required_inodes = directory_count + len(files)
    return {
        "allocation_unit_bytes": unit,
        "regular_file_allocation_bytes": regular_allocation,
        "metadata_allocation_bytes": metadata_allocation,
        "required_allocation_bytes": regular_allocation + metadata_allocation,
        "required_inodes": required_inodes,
        "directory_count": directory_count,
        "symlink_count": symlinks,
    }


def _validate_installation_receipt(
    receipt: dict[str, Any], package_id: str, release: Path
) -> None:
    if (
        receipt.get("schema") != INSTALLATION_SCHEMA
        or receipt.get("phase") != "installed"
        or receipt.get("package_id") != package_id
        or receipt.get("installed_release") != str(release)
        or receipt.get("installed_package_verified") is not True
        or receipt.get("source_package_verified") is not True
        or receipt.get("overwrite_performed") is not False
        or receipt.get("installation_receipt_sha256")
        != document_identity(receipt, "installation_receipt_sha256")
    ):
        raise CapacityError(
            f"release installation receipt does not prove immutable residue: {package_id}"
        )


def _runner_process_references(
    release: Path, proc_root: Path = Path("/proc")
) -> list[str]:
    needle = str(release).encode("utf-8")
    references: set[str] = set()
    try:
        processes = list(proc_root.iterdir())
    except OSError as error:
        raise CapacityError(f"cannot enumerate process state: {error}") from error
    effective_uid = os.geteuid()
    for process in processes:
        if not process.name.isdigit():
            continue
        try:
            if process.stat().st_uid != effective_uid:
                continue
        except (FileNotFoundError, PermissionError):
            continue
        except OSError as error:
            raise CapacityError(f"cannot stat process {process}: {error}") from error
        for name in ("cwd", "exe", "root"):
            try:
                raw = os.readlink(process / name)
            except (FileNotFoundError, PermissionError):
                continue
            except OSError as error:
                raise CapacityError(
                    f"cannot inspect process reference {process / name}: {error}"
                ) from error
            if raw == str(release) or raw.startswith(str(release) + "/"):
                references.add(f"{process.name}:{name}")
        maps = process / "maps"
        try:
            payload = maps.read_bytes()
        except (FileNotFoundError, PermissionError):
            payload = b""
        except OSError as error:
            raise CapacityError(
                f"cannot inspect process maps {maps}: {error}"
            ) from error
        if needle + b"/" in payload or payload.endswith(needle):
            references.add(f"{process.name}:maps")
    return sorted(references)


def _tree_allocated_bytes(path: Path, expected_device: int) -> int:
    metadata = path.lstat()
    if metadata.st_dev != expected_device:
        raise CapacityError(f"release residue crosses filesystem boundary: {path}")
    total = metadata.st_blocks * 512
    if stat.S_ISDIR(metadata.st_mode) and not stat.S_ISLNK(metadata.st_mode):
        with os.scandir(path) as entries:
            children = [Path(entry.path) for entry in entries]
        for child in children:
            total += _tree_allocated_bytes(child, expected_device)
    return total


def _remove_tree(path: Path, expected_device: int) -> int:
    metadata = path.lstat()
    if metadata.st_dev != expected_device:
        raise CapacityError(f"refusing cross-device release cleanup at {path}")
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
        path.unlink()
        return 1
    removed = 0
    with os.scandir(path) as entries:
        children = [Path(entry.path) for entry in entries]
    for child in children:
        removed += _remove_tree(child, expected_device)
    path.rmdir()
    return removed + 1


def _fsync_directory(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _activation_files(evidence: Path) -> list[Path]:
    return [
        evidence / "activation-complete.json",
        evidence / "activation-result.json",
    ]


def _safe_release_candidates(
    release_root: Path,
    receipt_root: Path,
    protected: set[str],
    successor_package_id: str,
    proc_root: Path,
    rollback_releases: int,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    if rollback_releases < 0:
        raise CapacityError("rollback release count cannot be negative")

    never_activated: list[dict[str, Any]] = []
    activated: list[dict[str, Any]] = []
    preserved: list[dict[str, Any]] = []
    try:
        children = sorted(release_root.iterdir(), key=lambda item: item.name)
    except OSError as error:
        raise CapacityError(f"cannot enumerate product releases: {error}") from error

    for child in children:
        package_id = child.name
        if PACKAGE_ID.fullmatch(package_id) is None:
            continue
        if child.is_symlink() or not child.is_dir():
            raise CapacityError(
                f"canonical release path is not one physical directory: {child}"
            )
        if package_id == successor_package_id:
            preserved.append(
                {"package_id": package_id, "reason": "requested-successor"}
            )
            continue
        if package_id in protected:
            preserved.append(
                {"package_id": package_id, "reason": "selected-product-pointer"}
            )
            continue

        evidence = receipt_root / "cluster-activation" / package_id
        installation_path = evidence / "package-installation.json"
        if not installation_path.is_file() or installation_path.is_symlink():
            preserved.append(
                {
                    "package_id": package_id,
                    "reason": "no-verifiable-installation-receipt",
                }
            )
            continue

        installation = load_json(installation_path)
        try:
            _validate_installation_receipt(installation, package_id, child)
        except CapacityError as error:
            preserved.append(
                {
                    "package_id": package_id,
                    "reason": "invalid-installation-receipt",
                    "detail": str(error),
                }
            )
            continue

        references = _runner_process_references(child, proc_root)
        if references:
            preserved.append(
                {
                    "package_id": package_id,
                    "reason": "live-runner-process-reference",
                    "references": references,
                }
            )
            continue

        base = {
            "package_id": package_id,
            "path": str(child),
            "installation_receipt_sha256": installation[
                "installation_receipt_sha256"
            ],
            "installed_file_bytes": installation.get("total_file_bytes"),
        }
        activation_paths = [
            path for path in _activation_files(evidence) if path.exists() or path.is_symlink()
        ]
        if any(path.is_symlink() for path in activation_paths):
            preserved.append(
                {
                    "package_id": package_id,
                    "reason": "ambiguous-activation-evidence",
                }
            )
            continue
        if activation_paths:
            activation_time_ns = max(path.stat().st_mtime_ns for path in activation_paths)
            activated.append(
                {
                    **base,
                    "activation_time_ns": activation_time_ns,
                    "cleanup_class": "activated",
                }
            )
        else:
            never_activated.append(
                {**base, "cleanup_class": "never-activated", "mandatory_reclaim": False}
            )

    activated.sort(
        key=lambda item: (int(item["activation_time_ns"]), str(item["package_id"])),
        reverse=True,
    )
    for item in activated[:rollback_releases]:
        preserved.append(
            {
                "package_id": item["package_id"],
                "reason": "rollback-window-retained",
                "activation_time_ns": item["activation_time_ns"],
            }
        )

    expired_rollback = []
    for item in reversed(activated[rollback_releases:]):
        expired_rollback.append(
            {**item, "mandatory_reclaim": True}
        )

    return expired_rollback + never_activated, preserved


def _remove_old_install_temporaries(
    release_root: Path,
    now: float,
    minimum_age_seconds: int,
    protected: set[str],
    successor_package_id: str,
    proc_root: Path,
) -> list[dict[str, Any]]:
    removed: list[dict[str, Any]] = []
    root_device = release_root.lstat().st_dev
    for child in sorted(release_root.iterdir(), key=lambda item: item.name):
        match = INSTALL_TEMP.fullmatch(child.name)
        if match is None:
            continue
        package_id = match.group(1)
        if package_id in protected or package_id == successor_package_id:
            continue
        metadata = child.lstat()
        age = max(0.0, now - metadata.st_mtime)
        if age < minimum_age_seconds:
            continue
        if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
            raise CapacityError(
                f"install staging residue is not one directory: {child}"
            )
        references = _runner_process_references(child, proc_root)
        if references:
            continue
        allocated = _tree_allocated_bytes(child, root_device)
        entries = _remove_tree(child, root_device)
        removed.append(
            {
                "path": str(child),
                "package_id": package_id,
                "allocated_bytes_before": allocated,
                "removed_entries": entries,
                "reason": "stale-unpublished-install-staging",
            }
        )
    if removed:
        _fsync_directory(release_root)
    return removed


def _capacity_satisfied(
    release_root: Path, required_bytes: int, required_inodes: int
) -> bool:
    return (
        _disk_free(release_root) >= required_bytes
        and _disk_free_inodes(release_root) >= required_inodes
    )


def reconcile_capacity(
    contract: dict[str, Any],
    product_receipt: dict[str, Any],
    manifest: dict[str, Any],
    *,
    proc_root: Path = Path("/proc"),
    minimum_temp_age_seconds: int = DEFAULT_MINIMUM_TEMP_AGE_SECONDS,
    rollback_releases: int = DEFAULT_ROLLBACK_RELEASES,
    now: float | None = None,
) -> dict[str, Any]:
    if contract.get("schema") != "laplace.postgresql-cluster-contract/v1":
        raise CapacityError("unsupported PostgreSQL cluster contract")
    package = contract.get("package")
    instance = contract.get("instance")
    if not isinstance(package, dict) or not isinstance(instance, dict):
        raise CapacityError("cluster contract package/instance state is absent")
    successor_package_id = manifest.get("package_id")
    if (
        not isinstance(successor_package_id, str)
        or PACKAGE_ID.fullmatch(successor_package_id) is None
    ):
        raise CapacityError("successor package identity is invalid")
    if product_receipt.get("package_id") != successor_package_id:
        raise CapacityError("product receipt and package identity differ")

    release_root = Path(str(package.get("release_root", "")))
    active_link = Path(str(package.get("active_link", "")))
    receipt_root = Path(str(instance.get("receipt_directory", "")))
    runtime_link = Path("/opt/laplace/runtime/refactor")
    for value, label in (
        (release_root, "release root"),
        (active_link, "active link"),
        (receipt_root, "receipt root"),
        (runtime_link, "runtime link"),
    ):
        if not value.is_absolute():
            raise CapacityError(f"{label} must be absolute")
    if not release_root.is_dir() or release_root.is_symlink():
        raise CapacityError("release root must be one physical directory")

    protected = {
        package_id
        for package_id in (
            _package_id_from_link(active_link, release_root),
            _package_id_from_link(runtime_link, release_root),
        )
        if package_id is not None
    }
    source_package_bytes = _source_package_bytes(product_receipt, manifest)
    successor_release = release_root / successor_package_id
    if successor_release.is_symlink():
        raise CapacityError("successor release path is a symlink")
    copy_required = not successor_release.exists()
    footprint = _target_copy_footprint(product_receipt, manifest, release_root)
    required_copy_bytes = source_package_bytes if copy_required else 0
    required_allocation_bytes = (
        footprint["required_allocation_bytes"] if copy_required else 0
    )
    required_inodes = footprint["required_inodes"] if copy_required else 0
    free_before = _disk_free(release_root)
    inodes_before = _disk_free_inodes(release_root)

    now_value = time.time() if now is None else now
    removed_temporaries = _remove_old_install_temporaries(
        release_root,
        now_value,
        minimum_temp_age_seconds,
        protected,
        successor_package_id,
        proc_root,
    )
    free_after_temporaries = _disk_free(release_root)
    inodes_after_temporaries = _disk_free_inodes(release_root)

    candidates, preserved = _safe_release_candidates(
        release_root,
        receipt_root,
        protected,
        successor_package_id,
        proc_root,
        rollback_releases,
    )
    removed_releases: list[dict[str, Any]] = []
    root_device = release_root.lstat().st_dev
    for candidate in candidates:
        mandatory = bool(candidate.get("mandatory_reclaim"))
        if not mandatory and _capacity_satisfied(
            release_root, required_allocation_bytes, required_inodes
        ):
            break
        path = Path(candidate["path"])
        allocated = _tree_allocated_bytes(path, root_device)
        entries = _remove_tree(path, root_device)
        removed = dict(candidate)
        removed.pop("mandatory_reclaim", None)
        cleanup_class = removed.pop("cleanup_class", "never-activated")
        removed.update(
            {
                "allocated_bytes_before": allocated,
                "removed_entries": entries,
                "reason": (
                    "verified-activated-release-outside-rollback-window"
                    if cleanup_class == "activated"
                    else "verified-installed-never-activated-release"
                ),
            }
        )
        removed_releases.append(removed)
        _fsync_directory(release_root)

    free_after = _disk_free(release_root)
    inodes_after = _disk_free_inodes(release_root)
    satisfied = (
        free_after >= required_allocation_bytes and inodes_after >= required_inodes
    )
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "successor_package_id": successor_package_id,
        "release_root": str(release_root),
        "protected_package_ids": sorted(protected),
        "rollback_release_count": rollback_releases,
        "source_package_bytes": source_package_bytes,
        "copy_required": copy_required,
        "required_copy_bytes": required_copy_bytes,
        "allocation_unit_bytes": footprint["allocation_unit_bytes"],
        "regular_file_allocation_bytes": (
            footprint["regular_file_allocation_bytes"] if copy_required else 0
        ),
        "minimum_headroom_bytes": (
            footprint["metadata_allocation_bytes"] if copy_required else 0
        ),
        "required_available_bytes": required_allocation_bytes,
        "required_available_inodes": required_inodes,
        "free_bytes_before": free_before,
        "free_inodes_before": inodes_before,
        "free_bytes_after_staging_cleanup": free_after_temporaries,
        "free_inodes_after_staging_cleanup": inodes_after_temporaries,
        "free_bytes_after": free_after,
        "free_inodes_after": inodes_after,
        "removed_temporaries": removed_temporaries,
        "removed_releases": removed_releases,
        "preserved_releases": preserved,
        "remaining_safe_candidate_count": max(
            0, len(candidates) - len(removed_releases)
        ),
        "capacity_satisfied": satisfied,
    }
    result["receipt_sha256"] = document_identity(result, "receipt_sha256")
    if not result["capacity_satisfied"]:
        raise CapacityError(
            "product release capacity remains insufficient after safe residue cleanup: "
            f"required_bytes={required_allocation_bytes} available_bytes={free_after} "
            f"required_inodes={required_inodes} available_inodes={inodes_after}"
        )
    return result


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", required=True)
    parser.add_argument("--product-receipt", required=True)
    parser.add_argument("--minimum-temp-age-seconds", type=int, default=300)
    parser.add_argument(
        "--rollback-releases", type=int, default=DEFAULT_ROLLBACK_RELEASES
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    contract = load_json(Path(args.contract))
    product_receipt = load_json(Path(args.product_receipt))
    manifest = load_json(Path(str(product_receipt.get("manifest", ""))))
    result = reconcile_capacity(
        contract,
        product_receipt,
        manifest,
        minimum_temp_age_seconds=args.minimum_temp_age_seconds,
        rollback_releases=args.rollback_releases,
    )
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except CapacityError as error:
        print(f"product_release_capacity: {error}", file=sys.stderr)
        raise SystemExit(1) from error
