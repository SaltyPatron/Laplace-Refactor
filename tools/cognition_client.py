#!/usr/bin/env python3
"""Execute a prompt against the active persistent Laplace cognition service."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import sys

RELATIONS = {"container", "constituent", "predecessor", "successor", "cooccur", "semantic"}
DEFAULT_SOCKET = Path("/opt/laplace/runtime/laplace-cognition.sock")
MAXIMUM_RESPONSE_BYTES = 16 * 1024 * 1024


def parse_relations(raw: str) -> list[str]:
    values = [value.strip() for value in raw.split(",") if value.strip()]
    if not values or any(value not in RELATIONS for value in values):
        raise ValueError(
            "relations must be a comma-separated sequence of: "
            + ", ".join(sorted(RELATIONS))
        )
    return values


def read_prompt(argument: str | None) -> str:
    if argument is not None:
        return argument
    value = sys.stdin.read()
    if not value:
        raise ValueError("provide a prompt argument or pipe prompt text on stdin")
    return value


def read_response(connection: socket.socket) -> tuple[int, bytes]:
    chunks: list[bytes] = []
    total = 0
    while True:
        block = connection.recv(65536)
        if not block:
            break
        total += len(block)
        if total > MAXIMUM_RESPONSE_BYTES:
            raise RuntimeError("cognition service response exceeds 16 MiB")
        chunks.append(block)
    raw = b"".join(chunks)
    marker = raw.find(b"\r\n\r\n")
    if marker < 0:
        raise RuntimeError("cognition service returned a malformed HTTP response")
    header = raw[:marker].decode("iso-8859-1")
    body = raw[marker + 4:]
    first = header.split("\r\n", 1)[0].split(" ", 2)
    if len(first) < 2 or not first[1].isdigit():
        raise RuntimeError("cognition service returned a malformed HTTP status")
    return int(first[1]), body


def request_cognition(socket_path: Path, prompt: str, relations: list[str]) -> dict:
    body = json.dumps(
        {"prompt": prompt, "relations": relations},
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")
    request = (
        b"POST /v1/cognition HTTP/1.1\r\n"
        b"Host: laplace.local\r\n"
        b"Content-Type: application/json; charset=utf-8\r\n"
        + f"Content-Length: {len(body)}\r\n".encode("ascii")
        + b"Connection: close\r\n\r\n"
        + body
    )
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.settimeout(310)
    try:
        connection.connect(str(socket_path))
        connection.sendall(request)
        status, payload = read_response(connection)
    except OSError as error:
        raise RuntimeError(f"cognition service is unavailable at {socket_path}: {error}") from error
    finally:
        connection.close()
    try:
        value = json.loads(payload)
    except json.JSONDecodeError as error:
        raise RuntimeError("cognition service returned invalid JSON") from error
    if not isinstance(value, dict):
        raise RuntimeError("cognition service returned an invalid response")
    if status >= 400:
        detail = value.get("error")
        execution = value.get("execution")
        if not detail and isinstance(execution, dict):
            detail = (
                f"status={execution.get('status')} failed_step={execution.get('failed_step')} "
                f"native_status={execution.get('native_status')}"
            )
        raise RuntimeError(detail or f"cognition service returned HTTP {status}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prompt", nargs="?", help="exact UTF-8 prompt; omit to read stdin")
    parser.add_argument(
        "--relations",
        default="semantic",
        help="ordered cognition relation chain (default: semantic)",
    )
    parser.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    parser.add_argument("--json", action="store_true", help="print the complete execution result")
    args = parser.parse_args()
    try:
        prompt = read_prompt(args.prompt)
        relations = parse_relations(args.relations)
        result = request_cognition(args.socket, prompt, relations)
        if result.get("status") != 0:
            raise RuntimeError(f"cognition returned status {result.get('status')}")
        if args.json:
            print(json.dumps(result, ensure_ascii=False, sort_keys=True))
            return 0
        output = result.get("output_utf8")
        if isinstance(output, str):
            sys.stdout.write(output)
            if not output.endswith("\n"):
                sys.stdout.write("\n")
            return 0
        output_hex = result.get("output_hex")
        if not isinstance(output_hex, str):
            raise RuntimeError("cognition returned neither UTF-8 nor binary output")
        sys.stdout.buffer.write(bytes.fromhex(output_hex))
        return 0
    except (RuntimeError, ValueError) as error:
        print(f"laplace-cognition: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
