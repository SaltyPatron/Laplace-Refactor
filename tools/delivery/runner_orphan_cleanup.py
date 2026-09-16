#!/usr/bin/env python3
"""Terminate only identified disposable PostgreSQL proof postmasters.

A runner-owned persistent cluster can legitimately be reparented to init in the
runner cgroup. Name, UID, parent and cgroup are therefore necessary but insufficient:
the postmaster must also select an owned physical data directory inside one of the
existing disposable proof namespaces. Signals use a pidfd and revalidated birth,
process and data identities so PID reuse cannot redirect cleanup.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import stat
import sys
import time
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
from proof_workspace_cleanup import RUNNER_WORKSPACE_PREFIXES, SAFE_NAME


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
    return dict(line.split(":", 1) for line in text.splitlines() if ":" in line)


def _start_ticks(process: Path) -> int | None:
    text = _read(process / "stat")
    if text is None:
        return None
    close = text.rfind(")")
    fields = text[close + 2:].split() if close >= 0 else []
    if len(fields) <= 19 or not fields[19].isdecimal():
        return None
    return int(fields[19])


def _boot_time_seconds(proc_root: Path) -> float:
    text = _read(proc_root / "stat")
    if text is None:
        raise CleanupError("cannot read process accounting clock")
    for line in text.splitlines():
        if line.startswith("btime "):
            return float(line.split()[1])
    raise CleanupError("process accounting clock has no boot time")


def _process_age_seconds(process: Path, proc_root: Path, now: float) -> float | None:
    ticks = _start_ticks(process)
    if ticks is None:
        return None
    started = _boot_time_seconds(proc_root) + ticks / os.sysconf("SC_CLK_TCK")
    return max(0.0, now - started)


def workspace_parents() -> tuple[Path, ...]:
    # These are the roots used by the existing proof runners and workflow sweep.
    values = [os.environ.get("RUNNER_TEMP"), os.environ.get("TMPDIR"),
              "/build/laplace/work", "/build/laplace/work/refactor-scratch", "/tmp"]
    return tuple(dict.fromkeys(Path(value) for value in values if value))


def _physical_directory(path: Path, uid: int | None = None) -> tuple[int, int] | None:
    try:
        info = path.lstat()
        if (not path.is_absolute() or not stat.S_ISDIR(info.st_mode)
                or path.resolve(strict=True) != path
                or (uid is not None and info.st_uid != uid)):
            return None
        return info.st_dev, info.st_ino
    except (OSError, RuntimeError):
        return None


def _owned_file(path: Path, uid: int, limit: int = 8192) -> dict[str, object] | None:
    try:
        with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK), "rb") as stream:
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode) or before.st_uid != uid or not 0 < before.st_size <= limit:
                return None
            body = stream.read(limit + 1)
            after = os.fstat(stream.fileno())
            fields = ("st_dev", "st_ino", "st_uid", "st_mode", "st_size", "st_mtime_ns", "st_ctime_ns")
            if (any(getattr(before, field) != getattr(after, field) for field in fields)
                    or len(body) != before.st_size
                    or any(getattr(path.lstat(), field) != getattr(before, field) for field in fields)):
                return None
        return {"device": before.st_dev, "inode": before.st_ino,
                "sha256": hashlib.sha256(body).hexdigest(), "body": body}
    except (OSError, ValueError):
        return None


def _identity(
    process: Path, *, caller_uid: int, caller_cgroup: str,
    parents: Sequence[Path],
) -> dict[str, object] | None:
    try:
        status = {key: value.strip() for key, value in _status_fields(process / "status").items()}
        uids = status.get("Uid", "").split()
        if (status.get("Name") != "postgres" or status.get("PPid") != "1"
                or len(uids) != 4 or any(value != str(caller_uid) for value in uids)
                or _read(process / "cgroup") != caller_cgroup):
            return None
        birth = _start_ticks(process)
        if birth is None:
            return None
        with (process / "cmdline").open("rb") as stream:
            command = stream.read(65537)
        if not command.endswith(b"\0") or len(command) > 65536:
            return None
        argv = command[:-1].decode("utf-8", "strict").split("\0")
        if argv.count("-D") != 1:
            return None
        index = argv.index("-D")
        if index + 1 == len(argv):
            return None
        data = Path(argv[index + 1])
        workspace = data.parent
        parent = workspace.parent
        if (data.name != "data" or parent not in parents
                or SAFE_NAME.fullmatch(workspace.name) is None
                or not any(workspace.name.startswith(prefix) for prefix in RUNNER_WORKSPACE_PREFIXES)):
            return None
        parent_identity = _physical_directory(parent)
        workspace_identity = _physical_directory(workspace, caller_uid)
        data_identity = _physical_directory(data, caller_uid)
        if None in (parent_identity, workspace_identity, data_identity):
            return None
        executable = os.readlink(process / "exe")
        if not Path(executable).is_absolute() or Path(executable).name != "postgres":
            return None
        executable_stat = (process / "exe").stat()
        if not stat.S_ISREG(executable_stat.st_mode):
            return None
        pidfile = _owned_file(data / "postmaster.pid", caller_uid)
        version = _owned_file(data / "PG_VERSION", caller_uid, 32)
        if pidfile is None or version is None:
            return None
        pid_lines = pidfile["body"].decode("utf-8", "strict").splitlines()
        if (len(pid_lines) < 2 or pid_lines[0] != process.name
                or pid_lines[1] != str(data)
                or not version["body"].strip().isdigit()):
            return None
        return {
            "pid": int(process.name), "start_ticks": birth,
            "uid": caller_uid, "cgroup": caller_cgroup,
            "executable": executable,
            "executable_identity": [executable_stat.st_dev, executable_stat.st_ino],
            "data_directory": str(data), "workspace_parent": str(parent),
            "parent_identity": list(parent_identity),
            "workspace_identity": list(workspace_identity),
            "data_identity": list(data_identity),
            "postmaster_file": {key: value for key, value in pidfile.items() if key != "body"},
            "version_file": {key: value for key, value in version.items() if key != "body"},
        }
    except (OSError, UnicodeError, ValueError, RuntimeError):
        return None


def discover(
    proc_root: Path = Path("/proc"),
    *,
    caller_pid: int | None = None,
    caller_uid: int | None = None,
    minimum_age_seconds: int = 300,
    parents: Sequence[Path] | None = None,
) -> list[dict[str, object]]:
    if minimum_age_seconds < 0:
        raise CleanupError("minimum process age cannot be negative")
    caller_pid = os.getpid() if caller_pid is None else caller_pid
    caller_uid = os.geteuid() if caller_uid is None else caller_uid
    caller_cgroup = _read(proc_root / str(caller_pid) / "cgroup")
    if not caller_cgroup:
        raise CleanupError("cannot identify the caller's process cgroup")
    selected_parents = workspace_parents() if parents is None else tuple(parents)
    now = time.time()
    candidates: list[dict[str, object]] = []
    try:
        processes = list(proc_root.iterdir())
    except OSError as error:
        raise CleanupError(f"cannot enumerate processes: {error}") from error
    for process in processes:
        if not process.name.isdigit() or int(process.name) == caller_pid:
            continue
        identity = _identity(process, caller_uid=caller_uid,
                             caller_cgroup=caller_cgroup, parents=selected_parents)
        if identity is None:
            continue
        age = _process_age_seconds(process, proc_root, now)
        if age is None or age < minimum_age_seconds:
            continue
        candidates.append({**identity, "age_seconds": age})
    return sorted(candidates, key=lambda item: int(item["pid"]))


def terminate(
    candidates: Sequence[dict[str, object]], grace_seconds: float,
    *, proc_root: Path = Path("/proc"), caller_pid: int | None = None,
    caller_uid: int | None = None, parents: Sequence[Path] | None = None,
) -> list[int]:
    if not math.isfinite(grace_seconds) or grace_seconds < 0:
        raise CleanupError("termination grace must be finite and nonnegative")
    if not candidates:
        return []
    if not hasattr(os, "pidfd_open") or not hasattr(signal, "pidfd_send_signal"):
        raise CleanupError("safe orphan cleanup requires Linux pidfd signalling")
    caller_pid = os.getpid() if caller_pid is None else caller_pid
    caller_uid = os.geteuid() if caller_uid is None else caller_uid
    selected_parents = workspace_parents() if parents is None else tuple(parents)
    descriptors: dict[int, tuple[int, dict[str, object]]] = {}

    def unchanged(candidate: dict[str, object]) -> bool:
        cgroup = _read(proc_root / str(caller_pid) / "cgroup")
        if not cgroup:
            return False
        expected = {key: value for key, value in candidate.items() if key != "age_seconds"}
        return _identity(proc_root / str(candidate["pid"]), caller_uid=caller_uid,
                         caller_cgroup=cgroup, parents=selected_parents) == expected

    forced: list[int] = []
    try:
        for candidate in candidates:
            pid = int(candidate["pid"])
            if not unchanged(candidate):
                continue
            try:
                descriptor = os.pidfd_open(pid, 0)
            except ProcessLookupError:
                continue
            descriptors[pid] = (descriptor, candidate)
            # Bind the proc observation to the opened process handle, then signal it.
            if unchanged(candidate):
                try:
                    signal.pidfd_send_signal(descriptor, signal.SIGTERM, None, 0)
                except ProcessLookupError:
                    pass
        deadline = time.monotonic() + grace_seconds
        remaining = dict(descriptors)
        while remaining and time.monotonic() < deadline:
            for pid, (_, candidate) in tuple(remaining.items()):
                if not unchanged(candidate):
                    remaining.pop(pid)
            if remaining:
                time.sleep(min(0.05, max(0.0, deadline - time.monotonic())))
        for pid, (descriptor, candidate) in sorted(remaining.items()):
            if not unchanged(candidate):
                continue
            try:
                signal.pidfd_send_signal(descriptor, signal.SIGKILL, None, 0)
                forced.append(pid)
            except ProcessLookupError:
                pass
    finally:
        for descriptor, _ in descriptors.values():
            os.close(descriptor)
    return forced


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
            {"schema": "laplace.runner-orphan-cleanup/v1",
             "mode": "terminate" if arguments.terminate else "observe",
             "candidates": candidates, "forced_kill_pids": forced},
            sort_keys=True, separators=(",", ":"),
        ) + "\n"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (CleanupError, ValueError, OSError) as error:
        print(f"runner-orphan-cleanup: {error}", file=sys.stderr)
        raise SystemExit(1) from error
