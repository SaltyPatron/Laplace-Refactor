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
HEX128 = re.compile(r"^[0-9a-f]{32}$")
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


def json_http_expected(method: str, path: str, expected_status: int, payload: Any | None = None, timeout: float = 320.0) -> dict[str, Any]:
    status, content_type, data = http(method, path, payload, timeout)
    if status != expected_status:
        raise RuntimeError(f"{method} {path} returned HTTP {status}, expected {expected_status}")
    if "application/json" not in content_type:
        raise RuntimeError(f"{method} {path} returned unexpected content type {content_type!r}")
    try:
        value = json.loads(data)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"{method} {path} returned invalid JSON") from exc
    if not isinstance(value, dict):
        raise RuntimeError(f"{method} {path} returned non-object JSON")
    return value


def json_http(method: str, path: str, payload: Any | None = None, timeout: float = 320.0) -> dict[str, Any]:
    return json_http_expected(method, path, 200, payload, timeout)


def stream_chat(messages: list[dict[str, str]]) -> dict[str, Any]:
    status, content_type, data = http("POST", "/v1/chat/completions", {"model": "laplace-native", "messages": messages, "stream": True})
    if status != 200 or "text/event-stream" not in content_type:
        raise RuntimeError(f"streaming Chat Completions returned status/content-type {status} {content_type!r}")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RuntimeError("streaming Chat Completions returned non-UTF-8 SSE") from exc
    chunks: list[dict[str, Any]] = []
    done = False
    for line in text.splitlines():
        if not line.startswith("data: "):
            continue
        payload = line[6:]
        if payload == "[DONE]":
            done = True
            continue
        try:
            value = json.loads(payload)
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"streaming Chat Completions returned invalid SSE JSON: {payload!r}") from exc
        if not isinstance(value, dict) or value.get("object") != "chat.completion.chunk":
            raise RuntimeError(f"streaming Chat Completions returned invalid chunk: {value!r}")
        chunks.append(value)
    if not done or len(chunks) < 2:
        raise RuntimeError("streaming Chat Completions did not terminate with chunk sequence plus [DONE]")
    pieces: list[str] = []
    finished = False
    for chunk in chunks:
        choices = chunk.get("choices")
        if not isinstance(choices, list) or len(choices) != 1 or not isinstance(choices[0], dict):
            raise RuntimeError("streaming Chat Completions returned invalid choices")
        choice = choices[0]
        delta = choice.get("delta")
        if not isinstance(delta, dict):
            raise RuntimeError("streaming Chat Completions returned invalid delta")
        content = delta.get("content")
        if content is not None:
            if not isinstance(content, str):
                raise RuntimeError("streaming Chat Completions returned non-text delta content")
            pieces.append(content)
        finished = finished or choice.get("finish_reason") == "stop"
    assistant_content = "".join(pieces)
    if not assistant_content:
        raise RuntimeError("streaming Chat Completions returned no assistant content")
    if not finished:
        raise RuntimeError("streaming Chat Completions never emitted finish_reason=stop")
    return {"chunk_count": len(chunks), "done": done, "finished": finished, "assistant_content": assistant_content}


def require_native_success(value: dict[str, Any], label: str) -> None:
    if value.get("status") != 0:
        raise RuntimeError(f"{label} returned native status {value.get('status')}: {value}")
    if not isinstance(value.get("output_utf8"), str):
        raise RuntimeError(f"{label} returned no UTF-8 cognition output")
    checkpoint = value.get("next_checkpoint_fingerprint")
    if not isinstance(checkpoint, str) or HEX256.fullmatch(checkpoint) is None:
        raise RuntimeError(f"{label} returned invalid checkpoint fingerprint")


def prove_explore() -> dict[str, Any]:
    summary = json_http("GET", "/api/v1/summary", timeout=30.0)
    if summary.get("schema") != "laplace.inspect.summary/v2":
        raise RuntimeError(f"Explore summary is not the v2 world summary: {summary.get('schema')}")
    counts = summary.get("counts")
    if not isinstance(counts, dict):
        raise RuntimeError("Explore summary returned no counts")
    for field in ("entities", "physicalities", "attestations", "consensus", "evidence_nodes", "standing_states", "standing_arenas", "source_profiles"):
        if not isinstance(counts.get(field), int):
            raise RuntimeError(f"Explore summary lacks integer count {field}")

    facets: dict[str, str] = {}
    for path, schema in (
        ("/api/v1/entities?limit=25", "laplace.inspect.entities/v1"),
        ("/api/v1/consensus?limit=25", "laplace.inspect.consensus/v1"),
        ("/api/v1/standings?limit=25", "laplace.inspect.standings/v1"),
        ("/api/v1/evidence?limit=25", "laplace.inspect.evidence/v1"),
        ("/api/v1/sources?limit=25", "laplace.inspect.source-profiles/v1"),
        ("/api/v1/physicalities?limit=25", "laplace.inspect.physicalities/v1"),
        ("/api/v1/attestations?limit=25", "laplace.inspect.attestations/v1"),
    ):
        value = json_http("GET", path, timeout=60.0)
        if value.get("schema") != schema or not isinstance(value.get("rows"), list):
            raise RuntimeError(f"Explore facet {path} returned invalid schema/rows: {value}")
        facets[path.split("?", 1)[0]] = schema

    entities = json_http("GET", "/api/v1/entities?limit=1", timeout=30.0)
    entity_proof: dict[str, Any] = {"present": False}
    entity_rows = entities.get("rows")
    if isinstance(entity_rows, list) and entity_rows:
        entity_id = entity_rows[0].get("entity_id") if isinstance(entity_rows[0], dict) else None
        if not isinstance(entity_id, str) or HEX128.fullmatch(entity_id) is None:
            raise RuntimeError("Explore entity list returned invalid entity id")
        detail = json_http("GET", f"/api/v1/entities/{entity_id}", timeout=30.0)
        if detail.get("schema") != "laplace.inspect.entity/v2":
            raise RuntimeError(f"Explore entity detail is not v2: {detail.get('schema')}")
        for facet in ("physicalities", "attestations", "consensus", "evidence"):
            if not isinstance(detail.get(facet), list):
                raise RuntimeError(f"Explore entity detail lacks facet {facet}")
        entity_proof = {"present": True, "entity_id": entity_id, "schema": detail.get("schema")}

    return {"summary_schema": summary.get("schema"), "counts": counts, "facets": facets, "entity": entity_proof}


def prove_source_ingestion(package_id: str) -> dict[str, Any]:
    source_id = "iso-639-3-20260415"
    catalog = json_http("GET", "/api/v1/source-catalog", timeout=60.0)
    if catalog.get("schema") != "laplace.product.source-catalog/v1":
        raise RuntimeError(f"source catalog schema is invalid: {catalog.get('schema')}")
    rows = catalog.get("rows")
    selected = next((row for row in rows if isinstance(row, dict) and row.get("source_id") == source_id), None) if isinstance(rows, list) else None
    if not isinstance(selected, dict):
        raise RuntimeError(f"configured source catalog does not contain {source_id}")
    if selected.get("profile_available") is not True or selected.get("preflight_available") is not True:
        raise RuntimeError(f"configured source is not preflight-ready: {selected}")
    plan = json_http("POST", "/api/v1/sources/preflight", {"source_id": source_id}, timeout=120.0)
    if plan.get("schema") != "laplace.product.source-ingestion-plan/v1" or plan.get("source_id") != source_id:
        raise RuntimeError(f"source preflight contract is invalid: {plan}")
    plan_id = plan.get("plan_id")
    if not isinstance(plan_id, str) or HEX256.fullmatch(plan_id) is None:
        raise RuntimeError("source preflight returned invalid plan identity")
    resolution = plan.get("physical_resolution")
    if not isinstance(resolution, dict) or resolution.get("exact") is not True:
        raise RuntimeError("source preflight did not bind an exact physical artifact graph")
    submission = {"source_id": source_id, "plan_id": plan_id, "idempotency_key": f"installed-proof-{package_id[:24]}"}
    first = json_http_expected("POST", "/api/v1/sources/admit", 202, submission, timeout=120.0)
    second = json_http_expected("POST", "/api/v1/sources/admit", 202, submission, timeout=120.0)
    job_id = first.get("job_id")
    if not isinstance(job_id, str) or HEX256.fullmatch(job_id) is None:
        raise RuntimeError(f"source admission returned invalid job id: {job_id!r}")
    if second.get("job_id") != job_id:
        raise RuntimeError("idempotent source admission retry created a second effective job")
    deadline = time.monotonic() + 900.0
    job = first
    while time.monotonic() < deadline:
        job = json_http("GET", f"/api/v1/source-jobs/{job_id}", timeout=30.0)
        if job.get("state") in {"succeeded", "failed", "cancelled"}:
            break
        time.sleep(2.0)
    if job.get("state") != "succeeded":
        raise RuntimeError(f"durable source ingestion did not succeed: {job}")
    result, readback, result_sha = job.get("result"), job.get("readback"), job.get("result_sha256")
    if not isinstance(result, dict) or result.get("schema") != "laplace.admit-source/v1" or result.get("profile") != source_id:
        raise RuntimeError("durable source job did not retain the admission result")
    if not isinstance(readback, dict) or readback.get("schema") != "laplace.inspect.source-profiles/v1":
        raise RuntimeError("durable source job did not retain source-profile readback")
    if not isinstance(result_sha, str) or HEX256.fullmatch(result_sha) is None:
        raise RuntimeError("durable source job did not retain a result identity")
    return {"catalog_boundary_sha256": catalog.get("boundary_sha256"), "source_id": source_id, "plan_id": plan_id, "job_id": job_id, "idempotent_retry_same_job": True, "state": job.get("state"), "result_sha256": result_sha, "admission_schema": result.get("schema"), "readback_schema": readback.get("schema"), "entity_count": result.get("entity_count"), "physicality_count": result.get("physicality_count"), "attestation_count": result.get("attestation_count")}


def prove(output: Path, package_id: str) -> None:
    status, content_type, index = http("GET", "/", timeout=10.0)
    if status != 200 or "text/html" not in content_type or b"Laplace" not in index:
        raise RuntimeError("installed browser root did not return the Laplace application")
    for marker in (b'data-workspace="explore"', b'data-workspace="chat"', b'data-workspace="operator"', b'data-facet="consensus"', b'data-facet="standings"', b'data-facet="evidence"'):
        if marker not in index:
            raise RuntimeError(f"installed browser index lacks product marker {marker!r}")
    if b'data-workspace="graph"' in index or b'data-workspace="sources"' in index or b'data-workspace="sql"' in index:
        raise RuntimeError("installed browser still exposes implementation workspaces as primary navigation")

    status, content_type, app = http("GET", "/app.js", timeout=10.0)
    if status != 200 or "javascript" not in content_type:
        raise RuntimeError("installed browser JavaScript is unavailable")
    for marker in (b"/api/v1/cognition", b"/api/v1/consensus", b"/api/v1/evidence", b"/api/v1/standings", b"/api/v1/source-catalog", b"/api/v1/sources/preflight", b"/api/v1/sources/admit", b"/api/v1/source-jobs/", b"/api/v1/sql"):
        if marker not in app:
            raise RuntimeError(f"installed browser application lacks required route marker {marker!r}")

    health = json_http("GET", "/api/v1/health", timeout=10.0)
    if health.get("schema") != "laplace.product.health/v1" or health.get("status") != "ready":
        raise RuntimeError(f"gateway health is not ready: {health}")

    descriptors = json_http("GET", "/api/v1/descriptors", timeout=10.0)
    if descriptors.get("schema") != "laplace.product.descriptors/v1":
        raise RuntimeError("gateway descriptors schema is invalid")
    transports = descriptors.get("transports")
    if not isinstance(transports, dict) or transports.get("mcp") != "/mcp" or transports.get("openai") != "/v1":
        raise RuntimeError("gateway descriptors do not expose MCP and OpenAI transports")
    explore_descriptor = descriptors.get("explore")
    if not isinstance(explore_descriptor, dict) or explore_descriptor.get("schema") != "laplace.product.explore/v1":
        raise RuntimeError("gateway descriptors do not expose the Explore information-world contract")
    source_descriptor = descriptors.get("source_ingestion")
    if not isinstance(source_descriptor, dict) or source_descriptor.get("caller_supplied_server_path") is not False or source_descriptor.get("idempotent_durable_jobs") is not True:
        raise RuntimeError("gateway descriptors do not expose the selected durable source-ingestion boundary")

    explore = prove_explore()

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
    chat = json_http("POST", "/v1/chat/completions", {"model": "laplace-native", "messages": chat_messages})
    if chat.get("object") != "chat.completion" or chat.get("model") != "laplace-native":
        raise RuntimeError(f"OpenAI-compatible chat response is invalid: {chat}")
    choices = chat.get("choices")
    if not isinstance(choices, list) or len(choices) != 1:
        raise RuntimeError("OpenAI-compatible chat returned an invalid choice set")
    message = choices[0].get("message") if isinstance(choices[0], dict) else None
    if not isinstance(message, dict) or message.get("role") != "assistant" or not isinstance(message.get("content"), str) or not message.get("content"):
        raise RuntimeError("OpenAI-compatible chat returned no assistant text")
    streamed = stream_chat(chat_messages)

    initialized = json_http("POST", "/mcp", {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2024-11-05"}}, timeout=10.0)
    if initialized.get("jsonrpc") != "2.0" or not isinstance(initialized.get("result"), dict):
        raise RuntimeError("MCP initialize response is invalid")
    tools = json_http("POST", "/mcp", {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}}, timeout=10.0)
    tool_values = tools.get("result", {}).get("tools") if isinstance(tools.get("result"), dict) else None
    names = {item.get("name") for item in tool_values if isinstance(item, dict)} if isinstance(tool_values, list) else set()
    required_tools = {"laplace.query", "laplace.cognition", "laplace.source_catalog", "laplace.source_preflight", "laplace.source_admit", "laplace.source_job", "laplace.sql"}
    if not required_tools.issubset(names):
        raise RuntimeError(f"MCP tool surface is incomplete: {sorted(names)}")
    query = json_http("POST", "/mcp", {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "laplace.query", "arguments": {"collection": "summary"}}}, timeout=60.0)
    result = query.get("result")
    structured = result.get("structuredContent") if isinstance(result, dict) else None
    if not isinstance(structured, dict) or structured.get("schema") != "laplace.inspect.summary/v2":
        raise RuntimeError("MCP laplace.query did not return live v2 canonical substrate state")

    source_ingestion = prove_source_ingestion(package_id)
    proof = {
        "schema": "laplace.installed-product-gateway-proof/v2",
        "package_id": package_id,
        "timestamp": int(time.time()),
        "browser": {"index": "explore-chat-operator", "app": "ok"},
        "explore": explore,
        "health": health,
        "descriptors": descriptors,
        "cognition": {
            "session": session,
            "turn_0": {"status": first.get("status"), "ordinal": first.get("turn_ordinal"), "checkpoint": first.get("next_checkpoint_fingerprint"), "output_utf8": first.get("output_utf8")},
            "turn_1": {"status": second.get("status"), "ordinal": second.get("turn_ordinal"), "continued": second.get("continued"), "checkpoint": second.get("next_checkpoint_fingerprint"), "output_utf8": second.get("output_utf8")},
        },
        "openai_chat": {"message_count": len(chat_messages), "roles": [entry["role"] for entry in chat_messages], "object": chat.get("object"), "model": chat.get("model"), "assistant_content": message.get("content") if isinstance(message, dict) else None, "streaming": streamed},
        "source_ingestion": source_ingestion,
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
