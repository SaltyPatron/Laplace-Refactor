#!/usr/bin/env python3
"""Run the installed Laplace cognition core and local API transports as one service."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

DEFAULT_SOCKET = Path("/opt/laplace/runtime/laplace-cognition.sock")
DEFAULT_API_LISTEN = "127.0.0.1"
DEFAULT_API_PORT = 55434


def sibling(name: str) -> Path:
    path = Path(__file__).resolve().parent / name
    if not path.is_file() or path.is_symlink():
        raise RuntimeError(f"installed Laplace service component is unavailable: {path}")
    if not os.access(path, os.X_OK):
        raise RuntimeError(f"installed Laplace service component is not executable: {path}")
    return path


def terminate(processes: list[subprocess.Popen[bytes]]) -> None:
    for process in processes:
        if process.poll() is None:
            try:
                process.terminate()
            except ProcessLookupError:
                pass
    deadline = time.monotonic() + 10.0
    for process in processes:
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
    for process in processes:
        if process.poll() is None:
            try:
                process.kill()
            except ProcessLookupError:
                pass
    for process in processes:
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    parser.add_argument("--api-listen", default=DEFAULT_API_LISTEN)
    parser.add_argument("--api-port", type=int, default=DEFAULT_API_PORT)
    parser.add_argument("--api-bearer-token-file", type=Path, default=None)
    args = parser.parse_args()

    core = sibling("laplace-cognition-core-service")
    api = sibling("laplace-openai-api")
    api_command = [
        str(api),
        "--listen",
        args.api_listen,
        "--port",
        str(args.api_port),
        "--cognition-socket",
        str(args.socket),
    ]
    if args.api_bearer_token_file is not None:
        api_command.extend(["--bearer-token-file", str(args.api_bearer_token_file)])

    processes: list[subprocess.Popen[bytes]] = []
    stopping = False

    def request_stop(_signum: int, _frame: object) -> None:
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)

    try:
        core_process = subprocess.Popen(
            [str(core), "--socket", str(args.socket)],
            stdin=subprocess.DEVNULL,
            close_fds=True,
        )
        processes.append(core_process)
        api_process = subprocess.Popen(
            api_command,
            stdin=subprocess.DEVNULL,
            close_fds=True,
        )
        processes.append(api_process)
        print(
            "laplace-cognition-service: "
            f"core_pid={core_process.pid} api_pid={api_process.pid} "
            f"socket={args.socket} api=http://{args.api_listen}:{args.api_port}/v1",
            file=sys.stderr,
        )

        while not stopping:
            for name, process in (("cognition-core", core_process), ("openai-api", api_process)):
                code = process.poll()
                if code is not None:
                    raise RuntimeError(f"{name} exited with status {code}")
            time.sleep(0.2)
    except KeyboardInterrupt:
        stopping = True
    finally:
        terminate(processes)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"laplace-cognition-service: {error}", file=sys.stderr)
        raise SystemExit(1) from error
