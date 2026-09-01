#!/usr/bin/env python3
"""Run one PostgreSQL test client under exact wall, disk, WAL, and RSS ceilings."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from typing import Sequence


def allocated_bytes(root: Path) -> int:
    total = 0
    pending = [root]
    while pending:
        current = pending.pop()
        try:
            with os.scandir(current) as entries:
                for entry in entries:
                    try:
                        metadata = entry.stat(follow_symlinks=False)
                    except (FileNotFoundError, PermissionError, OSError):
                        continue
                    total += metadata.st_blocks * 512
                    if entry.is_dir(follow_symlinks=False):
                        pending.append(Path(entry.path))
        except (FileNotFoundError, PermissionError, NotADirectoryError, OSError):
            continue
    return total


def process_tree(root_pid: int) -> set[int]:
    found: set[int] = set()
    pending = [root_pid]
    while pending:
        pid = pending.pop()
        if pid in found:
            continue
        found.add(pid)
        try:
            children = Path(f"/proc/{pid}/task/{pid}/children").read_text(
                encoding="ascii"
            )
        except (FileNotFoundError, PermissionError, OSError, UnicodeError):
            continue
        pending.extend(int(child) for child in children.split() if child.isdigit())
    return found


def resident_bytes(root_pid: int) -> int:
    total = 0
    for pid in process_tree(root_pid):
        try:
            status = Path(f"/proc/{pid}/status").read_text(encoding="ascii")
        except (FileNotFoundError, PermissionError, OSError, UnicodeError):
            continue
        for line in status.splitlines():
            if line.startswith("VmRSS:"):
                fields = line.split()
                if len(fields) >= 2 and fields[1].isdigit():
                    total += int(fields[1]) * 1024
                break
    return total


def parse_args(arguments: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-directory", type=Path, required=True)
    parser.add_argument("--workspace-directory", type=Path, required=True)
    parser.add_argument("--postmaster-pid", type=int, required=True)
    parser.add_argument("--max-wall-seconds", type=float, required=True)
    parser.add_argument("--max-data-bytes", type=int, required=True)
    parser.add_argument("--max-wal-bytes", type=int, required=True)
    parser.add_argument("--max-workspace-bytes", type=int, required=True)
    parser.add_argument("--max-rss-bytes", type=int, required=True)
    parser.add_argument("--sample-seconds", type=float, default=0.1)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    parsed = parser.parse_args(arguments)
    if parsed.command and parsed.command[0] == "--":
        parsed.command = parsed.command[1:]
    numeric = (
        parsed.postmaster_pid,
        parsed.max_wall_seconds,
        parsed.max_data_bytes,
        parsed.max_wal_bytes,
        parsed.max_workspace_bytes,
        parsed.max_rss_bytes,
        parsed.sample_seconds,
    )
    if not parsed.command or any(value <= 0 for value in numeric):
        parser.error("command, process identity, ceilings, and sample interval must be positive")
    return parsed


def run(arguments: argparse.Namespace) -> int:
    baseline = {
        "data_bytes": allocated_bytes(arguments.data_directory),
        "wal_bytes": allocated_bytes(arguments.data_directory / "pg_wal"),
        "workspace_bytes": allocated_bytes(arguments.workspace_directory),
    }
    started = time.monotonic()
    child = subprocess.Popen(arguments.command)
    maxima = {"data_bytes": 0, "wal_bytes": 0, "workspace_bytes": 0, "rss_bytes": 0}
    breach: dict[str, int | float] | None = None
    while child.poll() is None:
        elapsed = time.monotonic() - started
        observed = {
            "data_bytes": max(
                0, allocated_bytes(arguments.data_directory) - baseline["data_bytes"]
            ),
            "wal_bytes": max(
                0,
                allocated_bytes(arguments.data_directory / "pg_wal")
                - baseline["wal_bytes"],
            ),
            "workspace_bytes": max(
                0,
                allocated_bytes(arguments.workspace_directory)
                - baseline["workspace_bytes"],
            ),
            "rss_bytes": resident_bytes(arguments.postmaster_pid),
        }
        maxima = {name: max(maxima[name], value) for name, value in observed.items()}
        ceilings = {
            "data_bytes": arguments.max_data_bytes,
            "wal_bytes": arguments.max_wal_bytes,
            "workspace_bytes": arguments.max_workspace_bytes,
            "rss_bytes": arguments.max_rss_bytes,
        }
        for dimension, value in observed.items():
            if value > ceilings[dimension]:
                breach = {
                    "dimension": dimension,
                    "observed": value,
                    "ceiling": ceilings[dimension],
                }
                break
        if breach is None and elapsed > arguments.max_wall_seconds:
            breach = {
                "dimension": "wall_seconds",
                "observed": elapsed,
                "ceiling": arguments.max_wall_seconds,
            }
        if breach is not None:
            child.send_signal(signal.SIGINT)
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.terminate()
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
            break
        time.sleep(arguments.sample_seconds)

    elapsed = time.monotonic() - started
    receipt = {
        "schema": "laplace.postgresql-test-resource-guard/v1",
        "result": "resource-ceiling-breached" if breach is not None else "completed",
        "elapsed_seconds": round(elapsed, 6),
        "accounting": {
            "data_bytes": "allocated growth from pre-command baseline",
            "wal_bytes": "allocated growth from pre-command baseline",
            "workspace_bytes": "allocated growth from pre-command baseline",
            "rss_bytes": "absolute PostgreSQL process-tree resident bytes",
        },
        "baseline": baseline,
        "maxima": maxima,
        "ceilings": {
            "wall_seconds": arguments.max_wall_seconds,
            "data_bytes": arguments.max_data_bytes,
            "wal_bytes": arguments.max_wal_bytes,
            "workspace_bytes": arguments.max_workspace_bytes,
            "rss_bytes": arguments.max_rss_bytes,
        },
        "breach": breach,
        "client_returncode": child.returncode,
    }
    arguments.receipt.write_text(
        json.dumps(receipt, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    if breach is not None:
        print(json.dumps(receipt, sort_keys=True), file=sys.stderr)
        return 90
    return int(child.returncode or 0)


def main(arguments: Sequence[str]) -> int:
    return run(parse_args(arguments))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
