#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


class OpenAIChatCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        path = Path(__file__).resolve().parents[1] / "tools/openai_api_compat.py"
        spec = importlib.util.spec_from_file_location("laplace_openai_api_compat", path)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"cannot load {path}")
        cls.module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = cls.module
        spec.loader.exec_module(cls.module)

    def payload(self, messages: list[dict[str, str]], **extra: object) -> dict[str, object]:
        return {"model": "laplace-native", "messages": messages, **extra}

    def test_accepts_multi_message_conversation(self) -> None:
        prompt = self.module.normalize_chat_payload(self.payload([
            {"role": "system", "content": "You are Laplace."},
            {"role": "user", "content": "Write a Python function."},
            {"role": "assistant", "content": "def f():\n    return 1"},
            {"role": "user", "content": "Now add a type annotation."},
        ]))
        self.assertIn("<SYSTEM>\nYou are Laplace.", prompt)
        self.assertIn("<USER>\nWrite a Python function.", prompt)
        self.assertIn("<ASSISTANT>\ndef f():", prompt)
        self.assertTrue(prompt.endswith("<ASSISTANT>\n"))

    def test_accepts_streaming_profile(self) -> None:
        prompt = self.module.normalize_chat_payload(
            self.payload([{"role": "user", "content": "stream this"}], stream=True)
        )
        self.assertIn("<USER>\nstream this", prompt)

    def test_rejects_non_boolean_stream(self) -> None:
        with self.assertRaises(self.module.ChatProfileError):
            self.module.normalize_chat_payload(
                self.payload([{"role": "user", "content": "hello"}], stream="true")
            )

    def test_requires_at_least_one_user_message(self) -> None:
        with self.assertRaises(self.module.ChatProfileError):
            self.module.normalize_chat_payload(self.payload([{"role": "system", "content": "Only system context."}]))

    def test_rejects_unsupported_role(self) -> None:
        with self.assertRaises(self.module.ChatProfileError):
            self.module.normalize_chat_payload(self.payload([
                {"role": "tool", "content": "opaque"},
                {"role": "user", "content": "continue"},
            ]))

    def test_rejects_unsupported_top_level_parameter(self) -> None:
        value = self.payload([{"role": "user", "content": "hello"}])
        value["temperature"] = 0.2
        with self.assertRaises(self.module.ChatProfileError):
            self.module.normalize_chat_payload(value)


if __name__ == "__main__":
    unittest.main()
