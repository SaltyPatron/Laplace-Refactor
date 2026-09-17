#!/usr/bin/env python3
"""Run one managed build without persistent servers or an inherited host lock."""

from __future__ import annotations

import errno
import os
import sys


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: build-dotnet.py DOTNET PROJECT [BUILD_ARGUMENT ...]", file=sys.stderr)
        return 64

    environment = dict(os.environ)
    environment["MSBUILDDISABLENODEREUSE"] = "1"
    environment["DOTNET_CLI_USE_MSBUILD_SERVER"] = "0"
    environment["MSBUILDUSESERVER"] = "0"
    # FD 9 belongs to run-exclusive.sh. CMake/Ninja and the foreground shell
    # retain their descriptors; only this managed child loses it at exec.
    try:
        os.set_inheritable(9, False)
    except OSError as error:
        if error.errno != errno.EBADF:
            raise

    os.execvpe(
        sys.argv[1],
        [
            sys.argv[1],
            "build",
            *sys.argv[2:],
            "--disable-build-servers",
            "-p:UseSharedCompilation=false",
            "-nodeReuse:false",
        ],
        environment,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
