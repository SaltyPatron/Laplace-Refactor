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

    def test_preserves_single_user_message_exactly(self) -> None:
        prompt = self.module.normalize_chat_payload(
            self.payload([{"role": "user", "content": "Write a Python function."}])
        )
        self.assertEqual(prompt, "Write a Python function.")
        self.assertNotIn("<USER>", prompt)
        self.assertNotIn("OpenAI-compatible conversation transcript", prompt)

    def test_rejects_multi_message_history_until_roles_are_native(self) -> None:
        with self.assertRaises(self.module.ChatProfileError) as caught:
            self.module.normalize_chat_payload(self.payload([
                {"role": "system", "content": "You are Laplace."},
                {"role": "user", "content": "Write a Python function."},
                {"role": "assistant", "content": "def f():\n    return 1"},
                {"role": "user", "content": "Now add a type annotation."},
            ]))
        self.assertEqual(caught.exception.code, "unsupported_conversation_history")

    def test_rejects_non_user_role_until_roles_are_native(self) -> None:
        with self.assertRaises(self.module.ChatProfileError) as caught:
            self.module.normalize_chat_payload(
                self.payload([{"role": "system", "content": "Only system context."}])
            )
        self.assertEqual(caught.exception.code, "unsupported_message_role")

    def test_accepts_streaming_transport_for_single_user_message(self) -> None:
        prompt = self.module.normalize_chat_payload(
            self.payload([{"role": "user", "content": "stream this"}], stream=True)
        )
        self.assertEqual(prompt, "stream this")

    def test_rejects_non_boolean_stream(self) -> None:
        with self.assertRaises(self.module.ChatProfileError):
            self.module.normalize_chat_payload(
                self.payload([{"role": "user", "content": "hello"}], stream="true")
            )

    def test_rejects_unsupported_message_name_until_typed(self) -> None:
        with self.assertRaises(self.module.ChatProfileError):
            self.module.normalize_chat_payload(self.payload([
                {"role": "user", "content": "hello", "name": "anthony"},
            ]))

    def test_rejects_unsupported_top_level_parameter(self) -> None:
        value = self.payload([{"role": "user", "content": "hello"}])
        value["temperature"] = 0.2
        with self.assertRaises(self.module.ChatProfileError):
            self.module.normalize_chat_payload(value)


if __name__ == "__main__":
    unittest.main()
