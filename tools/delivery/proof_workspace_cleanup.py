#!/usr/bin/env python3
"""Remove disposable self-hosted proof workspaces without crossing authority boundaries.

Durable receipts and published product generations are deliberately outside this tool's
scope. Callers name direct children of one runner-temporary root. The tool refuses path
traversal, symlink roots, nested mount/device boundaries, and workspaces still referenced
by a live process. A minimum-age gate supports recovery of residue from interrupted
self-hosted jobs without allowing a cleanup pass to race freshly created state.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import stat
import sys
import time
from typing import Sequence


SAFE_NAME = re.compile(
    r"^(?:laplace-postgres-test\.[A-Za-z0-9]+|"
    r"laplace-[a-z0-9][a-z0-9._-]*|lp-pg\.[A-Za-z0-9]+)$"
)
SAFE_DISCOVERY_PREFIXES = frozenset(("laplace-postgres-test.", "lp-pg."))


class CleanupError(RuntimeError):
    pass


def _physical_root(path: Path) -> Path:
    if not path.is_absolute():
        raise CleanupError("runner temporary root must be absolute")
    try:
        metadata = path.lstat()
    except OSError as error:
        raise CleanupError(f"cannot inspect runner temporary root {path}: {error}") from error
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
        raise CleanupError("runner temporary root must be one physical directory")
    return path.resolve(strict=True)


def _target(root: Path, name: str) -> Path:
    if SAFE_NAME.fullmatch(name) is None:
        raise CleanupError(f"unsafe disposable workspace name: {name!r}")
    target = root / name
    if target.parent != root:
        raise CleanupError("disposable workspace must be a direct child of runner temp")
    return target


def discover_names(root: Path, prefixes: Sequence[str]) -> list[str]:
    """Discover only the two exact disposable PostgreSQL workspace namespaces."""

    physical_root = _physical_root(root)
    for prefix in prefixes:
        if prefix not in SAFE_DISCOVERY_PREFIXES:
            raise CleanupError(f"unsafe disposable workspace discovery prefix: {prefix!r}")
    try:
        names = [entry.name for entry in os.scandir(physical_root)]
    except OSError as error:
        raise CleanupError(f"cannot enumerate runner temporary root {root}: {error}") from error
    return sorted(
        name
        for name in names
        if SAFE_NAME.fullmatch(name) is not None
        and any(name.startswith(prefix) for prefix in prefixes)
    )


def _contains(root: Path, candidate: Path) -> bool:
    try:
        candidate.relative_to(root)
        return True
    except ValueError:
        return False


def _proc_link_target(link: Path) -> Path | None:
    try:
        raw = os.readlink(link)
    except (FileNotFoundError, PermissionError, OSError):
        return None
    if raw.endswith(" (deleted)"):
        # Linux appends this marker when a process still holds an inode whose
        # namespace entry is already gone. It cannot reference a later tree
        # recreated at the same pathname and therefore must not block cleanup
        # of that distinct current workspace.
        return None
    candidate = Path(raw)
    if not candidate.is_absolute():
        return None
    return candidate


def _active_references(target: Path, proc_root: Path = Path("/proc")) -> list[str]:
    """Return accessible process references into target without following target links."""

    references: set[str] = set()
    try:
        processes = list(proc_root.iterdir())
    except OSError:
        return []
    for process in processes:
        if not process.name.isdigit() or not process.is_dir():
            continue
        for name in ("cwd", "exe", "root"):
            candidate = _proc_link_target(process / name)
            if candidate is not None and (candidate == target or _contains(target, candidate)):
                references.add(f"{process.name}:{name}")
        fd_root = process / "fd"
        try:
            descriptors = list(fd_root.iterdir())
        except (FileNotFoundError, PermissionError, OSError):
            continue
        for descriptor in descriptors:
            candidate = _proc_link_target(descriptor)
            if candidate is not None and (candidate == target or _contains(target, candidate)):
                references.add(f"{process.name}:fd/{descriptor.name}")
    return sorted(references)


def _remove_entry(path: Path, expected_device: int) -> int:
    """Remove one entry without following symlinks or crossing a filesystem boundary."""

    metadata = path.lstat()
    if metadata.st_dev != expected_device:
        raise CleanupError(f"refusing cross-device cleanup at {path}")
    if stat.S_ISLNK(metadata.st_mode):
        path.unlink()
        return 1
    if stat.S_ISDIR(metadata.st_mode):
        removed = 0
        with os.scandir(path) as entries:
            children = [Path(entry.path) for entry in entries]
        for child in children:
            removed += _remove_entry(child, expected_device)
        path.rmdir()
        return removed + 1
    path.unlink()
    return 1


def cleanup(
    root: Path,
    names: Sequence[str],
    *,
    minimum_age_seconds: int = 0,
    proc_root: Path = Path("/proc"),
) -> dict[str, object]:
    if minimum_age_seconds < 0:
        raise CleanupError("minimum workspace age cannot be negative")
    physical_root = _physical_root(root)
    root_device = physical_root.lstat().st_dev
    results: list[dict[str, object]] = []
    seen: set[str] = set()
    now = time.time()

    for name in names:
        if name in seen:
            raise CleanupError(f"duplicate disposable workspace name: {name}")
        seen.add(name)
        target = _target(physical_root, name)
        try:
            metadata = target.lstat()
        except FileNotFoundError:
            results.append({"name": name, "state": "absent", "removed_entries": 0})
            continue
        except OSError as error:
            raise CleanupError(f"cannot inspect disposable workspace {target}: {error}") from error

        if stat.S_ISLNK(metadata.st_mode):
            raise CleanupError(f"refusing symlink workspace root: {target}")
        if metadata.st_dev != root_device:
            raise CleanupError(f"refusing cross-device workspace root: {target}")
        age_seconds = max(0.0, now - metadata.st_mtime)
        if age_seconds < minimum_age_seconds:
            raise CleanupError(
                f"refusing recent workspace {target}: age {age_seconds:.3f}s is below "
                f"minimum {minimum_age_seconds}s"
            )
        active = _active_references(target, proc_root)
        if active:
            raise CleanupError(
                f"refusing active workspace {target}: process references " + ", ".join(active[:16])
            )
        removed = _remove_entry(target, root_device)
        results.append(
            {
                "name": name,
                "state": "removed",
                "removed_entries": removed,
                "age_seconds": age_seconds,
            }
        )

    return {
        "schema": "laplace.proof-workspace-cleanup/v1",
        "runner_temp": str(physical_root),
        "minimum_age_seconds": minimum_age_seconds,
        "results": results,
    }


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner-temp", required=True)
    parser.add_argument("--name", action="append", default=[])
    parser.add_argument("--discover-prefix", action="append", default=[])
    parser.add_argument("--minimum-age-seconds", type=int, default=0)
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_args(argv)
    if not arguments.name and not arguments.discover_prefix:
        raise CleanupError("at least one explicit name or safe discovery prefix is required")
    names = list(arguments.name)
    names.extend(discover_names(Path(arguments.runner_temp), arguments.discover_prefix))
    result = cleanup(
        Path(arguments.runner_temp),
        names,
        minimum_age_seconds=arguments.minimum_age_seconds,
    )
    sys.stdout.write(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except CleanupError as error:
        print(f"proof-workspace-cleanup: {error}", file=sys.stderr)
        raise SystemExit(1) from error
