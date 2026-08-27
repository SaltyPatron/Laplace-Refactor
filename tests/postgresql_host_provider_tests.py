#!/usr/bin/env python3
"""Mutation tests for the PostgreSQL host build-provider receipt."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[1]
PATH = REPOSITORY / "tools/postgresql/host-provider.py"
SPEC = importlib.util.spec_from_file_location("laplace_host_provider", PATH)
assert SPEC is not None and SPEC.loader is not None
HOST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HOST)


class HostProviderTests(unittest.TestCase):
    def test_exact_tree_and_file_are_verified(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            provider = root / "provider"
            provider.mkdir()
            (provider / "input").write_bytes(b"input\n")
            selected = root / "selected"
            selected.write_bytes(b"selected\n")
            receipt = HOST.observe([provider], [selected])
            self.assertEqual(HOST.verify(receipt), receipt)

    def test_tree_mutation_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            provider = Path(temporary) / "provider"
            provider.mkdir()
            item = provider / "input"
            item.write_bytes(b"before\n")
            receipt = HOST.observe([provider], [])
            item.write_bytes(b"after\n")
            with self.assertRaisesRegex(HOST.ProviderError, "differ"):
                HOST.verify(receipt)

    def test_receipt_label_mutation_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            provider = Path(temporary)
            receipt = HOST.observe([provider], [])
            receipt["scope"] = "product-runtime"
            with self.assertRaisesRegex(HOST.ProviderError, "identity differs"):
                HOST.verify(receipt)

    def test_duplicate_receipt_key_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "receipt.json"
            path.write_text(
                '{"schema":"first","schema":"second"}\n', encoding="utf-8"
            )
            with self.assertRaisesRegex(HOST.ProviderError, "duplicate JSON object key"):
                HOST.read_receipt(path)


if __name__ == "__main__":
    unittest.main()
