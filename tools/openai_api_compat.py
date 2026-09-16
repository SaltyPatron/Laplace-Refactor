#!/usr/bin/env python3
"""Truthful OpenAI-compatible boundary over the installed Laplace gateway.

The prior adapter flattened system/developer/user/assistant history into one textual
prompt and called that multi-message compatibility.  That destroys typed role state.
Until #271 is implemented through native discourse, this public entrypoint exposes only
the narrower operation it can preserve exactly: one user message.
"""
from __future__ import annotations

from importlib.machinery import SourceFileLoader
from importlib.util import module_from_spec, spec_from_loader
from pathlib import Path
import sys
from typing import Any

MODEL_ID = "laplace-native"
MAXIMUM_PROMPT_BYTES = 1024 * 1024


class ChatProfileError(ValueError):
    def __init__(
        self,
        status: int,
        message: str,
        *,
        param: str | None = None,
        code: str | None = None,
    ) -> None:
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
        raise ChatProfileError(
            400,
            "unsupported Chat Completions field(s): " + ", ".join(unsupported),
            param=unsupported[0],
            code="unsupported_parameter",
        )
    if payload.get("model") != MODEL_ID:
        raise ChatProfileError(
            404, f"model must be {MODEL_ID!r}", param="model", code="model_not_found"
        )
    stream = payload.get("stream", False)
    if not isinstance(stream, bool):
        raise ChatProfileError(400, "stream must be a boolean", param="stream")
    n = payload.get("n", 1)
    if isinstance(n, bool) or not isinstance(n, int) or n != 1:
        raise ChatProfileError(400, "this compatibility profile requires n=1", param="n")

    messages = payload.get("messages")
    if not isinstance(messages, list) or not messages:
        raise ChatProfileError(400, "messages must be a non-empty array", param="messages")
    if len(messages) != 1:
        raise ChatProfileError(
            400,
            "client-supplied multi-message role history is not supported until native discourse preserves roles end to end",
            param="messages",
            code="unsupported_conversation_history",
        )

    message = messages[0]
    if not isinstance(message, dict):
        raise ChatProfileError(400, "messages[0] must be an object", param="messages[0]")
    extra = sorted(set(message) - {"role", "content"})
    if extra:
        raise ChatProfileError(
            400,
            "messages[0] contains unsupported field(s): " + ", ".join(extra),
            param=f"messages[0].{extra[0]}",
            code="unsupported_parameter",
        )
    if message.get("role") != "user":
        raise ChatProfileError(
            400,
            "this compatibility profile currently accepts exactly one user message; other roles require typed native discourse support",
            param="messages[0].role",
            code="unsupported_message_role",
        )
    content = message.get("content")
    if not isinstance(content, str) or not content:
        raise ChatProfileError(
            400,
            "messages[0].content must be non-empty text",
            param="messages[0].content",
        )
    if len(content.encode("utf-8")) > MAXIMUM_PROMPT_BYTES:
        raise ChatProfileError(
            413,
            "user message exceeds the installed Laplace 1 MiB prompt limit",
            param="messages[0].content",
        )
    return content


def _load_previous_transport() -> Any:
    directory = Path(__file__).resolve().parent
    installed = directory / "laplace-openai-api-transport"
    source = directory / "_openai_api_compat_pre_reconciliation.py"
    target = installed if installed.is_file() else source
    if not target.is_file() or target.is_symlink():
        raise RuntimeError(f"Laplace compatibility transport implementation is unavailable: {target}")
    loader = SourceFileLoader("laplace_openai_api_transport", str(target))
    spec = spec_from_loader(loader.name, loader)
    if spec is None:
        raise RuntimeError(f"cannot load Laplace compatibility transport: {target}")
    module = module_from_spec(spec)
    sys.modules[loader.name] = module
    # Installed package bytes are immutable. Keep dynamic sibling imports from
    # creating __pycache__ entries, including imports performed by that module.
    previous_bytecode_policy = sys.dont_write_bytecode
    sys.dont_write_bytecode = True
    try:
        loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = previous_bytecode_policy
    return module


def main() -> int:
    transport = _load_previous_transport()
    # Reuse only the HTTP/source/Explore transport implementation.  Replace its false
    # transcript-flattening Chat profile before it constructs the request handler.
    transport.ChatProfileError = ChatProfileError
    transport.normalize_chat_payload = normalize_chat_payload
    return int(transport.main())


if __name__ == "__main__":
    raise SystemExit(main())
