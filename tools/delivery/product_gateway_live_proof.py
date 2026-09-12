#!/usr/bin/env python3
"""Prove the installed Laplace browser/gateway through its real local HTTP surface."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import time
from typing import Any
from urllib import error, request

BASE = "http://127.0.0.1:55434"
HEX256 = re.compile(r"^[0-9a-f]{64}$")
MAXIMUM_RESPONSE_BYTES = 24 * 1024 * 1024


def http(method: str, path: str, payload: Any | None = None, timeout: float = 320.0) -> tuple[int, str, bytes]:
    body = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = request.Request(BASE + path, data=body, headers=headers, method=method)
    try:
        with request.urlopen(req, timeout=timeout) as response:
            data = response.read(MAXIMUM_RESPONSE_BYTES + 1)
            if len(data) > MAXIMUM_RESPONSE_BYTES:
                raise RuntimeError(f"{path} response exceeded proof limit")
            return response.status, response.headers.get("Content-Type", ""), data
    except error.HTTPError as exc:
        data = exc.read(MAXIMUM_RESPONSE_BYTES + 1)
        raise RuntimeError(f"{method} {path} returned HTTP {exc.code}: {data[-4000:]!r}") from exc
    except OSError as exc:
        raise RuntimeError(f"{method} {path} failed: {exc}") from exc


def json_http(method: str, path: str, payload: Any | None = None, timeout: float = 320.0) -> dict[str, Any]:
    status, content_type, data = http(method, path, payload, timeout)
    if status != 200:
        raise RuntimeError(f"{method} {path} returned HTTP {status}")
    if "application/json" not in content_type:
        raise RuntimeError(f"{method} {path} returned unexpected content type {content_type!r}")
    try:
        value = json.loads(data)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"{method} {path} returned invalid JSON") from exc
    if not isinstance(value, dict):
        raise RuntimeError(f"{method} {path} returned non-object JSON")
    return value


def require_native_success(value: dict[str, Any], label: str) -> None:
    if value.get("status") != 0:
        raise RuntimeError(f"{label} returned native status {value.get('status')}: {value}")
    output = value.get("output_utf8")
    if not isinstance(output, str):
        raise RuntimeError(f"{label} returned no UTF-8 cognition output")
    checkpoint = value.get("next_checkpoint_fingerprint")
    if not isinstance(checkpoint, str) or HEX256.fullmatch(checkpoint) is None:
        raise RuntimeError(f"{label} returned invalid checkpoint fingerprint")


def prove(output: Path, package_id: str) -> None:
    status, content_type, index = http("GET", "/", timeout=10.0)
    if status != 200 or "text/html" not in content_type or b"Laplace" not in index:
        raise RuntimeError("installed browser root did not return the Laplace application")
    status, content_type, app = http("GET", "/app.js", timeout=10.0)
    if status != 200 or "javascript" not in content_type:
        raise RuntimeError("installed browser JavaScript is unavailable")
    for marker in (b"/api/v1/cognition", b"/api/v1/sources/admit", b"/api/v1/sql"):
        if marker not in app:
            raise RuntimeError(f"installed browser application lacks required route marker {marker!r}")

    health = json_http("GET", "/api/v1/health", timeout=10.0)
    if health.get("schema") != "laplace.product.health/v1" or health.get("status") != "ready":
        raise RuntimeError(f"gateway health is not ready: {health}")

    descriptors = json_http("GET", "/api/v1/descriptors", timeout=10.0)
    expected_workspaces = {"graph", "sources", "cognition", "sql", "events", "admin"}
    if descriptors.get("schema") != "laplace.product.descriptors/v1":
        raise RuntimeError("gateway descriptors schema is invalid")
    if set(descriptors.get("workspaces") or []) != expected_workspaces:
        raise RuntimeError(f"gateway workspaces differ from product contract: {descriptors.get('workspaces')}")
    transports = descriptors.get("transports")
    if not isinstance(transports, dict) or transports.get("mcp") != "/mcp" or transports.get("openai") != "/v1":
        raise RuntimeError("gateway descriptors do not expose MCP and OpenAI transports")

    session = f"proof-{package_id[:24]}"
    first = json_http("POST", "/api/v1/cognition", {"prompt": "AA", "session": session})
    require_native_success(first, "gateway cognition turn 0")
    if first.get("turn_ordinal") != 0:
        raise RuntimeError(f"gateway cognition turn 0 reported ordinal {first.get('turn_ordinal')}")
    second = json_http("POST", "/api/v1/cognition", {"prompt": "AAA", "session": session})
    require_native_success(second, "gateway cognition turn 1")
    if second.get("turn_ordinal") != 1 or second.get("continued") is not True:
        raise RuntimeError(f"gateway cognition did not continue the installed session: {second}")
    if first.get("next_checkpoint_fingerprint") == second.get("next_checkpoint_fingerprint"):
        raise RuntimeError("gateway cognition did not advance the durable checkpoint")

    chat_messages = [
        {"role": "system", "content": "You are the installed Laplace product."},
        {"role": "user", "content": "Write a Python function named answer that returns 41."},
        {"role": "assistant", "content": "def answer() -> int:\n    return 41"},
        {"role": "user", "content": "Continue the conversation and change it to return 42."},
    ]
    chat = json_http(
        "POST",
        "/v1/chat/completions",
        {"model": "laplace-native", "messages": chat_messages},
    )
    if chat.get("object") != "chat.completion" or chat.get("model") != "laplace-native":
        raise RuntimeError(f"OpenAI-compatible chat response is invalid: {chat}")
    choices = chat.get("choices")
    if not isinstance(choices, list) or len(choices) != 1:
        raise RuntimeError("OpenAI-compatible chat returned an invalid choice set")
    message = choices[0].get("message") if isinstance(choices[0], dict) else None
    if not isinstance(message, dict) or message.get("role") != "assistant" or not isinstance(message.get("content"), str):
        raise RuntimeError("OpenAI-compatible chat returned no assistant text")

    initialized = json_http(
        "POST",
        "/mcp",
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2024-11-05"}},
        timeout=10.0,
    )
    if initialized.get("jsonrpc") != "2.0" or not isinstance(initialized.get("result"), dict):
        raise RuntimeError("MCP initialize response is invalid")
    tools = json_http("POST", "/mcp", {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}}, timeout=10.0)
    tool_values = tools.get("result", {}).get("tools") if isinstance(tools.get("result"), dict) else None
    names = {item.get("name") for item in tool_values if isinstance(item, dict)} if isinstance(tool_values, list) else set()
    required_tools = {"laplace.query", "laplace.cognition", "laplace.source_admit", "laplace.sql"}
    if not required_tools.issubset(names):
        raise RuntimeError(f"MCP tool surface is incomplete: {sorted(names)}")
    query = json_http(
        "POST",
        "/mcp",
        {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "laplace.query", "arguments": {"collection": "summary"}}},
        timeout=60.0,
    )
    result = query.get("result")
    structured = result.get("structuredContent") if isinstance(result, dict) else None
    if not isinstance(structured, dict) or structured.get("schema") != "laplace.inspect.summary/v1":
        raise RuntimeError("MCP laplace.query did not return live canonical substrate state")

    proof = {
        "schema": "laplace.installed-product-gateway-proof/v1",
        "package_id": package_id,
        "timestamp": int(time.time()),
        "browser": {"index": "ok", "app": "ok"},
        "health": health,
        "descriptors": descriptors,
        "cognition": {
            "session": session,
            "turn_0": {
                "status": first.get("status"),
                "ordinal": first.get("turn_ordinal"),
                "checkpoint": first.get("next_checkpoint_fingerprint"),
                "output_utf8": first.get("output_utf8"),
            },
            "turn_1": {
                "status": second.get("status"),
                "ordinal": second.get("turn_ordinal"),
                "continued": second.get("continued"),
                "checkpoint": second.get("next_checkpoint_fingerprint"),
                "output_utf8": second.get("output_utf8"),
            },
        },
        "openai_chat": {
            "message_count": len(chat_messages),
            "roles": [entry["role"] for entry in chat_messages],
            "object": chat.get("object"),
            "model": chat.get("model"),
            "assistant_content": message.get("content") if isinstance(message, dict) else None,
        },
        "mcp": {"tools": sorted(names), "query_schema": structured.get("schema")},
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(proof, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(proof, sort_keys=True))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--package-id", required=True)
    args = parser.parse_args()
    if HEX256.fullmatch(args.package_id) is None:
        parser.error("--package-id must be a 64-character lowercase hexadecimal package identity")
    prove(args.output, args.package_id)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
