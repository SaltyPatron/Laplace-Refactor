#!/usr/bin/env python3
"""Prove the installed Laplace browser/gateway through its real local HTTP surface."""
from __future__ import annotations

import argparse
import hashlib
from http.client import HTTPConnection, HTTPException, HTTPResponse
import importlib.util
import io
import json
from pathlib import Path
import re
import stat
import sys
import time
from typing import Any
from urllib import error, request
from urllib.parse import urlsplit

BASE = "http://127.0.0.1:55434"
HEX256 = re.compile(r"^[0-9a-f]{64}$")
HEX128 = re.compile(r"^[0-9a-f]{32}$")
MAXIMUM_RESPONSE_BYTES = 24 * 1024 * 1024

# Activation readiness reuses the package owner's canonical manifest validation.
# It reads the installed surface only; the broader gateway proof below owns
# cognition, transport and explicitly selected source-admission exercises.
ROOT = Path(__file__).resolve().parents[2]
READINESS_RESPONSE_BYTES = 4 * 1024 * 1024
READINESS_TIMEOUT_SECONDS = 30.0
WEB_ASSETS = (
    ("/", "index.html", ("text/html",)),
    ("/app.js", "app.js", ("text/javascript", "application/javascript")),
    ("/styles.css", "styles.css", ("text/css",)),
)


def package_owner() -> Any:
    name = "laplace_gateway_package_owner"
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, ROOT / "tools/postgresql/cluster_core.py")
        if spec is None or spec.loader is None:
            raise RuntimeError("cannot load the canonical package owner")
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


def readiness_timeout(deadline: float) -> float:
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise RuntimeError("installed HTTP readiness deadline expired")
    return min(10.0, remaining)


class ReadinessSocketReader(io.RawIOBase):
    """Apply the shared wall deadline to every underlying receive.

    HTTPResponse may read a status/header line or chunk framing using many
    receives. A fixed socket timeout limits inactivity, not total elapsed time.
    The unbuffered socket file keeps its descriptor alive if HTTPConnection
    closes its socket after receiving a Connection: close response.
    """
    def __init__(self, raw: Any, sock: Any, deadline: float) -> None:
        super().__init__()
        self.raw = raw
        self.sock = sock
        self.deadline = deadline

    def readable(self) -> bool:
        return True

    def readinto(self, buffer: Any) -> int:
        self.sock.settimeout(readiness_timeout(self.deadline))
        return self.raw.readinto(buffer)

    def close(self) -> None:
        try:
            self.raw.close()
        finally:
            super().close()


class ReadinessResponseSocket:
    def __init__(self, sock: Any, deadline: float) -> None:
        self.sock = sock
        self.deadline = deadline

    def makefile(self, mode: str) -> io.BufferedReader:
        if mode != "rb":
            raise RuntimeError("HTTP readiness requires a read-only response stream")
        raw = self.sock.makefile(mode, buffering=0)
        return io.BufferedReader(ReadinessSocketReader(raw, self.sock, self.deadline))


def readiness_get(path: str, deadline: float) -> tuple[str, bytes]:
    timeout = readiness_timeout(deadline)
    endpoint = urlsplit(BASE)
    if (endpoint.scheme != "http" or endpoint.hostname != "127.0.0.1"
            or endpoint.username is not None or endpoint.password is not None
            or endpoint.path or endpoint.query or endpoint.fragment
            or not path.startswith("/") or path.startswith("//")):
        raise RuntimeError("HTTP readiness requires the direct IPv4 loopback endpoint")
    # HTTPConnection goes directly to the selected loopback socket. There is no
    # proxy lookup or redirect handler that can certify a different service.
    connection = HTTPConnection(endpoint.hostname, endpoint.port or 80, timeout=timeout)
    connection.response_class = lambda sock, *args, **kwargs: HTTPResponse(
        ReadinessResponseSocket(sock, deadline), *args, **kwargs)
    try:
        connection.connect()
        assert connection.sock is not None
        connection.sock.settimeout(readiness_timeout(deadline))
        connection.request("GET", path)
        with connection.getresponse() as response:
            readiness_timeout(deadline)
            if response.status != 200:
                raise RuntimeError(f"GET {path} returned HTTP {response.status}, expected 200")
            content_type = response.headers.get_content_type()
            chunks = []
            size = 0
            while size <= READINESS_RESPONSE_BYTES:
                readiness_timeout(deadline)
                chunk = response.read1(min(64 * 1024, READINESS_RESPONSE_BYTES + 1 - size))
                if not chunk:
                    break
                chunks.append(chunk)
                size += len(chunk)
            data = b"".join(chunks)
    except (OSError, HTTPException) as exc:
        if time.monotonic() >= deadline:
            raise RuntimeError("installed HTTP readiness deadline expired") from exc
        raise RuntimeError(f"GET {path} failed: {type(exc).__name__}") from exc
    finally:
        connection.close()
    if len(data) > READINESS_RESPONSE_BYTES:
        raise RuntimeError(f"GET {path} exceeded the readiness response limit")
    readiness_timeout(deadline)
    return content_type, data


def prove_readiness(output: Path, package_id: str, product_receipt: Path,
                    *, root: Path = Path("/")) -> None:
    owner = package_owner()
    report: dict[str, Any] = {
        "schema": "laplace.installed-product-http-readiness/v1",
        "package_id": package_id,
        "endpoint": BASE,
        "scope": "GET-only health and exact installed web assets; no cognition or source admission",
        "status": "failed",
        "assets": [],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    # Never let an earlier success at this output path stand in for a new probe.
    with output.open("x", encoding="utf-8") as receipt_output:
        try:
            if HEX256.fullmatch(package_id) is None or not root.is_absolute():
                raise RuntimeError("invalid selected package identity or physical root")
            contract = owner.load_json(ROOT / "contracts/postgresql-cluster.json")
            if product_receipt.is_symlink() or not product_receipt.is_file():
                raise RuntimeError("selected product receipt is not a physical file")
            selected_sha = owner.sha256_file(product_receipt)
            selected = owner.load_json(product_receipt)
            if (selected.get("schema") != "laplace.product-package-receipt/v1"
                    or selected.get("package_id") != package_id
                    or selected.get("activation_eligible") is not True
                    or selected.get("build_input_closure_complete") is not True
                    or selected.get("product_activated") is not False):
                raise RuntimeError("product receipt differs from the selected eligible package")
            manifest_path = Path(owner.require_absolute_path(selected.get("manifest"), "manifest"))
            if manifest_path.is_symlink() or not manifest_path.is_file():
                raise RuntimeError("selected package manifest is not a physical file")
            manifest_sha = owner.sha256_file(manifest_path)
            if manifest_sha != selected.get("manifest_sha256"):
                raise RuntimeError("selected package manifest bytes differ from its receipt")
            manifest = owner.load_json(manifest_path)
            metadata = owner.verify_package(manifest, contract, None)
            if manifest["package_id"] != package_id:
                raise RuntimeError("canonical manifest identity differs from the selected package")
            release = owner.prefixed(root, manifest["root"])
            if release.is_symlink() or not release.is_dir():
                raise RuntimeError("selected installed release is not a physical directory")
            release = release.resolve(strict=True)
            links = {
                "active": owner.prefixed(root, contract["package"]["active_link"]),
                "runtime": owner.prefixed(root, "/opt/laplace/runtime/refactor"),
            }

            def verify_selection() -> None:
                for label, link in links.items():
                    if not link.is_symlink() or link.resolve(strict=True) != release:
                        raise RuntimeError(f"{label} selection differs from the selected installed package")

            def asset_bytes(name: str) -> bytes:
                relative = "share/laplace/web/" + name
                entry = metadata.files.get(relative)
                path = release / relative
                if (entry is None or entry.get("kind", "file") != "file"
                        or path.is_symlink() or not path.is_file()
                        or path.resolve(strict=True) != path
                        or stat.S_IMODE(path.stat().st_mode) != entry["mode"]):
                    raise RuntimeError(f"installed web asset is not its declared package file: {name}")
                with path.open("rb") as stream:
                    data = stream.read(READINESS_RESPONSE_BYTES + 1)
                if len(data) > READINESS_RESPONSE_BYTES:
                    raise RuntimeError(f"installed web asset exceeds the readiness response limit: {name}")
                if hashlib.sha256(data).hexdigest() != entry["sha256"]:
                    raise RuntimeError(f"installed web asset differs from its package manifest: {name}")
                return data

            verify_selection()
            expected = {name: asset_bytes(name) for _, name, _ in WEB_ASSETS}
            report["product_receipt_sha256"] = selected_sha
            report["package_manifest_sha256"] = manifest_sha
            report["installed_release"] = str(release)
            report["selection"] = {label: str(link) for label, link in links.items()}
            started = time.monotonic()
            deadline = started + READINESS_TIMEOUT_SECONDS
            content_type, body = readiness_get("/health", deadline)
            if content_type != "application/json":
                raise RuntimeError("GET /health returned a non-JSON content type")
            try:
                health = json.loads(body)
            except (json.JSONDecodeError, UnicodeDecodeError) as exc:
                raise RuntimeError("GET /health returned malformed JSON") from exc
            cognition = health.get("cognition") if isinstance(health, dict) else None
            if (not isinstance(health, dict)
                    or health.get("schema") != "laplace.product.health/v1"
                    or health.get("status") != "ready"
                    or not isinstance(cognition, dict)
                    or cognition.get("status") != "ready"
                    or cognition.get("package_id") != package_id
                    or cognition.get("unicode_present") is not True
                    or cognition.get("highway_present") is not True
                    or health.get("web_root") != str(release / "share/laplace/web")):
                raise RuntimeError("GET /health does not identify the ready selected package and web root")
            report["health"] = {
                "path": "/health", "status": 200, "schema": health["schema"],
                "cognition_package_id": cognition["package_id"],
                "unicode_present": True, "highway_present": True,
                "web_root": health["web_root"], "response_sha256": hashlib.sha256(body).hexdigest(),
            }
            for route, name, media_types in WEB_ASSETS:
                content_type, body = readiness_get(route, deadline)
                if content_type not in media_types or body != expected[name]:
                    raise RuntimeError(f"GET {route} differs from the selected package asset or content type")
                report["assets"].append({
                    "path": route, "package_path": "share/laplace/web/" + name,
                    "status": 200, "bytes": len(body), "sha256": hashlib.sha256(body).hexdigest(),
                })
            # Detect selection or on-disk drift during the network observations.
            verify_selection()
            if owner.sha256_file(product_receipt) != selected_sha:
                raise RuntimeError("selected product receipt changed during HTTP readiness")
            if owner.sha256_file(manifest_path) != manifest_sha:
                raise RuntimeError("selected package manifest changed during HTTP readiness")
            for _, name, _ in WEB_ASSETS:
                if asset_bytes(name) != expected[name]:
                    raise RuntimeError(f"installed web asset changed during HTTP readiness: {name}")
            report["elapsed_seconds"] = time.monotonic() - started
            report["status"] = "passed"
        except (RuntimeError, OSError, ValueError) as exc:
            report["failure"] = str(exc)[:1000]
            raise
        finally:
            receipt_output.write(json.dumps(report, indent=2, sort_keys=True) + "\n")


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
    if summary.get("schema") != "laplace.inspect.summary/v1":
        raise RuntimeError(f"Explore summary is not the compatible world summary: {summary.get('schema')}")
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


def prove(output: Path, package_id: str, *, include_source_ingestion: bool = False) -> None:
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

    # The compatibility adapter currently preserves one exact user message only.
    # Client-supplied system/developer/assistant history is intentionally unsupported
    # until #271 carries those roles into native discourse without text flattening.
    chat_messages = [
        {"role": "user", "content": "AA"},
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
    if not isinstance(structured, dict) or structured.get("schema") != "laplace.inspect.summary/v1":
        raise RuntimeError("MCP laplace.query did not return live canonical substrate state")

    source_ingestion = (prove_source_ingestion(package_id) if include_source_ingestion else
                        {"disposition": "not_requested", "reason": "corpus acceptance requires explicit selection"})
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
        "openai_chat": {
            "declared_boundary": "single-user-message",
            "client_supplied_role_history": False,
            "message_count": len(chat_messages),
            "roles": [entry["role"] for entry in chat_messages],
            "object": chat.get("object"),
            "model": chat.get("model"),
            "assistant_content": message.get("content") if isinstance(message, dict) else None,
            "streaming": streamed,
        },
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
    parser.add_argument("--include-source-ingestion", action="store_true")
    parser.add_argument("--readiness-only", action="store_true",
                        help="GET the installed health and package-bound web assets only")
    parser.add_argument("--product-receipt", type=Path,
                        help="existing selected product package receipt for --readiness-only")
    args = parser.parse_args()
    if HEX256.fullmatch(args.package_id) is None:
        parser.error("--package-id must be a 64-character lowercase hexadecimal package identity")
    if args.readiness_only:
        if args.product_receipt is None or args.include_source_ingestion:
            parser.error("--readiness-only requires --product-receipt and excludes source ingestion")
        prove_readiness(args.output, args.package_id, args.product_receipt)
    else:
        if args.product_receipt is not None:
            parser.error("--product-receipt is only used by --readiness-only")
        prove(args.output, args.package_id, include_source_ingestion=args.include_source_ingestion)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
