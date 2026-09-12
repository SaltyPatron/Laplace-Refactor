#!/usr/bin/env python3
"""Laplace installed HTTP product gateway.

The gateway exposes one product surface over the installed native cognition,
canonical substrate inspection, source admission, read-only SQL, MCP tools, and
browser assets. It owns transport and product interaction semantics only; native
cognition, persistence, identity, geometry, and source compilation remain in
their existing owners.
"""
from __future__ import annotations

import argparse
import csv
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hmac
import io
import json
import mimetypes
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time
from typing import Any
from urllib.parse import parse_qs, unquote, urlparse

DEFAULT_LISTEN = "127.0.0.1"
DEFAULT_PORT = 55434
DEFAULT_COGNITION_SOCKET = Path("/opt/laplace/runtime/laplace-cognition.sock")
DEFAULT_DATABASE_SOCKET = Path("/opt/laplace/runtime/postgresql/refactor")
DEFAULT_DATABASE_PORT = 55433
DEFAULT_DATABASE = "laplace_refactor"
DEFAULT_DATABASE_ROLE = "laplace_admin"
MAXIMUM_REQUEST_BYTES = 20 * 1024 * 1024
MAXIMUM_RESPONSE_BYTES = 24 * 1024 * 1024
MAXIMUM_SQL_BYTES = 64 * 1024
MAXIMUM_SQL_RESULT_BYTES = 8 * 1024 * 1024
MODEL_ID = "laplace-native"
HEX128 = re.compile(r"^[0-9a-f]{32}$")
HEX256 = re.compile(r"^[0-9a-f]{64}$")
RECEIPT_BYTEA = re.compile(r"^\\x([0-9a-f]{64})$")
SESSION_NAME = re.compile(r"^[A-Za-z0-9._-]{1,64}$")
READ_ONLY_SQL = re.compile(r"^\s*(select|with|values)\b", re.IGNORECASE | re.DOTALL)
DISALLOWED_SQL_TOKEN = re.compile(
    r"\b(copy|insert|update|delete|merge|create|alter|drop|truncate|grant|revoke|"
    r"vacuum|analyze|refresh|reindex|cluster|call|do|set|reset|listen|notify|"
    r"unlisten|lock|checkpoint|discard)\b",
    re.IGNORECASE,
)
COLLECTIONS = {
    "summary": ("summary", False),
    "entities": ("entities", True),
    "physicalities": ("physicalities", True),
    "attestations": ("attestations", True),
    "source-profiles": ("source-profiles", True),
    "entity": ("entity", False),
}


class ApiError(RuntimeError):
    def __init__(
        self,
        status: int,
        message: str,
        error_type: str = "invalid_request_error",
        param: str | None = None,
        code: str | None = None,
    ) -> None:
        super().__init__(message)
        self.status = status
        self.message = message
        self.error_type = error_type
        self.param = param
        self.code = code


def installed_sibling(name: str) -> Path:
    return Path(__file__).resolve().parent / name


def default_web_root() -> Path:
    executable = Path(__file__).resolve()
    installed = executable.parent.parent / "share/laplace/web"
    if installed.is_dir():
        return installed
    return executable.parent.parent / "product/web"


def default_psql() -> Path:
    executable = Path(__file__).resolve()
    packaged = executable.parent.parent / "pgsql-18/bin/psql"
    if packaged.is_file():
        return packaged
    return Path("/opt/laplace/current/pgsql-18/bin/psql")


def read_http_response(connection: socket.socket) -> tuple[int, dict[str, Any]]:
    chunks: list[bytes] = []
    total = 0
    while True:
        block = connection.recv(65536)
        if not block:
            break
        total += len(block)
        if total > MAXIMUM_RESPONSE_BYTES:
            raise ApiError(502, "Laplace cognition response exceeded the transport limit", "api_error", code="upstream_response_too_large")
        chunks.append(block)
    raw = b"".join(chunks)
    marker = raw.find(b"\r\n\r\n")
    if marker < 0:
        raise ApiError(502, "Laplace cognition service returned malformed HTTP", "api_error", code="malformed_upstream_response")
    header = raw[:marker].decode("iso-8859-1")
    body = raw[marker + 4 :]
    status_line = header.split("\r\n", 1)[0].split(" ", 2)
    if len(status_line) < 2 or not status_line[1].isdigit():
        raise ApiError(502, "Laplace cognition service returned malformed HTTP status", "api_error", code="malformed_upstream_status")
    try:
        payload = json.loads(body)
    except json.JSONDecodeError as error:
        raise ApiError(502, "Laplace cognition service returned invalid JSON", "api_error", code="invalid_upstream_json") from error
    if not isinstance(payload, dict):
        raise ApiError(502, "Laplace cognition service returned a non-object response", "api_error", code="invalid_upstream_payload")
    return int(status_line[1]), payload


def cognition_request(socket_path: Path, request_value: dict[str, Any], timeout_seconds: float = 310.0) -> dict[str, Any]:
    body = json.dumps(request_value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    request = (
        b"POST /v1/cognition HTTP/1.1\r\n"
        b"Host: laplace.local\r\n"
        b"Content-Type: application/json; charset=utf-8\r\n"
        + f"Content-Length: {len(body)}\r\n".encode("ascii")
        + b"Connection: close\r\n\r\n"
        + body
    )
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.settimeout(timeout_seconds)
    try:
        connection.connect(str(socket_path))
        connection.sendall(request)
        status, payload = read_http_response(connection)
    except OSError as error:
        raise ApiError(503, f"Laplace cognition service is unavailable at {socket_path}: {error}", "api_error", code="cognition_unavailable") from error
    finally:
        connection.close()
    if status >= 400:
        detail = payload.get("error")
        raise ApiError(502 if status >= 500 else status, detail if isinstance(detail, str) and detail else f"Laplace cognition HTTP {status}", "api_error", code="cognition_error")
    return payload


def health_payload(socket_path: Path) -> dict[str, Any]:
    request = b"GET /health HTTP/1.1\r\nHost: laplace.local\r\nConnection: close\r\n\r\n"
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.settimeout(3.0)
    try:
        connection.connect(str(socket_path))
        connection.sendall(request)
        status, payload = read_http_response(connection)
    except OSError as error:
        raise ApiError(503, f"Laplace cognition service is unavailable at {socket_path}: {error}", "api_error", code="cognition_unavailable") from error
    finally:
        connection.close()
    if status != 200:
        raise ApiError(503, f"Laplace cognition health returned HTTP {status}", "api_error", code="cognition_unavailable")
    return payload


def run_json_command(command: list[str], *, timeout: float, label: str) -> Any:
    try:
        result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False, timeout=timeout, close_fds=True)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ApiError(503, f"{label} could not execute: {error}", "api_error") from error
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit status {result.returncode}"
        raise ApiError(422, f"{label} failed: {detail[-4000:]}", "laplace_execution_error")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ApiError(502, f"{label} returned invalid JSON", "api_error") from error


def require_executable(path: Path, label: str) -> Path:
    if not path.is_file() or path.is_symlink() or not os.access(path, os.X_OK):
        raise ApiError(503, f"{label} is unavailable: {path}", "api_error")
    return path


def bounded_limit(value: Any) -> int:
    if isinstance(value, bool):
        raise ApiError(400, "limit must be an integer", param="limit")
    try:
        parsed = int(value)
    except (TypeError, ValueError) as error:
        raise ApiError(400, "limit must be an integer", param="limit") from error
    if parsed < 1 or parsed > 1000:
        raise ApiError(400, "limit must be between 1 and 1000", param="limit")
    return parsed


def inspect_value(inspect_binary: Path, collection: str, *, limit: int = 25, entity_id: str | None = None) -> Any:
    require_executable(inspect_binary, "laplace-inspect")
    spec = COLLECTIONS.get(collection)
    if spec is None:
        raise ApiError(400, f"unknown collection: {collection}", param="collection")
    command = [str(inspect_binary)]
    if collection == "entity":
        if not isinstance(entity_id, str) or HEX128.fullmatch(entity_id.lower()) is None:
            raise ApiError(400, "entity query requires id with exactly 32 hexadecimal characters", param="id")
        command.extend(["entity", entity_id.lower()])
    else:
        command.append(spec[0])
        if spec[1]:
            command.extend(["--limit", str(bounded_limit(limit))])
    return run_json_command(command, timeout=60.0, label="Laplace substrate query")


def cognition_cli_value(cognition_client: Path, cognition_socket: Path, prompt: Any, session: Any) -> Any:
    require_executable(cognition_client, "laplace-cognition")
    if not isinstance(prompt, str) or not prompt:
        raise ApiError(400, "prompt must be non-empty UTF-8 text", param="prompt")
    if len(prompt.encode("utf-8")) > 1024 * 1024:
        raise ApiError(413, "prompt exceeds the installed Laplace 1 MiB limit", param="prompt")
    command = [str(cognition_client), "--socket", str(cognition_socket), "--json"]
    if session is None:
        command.append("--stateless")
    else:
        if not isinstance(session, str) or SESSION_NAME.fullmatch(session) is None:
            raise ApiError(400, "session must use 1-64 letters, digits, '.', '_' or '-'", param="session")
        command.extend(["--session", session])
    command.append(prompt)
    return run_json_command(command, timeout=320.0, label="Laplace cognition")


def source_admit_value(admit_binary: Path, payload: Any) -> Any:
    require_executable(admit_binary, "laplace-admit-source")
    if not isinstance(payload, dict):
        raise ApiError(400, "source admission body must be a JSON object")
    profile = payload.get("profile")
    source_root = payload.get("source_root")
    if not isinstance(profile, str) or not profile:
        raise ApiError(400, "profile is required", param="profile")
    if not isinstance(source_root, str) or not source_root:
        raise ApiError(400, "source_root is required", param="source_root")
    root = Path(source_root)
    if not root.is_absolute():
        raise ApiError(400, "source_root must be an absolute server path", param="source_root")
    command = [str(admit_binary), profile, str(root)]
    unicode_root = payload.get("unicode_root")
    if unicode_root is not None:
        if not isinstance(unicode_root, str) or not Path(unicode_root).is_absolute():
            raise ApiError(400, "unicode_root must be an absolute server path", param="unicode_root")
        command.extend(["--unicode-root", unicode_root])
    return run_json_command(command, timeout=900.0, label="Laplace source admission")


def sql_value(psql: Path, database_socket: Path, database_port: int, database: str, role: str, payload: Any) -> dict[str, Any]:
    require_executable(psql, "PostgreSQL client")
    if not isinstance(payload, dict):
        raise ApiError(400, "SQL body must be a JSON object")
    sql = payload.get("sql")
    if not isinstance(sql, str) or not sql.strip():
        raise ApiError(400, "sql must be non-empty text", param="sql")
    if len(sql.encode("utf-8")) > MAXIMUM_SQL_BYTES:
        raise ApiError(413, "SQL text exceeds 64 KiB", param="sql")
    stripped = sql.strip()
    if stripped.endswith(";"):
        stripped = stripped[:-1].rstrip()
    if ";" in stripped:
        raise ApiError(400, "SQL workspace accepts exactly one statement", param="sql")
    if READ_ONLY_SQL.match(stripped) is None or DISALLOWED_SQL_TOKEN.search(stripped):
        raise ApiError(400, "SQL workspace admits one read-only SELECT, WITH, or VALUES statement", param="sql", code="read_only_sql_required")
    copy_sql = (
        "BEGIN READ ONLY;\n"
        "SET LOCAL statement_timeout = '15s';\n"
        "SET LOCAL lock_timeout = '3s';\n"
        f"COPY ({stripped}) TO STDOUT WITH (FORMAT CSV, HEADER TRUE);\n"
        "ROLLBACK;\n"
    )
    command = [str(psql), "--host", str(database_socket), "--port", str(database_port), "--username", role, "--dbname", database, "--no-psqlrc", "--set", "ON_ERROR_STOP=1", "--quiet"]
    try:
        result = subprocess.run(command, input=copy_sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False, timeout=20, close_fds=True)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ApiError(503, f"SQL query could not execute: {error}", "api_error") from error
    if result.returncode != 0:
        detail = result.stderr.strip() or "PostgreSQL returned no diagnostic"
        raise ApiError(422, f"read-only SQL failed: {detail[-4000:]}", "laplace_execution_error")
    if len(result.stdout.encode("utf-8")) > MAXIMUM_SQL_RESULT_BYTES:
        raise ApiError(413, "SQL result exceeds 8 MiB", code="result_too_large")
    reader = csv.reader(io.StringIO(result.stdout))
    rows = list(reader)
    columns = rows[0] if rows else []
    data = rows[1:] if rows else []
    return {"schema": "laplace.product.sql-result/v1", "columns": columns, "rows": data, "row_count": len(data)}


def require_exact_chat_profile(payload: Any) -> str:
    if not isinstance(payload, dict):
        raise ApiError(400, "request body must be a JSON object")
    allowed = {"model", "messages", "stream", "n", "user"}
    unsupported = sorted(set(payload) - allowed)
    if unsupported:
        raise ApiError(400, "unsupported Chat Completions field(s): " + ", ".join(unsupported), param=unsupported[0], code="unsupported_parameter")
    if payload.get("model") != MODEL_ID:
        raise ApiError(404, f"model must be {MODEL_ID!r}", param="model", code="model_not_found")
    if payload.get("stream", False) is not False:
        raise ApiError(400, "streaming Chat Completions are not implemented by this compatibility profile", param="stream", code="unsupported_parameter")
    n = payload.get("n", 1)
    if isinstance(n, bool) or not isinstance(n, int) or n != 1:
        raise ApiError(400, "this compatibility profile requires n=1", param="n")
    messages = payload.get("messages")
    if not isinstance(messages, list) or len(messages) != 1:
        raise ApiError(400, "this compatibility profile requires exactly one user message", param="messages", code="unsupported_conversation_shape")
    message = messages[0]
    if not isinstance(message, dict) or set(message) - {"role", "content"}:
        raise ApiError(400, "messages[0] must contain only role and content", param="messages")
    if message.get("role") != "user":
        raise ApiError(400, "messages[0].role must be user", param="messages[0].role")
    content = message.get("content")
    if not isinstance(content, str) or not content:
        raise ApiError(400, "messages[0].content must be non-empty text", param="messages[0].content")
    if len(content.encode("utf-8")) > 1024 * 1024:
        raise ApiError(413, "user message exceeds the installed Laplace 1 MiB prompt limit")
    return content


def receipt_id(result: dict[str, Any]) -> str | None:
    execution = result.get("execution")
    if not isinstance(execution, dict):
        return None
    raw = execution.get("execution_receipt_id")
    if not isinstance(raw, str):
        return None
    match = RECEIPT_BYTEA.fullmatch(raw.lower())
    return match.group(1) if match is not None else None


def completion_from_cognition(result: dict[str, Any]) -> tuple[dict[str, Any], str | None]:
    status = result.get("status")
    if isinstance(status, bool) or not isinstance(status, int):
        raise ApiError(502, "Laplace cognition returned an invalid native status", "api_error")
    if status != 0:
        execution = result.get("execution")
        detail = f"native cognition status={status}"
        if isinstance(execution, dict):
            detail += f" failed_step={execution.get('failed_step')} native_status={execution.get('native_status')}"
        raise ApiError(422, detail, "laplace_execution_error", code="native_cognition_failed")
    output = result.get("output_utf8")
    if not isinstance(output, str):
        raise ApiError(422, "native cognition produced non-UTF-8 output", "laplace_execution_error")
    program_id = result.get("program_id")
    if not isinstance(program_id, str) or HEX256.fullmatch(program_id) is None:
        raise ApiError(502, "Laplace cognition returned an invalid program identity", "api_error")
    rid = receipt_id(result)
    correlation = rid or result.get("next_checkpoint_fingerprint")
    if not isinstance(correlation, str) or HEX256.fullmatch(correlation) is None:
        correlation = program_id
    response = {
        "id": f"chatcmpl-laplace-{correlation[:24]}",
        "object": "chat.completion",
        "created": int(time.time()),
        "model": MODEL_ID,
        "choices": [{"index": 0, "message": {"role": "assistant", "content": output}, "finish_reason": "stop"}],
        "system_fingerprint": f"laplace-{program_id[:24]}",
    }
    return response, rid


def load_bearer_token(path: Path | None) -> str | None:
    if path is None:
        return None
    try:
        token = path.read_text(encoding="utf-8").strip()
    except OSError as error:
        raise ValueError(f"cannot read bearer token file {path}: {error}") from error
    if not token or "\n" in token or "\r" in token:
        raise ValueError("bearer token file must contain exactly one non-empty token")
    return token


def is_loopback(host: str) -> bool:
    return host in {"127.0.0.1", "::1", "localhost"}


def descriptors() -> dict[str, Any]:
    return {
        "schema": "laplace.product.descriptors/v1",
        "model": MODEL_ID,
        "workspaces": ["graph", "sources", "cognition", "sql", "events", "admin"],
        "collections": [
            {"id": "entities", "key": "entity_id", "detail": "entity"},
            {"id": "physicalities", "key": "physicality_id"},
            {"id": "attestations", "key": "attestation_id"},
            {"id": "source-profiles", "key": "profile_id"},
        ],
        "transports": {"rest": "/api/v1", "events": "/api/v1/stream", "mcp": "/mcp", "openai": "/v1"},
        "operations": {"query": "/api/v1/query", "cognition": "/api/v1/cognition", "source_admit": "/api/v1/sources/admit", "sql": "/api/v1/sql"},
    }


def mcp_tool_list() -> list[dict[str, Any]]:
    return [
        {
            "name": "laplace.query",
            "description": "Inspect canonical persisted Laplace substrate state.",
            "inputSchema": {"type": "object", "properties": {"collection": {"type": "string", "enum": list(COLLECTIONS)}, "limit": {"type": "integer", "minimum": 1, "maximum": 1000}, "id": {"type": "string"}}, "required": ["collection"], "additionalProperties": False},
        },
        {
            "name": "laplace.cognition",
            "description": "Execute installed native Laplace cognition.",
            "inputSchema": {"type": "object", "properties": {"prompt": {"type": "string"}, "session": {"type": "string"}}, "required": ["prompt"], "additionalProperties": False},
        },
        {
            "name": "laplace.source_admit",
            "description": "Compile, admit, and read back a locked source profile.",
            "inputSchema": {"type": "object", "properties": {"profile": {"type": "string"}, "source_root": {"type": "string"}, "unicode_root": {"type": "string"}}, "required": ["profile", "source_root"], "additionalProperties": False},
        },
        {
            "name": "laplace.sql",
            "description": "Execute one bounded read-only SQL query against the active substrate.",
            "inputSchema": {"type": "object", "properties": {"sql": {"type": "string"}}, "required": ["sql"], "additionalProperties": False},
        },
    ]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    cognition_socket = DEFAULT_COGNITION_SOCKET
    cognition_client = installed_sibling("laplace-cognition")
    inspect_binary = installed_sibling("laplace-inspect")
    admit_binary = installed_sibling("laplace-admit-source")
    psql = default_psql()
    database_socket = DEFAULT_DATABASE_SOCKET
    database_port = DEFAULT_DATABASE_PORT
    database = DEFAULT_DATABASE
    database_role = DEFAULT_DATABASE_ROLE
    web_root = default_web_root()
    bearer_token: str | None = None

    def _send_json(self, status: int, payload: Any, *, headers: dict[str, str] | None = None) -> None:
        body = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        if headers:
            for name, value in headers.items():
                self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def _send_error(self, error: ApiError) -> None:
        self._send_json(error.status, {"error": {"message": error.message, "type": error.error_type, "param": error.param, "code": error.code}})

    def _authorize(self) -> None:
        expected = self.bearer_token
        if expected is None:
            return
        value = self.headers.get("Authorization")
        if not isinstance(value, str) or not value.startswith("Bearer "):
            raise ApiError(401, "missing bearer token", "authentication_error", code="invalid_api_key")
        if not hmac.compare_digest(value[len("Bearer ") :], expected):
            raise ApiError(401, "invalid bearer token", "authentication_error", code="invalid_api_key")

    def _read_json(self) -> Any:
        try:
            length = int(self.headers.get("Content-Length") or "0")
        except ValueError as error:
            raise ApiError(400, "invalid Content-Length") from error
        if length <= 0:
            raise ApiError(400, "request body must be non-empty JSON")
        if length > MAXIMUM_REQUEST_BYTES:
            raise ApiError(413, "request body exceeds the API transport limit")
        try:
            return json.loads(self.rfile.read(length))
        except json.JSONDecodeError as error:
            raise ApiError(400, "request body must be valid JSON") from error

    def _serve_asset(self, route_path: str) -> bool:
        mapping = {"/": "index.html", "/index.html": "index.html", "/app.js": "app.js", "/styles.css": "styles.css"}
        filename = mapping.get(route_path)
        if filename is None:
            return False
        root = self.web_root.resolve()
        target = (root / filename).resolve()
        try:
            target.relative_to(root)
        except ValueError as error:
            raise ApiError(404, "asset not found", code="not_found") from error
        if not target.is_file():
            raise ApiError(503, f"installed web asset is unavailable: {target}", "api_error")
        body = target.read_bytes()
        mime = mimetypes.guess_type(target.name)[0] or "application/octet-stream"
        self.send_response(200)
        suffix = "; charset=utf-8" if mime.startswith("text/") or mime == "application/javascript" else ""
        self.send_header("Content-Type", mime + suffix)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; object-src 'none'; base-uri 'none'")
        self.end_headers()
        self.wfile.write(body)
        return True

    def _query_from_payload(self, payload: Any) -> Any:
        if not isinstance(payload, dict):
            raise ApiError(400, "query body must be a JSON object")
        collection = payload.get("collection")
        if not isinstance(collection, str):
            raise ApiError(400, "collection is required", param="collection")
        return inspect_value(self.inspect_binary, collection, limit=bounded_limit(payload.get("limit", 25)), entity_id=payload.get("id"))

    def _mcp(self, payload: Any) -> dict[str, Any] | None:
        if not isinstance(payload, dict) or payload.get("jsonrpc") != "2.0":
            raise ApiError(400, "MCP request must be a JSON-RPC 2.0 object")
        request_id = payload.get("id")
        method = payload.get("method")
        params = payload.get("params") or {}
        if method == "notifications/initialized":
            return None
        if method == "initialize":
            requested = params.get("protocolVersion") if isinstance(params, dict) else None
            return {"jsonrpc": "2.0", "id": request_id, "result": {"protocolVersion": requested if isinstance(requested, str) else "2024-11-05", "capabilities": {"tools": {"listChanged": False}}, "serverInfo": {"name": "laplace", "version": "1"}}}
        if method == "tools/list":
            return {"jsonrpc": "2.0", "id": request_id, "result": {"tools": mcp_tool_list()}}
        if method == "tools/call":
            if not isinstance(params, dict):
                raise ApiError(400, "tools/call params must be an object")
            name = params.get("name")
            arguments = params.get("arguments") or {}
            if not isinstance(arguments, dict):
                raise ApiError(400, "tools/call arguments must be an object")
            if name == "laplace.query":
                value = self._query_from_payload(arguments)
            elif name == "laplace.cognition":
                value = cognition_cli_value(self.cognition_client, self.cognition_socket, arguments.get("prompt"), arguments.get("session"))
            elif name == "laplace.source_admit":
                value = source_admit_value(self.admit_binary, arguments)
            elif name == "laplace.sql":
                value = sql_value(self.psql, self.database_socket, self.database_port, self.database, self.database_role, arguments)
            else:
                return {"jsonrpc": "2.0", "id": request_id, "error": {"code": -32602, "message": f"unknown tool: {name}"}}
            return {"jsonrpc": "2.0", "id": request_id, "result": {"content": [{"type": "text", "text": json.dumps(value, ensure_ascii=False, separators=(",", ":"))}], "structuredContent": value, "isError": False}}
        return {"jsonrpc": "2.0", "id": request_id, "error": {"code": -32601, "message": f"method not found: {method}"}}

    def _stream_events(self) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("X-Accel-Buffering", "no")
        self.end_headers()
        ordinal = 0
        while True:
            try:
                payload = {"schema": "laplace.product.event/v1", "ordinal": ordinal, "timestamp": int(time.time()), "kind": "product-snapshot", "cognition": health_payload(self.cognition_socket), "substrate": inspect_value(self.inspect_binary, "summary")}
                frame = (f"id: {ordinal}\n" "event: product-snapshot\n" f"data: {json.dumps(payload, ensure_ascii=False, separators=(',', ':'))}\n\n").encode("utf-8")
                self.wfile.write(frame)
                self.wfile.flush()
                ordinal += 1
                time.sleep(5)
            except (BrokenPipeError, ConnectionResetError):
                return
            except ApiError as error:
                payload = {"schema": "laplace.product.event/v1", "ordinal": ordinal, "timestamp": int(time.time()), "kind": "error", "error": error.message}
                try:
                    frame = (f"id: {ordinal}\n" "event: error\n" f"data: {json.dumps(payload, ensure_ascii=False, separators=(',', ':'))}\n\n").encode("utf-8")
                    self.wfile.write(frame)
                    self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    return
                ordinal += 1
                time.sleep(5)

    def do_GET(self) -> None:
        try:
            parsed = urlparse(self.path)
            if self._serve_asset(parsed.path):
                return
            self._authorize()
            if parsed.path in {"/health", "/api/v1/health"}:
                self._send_json(200, {"schema": "laplace.product.health/v1", "status": "ready", "model": MODEL_ID, "cognition": health_payload(self.cognition_socket), "web_root": str(self.web_root)})
                return
            if parsed.path == "/api/v1/descriptors":
                self._send_json(200, descriptors())
                return
            if parsed.path == "/v1/models":
                self._send_json(200, {"object": "list", "data": [{"id": MODEL_ID, "object": "model", "created": 0, "owned_by": "laplace"}]})
                return
            if parsed.path == "/api/v1/stream":
                self._stream_events()
                return
            direct = {"/api/v1/summary": "summary", "/api/v1/entities": "entities", "/api/v1/physicalities": "physicalities", "/api/v1/attestations": "attestations", "/api/v1/sources": "source-profiles"}
            collection = direct.get(parsed.path)
            if collection is not None:
                query = parse_qs(parsed.query)
                limit = bounded_limit((query.get("limit") or ["25"])[0])
                self._send_json(200, inspect_value(self.inspect_binary, collection, limit=limit))
                return
            prefix = "/api/v1/entities/"
            if parsed.path.startswith(prefix):
                entity_id = unquote(parsed.path[len(prefix) :]).lower()
                self._send_json(200, inspect_value(self.inspect_binary, "entity", entity_id=entity_id))
                return
            raise ApiError(404, "route not found", code="not_found")
        except ApiError as error:
            self._send_error(error)
        except OSError as error:
            self._send_error(ApiError(503, f"product asset error: {error}", "api_error"))

    def do_POST(self) -> None:
        try:
            parsed = urlparse(self.path)
            self._authorize()
            payload = self._read_json()
            if parsed.path == "/v1/chat/completions":
                prompt = require_exact_chat_profile(payload)
                completion, rid = completion_from_cognition(cognition_request(self.cognition_socket, {"prompt": prompt}))
                self._send_json(200, completion, headers={"X-Laplace-Receipt-ID": rid} if rid is not None else None)
                return
            if parsed.path == "/api/v1/query":
                self._send_json(200, self._query_from_payload(payload))
                return
            if parsed.path == "/api/v1/cognition":
                if not isinstance(payload, dict):
                    raise ApiError(400, "cognition body must be a JSON object")
                self._send_json(200, cognition_cli_value(self.cognition_client, self.cognition_socket, payload.get("prompt"), payload.get("session")))
                return
            if parsed.path == "/api/v1/sources/admit":
                self._send_json(200, source_admit_value(self.admit_binary, payload))
                return
            if parsed.path == "/api/v1/sql":
                self._send_json(200, sql_value(self.psql, self.database_socket, self.database_port, self.database, self.database_role, payload))
                return
            if parsed.path == "/mcp":
                response = self._mcp(payload)
                if response is None:
                    self.send_response(202)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                else:
                    self._send_json(200, response)
                return
            raise ApiError(404, "route not found", code="not_found")
        except ApiError as error:
            self._send_error(error)

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"laplace-product-gateway: {fmt % args}", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", default=DEFAULT_LISTEN)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--cognition-socket", type=Path, default=DEFAULT_COGNITION_SOCKET)
    parser.add_argument("--cognition-client", type=Path, default=installed_sibling("laplace-cognition"))
    parser.add_argument("--inspect", type=Path, default=installed_sibling("laplace-inspect"))
    parser.add_argument("--admit-source", type=Path, default=installed_sibling("laplace-admit-source"))
    parser.add_argument("--psql", type=Path, default=default_psql())
    parser.add_argument("--database-socket", type=Path, default=DEFAULT_DATABASE_SOCKET)
    parser.add_argument("--database-port", type=int, default=DEFAULT_DATABASE_PORT)
    parser.add_argument("--database", default=DEFAULT_DATABASE)
    parser.add_argument("--database-role", default=DEFAULT_DATABASE_ROLE)
    parser.add_argument("--web-root", type=Path, default=default_web_root())
    parser.add_argument("--bearer-token-file", type=Path, default=None)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if not 1 <= args.database_port <= 65535:
        parser.error("--database-port must be between 1 and 65535")
    token = load_bearer_token(args.bearer_token_file)
    if not is_loopback(args.listen) and token is None:
        parser.error("non-loopback listening requires --bearer-token-file")
    Handler.cognition_socket = args.cognition_socket
    Handler.cognition_client = args.cognition_client
    Handler.inspect_binary = args.inspect
    Handler.admit_binary = args.admit_source
    Handler.psql = args.psql
    Handler.database_socket = args.database_socket
    Handler.database_port = args.database_port
    Handler.database = args.database
    Handler.database_role = args.database_role
    Handler.web_root = args.web_root
    Handler.bearer_token = token
    server = ThreadingHTTPServer((args.listen, args.port), Handler)
    server.daemon_threads = True
    print(f"laplace-product-gateway listening on http://{args.listen}:{args.port}/ cognition={args.cognition_socket} database={args.database_socket}:{args.database_port}", file=sys.stderr)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())