#!/usr/bin/env python3
"""Reclaim obsolete product build/stage generations without losing build metadata.

`/build/laplace/runner/product/{build,stage}` is rebuildable execution state, not the
canonical product evidence store. This controller preserves the currently selected
plan, serializes against the same per-plan locks used by build-package.py, copies
verified completed-plan metadata into a bounded product execution-retention root, and
removes only exact plan-addressed physical directories. Interrupted old plan
workspaces are also reclaimable after the age gate because they never became a
completed package.

Unknown names, symlinked plan roots, cross-device trees, busy locks, and malformed
completed-plan evidence are preserved fail-closed. Deletion never follows symlinks.
"""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import time
from typing import Any, Iterable, Sequence


SCHEMA = "laplace.product-build-workspace-retention/v1"
PACKAGE_RECEIPT_SCHEMA = "laplace.product-package-receipt/v1"
PLAN_ID = re.compile(r"^[0-9a-f]{64}$")


class RetentionError(RuntimeError):
    pass


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RetentionError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise RetentionError(f"JSON root must be an object: {path}")
    return value


def require_physical_directory(path: Path, label: str) -> os.stat_result:
    if not path.is_absolute() or path.is_symlink() or not path.is_dir():
        raise RetentionError(f"{label} is not one physical absolute directory: {path}")
    return path.lstat()


def _allocated_bytes(path: Path, expected_device: int) -> int:
    metadata = path.lstat()
    if metadata.st_dev != expected_device:
        raise RetentionError(f"workspace crosses filesystem boundary: {path}")
    total = metadata.st_blocks * 512
    if stat.S_ISDIR(metadata.st_mode) and not stat.S_ISLNK(metadata.st_mode):
        with os.scandir(path) as entries:
            children = [Path(entry.path) for entry in entries]
        for child in children:
            total += _allocated_bytes(child, expected_device)
    return total


def _remove_tree(path: Path, expected_device: int) -> int:
    metadata = path.lstat()
    if metadata.st_dev != expected_device:
        raise RetentionError(f"refusing cross-device workspace cleanup: {path}")
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
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _plan_names(root: Path) -> set[str]:
    names: set[str] = set()
    with os.scandir(root) as entries:
        for entry in entries:
            if PLAN_ID.fullmatch(entry.name) is not None:
                names.add(entry.name)
    return names


def _canonical_manifest_root(value: Any) -> PurePosixPath:
    if not isinstance(value, str):
        raise RetentionError("package manifest root is absent")
    root = PurePosixPath(value)
    if not root.is_absolute() or ".." in root.parts:
        raise RetentionError("package manifest root is not canonical absolute")
    return root


def _completed_metadata(
    plan_id: str, build: Path, stage: Path
) -> tuple[bytes, bytes, dict[str, Any]] | None:
    receipt_path = build / "package-receipt.json"
    manifest_path = build / "package-manifest.json"
    if not receipt_path.exists() and not manifest_path.exists():
        return None
    if (
        not receipt_path.is_file()
        or receipt_path.is_symlink()
        or not manifest_path.is_file()
        or manifest_path.is_symlink()
    ):
        raise RetentionError(f"plan {plan_id} has partial or unsafe completion metadata")
    receipt_bytes = receipt_path.read_bytes()
    manifest_bytes = manifest_path.read_bytes()
    receipt = load_json(receipt_path)
    manifest = load_json(manifest_path)
    if (
        receipt.get("schema") != PACKAGE_RECEIPT_SCHEMA
        or receipt.get("activation_eligible") is not True
        or receipt.get("build_input_closure_complete") is not True
        or receipt.get("product_activated") is not False
        or receipt.get("manifest") != str(manifest_path)
        or receipt.get("manifest_sha256") != sha256_bytes(manifest_bytes)
    ):
        raise RetentionError(f"plan {plan_id} package receipt is not a completed exact build")
    package_id = receipt.get("package_id")
    if not isinstance(package_id, str) or PLAN_ID.fullmatch(package_id) is None:
        raise RetentionError(f"plan {plan_id} package identity is invalid")
    if manifest.get("package_id") != package_id:
        raise RetentionError(f"plan {plan_id} manifest package identity differs")
    logical = _canonical_manifest_root(manifest.get("root"))
    physical = stage / "root"
    for part in logical.parts[1:]:
        physical /= part
    if receipt.get("physical_root") != str(physical):
        raise RetentionError(f"plan {plan_id} physical package root differs")
    if not physical.is_dir() or physical.is_symlink():
        raise RetentionError(f"plan {plan_id} completed physical package is absent")
    summary = {
        "plan_id": plan_id,
        "plan_sha256": receipt.get("plan_sha256"),
        "package_id": package_id,
        "package_receipt_sha256": sha256_bytes(receipt_bytes),
        "package_manifest_sha256": sha256_bytes(manifest_bytes),
        "original_build_directory": str(build),
        "original_stage_directory": str(stage),
        "payload_rebuildable": True,
        "payload_reclaimed": True,
    }
    return receipt_bytes, manifest_bytes, summary


def _publish_metadata(
    retention_root: Path,
    plan_id: str,
    receipt_bytes: bytes,
    manifest_bytes: bytes,
    summary: dict[str, Any],
) -> Path:
    plan_destination = retention_root / plan_id
    plan_destination.mkdir(mode=0o2770, exist_ok=True)
    if plan_destination.is_symlink() or not plan_destination.is_dir():
        raise RetentionError(
            f"execution-retention destination is unsafe: {plan_destination}"
        )

    # A build plan identifies its inputs, not its execution metadata. Rebuilding
    # the same plan can change the build log and therefore the package receipt
    # while retaining the same package. Keep each exact receipt's evidence.
    # Receipt validation already binds these bytes to the exact manifest.
    receipt_id = sha256_bytes(receipt_bytes)
    legacy_receipt = plan_destination / "package-receipt.json"
    if (legacy_receipt.is_file() and not legacy_receipt.is_symlink()
            and legacy_receipt.read_bytes() == receipt_bytes):
        # Reuse an identical pre-versioning record without rewriting history.
        # The common exact checks below still reject any changed companion.
        destination = plan_destination
    else:
        destination = plan_destination / receipt_id
        destination.mkdir(mode=0o2770, exist_ok=True)
        if destination.is_symlink() or not destination.is_dir():
            raise RetentionError(
                f"execution-retention destination is unsafe: {destination}"
            )
    documents = {
        "package-receipt.json": receipt_bytes,
        "package-manifest.json": manifest_bytes,
        "retention.json": canonical_bytes({"schema": SCHEMA, **summary}),
    }
    for name, payload in documents.items():
        target = destination / name
        if target.exists():
            if target.is_symlink() or not target.is_file() or target.read_bytes() != payload:
                raise RetentionError(f"execution-retention metadata collision: {target}")
            continue
        with target.open("xb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    _fsync_directory(destination)
    if destination != plan_destination:
        _fsync_directory(plan_destination)
    _fsync_directory(retention_root)
    return destination


# Readers may outlive rebuildable build/stage directories. Resolve only exact
# metadata retained by this owner; original receipt provenance is never rewritten.
MAXIMUM_METADATA_BYTES = 32 * 1024 * 1024
MAXIMUM_RETAINED_VERSIONS = 1024
_METADATA_NAMES = ("package-receipt.json", "package-manifest.json", "retention.json")


def read_metadata_document(path: Path) -> tuple[bytes, dict[str, Any]]:
    if not path.is_absolute() or ".." in path.parts:
        raise RetentionError(f"metadata path is not canonical absolute: {path}")
    try:
        if path.parent.resolve(strict=True) != path.parent:
            raise RetentionError(f"metadata directory contains a link: {path.parent}")
        descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(descriptor, "rb") as stream:
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode) or before.st_size > MAXIMUM_METADATA_BYTES:
                raise RetentionError(f"metadata is not a bounded regular file: {path}")
            payload = stream.read(MAXIMUM_METADATA_BYTES + 1)
            after = os.fstat(stream.fileno())
        current = path.lstat()
        identity = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns)
        if (len(payload) > MAXIMUM_METADATA_BYTES or len(payload) != before.st_size
                or identity(before) != identity(after) or identity(after) != identity(current)):
            raise RetentionError(f"metadata changed during read: {path}")
        document = json.loads(payload)
    except (OSError, ValueError) as error:
        raise RetentionError(f"cannot read exact metadata {path}: {error}") from error
    if not isinstance(document, dict):
        raise RetentionError(f"metadata JSON root is not an object: {path}")
    return payload, document


def _metadata_roots(build_root: Path, stage_root: Path) -> Path:
    if (not build_root.is_absolute() or not stage_root.is_absolute()
            or ".." in build_root.parts or ".." in stage_root.parts
            or build_root.parent != stage_root.parent or build_root == stage_root):
        raise RetentionError("metadata build/stage roots do not share one absolute product estate")
    return build_root.parent / "retention"


def _metadata_at(directory: Path, build: Path, stage: Path, *, retained: bool) -> dict[str, Any]:
    require_physical_directory(directory, "package metadata directory")
    receipt_bytes, receipt = read_metadata_document(directory / _METADATA_NAMES[0])
    manifest_bytes, manifest = read_metadata_document(directory / _METADATA_NAMES[1])
    package_id = receipt.get("package_id")
    manifest_sha = sha256_bytes(manifest_bytes)
    if (receipt.get("schema") != PACKAGE_RECEIPT_SCHEMA
            or receipt.get("activation_eligible") is not True
            or receipt.get("build_input_closure_complete") is not True
            or receipt.get("product_activated") is not False
            or not isinstance(package_id, str) or PLAN_ID.fullmatch(package_id) is None
            or not isinstance(receipt.get("plan_sha256"), str)
            or PLAN_ID.fullmatch(receipt["plan_sha256"]) is None
            or manifest.get("package_id") != package_id
            or receipt.get("manifest") != str(build / _METADATA_NAMES[1])
            or receipt.get("manifest_sha256") != manifest_sha):
        raise RetentionError(f"package metadata does not bind one completed exact build: {directory}")
    logical = _canonical_manifest_root(manifest.get("root"))
    physical = stage / "root" / str(logical).lstrip("/")
    if receipt.get("physical_root") != str(physical):
        raise RetentionError(f"package metadata original physical root differs: {directory}")
    receipt_sha = sha256_bytes(receipt_bytes)
    if retained:
        _, record = read_metadata_document(directory / _METADATA_NAMES[2])
        expected = {
            "schema": SCHEMA, "plan_id": build.name,
            "plan_sha256": receipt["plan_sha256"], "package_id": package_id,
            "package_receipt_sha256": receipt_sha, "package_manifest_sha256": manifest_sha,
            "original_build_directory": str(build), "original_stage_directory": str(stage),
            "payload_rebuildable": True, "payload_reclaimed": True,
        }
        if record != expected:
            raise RetentionError(f"retention metadata differs from its exact documents: {directory}")
        legacy = build.parent.parent / "retention" / build.name
        if directory != legacy and directory.name != receipt_sha:
            raise RetentionError(f"retained receipt address differs from its bytes: {directory}")
    return {
        "selection": "retained" if retained else "build",
        "plan_id": build.name, "package_id": package_id,
        "receipt_path": str(directory / _METADATA_NAMES[0]),
        "manifest_path": str(directory / _METADATA_NAMES[1]),
        "receipt_sha256": receipt_sha, "manifest_sha256": manifest_sha,
        "original_manifest_path": str(build / _METADATA_NAMES[1]),
        "receipt": receipt, "manifest": manifest,
    }


def resolve_package_metadata(
    build_root: Path, stage_root: Path, plan_id: str, package_id: str,
    manifest_sha256: str, *, selected_receipt: Path | None = None,
) -> dict[str, Any]:
    """Resolve authenticated metadata without reconstructing reclaimed payload.

    A matching live pair is preferred. If it is absent or a rebuild has replaced
    it, inspect this one plan's declared legacy/versioned retention records.
    A rejected live observation is reported separately from retained selection.
    Valid other generations are not matches. Equivalent manifest matches choose
    the lexically first exact receipt address, never an mtime/newest heuristic.
    Explicit receipt selection binds that exact execution instead.
    """
    for value in (plan_id, package_id, manifest_sha256):
        if not isinstance(value, str) or PLAN_ID.fullmatch(value) is None:
            raise RetentionError("invalid exact package metadata identity")
    retention = _metadata_roots(build_root, stage_root)
    build, stage = build_root / plan_id, stage_root / plan_id
    plan_retention = retention / plan_id

    def matches(value: dict[str, Any]) -> bool:
        return value["package_id"] == package_id and value["manifest_sha256"] == manifest_sha256

    if selected_receipt is not None and selected_receipt != build / _METADATA_NAMES[0]:
        directory = selected_receipt.parent
        if (selected_receipt.name != _METADATA_NAMES[0]
                or not (directory == plan_retention or
                        (directory.parent == plan_retention
                         and PLAN_ID.fullmatch(directory.name) is not None))):
            raise RetentionError("explicit receipt is outside this plan's metadata estate")
        value = _metadata_at(directory, build, stage, retained=True)
        if not matches(value):
            raise RetentionError("explicit retained metadata differs from selected package/manifest")
        value["build_metadata"] = {"status": "not-observed"}
        return value
    live = None
    build_observation = {"status": "absent"}
    try:
        if build.exists() or build.is_symlink():
            require_physical_directory(build, "product build plan")
            if any((build / name).exists() or (build / name).is_symlink()
                   for name in _METADATA_NAMES[:2]):
                live = _metadata_at(build, build, stage, retained=False)
                if not matches(live):
                    raise RetentionError("live build metadata differs from selected package/manifest")
                live["build_metadata"] = {"status": "matched"}
    except RetentionError as error:
        # This directory is rebuildable execution state. A new/partial execution
        # cannot invalidate independently retained exact installed-generation bytes.
        if selected_receipt is not None:
            raise
        live = None
        build_observation = {"status": "rejected", "failure": str(error)[:1000]}
    if live is not None:
        return live
    if selected_receipt is not None:
        raise RetentionError("explicit build receipt is absent")
    require_physical_directory(plan_retention, "retained product plan")
    candidates = []
    if any((plan_retention / name).exists() or (plan_retention / name).is_symlink()
           for name in _METADATA_NAMES):
        candidates.append(plan_retention)
    with os.scandir(plan_retention) as entries:
        for entry in entries:
            if entry.name in _METADATA_NAMES:
                continue
            if PLAN_ID.fullmatch(entry.name) is None:
                raise RetentionError(f"unexpected retained metadata entry: {entry.path}")
            candidates.append(Path(entry.path))
            if len(candidates) > MAXIMUM_RETAINED_VERSIONS:
                raise RetentionError("retained metadata inventory exceeds its declared version boundary")
    selected = None
    for directory in sorted(candidates):
        value = _metadata_at(directory, build, stage, retained=True)
        if matches(value) and selected is None:
            selected = value
    if selected is None:
        raise RetentionError("no retained metadata binds the selected package and manifest")
    selected["build_metadata"] = build_observation
    return selected


def resolve_product_receipt(
    selected_receipt: Path, build_root: Path, stage_root: Path, package_id: str,
) -> dict[str, Any]:
    """Resolve an explicit eligible build or retained receipt for read-only use."""
    _metadata_roots(build_root, stage_root)
    receipt_bytes, receipt = read_metadata_document(selected_receipt)
    original = Path(str(receipt.get("manifest", "")))
    try:
        relative = original.relative_to(build_root)
    except ValueError as error:
        raise RetentionError("receipt original manifest is outside declared build root") from error
    if len(relative.parts) != 2 or relative.parts[1] != _METADATA_NAMES[1]:
        raise RetentionError("receipt original manifest omits an exact plan address")
    value = resolve_package_metadata(
        build_root, stage_root, relative.parts[0], package_id,
        receipt.get("manifest_sha256"), selected_receipt=selected_receipt,
    )
    if value["receipt_sha256"] != sha256_bytes(receipt_bytes):
        raise RetentionError("selected receipt changed during metadata resolution")
    return value


def _tree_age_seconds(paths: Iterable[Path], now: float) -> float:
    newest = 0.0
    found = False
    for path in paths:
        if not path.exists() and not path.is_symlink():
            continue
        found = True
        newest = max(newest, path.lstat().st_mtime)
    return 0.0 if not found else max(0.0, now - newest)


def reconcile(
    contract: dict[str, Any],
    *,
    receipt_root: Path,
    preserve: set[str],
    minimum_age_seconds: int,
    now: float | None = None,
) -> dict[str, Any]:
    build_config = contract.get("build")
    if not isinstance(build_config, dict):
        raise RetentionError("product package build contract is absent")
    build_root = Path(str(build_config.get("root", "")))
    stage_root = Path(str(build_config.get("stage_root", "")))
    build_stat = require_physical_directory(build_root, "product build root")
    stage_stat = require_physical_directory(stage_root, "product stage root")
    if build_root.parent.parent != stage_root.parent.parent:
        raise RetentionError("product build/stage roots do not share one product root")
    product_root = build_root.parent
    if product_root != stage_root.parent:
        raise RetentionError("product build/stage roots do not share one product root")
    lock_root = product_root / "locks"
    lock_root.mkdir(mode=0o2770, exist_ok=True)
    if lock_root.is_symlink() or not lock_root.is_dir():
        raise RetentionError("product plan lock root is unsafe")
    if not receipt_root.is_absolute():
        raise RetentionError("execution-retention root must be absolute")
    receipt_root.mkdir(parents=True, mode=0o2770, exist_ok=True)
    if receipt_root.is_symlink() or not receipt_root.is_dir():
        raise RetentionError("execution-retention root is unsafe")
    if receipt_root.parent != product_root:
        raise RetentionError("execution-retention root must remain inside the product build estate")
    for value in preserve:
        if PLAN_ID.fullmatch(value) is None:
            raise RetentionError(f"preserved plan identity is invalid: {value}")
    if minimum_age_seconds < 0:
        raise RetentionError("minimum age cannot be negative")

    observed_now = time.time() if now is None else now
    names = sorted(_plan_names(build_root) | _plan_names(stage_root))
    removed: list[dict[str, Any]] = []
    preserved: list[dict[str, Any]] = []
    for plan_id in names:
        build = build_root / plan_id
        stage = stage_root / plan_id
        if plan_id in preserve:
            preserved.append({"plan_id": plan_id, "reason": "selected-plan"})
            continue
        age = _tree_age_seconds((build, stage), observed_now)
        if age < minimum_age_seconds:
            preserved.append({"plan_id": plan_id, "reason": "minimum-age", "age_seconds": age})
            continue
        for path, root_stat in ((build, build_stat), (stage, stage_stat)):
            if not path.exists() and not path.is_symlink():
                continue
            metadata = path.lstat()
            if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
                preserved.append({"plan_id": plan_id, "reason": "unsafe-plan-node", "path": str(path)})
                break
            if metadata.st_dev != root_stat.st_dev:
                preserved.append({"plan_id": plan_id, "reason": "cross-device-plan-node", "path": str(path)})
                break
        else:
            lock = lock_root / f"{plan_id}.lock"
            descriptor = os.open(
                lock,
                os.O_CREAT | os.O_RDWR | os.O_CLOEXEC | os.O_NOFOLLOW,
                0o660,
            )
            with os.fdopen(descriptor, "a+b") as stream:
                try:
                    fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    preserved.append({"plan_id": plan_id, "reason": "plan-lock-busy"})
                    continue
                metadata = _completed_metadata(plan_id, build, stage)
                build_bytes = _allocated_bytes(build, build_stat.st_dev) if build.exists() else 0
                stage_bytes = _allocated_bytes(stage, stage_stat.st_dev) if stage.exists() else 0
                retained_at = None
                reason = "stale-incomplete-plan-workspace"
                if metadata is not None:
                    receipt_bytes, manifest_bytes, summary = metadata
                    destination = _publish_metadata(
                        receipt_root, plan_id, receipt_bytes, manifest_bytes, summary
                    )
                    retained_at = str(destination)
                    reason = "completed-plan-payload-rebuildable"
                removed_nodes = 0
                if build.exists():
                    removed_nodes += _remove_tree(build, build_stat.st_dev)
                if stage.exists():
                    removed_nodes += _remove_tree(stage, stage_stat.st_dev)
                _fsync_directory(build_root)
                _fsync_directory(stage_root)
                removed.append(
                    {
                        "plan_id": plan_id,
                        "reason": reason,
                        "allocated_bytes_reclaimed": build_bytes + stage_bytes,
                        "removed_nodes": removed_nodes,
                        "retained_metadata": retained_at,
                    }
                )
    receipt = {
        "schema": SCHEMA,
        "build_root": str(build_root),
        "stage_root": str(stage_root),
        "retention_root": str(receipt_root),
        "preserved_plan_ids": sorted(preserve),
        "minimum_age_seconds": minimum_age_seconds,
        "removed": removed,
        "preserved": preserved,
        "allocated_bytes_reclaimed": sum(
            item["allocated_bytes_reclaimed"] for item in removed
        ),
    }
    receipt["receipt_sha256"] = sha256_bytes(canonical_bytes(receipt))
    return receipt


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", default="contracts/product-package.json")
    parser.add_argument(
        "--retention-root",
        "--receipt-root",
        dest="receipt_root",
        default="/build/laplace/runner/product/retention",
    )
    parser.add_argument("--preserve-plan", action="append", default=[])
    parser.add_argument("--minimum-age-seconds", type=int, default=300)
    parser.add_argument("--output", default="-")
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_args(argv)
    receipt = reconcile(
        load_json(Path(arguments.contract)),
        receipt_root=Path(arguments.receipt_root),
        preserve=set(arguments.preserve_plan),
        minimum_age_seconds=arguments.minimum_age_seconds,
    )
    payload = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if arguments.output == "-":
        sys.stdout.write(payload)
    else:
        output = Path(arguments.output)
        if not output.parent.is_dir():
            raise RetentionError("output parent is absent")
        output.write_text(payload, encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except RetentionError as error:
        print(f"product-build-retention: {error}", file=sys.stderr)
        raise SystemExit(1) from error
