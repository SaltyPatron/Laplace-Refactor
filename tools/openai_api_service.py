#!/usr/bin/env python3
"""OpenAI-compatible HTTP envelope for the installed Laplace cognition service.

This transport owns no cognition semantics. It validates a deliberately narrow
compatibility profile, forwards the exact user prompt to the installed
``laplace-cognition-service`` over its Unix socket, and maps the resulting native
Laplace response into an OpenAI Chat Completions envelope.

Unsupported protocol features fail explicitly rather than being silently ignored.
"""
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hmac
import json
from pathlib import Path
import re
import socket
import sys
import time
from typing import Any

DEFAULT_LISTEN = "127.0.0.1"
DEFAULT_PORT = 55434
DEFAULT_COGNITION_SOCKET = Path("/opt/laplace/runtime/laplace-cognition.sock")
MAXIMUM_REQUEST_BYTES = 20 * 1024 * 1024
MAXIMUM_RESPONSE_BYTES = 24 * 1024 * 1024
MODEL_ID = "laplace-native"
HEX256 = re.compile(r"^[0-9a-f]{64}$")
RECEIPT_BYTEA = re.compile(r"^\\x([0-9a-f]{64})$")


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


def cognition_request(socket_path: Path, prompt: str, timeout_seconds: float = 310.0) -> dict[str, Any]:
    body = json.dumps({"prompt": prompt}, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
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


def require_exact_chat_profile(payload: Any) -> str:
    if not isinstance(payload, dict):
        raise ApiError(400, "request body must be a JSON object")
    allowed = {"model", "messages", "stream", "n", "user"}
    unsupported = sorted(set(payload) - allowed)
    if unsupported:
        raise ApiError(400, "unsupported Chat Completions field(s): " + ", ".join(unsupported), param=unsupported[0], code="unsupported_parameter")
    model = payload.get("model")
    if model != MODEL_ID:
        raise ApiError(404, f"model must be {MODEL_ID!r}", param="model", code="model_not_found")
    if payload.get("stream", False) is not False:
        raise ApiError(400, "streaming Chat Completions are not implemented by this compatibility profile", param="stream", code="unsupported_parameter")
    n = payload.get("n", 1)
    if isinstance(n, bool) or not isinstance(n, int) or n != 1:
        raise ApiError(400, "this compatibility profile requires n=1", param="n", code="unsupported_parameter")
    messages = payload.get("messages")
    if not isinstance(messages, list) or len(messages) != 1:
        raise ApiError(400, "this compatibility profile currently requires exactly one user message; multi-message role-preserving admission is not yet implemented", param="messages", code="unsupported_conversation_shape")
    message = messages[0]
    if not isinstance(message, dict):
        raise ApiError(400, "messages[0] must be an object", param="messages")
    if set(message) - {"role", "content"}:
        extra = sorted(set(message) - {"role", "content"})
        raise ApiError(400, "unsupported message field(s): " + ", ".join(extra), param=f"messages[0].{extra[0]}", code="unsupported_parameter")
    if message.get("role") != "user":
        raise ApiError(400, "this compatibility profile currently admits only one user message", param="messages[0].role", code="unsupported_conversation_shape")
    content = message.get("content")
    if not isinstance(content, str) or not content:
        raise ApiError(400, "messages[0].content must be non-empty text", param="messages[0].content")
    if len(content.encode("utf-8")) > 1024 * 1024:
        raise ApiError(413, "user message exceeds the installed Laplace 1 MiB prompt limit", param="messages[0].content", code="request_too_large")
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
        raise ApiError(502, "Laplace cognition returned an invalid native status", "api_error", code="invalid_cognition_status")
    if status != 0:
        execution = result.get("execution")
        detail = f"native cognition status={status}"
        if isinstance(execution, dict):
            detail += f" failed_step={execution.get('failed_step')} native_status={execution.get('native_status')}"
        raise ApiError(422, detail, "laplace_execution_error", code="native_cognition_failed")
    output = result.get("output_utf8")
    if not isinstance(output, str):
        raise ApiError(422, "native cognition produced non-UTF-8 output; Chat Completions requires text", "laplace_execution_error", code="non_text_output")
    program_id = result.get("program_id")
    if not isinstance(program_id, str) or HEX256.fullmatch(program_id) is None:
        raise ApiError(502, "Laplace cognition returned an invalid program identity", "api_error", code="invalid_program_identity")
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


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    cognition_socket = DEFAULT_COGNITION_SOCKET
    bearer_token: str | None = None

    def _send_json(self, status: int, payload: dict[str, Any], *, headers: dict[str, str] | None = None) -> None:
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
        if not hmac.compare_digest(value[len("Bearer "):], expected):
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

    def do_GET(self) -> None:
        try:
            self._authorize()
            if self.path == "/health":
                self._send_json(200, {"status": "ready", "transport": "openai-compatible-local-http", "model": MODEL_ID, "cognition": health_payload(self.cognition_socket)})
                return
            if self.path == "/v1/models":
                self._send_json(200, {"object": "list", "data": [{"id": MODEL_ID, "object": "model", "created": 0, "owned_by": "laplace"}]})
                return
            raise ApiError(404, "route not found", code="not_found")
        except ApiError as error:
            self._send_error(error)

    def do_POST(self) -> None:
        try:
            self._authorize()
            if self.path != "/v1/chat/completions":
                raise ApiError(404, "route not found", code="not_found")
            prompt = require_exact_chat_profile(self._read_json())
            completion, rid = completion_from_cognition(cognition_request(self.cognition_socket, prompt))
            self._send_json(200, completion, headers={"X-Laplace-Receipt-ID": rid} if rid is not None else None)
        except ApiError as error:
            self._send_error(error)

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"laplace-openai-api: {fmt % args}", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", default=DEFAULT_LISTEN)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--cognition-socket", type=Path, default=DEFAULT_COGNITION_SOCKET)
    parser.add_argument("--bearer-token-file", type=Path, default=None)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    token = load_bearer_token(args.bearer_token_file)
    if not is_loopback(args.listen) and token is None:
        parser.error("non-loopback listening requires --bearer-token-file")
    Handler.cognition_socket = args.cognition_socket
    Handler.bearer_token = token
    server = ThreadingHTTPServer((args.listen, args.port), Handler)
    server.daemon_threads = True
    print(f"laplace-openai-api listening on http://{args.listen}:{args.port}/v1 -> {args.cognition_socket}", file=sys.stderr)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
