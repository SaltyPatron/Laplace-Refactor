#!/usr/bin/env python3
"""Recover empty PostgreSQL candidate leaves before recurring product activation.

The cluster contract reserves a fixed set of instance-local candidate directories. A
failed or historical activation can leave one of those exact leaves behind even when
it contains no state. Treating an empty reserved leaf as permanent product state wedges
all later activations; deleting arbitrary residue would be worse.

This provider therefore has a deliberately narrow authority boundary:

* only exact paths named by ``contracts/postgresql-cluster.json`` are inspected;
* an active product link disables recovery entirely;
* symlinks and non-directories are never followed or removed;
* non-empty directories are never modified;
* only empty direct candidate leaves are removed;
* every observation/removal is receipted with the pre-removal owner identity.

Anything outside that boundary remains a hard collision for ``clusterctl``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import tempfile
from typing import Any, Sequence


SCHEMA = "laplace.cluster-candidate-empty-recovery/v1"
TARGET_KEYS = (
    "config_directory",
    "data_directory",
    "wal_directory",
    "temp_directory",
    "perfcache_directory",
    "socket_directory",
    "log_directory",
)


class RecoveryError(RuntimeError):
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
        raise RecoveryError(f"cannot load cluster contract {path}: {error}") from error
    if not isinstance(value, dict):
        raise RecoveryError("cluster contract must be one JSON object")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o750)
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(json.dumps(value, indent=2, sort_keys=True).encode("utf-8"))
            stream.write(b"\n")
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(0o640)
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def _target_result(key: str, target: Path) -> dict[str, Any]:
    result: dict[str, Any] = {"key": key, "path": str(target)}
    try:
        metadata = target.lstat()
    except FileNotFoundError:
        result.update({"state": "absent", "removed": False})
        return result
    except OSError as error:
        raise RecoveryError(f"cannot inspect candidate target {target}: {error}") from error

    result["owner_uid"] = metadata.st_uid
    result["owner_gid"] = metadata.st_gid
    if stat.S_ISLNK(metadata.st_mode):
        result.update({"state": "blocked-symlink", "removed": False})
        return result
    if not stat.S_ISDIR(metadata.st_mode):
        result.update({"state": "blocked-non-directory", "removed": False})
        return result

    try:
        with os.scandir(target) as entries:
            first = next(entries, None)
    except OSError as error:
        raise RecoveryError(f"cannot enumerate candidate target {target}: {error}") from error
    if first is not None:
        result.update(
            {
                "state": "blocked-nonempty",
                "removed": False,
                "first_entry": first.name,
            }
        )
        return result

    try:
        target.rmdir()
    except OSError as error:
        raise RecoveryError(f"cannot remove empty candidate target {target}: {error}") from error
    result.update({"state": "removed-empty", "removed": True})
    return result


def recover(contract: dict[str, Any]) -> dict[str, Any]:
    if contract.get("schema") != "laplace.postgresql-cluster-contract/v1":
        raise RecoveryError("unsupported PostgreSQL cluster contract")
    package = contract.get("package")
    instance = contract.get("instance")
    if not isinstance(package, dict) or not isinstance(instance, dict):
        raise RecoveryError("cluster contract omits package or instance state")

    active_link = Path(str(package.get("active_link", "")))
    if not active_link.is_absolute():
        raise RecoveryError("cluster active link must be absolute")

    targets: list[tuple[str, Path]] = []
    for key in TARGET_KEYS:
        target = Path(str(instance.get(key, "")))
        if not target.is_absolute():
            raise RecoveryError(f"cluster candidate target {key} must be absolute")
        if target == Path("/") or target.parent == target:
            raise RecoveryError(f"unsafe cluster candidate target for {key}: {target}")
        targets.append((key, target))

    active_present = active_link.exists() or active_link.is_symlink()
    if active_present:
        results = [
            {"key": key, "path": str(target), "state": "active-product-present", "removed": False}
            for key, target in targets
        ]
    else:
        results = [_target_result(key, target) for key, target in targets]

    blocked = [result for result in results if str(result["state"]).startswith("blocked-")]
    receipt: dict[str, Any] = {
        "schema": SCHEMA,
        "cluster_contract_sha256": hashlib.sha256(canonical_bytes(contract)).hexdigest(),
        "instance_id": instance.get("id"),
        "active_link": str(active_link),
        "active_product_present": active_present,
        "effective_uid": os.geteuid(),
        "effective_gid": os.getegid(),
        "removed_count": sum(1 for result in results if result["removed"]),
        "blocked_count": len(blocked),
        "results": results,
    }
    receipt["receipt_sha256"] = document_identity(receipt, "receipt_sha256")
    if blocked:
        details = ", ".join(f"{item['path']}:{item['state']}" for item in blocked)
        raise RecoveryError(f"uncommitted candidate recovery refused occupied state: {details}")
    return receipt


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", required=True)
    parser.add_argument("--output")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args([] if argv is None else argv)
    receipt = recover(load_json(Path(args.contract)))
    if args.output:
        write_json(Path(args.output), receipt)
    print(json.dumps(receipt, sort_keys=True))
    return 0


if __name__ == "__main__":
    import sys

    try:
        raise SystemExit(main(sys.argv[1:]))
    except RecoveryError as error:
        print(f"cluster_candidate_recovery: {error}", file=sys.stderr)
        raise SystemExit(1) from error
