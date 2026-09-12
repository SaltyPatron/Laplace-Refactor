#!/usr/bin/env python3
"""Report host prerequisite path state without conflating ENOENT and EACCES."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import stat
from typing import Any


def describe(path: Path) -> dict[str, Any]:
    result: dict[str, Any] = {"path": str(path)}
    try:
        value = path.stat()
    except FileNotFoundError as error:
        result.update(state="missing", errno=error.errno, error=str(error))
        return result
    except PermissionError as error:
        result.update(state="inaccessible", errno=error.errno, error=str(error))
        return result
    except OSError as error:
        result.update(state="error", errno=error.errno, error=str(error))
        return result
    result.update(
        state="present",
        mode=oct(stat.S_IMODE(value.st_mode)),
        uid=value.st_uid,
        gid=value.st_gid,
        readable=os.access(path, os.R_OK),
        writable=os.access(path, os.W_OK),
        executable=os.access(path, os.X_OK),
        kind=(
            "directory" if stat.S_ISDIR(value.st_mode)
            else "file" if stat.S_ISREG(value.st_mode)
            else "other"
        ),
    )
    return result


def chain(path: Path) -> list[dict[str, Any]]:
    absolute = path.absolute()
    current = Path(absolute.parts[0])
    output = [describe(current)]
    for part in absolute.parts[1:]:
        current /= part
        output.append(describe(current))
        if output[-1]["state"] in {"missing", "inaccessible", "error"}:
            break
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--require-readable-file", action="store_true")
    args = parser.parse_args()

    target = Path(args.path)
    payload = {
        "schema": "laplace.host-prerequisite-path-probe/v1",
        "euid": os.geteuid(),
        "egid": os.getegid(),
        "target": describe(target),
        "path_chain": chain(target),
    }
    print(json.dumps(payload, indent=2, sort_keys=True))

    if not args.require_readable_file:
        return 0
    target_state = payload["target"]
    return 0 if (
        target_state.get("state") == "present"
        and target_state.get("kind") == "file"
        and target_state.get("readable") is True
    ) else 1


if __name__ == "__main__":
    raise SystemExit(main())
