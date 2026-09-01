#!/usr/bin/env python3
"""Terminate stale database workers orphaned by interrupted runner jobs.

The cleanup is intentionally narrower than a process-name sweep.  A candidate
must be owned by the caller, have init as its parent, be older than the
configured minimum, and remain in the caller's exact cgroup.  Durable services
in another systemd unit and live descendants of the current job cannot match.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import sys
import time
from typing import Sequence


class CleanupError(RuntimeError):
    pass


def _read(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8").strip()
    except (FileNotFoundError, PermissionError, OSError, UnicodeError):
        return None


def _status_fields(path: Path) -> dict[str, str]:
    text = _read(path)
    if text is None:
        return {}
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if ":" in line:
            name, value = line.split(":", 1)
            fields[name] = value.strip()
    return fields


def _boot_time_seconds(proc_root: Path) -> float:
    text = _read(proc_root / "stat")
    if text is None:
        raise CleanupError("cannot read process accounting clock")
    ticks = os.sysconf("SC_CLK_TCK")
    for line in text.splitlines():
        if line.startswith("btime "):
            return float(line.split()[1])
    raise CleanupError("process accounting clock has no boot time")


def _process_age_seconds(process: Path, proc_root: Path, now: float) -> float | None:
    text = _read(process / "stat")
    if text is None:
        return None
    close = text.rfind(")")
    if close < 0:
        return None
    fields = text[close + 2 :].split()
    if len(fields) <= 19:
        return None
    start_ticks = int(fields[19])
    started = _boot_time_seconds(proc_root) + start_ticks / os.sysconf("SC_CLK_TCK")
    return max(0.0, now - started)


def discover(
    proc_root: Path = Path("/proc"),
    *,
    caller_pid: int | None = None,
    caller_uid: int | None = None,
    minimum_age_seconds: int = 300,
) -> list[dict[str, object]]:
    if minimum_age_seconds < 0:
        raise CleanupError("minimum process age cannot be negative")
    caller_pid = os.getpid() if caller_pid is None else caller_pid
    caller_uid = os.geteuid() if caller_uid is None else caller_uid
    caller_cgroup = _read(proc_root / str(caller_pid) / "cgroup")
    if not caller_cgroup:
        raise CleanupError("cannot identify the caller's process cgroup")
    now = time.time()
    candidates: list[dict[str, object]] = []
    try:
        processes = list(proc_root.iterdir())
    except OSError as error:
        raise CleanupError(f"cannot enumerate processes: {error}") from error
    for process in processes:
        if not process.name.isdigit() or int(process.name) == caller_pid:
            continue
        status = _status_fields(process / "status")
        uid_fields = status.get("Uid", "").split()
        if (
            status.get("Name") != "postgres"
            or status.get("PPid") != "1"
            or not uid_fields
            or int(uid_fields[0]) != caller_uid
            or _read(process / "cgroup") != caller_cgroup
        ):
            continue
        age = _process_age_seconds(process, proc_root, now)
        if age is None or age < minimum_age_seconds:
            continue
        candidates.append({"pid": int(process.name), "age_seconds": age})
    return sorted(candidates, key=lambda item: int(item["pid"]))


def terminate(candidates: Sequence[dict[str, object]], grace_seconds: float) -> list[int]:
    if grace_seconds < 0:
        raise CleanupError("termination grace cannot be negative")
    pids = [int(candidate["pid"]) for candidate in candidates]
    for pid in pids:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    deadline = time.monotonic() + grace_seconds
    remaining = set(pids)
    while remaining and time.monotonic() < deadline:
        for pid in tuple(remaining):
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                remaining.remove(pid)
        if remaining:
            time.sleep(0.05)
    for pid in sorted(remaining):
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    return sorted(remaining)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--minimum-age-seconds", type=int, default=300)
    parser.add_argument("--grace-seconds", type=float, default=5.0)
    parser.add_argument("--terminate", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_args(argv)
    candidates = discover(minimum_age_seconds=arguments.minimum_age_seconds)
    forced = terminate(candidates, arguments.grace_seconds) if arguments.terminate else []
    sys.stdout.write(
        json.dumps(
            {
                "schema": "laplace.runner-orphan-cleanup/v1",
                "mode": "terminate" if arguments.terminate else "observe",
                "candidates": candidates,
                "forced_kill_pids": forced,
            },
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (CleanupError, ValueError) as error:
        print(f"runner-orphan-cleanup: {error}", file=sys.stderr)
        raise SystemExit(1) from error
