#!/usr/bin/env python3
"""Mutation tests for the PostgreSQL host build-provider receipt."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[1]
PATH = REPOSITORY / "tools/postgresql/host-provider.py"
SPEC = importlib.util.spec_from_file_location("laplace_host_provider", PATH)
assert SPEC is not None and SPEC.loader is not None
HOST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HOST)

PUBLISHED_PATH = REPOSITORY / "tools/product/published-host-provider.py"
PUBLISHED_SPEC = importlib.util.spec_from_file_location(
    "laplace_published_host_provider", PUBLISHED_PATH
)
assert PUBLISHED_SPEC is not None and PUBLISHED_SPEC.loader is not None
PUBLISHED = importlib.util.module_from_spec(PUBLISHED_SPEC)
PUBLISHED_SPEC.loader.exec_module(PUBLISHED)


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
            self.assertEqual(HOST.verify_inputs(receipt), receipt)

    def test_input_replay_preserves_historical_host_without_requalification(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            provider = Path(temporary) / "provider"
            provider.mkdir()
            (provider / "input").write_bytes(b"selected\n")
            receipt = HOST.observe([provider], [])
            historical = receipt["host"]
            different_host = mock.Mock(
                sysname=historical["sysname"],
                release=historical["release"] + "-later",
                version=historical["version"] + " later",
                machine=historical["machine"],
            )
            with mock.patch.object(HOST.os, "uname", return_value=different_host):
                self.assertEqual(HOST.verify_inputs(receipt), receipt)
                with self.assertRaisesRegex(
                    HOST.ProviderError, "bytes or kernel identity differ"
                ):
                    HOST.verify(receipt)

    def test_published_receipt_survives_later_ambient_provider_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            provider = Path(temporary) / "provider"
            provider.mkdir()
            item = provider / "input"
            item.write_bytes(b"qualified-build-input\n")
            receipt = HOST.observe([provider], [])

            item.write_bytes(b"later-unrelated-host-state\n")
            with self.assertRaisesRegex(HOST.ProviderError, "provider bytes differ"):
                HOST.verify_inputs(receipt)

            self.assertEqual(PUBLISHED.verify_published_receipt(receipt), receipt)

    def test_published_receipt_rejects_retained_provenance_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            provider = Path(temporary) / "provider"
            provider.mkdir()
            (provider / "input").write_bytes(b"qualified-build-input\n")
            receipt = HOST.observe([provider], [])
            mutated = copy.deepcopy(receipt)
            mutated["scope"] = "product-runtime"
            identity = dict(mutated)
            identity.pop("provider_id", None)
            mutated["provider_id"] = hashlib.sha256(
                HOST.canonical_bytes(identity)
            ).hexdigest()

            with self.assertRaisesRegex(HOST.ProviderError, "scope differs"):
                PUBLISHED.verify_published_receipt(mutated)

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

    def test_input_replay_still_rejects_provider_byte_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            provider = Path(temporary) / "provider"
            provider.mkdir()
            item = provider / "input"
            item.write_bytes(b"before\n")
            receipt = HOST.observe([provider], [])
            item.write_bytes(b"after\n")
            with self.assertRaises(HOST.ProviderError) as caught:
                HOST.verify_inputs(receipt)
            message = str(caught.exception)
            self.assertIn("host build provider bytes differ", message)
            self.assertIn(str(provider.resolve()), message)
            self.assertIn("tree_sha256", message)

    def test_input_replay_localizes_selected_file_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            provider = root / "provider"
            provider.mkdir()
            selected = root / "selected"
            selected.write_bytes(b"before\n")
            receipt = HOST.observe([provider], [selected])
            selected.write_bytes(b"after\n")
            with self.assertRaises(HOST.ProviderError) as caught:
                HOST.verify_inputs(receipt)
            message = str(caught.exception)
            self.assertIn(str(selected), message)
            self.assertIn("sha256", message)
            self.assertIn("size_bytes", message)

    def test_receipt_label_mutation_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            provider = Path(temporary)
            receipt = HOST.observe([provider], [])
            receipt["scope"] = "product-runtime"
            with self.assertRaisesRegex(HOST.ProviderError, "identity differs"):
                HOST.verify(receipt)
            with self.assertRaisesRegex(HOST.ProviderError, "identity differs"):
                HOST.verify_inputs(receipt)

    def test_duplicate_receipt_key_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "receipt.json"
            path.write_text(
                '{"schema":"first","schema":"second"}\n', encoding="utf-8"
            )
            with self.assertRaisesRegex(HOST.ProviderError, "duplicate JSON object key"):
                HOST.read_receipt(path)

    def test_product_contract_uses_published_provider_replay_verifier(self) -> None:
        contract = json.loads(
            (REPOSITORY / "contracts/product-package.json").read_text(encoding="utf-8")
        )
        self.assertEqual(
            contract["host_build_provider"]["verifier"],
            "tools/product/published-host-provider.py",
        )
        self.assertEqual(
            contract["host_build_provider"]["published_receipt_replay"],
            "historical-receipt-identity-no-ambient-requalification",
        )


if __name__ == "__main__":
    unittest.main()
