#!/usr/bin/env python3
"""Execute a prompt against the active persistent Laplace cognition service."""
from __future__ import annotations

import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import secrets
import socket
import stat
import sys
from typing import Any

RELATIONS = {"container", "constituent", "predecessor", "successor", "cooccur", "semantic"}
HEX256 = re.compile(r"^[0-9a-f]{64}$")
HEXBYTES = re.compile(r"^[0-9a-f]*$")
SESSION_NAME = re.compile(r"^[A-Za-z0-9._-]{1,64}$")
DEFAULT_SOCKET = Path("/opt/laplace/runtime/laplace-cognition.sock")
MAXIMUM_RESPONSE_BYTES = 24 * 1024 * 1024
MAXIMUM_CHECKPOINT_BYTES = 8 * 1024 * 1024
SESSION_SCHEMA = "laplace.cognition-client-session/v1"


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


def default_state_root() -> Path:
    xdg = os.environ.get("XDG_STATE_HOME")
    if xdg:
        base = Path(xdg).expanduser()
    else:
        base = Path.home() / ".local/state"
    return base / "laplace/cognition"


def validate_session_name(value: str) -> str:
    if SESSION_NAME.fullmatch(value) is None or value in {".", ".."}:
        raise ValueError("session name must use 1-64 letters, digits, '.', '_' or '-'")
    return value


def ensure_state_root(path: Path) -> Path:
    path = path.expanduser()
    if not path.is_absolute():
        raise ValueError("cognition state root must be an absolute path")
    if path.exists() or path.is_symlink():
        if path.is_symlink() or not path.is_dir():
            raise ValueError(f"unsafe cognition state root: {path}")
    else:
        path.mkdir(parents=True, mode=0o700)
    try:
        path.chmod(0o700)
    except OSError as error:
        raise ValueError(f"cannot secure cognition state root {path}: {error}") from error
    return path


def read_response(connection: socket.socket) -> tuple[int, bytes]:
    chunks: list[bytes] = []
    total = 0
    while True:
        block = connection.recv(65536)
        if not block:
            break
        total += len(block)
        if total > MAXIMUM_RESPONSE_BYTES:
            raise RuntimeError("cognition service response exceeds 24 MiB")
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


def request_cognition(socket_path: Path, request_value: dict[str, Any]) -> dict[str, Any]:
    body = json.dumps(
        request_value,
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
        raise RuntimeError(detail or f"cognition service returned HTTP {status}")
    return value


def validate_saved_state(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict) or value.get("schema") != SESSION_SCHEMA:
        raise ValueError("cognition session state has an invalid schema")
    for name in (
        "session_fingerprint", "discourse_id", "package_id", "program_id",
        "checkpoint_fingerprint",
    ):
        field = value.get(name)
        if not isinstance(field, str) or HEX256.fullmatch(field) is None:
            raise ValueError(f"cognition session state has invalid {name}")
    checkpoint = value.get("checkpoint_hex")
    if (
        not isinstance(checkpoint, str) or not checkpoint
        or HEXBYTES.fullmatch(checkpoint) is None or len(checkpoint) % 2
        or len(checkpoint) // 2 > MAXIMUM_CHECKPOINT_BYTES
    ):
        raise ValueError("cognition session state has an invalid checkpoint")
    ordinal = value.get("next_turn_ordinal")
    if isinstance(ordinal, bool) or not isinstance(ordinal, int) or ordinal < 1:
        raise ValueError("cognition session state has an invalid next turn ordinal")
    mode = value.get("mode")
    relations = value.get("relations")
    if mode not in {"auto", "explicit"}:
        raise ValueError("cognition session state has an invalid mode")
    if not isinstance(relations, list) or any(
        not isinstance(item, str) or item not in RELATIONS for item in relations
    ):
        raise ValueError("cognition session state has invalid relations")
    if mode == "auto" and relations:
        raise ValueError("automatic cognition session unexpectedly stores explicit relations")
    if mode == "explicit" and not relations:
        raise ValueError("explicit cognition session has no relation chain")
    return value


class SessionStore:
    def __init__(self, root: Path, name: str) -> None:
        self.root = ensure_state_root(root)
        self.name = validate_session_name(name)
        self.state_path = self.root / f"{self.name}.json"
        self.lock_path = self.root / f".{self.name}.lock"
        self.lock_fd: int | None = None

    def __enter__(self) -> "SessionStore":
        flags = os.O_RDWR | os.O_CREAT | getattr(os, "O_NOFOLLOW", 0)
        try:
            self.lock_fd = os.open(self.lock_path, flags, 0o600)
        except OSError as error:
            raise ValueError(f"cannot open cognition session lock: {error}") from error
        locked = False
        try:
            mode = os.fstat(self.lock_fd).st_mode
            if not stat.S_ISREG(mode):
                raise ValueError("cognition session lock is not a regular file")
            os.fchmod(self.lock_fd, 0o600)
            fcntl.flock(self.lock_fd, fcntl.LOCK_EX)
            locked = True
            return self
        except Exception:
            if locked:
                fcntl.flock(self.lock_fd, fcntl.LOCK_UN)
            os.close(self.lock_fd)
            self.lock_fd = None
            raise

    def __exit__(self, exc_type: Any, exc: Any, traceback: Any) -> None:
        if self.lock_fd is not None:
            fcntl.flock(self.lock_fd, fcntl.LOCK_UN)
            os.close(self.lock_fd)
            self.lock_fd = None

    def reset(self) -> None:
        if self.state_path.exists() or self.state_path.is_symlink():
            if self.state_path.is_symlink() or not self.state_path.is_file():
                raise ValueError(f"unsafe cognition session state: {self.state_path}")
            self.state_path.unlink()

    def load(self) -> dict[str, Any] | None:
        if not self.state_path.exists() and not self.state_path.is_symlink():
            return None
        if self.state_path.is_symlink() or not self.state_path.is_file():
            raise ValueError(f"unsafe cognition session state: {self.state_path}")
        mode = stat.S_IMODE(self.state_path.stat().st_mode)
        if mode & 0o077:
            raise ValueError("cognition session state is accessible by group or other users")
        try:
            value = json.loads(self.state_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise ValueError(f"cannot read cognition session state: {error}") from error
        return validate_saved_state(value)

    def save(self, value: dict[str, Any]) -> None:
        validate_saved_state(value)
        temporary = self.root / (
            f".{self.name}.{os.getpid()}.{secrets.token_hex(6)}.tmp"
        )
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
        fd: int | None = None
        try:
            fd = os.open(temporary, flags, 0o600)
            encoded = (
                json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
                + "\n"
            ).encode("utf-8")
            with os.fdopen(fd, "wb", closefd=True) as stream:
                fd = None
                stream.write(encoded)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, self.state_path)
            os.chmod(self.state_path, 0o600)
            directory_fd = os.open(self.root, os.O_RDONLY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        finally:
            if fd is not None:
                os.close(fd)
            try:
                temporary.unlink(missing_ok=True)
            except OSError:
                pass


def initial_session_request() -> dict[str, Any]:
    return {
        "session_fingerprint": secrets.token_hex(32),
        "discourse_id": secrets.token_hex(32),
        "turn_ordinal": 0,
    }


def continued_session_request(state: dict[str, Any]) -> dict[str, Any]:
    return {
        "session_fingerprint": state["session_fingerprint"],
        "discourse_id": state["discourse_id"],
        "turn_ordinal": state["next_turn_ordinal"],
        "previous_checkpoint_hex": state["checkpoint_hex"],
        "previous_checkpoint_fingerprint": state["checkpoint_fingerprint"],
        "previous_package_id": state["package_id"],
        "previous_program_id": state["program_id"],
    }


def assert_session_mode(
    state: dict[str, Any], mode: str, relations: list[str] | None
) -> None:
    requested = relations or []
    if state["mode"] != mode or state["relations"] != requested:
        raise ValueError(
            "cognition program selection differs from this session; use --reset-session"
        )


def state_from_success(
    session_request: dict[str, Any],
    mode: str,
    relations: list[str] | None,
    result: dict[str, Any],
) -> dict[str, Any]:
    package_id = result.get("package_id")
    program_id = result.get("program_id")
    checkpoint = result.get("checkpoint_hex")
    checkpoint_fingerprint = result.get("next_checkpoint_fingerprint")
    turn_ordinal = result.get("turn_ordinal")
    continued = result.get("continued")
    if not isinstance(package_id, str) or HEX256.fullmatch(package_id) is None:
        raise RuntimeError("cognition response has an invalid package identity")
    if not isinstance(program_id, str) or HEX256.fullmatch(program_id) is None:
        raise RuntimeError("cognition response has an invalid program identity")
    if (
        not isinstance(checkpoint, str) or not checkpoint
        or HEXBYTES.fullmatch(checkpoint) is None or len(checkpoint) % 2
        or len(checkpoint) // 2 > MAXIMUM_CHECKPOINT_BYTES
    ):
        raise RuntimeError("cognition response has an invalid checkpoint")
    if (
        not isinstance(checkpoint_fingerprint, str)
        or HEX256.fullmatch(checkpoint_fingerprint) is None
    ):
        raise RuntimeError("cognition response has an invalid checkpoint fingerprint")
    if turn_ordinal != session_request["turn_ordinal"]:
        raise RuntimeError("cognition response turn ordinal does not match the requested turn")
    if continued is not (turn_ordinal > 0):
        raise RuntimeError("cognition response continuation state is inconsistent")
    return {
        "schema": SESSION_SCHEMA,
        "session_fingerprint": session_request["session_fingerprint"],
        "discourse_id": session_request["discourse_id"],
        "next_turn_ordinal": turn_ordinal + 1,
        "package_id": package_id,
        "program_id": program_id,
        "mode": mode,
        "relations": relations or [],
        "checkpoint_hex": checkpoint,
        "checkpoint_fingerprint": checkpoint_fingerprint,
    }


def native_error(result: dict[str, Any]) -> str:
    execution = result.get("execution")
    if isinstance(execution, dict):
        return (
            f"native cognition status={result.get('status')} "
            f"failed_step={execution.get('failed_step')} "
            f"native_status={execution.get('native_status')}"
        )
    return f"native cognition status={result.get('status')}"


def emit_result(result: dict[str, Any], as_json: bool) -> int:
    if as_json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
        return 0 if result.get("status") == 0 else 2
    if result.get("status") != 0:
        raise RuntimeError(native_error(result))
    output = result.get("output_utf8")
    if isinstance(output, str):
        sys.stdout.write(output)
        if not output.endswith("\n"):
            sys.stdout.write("\n")
        return 0
    output_hex = result.get("output_hex")
    if not isinstance(output_hex, str) or HEXBYTES.fullmatch(output_hex) is None:
        raise RuntimeError("cognition returned neither UTF-8 nor binary output")
    sys.stdout.buffer.write(bytes.fromhex(output_hex))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prompt", nargs="?", help="exact UTF-8 prompt; omit to read stdin")
    parser.add_argument(
        "--relations",
        default=None,
        help="explicit ordered relation chain; omit for native automatic operation selection",
    )
    parser.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    parser.add_argument(
        "--session",
        default=None,
        help="persistent local conversation name (default: default for automatic cognition)",
    )
    parser.add_argument("--stateless", action="store_true", help="do not load or save conversation state")
    parser.add_argument("--reset-session", action="store_true", help="discard the named session before this turn")
    parser.add_argument("--state-root", type=Path, default=None, help=argparse.SUPPRESS)
    parser.add_argument("--json", action="store_true", help="print the complete execution result")
    args = parser.parse_args()
    try:
        prompt = read_prompt(args.prompt)
        relations = parse_relations(args.relations) if args.relations is not None else None
        mode = "explicit" if relations is not None else "auto"
        if args.stateless and args.reset_session:
            raise ValueError("--reset-session cannot be combined with --stateless")
        if args.stateless and args.session is not None:
            raise ValueError("--session cannot be combined with --stateless")
        effective_stateless = args.stateless or (relations is not None and args.session is None)
        if effective_stateless:
            request_value: dict[str, Any] = {"prompt": prompt}
            if relations is not None:
                request_value["relations"] = relations
            result = request_cognition(args.socket, request_value)
            return emit_result(result, args.json)

        state_root = args.state_root if args.state_root is not None else default_state_root()
        session_name = args.session if args.session is not None else "default"
        with SessionStore(state_root, session_name) as store:
            if args.reset_session:
                store.reset()
            state = store.load()
            if state is None:
                session_request = initial_session_request()
            else:
                assert_session_mode(state, mode, relations)
                session_request = continued_session_request(state)
            request_value = {"prompt": prompt, "session": session_request}
            if relations is not None:
                request_value["relations"] = relations
            result = request_cognition(args.socket, request_value)
            if result.get("status") == 0:
                store.save(state_from_success(session_request, mode, relations, result))
            return emit_result(result, args.json)
    except (RuntimeError, ValueError) as error:
        print(f"laplace-cognition: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
