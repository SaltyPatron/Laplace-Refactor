#!/usr/bin/env python3
"""Contract and deliberate-defect tests for the installed cognition command."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "laplace_installed_cognition", ROOT / "tools/cognition.py"
)
assert SPEC is not None and SPEC.loader is not None
COGNITION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COGNITION)


def identities() -> dict[str, str]:
    return {
        "schema": COGNITION.IDENTITIES_SCHEMA,
        "request_fingerprint": "10" * 32,
        "activation_epoch_id": "20" * 16,
        "activation_epoch_fingerprint": "30" * 32,
        "authority_fingerprint": "40" * 32,
        "source_epoch": "50" * 32,
        "identity_epoch": "51" * 32,
        "geometry_epoch": "52" * 32,
        "evidence_epoch": "53" * 32,
        "firmware_epoch": "54" * 32,
        "dependency_epoch": "55" * 32,
        "database_epoch": "56" * 32,
        "perfcache_epoch": "57" * 32,
        "numeric_epoch": "58" * 32,
        "package_epoch": "59" * 32,
    }


def firmware() -> dict[str, object]:
    return {
        "schema": COGNITION.FIRMWARE_SCHEMA,
        "relation": "semantic",
        "program_id": "60" * 32,
        "image_bytes": 4,
        "image_hex": "01020304",
    }


def result(program_id: str = "60" * 32) -> dict[str, object]:
    return {
        "status": 0,
        "program_id": "\\x" + program_id,
        "output": "\\x41",
        "checkpoint": "\\x0102",
        "next_checkpoint_fingerprint": "\\x" + "70" * 32,
        "execution_receipt_id": "\\x" + "71" * 32,
        "trace_fingerprint": "\\x" + "72" * 32,
        "output_fingerprint": "\\x" + "73" * 32,
        "prompt_admission_receipt_id": "\\x" + "74" * 32,
        "prompt_persistence_receipt_id": "\\x" + "75" * 32,
        "trunk_entity_id": "\\x" + "76" * 16,
    }


class InstalledCognitionCliTests(unittest.TestCase):
    def test_product_package_requires_installed_cognition_command(self) -> None:
        contract = json.loads((ROOT / "contracts/product-package.json").read_text(encoding="utf-8"))
        cluster = json.loads((ROOT / "contracts/postgresql-cluster.json").read_text(encoding="utf-8"))
        tools = (ROOT / "tools/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("bin/laplace-cognition", contract["package"]["required_files"])
        self.assertIn("bin/laplace-cognition", cluster["package"]["required_files"])
        self.assertIn("RENAME laplace-cognition", tools)

    def test_prompt_is_bound_as_exact_utf8_bytes_not_sql_text(self) -> None:
        prompt = "King' ; SELECT pg_sleep(999); -- ♔"
        sql = COGNITION.render_cognition_sql(
            prompt,
            identities(),
            firmware(),
            "30" * 32,
            "58" * 32,
        )
        self.assertNotIn(prompt, sql)
        self.assertIn(prompt.encode("utf-8").hex(), sql)
        self.assertIn("cognition_firmware_execute_product", sql)
        self.assertNotIn("expected_answer", sql.lower())

    def test_command_uses_bounded_product_profile_not_user_sql_or_opcode(self) -> None:
        sql = COGNITION.render_cognition_sql(
            "AA", identities(), firmware(), "30" * 32, "58" * 32
        )
        for required in (
            "64::numeric,256::numeric,128::numeric,64::numeric,1048576::numeric",
            "16::numeric,32::numeric,32::numeric,32::numeric,16::numeric,8192::numeric",
            "64::numeric,64::numeric,4096::numeric,16,1",
            "8388608::bigint",
        ):
            self.assertIn(required, sql)
        self.assertNotIn("isa_execute_batch", sql)

    def test_checkpoint_requires_exact_predecessor_fingerprint(self) -> None:
        with self.assertRaisesRegex(COGNITION.CognitionCommandError, "require --checkpoint-fingerprint"):
            COGNITION.render_cognition_sql(
                "next",
                identities(),
                firmware(),
                "30" * 32,
                "58" * 32,
                previous_checkpoint=b"checkpoint",
            )
        sql = COGNITION.render_cognition_sql(
            "next",
            identities(),
            firmware(),
            "30" * 32,
            "58" * 32,
            turn_ordinal=1,
            previous_checkpoint=b"checkpoint",
            previous_checkpoint_fingerprint="77" * 32,
        )
        self.assertIn("decode('" + "77" * 32 + "','hex'),true", sql)
        self.assertIn(b"checkpoint".hex(), sql)
        self.assertIn(",1::numeric,0,1::numeric,1048576::numeric", sql)

    def test_firmware_mismatch_is_detected_before_result_is_accepted(self) -> None:
        with self.assertRaisesRegex(COGNITION.CognitionCommandError, "differs"):
            COGNITION.validate_result(result("61" * 32), firmware())

    def test_success_returns_exact_output_and_checkpoint(self) -> None:
        output, checkpoint, fingerprint = COGNITION.validate_result(result(), firmware())
        self.assertEqual(output, b"A")
        self.assertEqual(checkpoint, b"\x01\x02")
        self.assertEqual(fingerprint, "70" * 32)

    def test_activation_identity_chain_must_match_product_receipt(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-cognition-test-") as temporary:
            root = Path(temporary)
            package_id = "aa" * 32
            receipt = {
                "schema": COGNITION.UNICODE_RECEIPT_SCHEMA,
                "phase": "product-activated",
                "package_id": package_id,
                "request_fingerprint": "10" * 32,
                "activation_epoch_id": "20" * 16,
                "activation_epoch_fingerprint": "30" * 32,
            }
            (root / "unicode-product-activation.json").write_text(
                json.dumps(receipt), encoding="utf-8"
            )
            identity_path = root / "unicode" / ("10" * 32) / "identities.json"
            identity_path.parent.mkdir(parents=True)
            identity_path.write_text(json.dumps(identities()), encoding="utf-8")
            self.assertEqual(
                COGNITION.load_activation_state(root, package_id)["authority_fingerprint"],
                "40" * 32,
            )
            broken = identities()
            broken["activation_epoch_fingerprint"] = "31" * 32
            identity_path.write_text(json.dumps(broken), encoding="utf-8")
            with self.assertRaisesRegex(COGNITION.CognitionCommandError, "differs"):
                COGNITION.load_activation_state(root, package_id)


if __name__ == "__main__":
    unittest.main()
