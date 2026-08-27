#!/usr/bin/env python3
"""Acceptance and deliberate-defect tests for product Unicode activation."""

from __future__ import annotations

import copy
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[1]
IDENTITY_EXECUTABLE = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None
MODULE_PATH = REPOSITORY / "tools/postgresql/unicodectl.py"
SPEC = importlib.util.spec_from_file_location("laplace_unicodectl_tests", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
unicode = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = unicode
SPEC.loader.exec_module(unicode)


def identities() -> dict[str, Any]:
    return {
        "schema": unicode.IDENTITY_SCHEMA,
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


def build_result(contract: dict[str, Any], identity: dict[str, Any]) -> dict[str, Any]:
    expected = contract["expected_result"]
    result = {
        "root_receipt": "61" * 32,
        "producer_receipt": "62" * 32,
        "staged_stream_receipt": "63" * 32,
        "sink_artifacts_fingerprint": "64" * 32,
        "postgresql_artifact_fingerprint": "65" * 32,
        "perfcache_artifact_set_fingerprint": "66" * 32,
        "perfcache_manifest_fingerprint": "67" * 32,
        "perfcache_encoded_manifest_fingerprint": "68" * 32,
        "admission_receipt": "69" * 32,
        "activation_epoch_id": identity["activation_epoch_id"],
        "activation_epoch_fingerprint": identity["activation_epoch_fingerprint"],
        "total_encoded_bytes": 1000000000,
        "batch_count": 68,
        "plan_sequence_fingerprint": "6a" * 32,
        "plan_count": 821,
    }
    for key in (
        "total_frame_count",
        "entity_count",
        "physicality_count",
        "atom_count",
        "ducet_position_count",
        "ducet_contraction_count",
        "normalization_composition_count",
        "tier0_artifact_bytes",
        "reverse_artifact_bytes",
        "tier0_artifact_digest",
        "reverse_artifact_digest",
        "plan_manifest_fingerprint",
    ):
        result[key] = expected[key]
    return result


def readback(contract: dict[str, Any], identity: dict[str, Any]) -> dict[str, Any]:
    positions = contract["operation"]["readback_positions"]
    return {
        "direct_positions": positions,
        "direct_found": [True] * len(positions),
        "direct_entity_ids": ["71" * 16, "72" * 16, "73" * 16],
        "direct_identity_preimage_fingerprints": ["81" * 32, "82" * 32, "83" * 32],
        "direct_physicality_ids": ["91" * 32, "92" * 32, "93" * 32],
        "direct_epoch_id": identity["activation_epoch_id"],
        "direct_epoch_fingerprint": identity["activation_epoch_fingerprint"],
        "reverse_positions": positions,
        "reverse_found": [True] * len(positions),
        "reverse_epoch_id": identity["activation_epoch_id"],
        "reverse_epoch_fingerprint": identity["activation_epoch_fingerprint"],
    }


class UnicodeProductActivationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = unicode.load_json(
            REPOSITORY / "contracts/unicode-product-activation.json"
        )
        self.cluster = unicode.load_json(REPOSITORY / "contracts/postgresql-cluster.json")
        self.source = unicode.load_json(REPOSITORY / "contracts/unicode-source.json")
        self.postgresql = unicode.load_json(
            REPOSITORY / "contracts/unicode-postgresql.json"
        )

    def test_complete_contract_binds_exact_authority_and_result(self) -> None:
        unicode.validate_activation_contract(
            self.contract, self.cluster, self.source, self.postgresql
        )

    def test_native_identity_provider_has_locked_domain_separated_vector(self) -> None:
        if IDENTITY_EXECUTABLE is None:
            self.skipTest("native identity provider path was not supplied")
        value, receipt = unicode.run_identity_provider(
            IDENTITY_EXECUTABLE,
            REPOSITORY / "tests/fixtures/unicode_activation_identity_request.json",
            self.contract,
        )
        self.assertEqual(receipt["exit_code"], 0)
        self.assertEqual(
            value,
            {
                "schema": unicode.IDENTITY_SCHEMA,
                "request_fingerprint": "1205a44594a14c53859cea41112d4acd76cb28a6507e6bf28e3d1df557a0e803",
                "activation_epoch_id": "fa3588a38983360fe985249f912ea613",
                "activation_epoch_fingerprint": "298ba47234976041e3191b718f8ec3547845af6d6116d0e9ba45a5ccfb786d7f",
                "authority_fingerprint": "056855c1826db4bdc28b7b2dbb5b50d8b20bfaea3c2b4950db9a31394ef88d4c",
                "source_epoch": "597881406952d3945c45d5a8236409f23208941bbc46100f5238601424ce12b1",
                "identity_epoch": "410357181dc16ecce7bd8525e8639fe876aab7ad64cd9e5cf70d7e27402420d5",
                "geometry_epoch": "1164e65979071e56a59bd2946fc6deea69ffc9335a537fe1beeb06bf6546cf9e",
                "evidence_epoch": "e80bea3e4efc766036c601ce68aaecaad5175c5181c91f09ed7f44be1a3b2e7b",
                "firmware_epoch": "0eec351707d20dec253e2e1f29a2eb388d7d3f656fd637d2bab1626924aca790",
                "dependency_epoch": "2cf88cdd41b4821ecdb9af388ea9654abb6d6718ae55cfa0248551e482dc84e6",
                "database_epoch": "d8157c1250040a24054521edab5b344fe489de563467c35a03624e326f5f9ab5",
                "perfcache_epoch": "8727ea132ce6c67af7408a7f627f78918a92f94ca521c19b5ac400565dcc3a23",
                "numeric_epoch": "f002152ba98c1060035e47046baa07214c2a189499b3a375141a1a5f3715741b",
                "package_epoch": "91e948bb7c830ec28188141decf31d49e4cef5a18985948b4f12488931289fea",
            },
        )
        self.assertEqual(len(set(value.values())) - 1, 14)

    def test_source_preflight_detects_one_changed_byte(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-unicode-source-") as temporary:
            root = Path(temporary)
            first = root / "ReadMe.txt"
            second = root / "ucd/UnicodeData.txt"
            second.parent.mkdir()
            first.write_text("Version 17.0.0\n", encoding="utf-8")
            second.write_bytes(b"0041;LATIN CAPITAL LETTER A\n")
            source = {
                "unicode_version": "17.0.0",
                "files": [
                    {"path": "ReadMe.txt", "bytes": first.stat().st_size, "sha256": unicode.sha256_file(first)},
                    {"path": "ucd/UnicodeData.txt", "bytes": second.stat().st_size, "sha256": unicode.sha256_file(second)},
                ],
                "version_markers": [{"path": "ReadMe.txt", "contains": "Version 17.0.0"}],
            }
            evidence = unicode.verify_source_bundle(source, root)
            self.assertEqual(len(evidence["files"]), 2)
            second.write_bytes(b"0042;LATIN CAPITAL LETTER B\n")
            with self.assertRaisesRegex(unicode.UnicodeActivationError, "source bytes differ"):
                unicode.verify_source_bundle(source, root)

    def test_initial_state_and_exact_postcommit_recovery_are_distinct(self) -> None:
        identity = identities()
        fresh = {
            "sequence": 0,
            "active_present": False,
            "activation_epoch_id": "0" * 32,
            "activation_epoch_fingerprint": "0" * 64,
            "generation_count": 0,
            "deposit_count": 0,
            "entity_count": 0,
            "physicality_count": 0,
            "atom_count": 0,
            "ducet_position_count": 0,
            "ducet_contraction_count": 0,
            "normalization_composition_count": 0,
            "perfcache_generation_count": 0,
            "activation_event_count": 0,
        }
        self.assertEqual(unicode.validate_inspection(fresh, self.contract, identity), "fresh")
        recovered = copy.deepcopy(fresh)
        recovered.update(
            {
                "sequence": 1,
                "active_present": True,
                "activation_epoch_id": identity["activation_epoch_id"],
                "activation_epoch_fingerprint": identity["activation_epoch_fingerprint"],
                "generation_count": 1,
                "deposit_count": 1,
                "entity_count": 1114112,
                "physicality_count": 1114112,
                "atom_count": 1114112,
                "ducet_position_count": 1114112,
                "ducet_contraction_count": 964,
                "normalization_composition_count": 961,
                "perfcache_generation_count": 1,
                "activation_event_count": 1,
            }
        )
        committed = build_result(self.contract, identity)
        for field in (
            "root_receipt",
            "producer_receipt",
            "staged_stream_receipt",
            "sink_artifacts_fingerprint",
            "postgresql_artifact_fingerprint",
            "perfcache_artifact_set_fingerprint",
            "perfcache_manifest_fingerprint",
            "perfcache_encoded_manifest_fingerprint",
            "admission_receipt",
            "total_frame_count",
            "total_encoded_bytes",
            "batch_count",
            "plan_manifest_fingerprint",
            "plan_sequence_fingerprint",
            "plan_count",
        ):
            recovered[field] = committed[field]
        self.assertEqual(
            unicode.validate_inspection(recovered, self.contract, identity),
            "recover-post-commit",
        )
        replay = unicode.recover_build_result(recovered, self.contract, identity)
        self.assertTrue(replay["recovered_from_exact_committed_state"])
        self.assertEqual(replay["root_receipt"], committed["root_receipt"])
        damaged_receipt = copy.deepcopy(recovered)
        damaged_receipt["root_receipt"] = "not-a-receipt"
        with self.assertRaisesRegex(
            unicode.UnicodeActivationError, "committed Unicode receipt is invalid"
        ):
            unicode.recover_build_result(
                damaged_receipt, self.contract, identity
            )
        recovered["atom_count"] -= 1
        with self.assertRaisesRegex(unicode.UnicodeActivationError, "neither empty nor exact recovery"):
            unicode.validate_inspection(recovered, self.contract, identity)

    def test_activation_program_is_one_native_transaction_with_exact_assertions(self) -> None:
        identity = identities()
        sql = unicode.render_activation_sql(
            self.contract, identity, "/source", "/spool", "/perfcache/tier0", "/perfcache/reverse"
        )
        self.assertEqual(sql.count("unicode_root_build_and_activate("), 1)
        self.assertTrue(sql.startswith("BEGIN;"))
        self.assertTrue(sql.endswith("COMMIT;\n"))
        for required in (
            "2230150",
            "1114112",
            "762586574",
            self.contract["expected_result"]["tier0_artifact_digest"],
            self.contract["expected_result"]["plan_manifest_fingerprint"],
            identity["activation_epoch_id"],
        ):
            self.assertIn(required, sql)

    def test_reverse_readback_mutation_is_detected(self) -> None:
        identity = identities()
        value = readback(self.contract, identity)
        unicode.validate_readback(value, self.contract, identity)
        value["reverse_positions"] = [0, 66, 1114111]
        with self.assertRaisesRegex(unicode.UnicodeActivationError, "reverse inversion differs"):
            unicode.validate_readback(value, self.contract, identity)

    def test_success_receipt_requires_restart_and_cold_public_readback(self) -> None:
        identity = identities()
        expected = self.contract["expected_result"]
        with tempfile.TemporaryDirectory(prefix="laplace-unicode-activation-") as temporary:
            root = Path(temporary)
            package_id = "ab" * 32
            package_root = f"/opt/laplace/releases/{package_id}"
            executable = unicode.prefixed(
                root, f"{package_root}/bin/laplace_unicode_activation_identify"
            )
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"fixture identity provider\n")
            executable.chmod(0o755)
            package = {
                "package_id": package_id,
                "root": package_root,
                "files": [{"path": "bin/laplace_unicode_activation_identify", "kind": "file", "sha256": unicode.sha256_file(executable)}],
            }
            plan = {
                "package_root": package_root,
                "package_id": package_id,
                "package_manifest_sha256": "cd" * 32,
                "plan_sha256": "de" * 32,
                "instance": self.cluster["instance"],
                "commands": {"probe_readiness": ["fixture-ready"]},
            }
            cluster_receipt = {
                "activation_receipt_sha256": "ef" * 32,
                "system_identifier": "8672946663471807927",
            }
            source_root = root / "source"
            source_root.mkdir()
            unicode.prefixed(root, self.cluster["instance"]["temp_directory"]).mkdir(
                parents=True
            )
            unicode.prefixed(
                root, self.cluster["instance"]["perfcache_directory"]
            ).mkdir(parents=True)
            observations = 0

            def observe(*_arguments: Any) -> dict[str, Any]:
                nonlocal observations
                observations += 1
                return {
                    "system_identifier": "8672946663471807927",
                    "postmaster_pid": 1000 + observations,
                    "loaded_objects": [{"path": "postgres", "sha256": "01" * 32}],
                    "config_files": [{"path": "postgresql.conf", "sha256": "02" * 32}],
                    "observation_sha256": f"{observations:064x}",
                }

            fresh = {
                "sequence": 0,
                "active_present": False,
                "activation_epoch_id": "0" * 32,
                "activation_epoch_fingerprint": "0" * 64,
                "generation_count": 0,
                "deposit_count": 0,
                "entity_count": 0,
                "physicality_count": 0,
                "atom_count": 0,
                "ducet_position_count": 0,
                "ducet_contraction_count": 0,
                "normalization_composition_count": 0,
                "perfcache_generation_count": 0,
                "activation_event_count": 0,
            }
            labels: list[str] = []

            def sql_runner(*args: Any) -> tuple[dict[str, Any], dict[str, Any]]:
                label = args[3]
                labels.append(label)
                if label == "inspect-unicode-product-state":
                    result = fresh
                elif label == "activate-unicode-product-root":
                    result = build_result(self.contract, identity)
                else:
                    result = readback(self.contract, identity)
                return result, {
                    "label": label,
                    "argv": ["fixture"],
                    "exit_code": 0,
                    "stdout_sha256": "03" * 32,
                    "stderr_sha256": "04" * 32,
                    "stdin_sha256": "05" * 32,
                }

            def observed_artifact(path: Path, size: int) -> dict[str, Any]:
                digest = "06" if size == expected["tier0_artifact_bytes"] else "07"
                return {"path": str(path), "bytes": size, "sha256": digest * 32}

            with mock.patch.object(unicode, "validate_product_boundary"), mock.patch.object(
                unicode.clusterctl, "verify_loaded"
            ), mock.patch.object(unicode, "artifact_observation", side_effect=observed_artifact):
                receipt = unicode.execute_unicode_activation(
                    self.contract,
                    self.cluster,
                    self.source,
                    self.postgresql,
                    package,
                    plan,
                    cluster_receipt,
                    source_root,
                    root,
                    False,
                    source_verifier=lambda _contract, path: {
                        "unicode_version": "17.0.0",
                        "source_root": str(path),
                        "files": [],
                        "source_evidence_sha256": "08" * 32,
                    },
                    identity_runner=lambda *_args: (
                        identity,
                        {"label": "identify-unicode-activation", "argv": ["fixture"], "exit_code": 0, "stdout_sha256": "09" * 32, "stderr_sha256": "0a" * 32},
                    ),
                    sql_runner=sql_runner,
                    loaded_observer=observe,
                    command_runner=lambda label, command, timeout: {"label": label, "argv": list(command), "exit_code": 0, "stdout_sha256": "0b" * 32, "stderr_sha256": "0c" * 32},
                    readiness_runner=lambda label, command, timeout: {"label": label, "argv": list(command), "exit_code": 0, "stdout_sha256": "0d" * 32, "stderr_sha256": "0e" * 32},
                )
            self.assertEqual(receipt["phase"], "product-activated")
            self.assertTrue(receipt["restart_proven"])
            self.assertTrue(receipt["cold_public_readback_proven"])
            self.assertEqual(labels, ["inspect-unicode-product-state", "activate-unicode-product-root", "cold-public-unicode-readback"])
            evidence = (
                unicode.prefixed(root, self.cluster["instance"]["receipt_directory"])
                / "unicode"
                / identity["request_fingerprint"]
                / f"activation-{receipt['receipt_sha256']}.json"
            )
            self.assertTrue(evidence.is_file())

    def test_failed_product_activation_emits_typed_failure_receipt(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-unicode-failure-") as temporary:
            root = Path(temporary)
            package = {"package_id": "aa" * 32}
            plan = {"plan_sha256": "bb" * 32}
            cluster_receipt = {"activation_receipt_sha256": "cc" * 32}
            with mock.patch.object(
                unicode,
                "execute_unicode_activation",
                side_effect=unicode.UnicodeActivationError("deliberate fixture failure"),
            ):
                with self.assertRaisesRegex(
                    unicode.UnicodeActivationError, "deliberate fixture failure"
                ):
                    unicode.execute_unicode_activation_receipted(
                        self.contract,
                        self.cluster,
                        self.source,
                        self.postgresql,
                        package,
                        plan,
                        cluster_receipt,
                        Path("/source"),
                        root,
                        False,
                    )
            failure_directory = (
                unicode.prefixed(root, self.cluster["instance"]["receipt_directory"])
                / "unicode/failures"
            )
            receipts = list(failure_directory.glob("failure-*.json"))
            self.assertEqual(len(receipts), 1)
            failure = unicode.load_json(receipts[0])
            self.assertEqual(failure["schema"], unicode.FAILURE_SCHEMA)
            self.assertFalse(failure["success_receipt_issued"])
            self.assertEqual(
                failure["context"]["semantic_activation_state"],
                "must-be-reinspected-before-retry",
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
