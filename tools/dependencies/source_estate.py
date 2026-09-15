#!/usr/bin/env python3
"""Persist the Refactor runner's writable source-generation parent in its estate."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import sys
import tempfile

SCHEMA = "laplace.refactor-source-estate-selection/v1"


def writable_parent(path: Path) -> bool:
    if path.is_symlink() or not path.is_dir() or not path.stat().st_mode & stat.S_IWGRP or not os.access(path, os.W_OK | os.X_OK):
        return False
    lock = path / ".source-acquisition.lock"
    return not lock.is_symlink() and (not lock.exists() or lock.is_file() and os.access(lock, os.W_OK))


def select_generation(lock: Path, external: Path) -> Path:
    physical = external.resolve(strict=True)
    if not physical.is_dir() or physical == Path("/") or (physical / "PG_VERSION").exists():
        raise ValueError(f"Configured external estate is not a source directory: {external}")
    destination = physical / "refactor-source-selection.json"
    allowed = {physical / "source-generations", physical / "refactor-source-generations"}
    if destination.exists() or destination.is_symlink():
        if destination.is_symlink() or not destination.is_file() or destination.stat().st_size > 16384:
            raise ValueError(f"Source selection must be a bounded physical receipt: {destination}")
        selected = json.loads(destination.read_text())
        parent = Path(selected.get("generation_parent", ""))
        if selected.get("schema") != SCHEMA or selected.get("physical_external") != str(physical) or parent not in allowed:
            raise ValueError(f"Source selection does not match the configured external estate: {destination}")
    else:
        legacy = physical / "source-generations"
        parent = legacy if writable_parent(legacy) else physical / "refactor-source-generations"
        reason = "existing shared generation parent is writable" if parent == legacy else "dedicated Refactor parent within the configured estate; existing operator generation parent or publication lock is not writable"
        if parent.is_symlink() or (parent.exists() and not parent.is_dir()):
            raise ValueError(f"Source-generation parent must be physical: {parent}")
        if not parent.exists():
            parent.mkdir(mode=0o2775)
            parent.chmod(stat.S_IMODE(parent.stat().st_mode) | 0o2070)
        if not writable_parent(parent):
            raise PermissionError(f"Selected source-generation parent is not writable: {parent}")
        selected = {"schema": SCHEMA, "configured_external": str(external), "physical_external": str(physical), "generation_parent": str(parent), "reason": reason, "selecting_uid": os.geteuid(), "selecting_gid": os.getegid()}
        content = (json.dumps(selected, sort_keys=True, indent=2) + "\n").encode()
        descriptor, temporary = tempfile.mkstemp(prefix=".refactor-source-selection-", dir=physical)
        try:
            with os.fdopen(descriptor, "wb") as stream:
                os.fchmod(stream.fileno(), 0o664)
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
            try:
                os.link(temporary, destination, follow_symlinks=False)
            except FileExistsError:
                return select_generation(lock, external)
        finally:
            os.unlink(temporary)
    if parent.is_symlink() or not parent.is_dir():
        raise ValueError(f"Persisted source-generation parent is unavailable: {parent}")
    fingerprint = hashlib.sha256(lock.read_bytes()).hexdigest()
    print(f"Refactor source estate: {external} -> {physical}; selected parent {parent}; selection receipt {destination}", file=sys.stderr)
    return parent / fingerprint


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=Path(__file__).resolve().parents[2] / "dependencies/lock.json")
    parser.add_argument("--external-root", type=Path, default=Path("/opt/laplace/external"))
    args = parser.parse_args()
    try:
        print(select_generation(args.lock, args.external_root))
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(f"Source estate selection failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
