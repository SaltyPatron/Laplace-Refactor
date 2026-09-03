#!/usr/bin/env python3

from __future__ import annotations

import base64
import importlib.util
import json
from pathlib import Path
import sys
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/product_activation_preflight.py"
SPEC = importlib.util.spec_from_file_location(
    "laplace_product_activation_preflight_tests", MODULE_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product activation preflight")
preflight = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = preflight
SPEC.loader.exec_module(preflight)


KEY = bytes(range(32))


class ProductActivationPreflightTests(unittest.TestCase):
    def test_source_preflight_binds_bundle_contract_and_key_without_exposing_key(self) -> None:
        contract_path = REPOSITORY / "contracts/product-activation-gateway.json"
        contract = preflight.activation.load_json(contract_path)
        secret_name = contract["request"]["secret_environment"]
        encoded = base64.b64encode(KEY).decode("ascii")
        result = preflight.source_preflight(
            REPOSITORY,
            contract_path,
            {secret_name: encoded},
        )

        self.assertEqual(
            result["schema"], "laplace.product-activation-source-preflight/v1"
        )
        self.assertRegex(result["bundle_id"], r"^[0-9a-f]{64}$")
        self.assertRegex(result["contract_sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(
            result["key_fingerprint_sha256"],
            preflight.gateway.activation_key_fingerprint(KEY),
        )
        self.assertNotIn(encoded, json.dumps(result, sort_keys=True))

    def test_key_fingerprint_is_domain_separated_and_changes_with_key(self) -> None:
        first = preflight.gateway.activation_key_fingerprint(KEY)
        second = preflight.gateway.activation_key_fingerprint(b"x" * 32)
        raw_sha = preflight.activation.sha256_bytes(KEY)

        self.assertNotEqual(first, second)
        self.assertNotEqual(first, raw_sha)
        self.assertRegex(first, r"^[0-9a-f]{64}$")

    def test_installed_probe_contract_requires_root_key_fingerprint(self) -> None:
        source = (
            REPOSITORY / "tools/delivery/product_activation_gateway.py"
        ).read_text(encoding="utf-8")
        self.assertIn("laplace.product-activation-gateway-probe/v2", source)
        self.assertIn('contract["request"]["secret_path"]', source)
        self.assertIn('"key_fingerprint_sha256"', source)
        self.assertIn("require_root_ownership=True", source)
        self.assertNotIn("read_text(encoding=\"ascii\").strip())\n    return key", source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
