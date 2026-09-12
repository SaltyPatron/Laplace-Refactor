#!/usr/bin/env python3
"""Installed Laplace HTTP compatibility and product-routing wrapper."""
from __future__ import annotations

from importlib.machinery import SourceFileLoader
from importlib.util import module_from_spec, spec_from_loader
import json
from pathlib import Path
import sys
from typing import Any
from urllib.parse import unquote, urlparse

MODEL_ID = "laplace-native"
MAXIMUM_PROMPT_BYTES = 1024 * 1024
MAXIMUM_MESSAGES = 256
MESSAGE_ROLES = {"system", "developer", "user", "assistant"}


class ChatProfileError(ValueError):
    def __init__(self, status: int, message: str, *, param: str | None = None, code: str | None = None) -> None:
        super().__init__(message)
        self.status = status
        self.message = message
        self.param = param
        self.code = code


def normalize_chat_payload(payload: Any) -> str:
    if not isinstance(payload, dict):
        raise ChatProfileError(400, "request body must be a JSON object")
    allowed = {"model", "messages", "stream", "n", "user"}
    unsupported = sorted(set(payload) - allowed)
    if unsupported:
        raise ChatProfileError(400, "unsupported Chat Completions field(s): " + ", ".join(unsupported), param=unsupported[0], code="unsupported_parameter")
    if payload.get("model") != MODEL_ID:
        raise ChatProfileError(404, f"model must be {MODEL_ID!r}", param="model", code="model_not_found")
    stream = payload.get("stream", False)
    if not isinstance(stream, bool):
        raise ChatProfileError(400, "stream must be a boolean", param="stream")
    n = payload.get("n", 1)
    if isinstance(n, bool) or not isinstance(n, int) or n != 1:
        raise ChatProfileError(400, "this compatibility profile requires n=1", param="n")

    messages = payload.get("messages")
    if not isinstance(messages, list) or not messages:
        raise ChatProfileError(400, "messages must be a non-empty array", param="messages")
    if len(messages) > MAXIMUM_MESSAGES:
        raise ChatProfileError(400, f"messages may contain at most {MAXIMUM_MESSAGES} entries", param="messages")

    transcript: list[str] = ["OpenAI-compatible conversation transcript:"]
    saw_user = False
    for index, message in enumerate(messages):
        param = f"messages[{index}]"
        if not isinstance(message, dict):
            raise ChatProfileError(400, f"{param} must be an object", param=param)
        extra = sorted(set(message) - {"role", "content", "name"})
        if extra:
            raise ChatProfileError(400, f"{param} contains unsupported field(s): " + ", ".join(extra), param=f"{param}.{extra[0]}", code="unsupported_parameter")
        role = message.get("role")
        if role not in MESSAGE_ROLES:
            raise ChatProfileError(400, f"{param}.role must be one of: " + ", ".join(sorted(MESSAGE_ROLES)), param=f"{param}.role")
        content = message.get("content")
        if not isinstance(content, str) or not content:
            raise ChatProfileError(400, f"{param}.content must be non-empty text", param=f"{param}.content")
        name = message.get("name")
        if name is not None and (not isinstance(name, str) or not name or len(name) > 64):
            raise ChatProfileError(400, f"{param}.name must be 1-64 characters", param=f"{param}.name")
        saw_user = saw_user or role == "user"
        heading = role.upper() if name is None else f"{role.upper()} {name}"
        transcript.extend((f"<{heading}>", content))

    if not saw_user:
        raise ChatProfileError(400, "messages must contain at least one user message", param="messages")
    transcript.extend(("<ASSISTANT>", ""))
    prompt = "\n".join(transcript)
    if len(prompt.encode("utf-8")) > MAXIMUM_PROMPT_BYTES:
        raise ChatProfileError(413, "conversation exceeds the installed Laplace 1 MiB prompt limit", param="messages")
    return prompt


def load_module(module_name: str, installed_name: str, source_name: str) -> Any:
    directory = Path(__file__).resolve().parent
    installed = directory / installed_name
    source = directory / source_name
    target = installed if installed.is_file() else source
    if not target.is_file() or target.is_symlink():
        raise RuntimeError(f"installed Laplace component is unavailable: {target}")
    loader = SourceFileLoader(module_name, str(target))
    spec = spec_from_loader(loader.name, loader)
    if spec is None:
        raise RuntimeError(f"cannot load installed Laplace component: {target}")
    module = module_from_spec(spec)
    sys.modules[loader.name] = module
    loader.exec_module(module)
    return module


def load_core() -> Any:
    return load_module("laplace_openai_api_core", "laplace-openai-api-core", "openai_api_service.py")


def load_source_product() -> Any:
    return load_module("laplace_source_product", "laplace-source-product", "source_product.py")


def as_core_chat_error(core: Any, error: ChatProfileError) -> Exception:
    return core.ApiError(error.status, error.message, param=error.param, code=error.code)


def as_core_source_error(core: Any, error: Exception) -> Exception:
    return core.ApiError(
        int(getattr(error, "status", 500)),
        str(getattr(error, "message", error)),
        "laplace_source_error",
        code=str(getattr(error, "code", "source_product_error")),
    )


def mcp_result(request_id: Any, value: Any) -> dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": request_id,
        "result": {
            "content": [{"type": "text", "text": json.dumps(value, ensure_ascii=False, separators=(",", ":"))}],
            "structuredContent": value,
            "isError": False,
        },
    }


def main() -> int:
    core = load_core()
    source_product = load_source_product()
    base_descriptors = core.descriptors
    base_mcp_tool_list = core.mcp_tool_list

    def product_descriptors() -> dict[str, Any]:
        value = base_descriptors()
        operations = dict(value.get("operations") or {})
        operations.update(
            {
                "source_catalog": "/api/v1/source-catalog",
                "source_preflight": "/api/v1/sources/preflight",
                "source_admit": "/api/v1/sources/admit",
                "source_job": "/api/v1/source-jobs/{job_id}",
            }
        )
        value["operations"] = operations
        value["source_ingestion"] = {
            "schema": "laplace.product.source-ingestion-job/v1",
            "selected_boundary": True,
            "caller_supplied_server_path": False,
            "preflight_required": True,
            "idempotent_durable_jobs": True,
        }
        return value

    def product_mcp_tools() -> list[dict[str, Any]]:
        tools: list[dict[str, Any]] = []
        for tool in base_mcp_tool_list():
            if tool.get("name") != "laplace.source_admit":
                tools.append(tool)
        tools.extend(
            [
                {
                    "name": "laplace.source_catalog",
                    "description": "List the authority-selected configured source boundary and physical readiness without admitting data.",
                    "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
                },
                {
                    "name": "laplace.source_preflight",
                    "description": "Bind one selected source to its exact installed profile and physical artifact graph, returning the reviewed plan identity.",
                    "inputSchema": {
                        "type": "object",
                        "properties": {"source_id": {"type": "string"}},
                        "required": ["source_id"],
                        "additionalProperties": False,
                    },
                },
                {
                    "name": "laplace.source_admit",
                    "description": "Submit one reviewed source-ingestion plan as an idempotent durable job.",
                    "inputSchema": {
                        "type": "object",
                        "properties": {
                            "source_id": {"type": "string"},
                            "plan_id": {"type": "string", "pattern": "^[0-9a-f]{64}$"},
                            "idempotency_key": {"type": "string"},
                        },
                        "required": ["source_id", "plan_id", "idempotency_key"],
                        "additionalProperties": False,
                    },
                },
                {
                    "name": "laplace.source_job",
                    "description": "Read durable source-ingestion job state, output, readback, and receipt identity.",
                    "inputSchema": {
                        "type": "object",
                        "properties": {"job_id": {"type": "string", "pattern": "^[0-9a-f]{64}$"}},
                        "required": ["job_id"],
                        "additionalProperties": False,
                    },
                },
            ]
        )
        return tools

    def durable_source_admit(_admit_binary: Path, payload: Any) -> Any:
        try:
            return source_product.submit(payload)
        except source_product.SourceProductError as error:
            raise as_core_source_error(core, error) from error

    core.descriptors = product_descriptors
    core.mcp_tool_list = product_mcp_tools
    core.source_admit_value = durable_source_admit

    class CompatibilityHandler(core.Handler):
        def _source(self, operation: str, payload: Any = None) -> Any:
            try:
                if operation == "catalog":
                    return source_product.catalog()
                if operation == "preflight":
                    if not isinstance(payload, dict) or set(payload) != {"source_id"} or not isinstance(payload.get("source_id"), str):
                        raise source_product.SourceProductError("preflight body must contain exactly one source_id", 400, "invalid_source_preflight")
                    return source_product.preflight(payload["source_id"])
                if operation == "submit":
                    return source_product.submit(payload)
                if operation == "job":
                    if not isinstance(payload, str):
                        raise source_product.SourceProductError("job id is required", 400, "invalid_job_id")
                    return source_product.job_status(payload)
                raise source_product.SourceProductError("unknown source product operation", 404, "source_operation_not_found")
            except source_product.SourceProductError as error:
                raise as_core_source_error(core, error) from error

        def _send_stream(self, completion: dict[str, Any], receipt: str | None) -> None:
            choices = completion.get("choices")
            message = choices[0].get("message") if isinstance(choices, list) and choices and isinstance(choices[0], dict) else None
            content = message.get("content") if isinstance(message, dict) else None
            if not isinstance(content, str):
                raise core.ApiError(502, "Laplace cognition returned no assistant text", "api_error")
            base = {
                "id": completion.get("id"),
                "object": "chat.completion.chunk",
                "created": completion.get("created"),
                "model": completion.get("model"),
                "system_fingerprint": completion.get("system_fingerprint"),
            }
            chunks = [
                {**base, "choices": [{"index": 0, "delta": {"role": "assistant", "content": content}, "finish_reason": None}]},
                {**base, "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]},
            ]
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Connection", "close")
            if receipt is not None:
                self.send_header("X-Laplace-Receipt-ID", receipt)
            self.end_headers()
            for chunk in chunks:
                frame = "data: " + json.dumps(chunk, ensure_ascii=False, separators=(",", ":")) + "\n\n"
                self.wfile.write(frame.encode("utf-8"))
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
            self.close_connection = True

        def _chat_completions(self) -> None:
            self._authorize()
            payload = self._read_json()
            try:
                prompt = normalize_chat_payload(payload)
            except ChatProfileError as error:
                raise as_core_chat_error(core, error) from error
            completion, receipt = core.completion_from_cognition(core.cognition_request(self.cognition_socket, {"prompt": prompt}))
            if payload.get("stream", False):
                self._send_stream(completion, receipt)
            else:
                self._send_json(200, completion, headers={"X-Laplace-Receipt-ID": receipt} if receipt is not None else None)

        def _mcp(self, payload: Any) -> dict[str, Any] | None:
            if isinstance(payload, dict) and payload.get("jsonrpc") == "2.0" and payload.get("method") == "tools/call":
                request_id = payload.get("id")
                params = payload.get("params") or {}
                if isinstance(params, dict):
                    name = params.get("name")
                    arguments = params.get("arguments") or {}
                    if name in {"laplace.source_catalog", "laplace.source_preflight", "laplace.source_admit", "laplace.source_job"}:
                        if not isinstance(arguments, dict):
                            raise core.ApiError(400, "tools/call arguments must be an object")
                        if name == "laplace.source_catalog":
                            value = self._source("catalog")
                        elif name == "laplace.source_preflight":
                            value = self._source("preflight", arguments)
                        elif name == "laplace.source_admit":
                            value = self._source("submit", arguments)
                        else:
                            value = self._source("job", arguments.get("job_id"))
                        return mcp_result(request_id, value)
            return super()._mcp(payload)

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            try:
                if parsed.path == "/api/v1/source-catalog":
                    self._authorize()
                    self._send_json(200, self._source("catalog"))
                    return
                prefix = "/api/v1/source-jobs/"
                if parsed.path.startswith(prefix):
                    self._authorize()
                    job_id = unquote(parsed.path[len(prefix):])
                    self._send_json(200, self._source("job", job_id))
                    return
                super().do_GET()
            except core.ApiError as error:
                self._send_error(error)

        def do_POST(self) -> None:
            parsed = urlparse(self.path)
            try:
                if parsed.path == "/v1/chat/completions":
                    self._chat_completions()
                    return
                if parsed.path == "/api/v1/sources/preflight":
                    self._authorize()
                    self._send_json(200, self._source("preflight", self._read_json()))
                    return
                if parsed.path == "/api/v1/sources/admit":
                    self._authorize()
                    job = self._source("submit", self._read_json())
                    self._send_json(202, job, headers={"Location": f"/api/v1/source-jobs/{job['job_id']}"})
                    return
                super().do_POST()
            except core.ApiError as error:
                self._send_error(error)

    core.Handler = CompatibilityHandler
    return int(core.main())


if __name__ == "__main__":
    raise SystemExit(main())
