#!/usr/bin/env python3
"""OpenAI Chat Completions compatibility wrapper for the installed Laplace gateway."""
from __future__ import annotations

from importlib.machinery import SourceFileLoader
from importlib.util import module_from_spec, spec_from_loader
from pathlib import Path
import sys
from typing import Any

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
    if payload.get("stream", False) is not False:
        raise ChatProfileError(400, "streaming Chat Completions are not implemented by this compatibility profile", param="stream", code="unsupported_parameter")
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


def load_core() -> Any:
    directory = Path(__file__).resolve().parent
    installed = directory / "laplace-openai-api-core"
    source = directory / "openai_api_service.py"
    target = installed if installed.is_file() else source
    if not target.is_file() or target.is_symlink():
        raise RuntimeError(f"Laplace gateway core is unavailable: {target}")
    loader = SourceFileLoader("laplace_openai_api_core", str(target))
    spec = spec_from_loader(loader.name, loader)
    if spec is None:
        raise RuntimeError(f"cannot load Laplace gateway core: {target}")
    module = module_from_spec(spec)
    sys.modules[loader.name] = module
    loader.exec_module(module)
    return module


def main() -> int:
    core = load_core()

    def require_chat_profile(payload: Any) -> str:
        try:
            return normalize_chat_payload(payload)
        except ChatProfileError as error:
            raise core.ApiError(error.status, error.message, param=error.param, code=error.code) from error

    core.require_exact_chat_profile = require_chat_profile
    return int(core.main())


if __name__ == "__main__":
    raise SystemExit(main())
