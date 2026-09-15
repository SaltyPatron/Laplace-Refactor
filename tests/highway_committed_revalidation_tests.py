#!/usr/bin/env python3
"""Test explicit recovery selection, exact native inputs, and truthful receipts."""
from __future__ import annotations
import copy
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import tempfile
import threading
import unittest
from unittest import mock
import highway_product_activation_tests as fixture

h = fixture.highway
r = h.revalidation


class CommittedRevalidationTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixture.HighwayProductActivationTests()
        self.fixture.setUp()
        self.tmp = tempfile.TemporaryDirectory(prefix="highway-revalidation-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.contract = fixture.load("contracts/highway-committed-revalidation.json")
        self.inspection = self.fixture.inspection("target-fresh")
        selected = self.contract["selection"]
        self.inspection.update(highway_epoch_id=selected["registry_epoch_id"],
                               highway_epoch_fingerprint=selected["registry_epoch_fingerprint"])
        self.proof = {name: "a1" * 16 for name in r.HEX128_FIELDS}
        self.proof.update({name: "b2" * 32 for name in r.HEX256_FIELDS})
        self.proof.update({name: self.fixture.contract["expected_result"][name]
            for name in ("registry_version", "registry_fingerprint", "kind_count", "alias_count", "disposition_count", "status")})
        self.proof.update(registry_epoch_id=selected["registry_epoch_id"],
            registry_epoch_fingerprint=selected["registry_epoch_fingerprint"], activation_sequence=1,
            unicode_activation_epoch_id=self.fixture.unicode_receipt["activation_epoch_id"],
            unicode_activation_epoch_fingerprint=self.fixture.unicode_receipt["activation_epoch_fingerprint"],
            activation_performed=False, canonical_entity_count=25, canonical_physicality_count=25,
            transient_occurrence_count=25, historical_composition_receipt_present=False,
            historical_intermediate_receipts_verified=False)
        self.proof["stored_working_set_receipt"] = "a3" * 32
        self.proof["stored_producer_receipt"] = "a4" * 32
        self.proof["current_isa_receipt"] = "c3" * 32
        self.readback = self.fixture.readback({**self.proof,
            "activation_receipt": self.proof["stored_activation_receipt"],
            "activation_fingerprint": self.proof["stored_activation_fingerprint"]})
        self.calls = []
        self.input_hash_wrong = False
        self.after_inspection = copy.deepcopy(self.inspection)
        self.cold_proof_override = None

    def execute(self, *, opt_in=True, selected_contract=None, system_identifier=None):
        f = self.fixture
        system = system_identifier or self.contract["selection"]["system_identifier"]
        observations = iter([{"system_identifier": system, "postmaster_pid": pid,
            "loaded_objects": ["same-exact-native-library"], "config_files": ["same-config"],
            "observation_sha256": str(pid) * 64} for pid in (1, 2)])

        def sql(_plan, _cluster, text, label, os_user, role, timeout):
            self.calls.append((label, text, role))
            receipt = {"label": label, "exit_code": 0,
                       "stdin_sha256": h.unicodectl.sha256_bytes(text.encode())}
            if label == "inspect-highway-product-state":
                return self.inspection, receipt
            if label == "revalidate-committed-highway-registry":
                if self.input_hash_wrong:
                    receipt["stdin_sha256"] = "00" * 32
                return self.proof, receipt
            if label == "cold-revalidate-committed-highway-registry":
                return self.cold_proof_override or self.proof, receipt
            if label == "inspect-highway-after-committed-revalidation":
                return self.after_inspection, receipt
            if label == "cold-application-highway-revalidation-readback":
                return self.readback, receipt
            raise AssertionError("unexpected native operation: " + label)

        with mock.patch.object(h.unicodectl, "validate_product_boundary"), mock.patch.object(
            h, "load_unicode_identities", return_value=f.identities), mock.patch.object(h.clusterctl, "verify_loaded"):
            return h.execute_highway_activation(f.contract, f.cluster, f.unicode_contract,
                f.registry, f.previous_registry, {"package_id": f.unicode_receipt["package_id"]},
                {"plan_sha256": "91" * 32, "commands": {"probe_readiness": ["/bin/true"]}},
                {"activation_receipt_sha256": "92" * 32, "system_identifier": system},
                f.unicode_receipt, self.root, False, sql_runner=sql,
                loaded_observer=lambda *_: next(observations),
                command_runner=lambda label, *args: {"label": label, "exit_code": 0},
                readiness_runner=lambda label, *args: {"label": label, "exit_code": 0},
                committed_revalidation_contract=(selected_contract or self.contract) if opt_in else None)

    def test_missing_retention_creates_new_current_proof_without_historical_activation(self):
        receipt = self.execute()
        self.assertEqual(receipt["schema"], r.RECEIPT_SCHEMA)
        self.assertFalse(receipt["activation_performed"])
        self.assertFalse(receipt["historical_request_present"])
        self.assertNotIn("activation", receipt)
        self.assertEqual(receipt["revalidation"]["stored_isa_receipt"], "b2" * 32)
        self.assertEqual(receipt["revalidation"]["current_isa_receipt"], "c3" * 32)
        self.assertEqual(len(receipt["revalidation"]), 37)
        self.assertEqual(receipt["revalidation"]["stored_working_set_receipt"], "a3" * 32)
        self.assertEqual(receipt["revalidation"]["stored_producer_receipt"], "a4" * 32)
        self.assertFalse(receipt["revalidation"]["historical_composition_receipt_present"])
        self.assertFalse(receipt["revalidation"]["historical_intermediate_receipts_verified"])
        request = receipt["revalidation_request"]
        self.assertEqual(receipt["request_sha256"], h.unicodectl.sha256_bytes(h.unicodectl.canonical_bytes(request)))
        self.assertEqual(request["context_epochs"]["numeric_epoch"], self.inspection["highway_epoch_fingerprint"])
        self.assertEqual(request["context_epochs"]["perfcache_epoch"], self.fixture.unicode_receipt["activation_epoch_fingerprint"])
        self.assertEqual(request["historical_reference"]["status"], "external-log-reference-only")
        native = next(text for label, text, _ in self.calls if label == "revalidate-committed-highway-registry")
        self.assertIn("highway_registry_revalidate_committed", native)
        for name in ("stored_working_set_receipt", "stored_producer_receipt"):
            self.assertIn(f"'{name}',encode({name},'hex')", native)
        for name in ("historical_composition_receipt_present", "historical_intermediate_receipts_verified"):
            self.assertIn(f"'{name}',{name}", native)
        self.assertNotIn("admit_and_activate", native)
        self.assertTrue(native.endswith("COMMIT;\n"))
        self.assertIn(("cold-application-highway-revalidation-readback", self.fixture.cluster["instance"]["app_role"]),
                      [(label, role) for label, _, role in self.calls])
        old_directory = h.unicodectl.prefixed(self.root, self.fixture.cluster["instance"]["receipt_directory"]) / "highway"
        self.assertFalse(old_directory.exists())
        path = old_directory.parent / "highway-revalidation" / receipt["request_sha256"] / "receipt.json"
        self.assertEqual(json.loads(path.read_text()), receipt)

    def test_present_composition_summary_does_not_promote_historical_verification(self):
        self.proof["historical_composition_receipt_present"] = True
        receipt = self.execute()
        self.assertTrue(receipt["revalidation"]["historical_composition_receipt_present"])
        self.assertFalse(receipt["revalidation"]["historical_intermediate_receipts_verified"])
        self.assertEqual(receipt["revalidation"], receipt["cold_revalidation"])
        for name in ("working_set_receipt", "producer_receipt"):
            self.assertNotEqual(receipt["revalidation"]["stored_" + name], receipt["revalidation"]["current_" + name])

    def test_json_transport_matches_every_native_result_field_and_bytea_encoding(self):
        source = (fixture.REPOSITORY / "integrations/postgresql/extension/laplace--version.sql.in").read_text()
        declaration = source.split(".highway_registry_revalidation_result AS (", 1)[1].split("\n);", 1)[0]
        members = dict(line.strip().removesuffix(",").split(maxsplit=1)
                       for line in declaration.splitlines() if line.strip())
        sql = r.render_sql(h, self.fixture.contract, self.fixture.identities,
                           self.fixture.unicode_receipt, self.inspection)
        expressions = re.findall(r"'([a-z_]+)',(encode\([a-z_]+,'hex'\)|[a-z_]+)", sql)
        self.assertEqual(len(expressions), 37)
        self.assertEqual(set(members), {name for name, _ in expressions})
        for name, expression in expressions:
            self.assertEqual(expression, f"encode({name},'hex')" if members[name] == "bytea" else name)
        self.assertEqual(set(self.proof), set(members))

    def test_stored_references_and_historical_coverage_are_required_and_typed(self):
        changes = [("historical_composition_receipt_present", 0),
                   ("historical_composition_receipt_present", None),
                   ("historical_intermediate_receipts_verified", True),
                   ("historical_intermediate_receipts_verified", 0)]
        for name in ("stored_working_set_receipt", "stored_producer_receipt"):
            changes.extend((name, value) for value in (None, "00" * 32, "a3" * 16))
        for name, value in changes:
            with self.subTest(name=name, value=value):
                invalid = {**self.proof, name: value}
                with self.assertRaises(h.HighwayActivationError):
                    r.validate_result(h, invalid, self.fixture.contract, self.fixture.unicode_receipt, self.inspection)
        for name in ("stored_working_set_receipt", "stored_producer_receipt",
                     "historical_composition_receipt_present", "historical_intermediate_receipts_verified"):
            with self.subTest(missing=name):
                invalid = dict(self.proof)
                invalid.pop(name)
                with self.assertRaisesRegex(h.HighwayActivationError, "fields differ"):
                    r.validate_result(h, invalid, self.fixture.contract, self.fixture.unicode_receipt, self.inspection)

    def test_absent_opt_in_preserves_original_missing_retention_failure(self):
        with self.assertRaisesRegex(h.HighwayActivationError, "lacks retained admission"):
            self.execute(opt_in=False)
        self.assertEqual(len(self.calls), 1)

    def test_present_invalid_request_never_selects_revalidation(self):
        path = h.unicodectl.prefixed(self.root, self.fixture.cluster["instance"]["receipt_directory"]) / "highway" / ("ab" * 32)
        path.mkdir(parents=True)
        (path / "request.json").write_text("{}\n")
        with self.assertRaisesRegex(h.HighwayActivationError, "identity differs"):
            self.execute()
        self.assertEqual(len(self.calls), 1)

    def test_symlink_or_unknown_retention_cannot_select_revalidation(self):
        path = h.unicodectl.prefixed(self.root, self.fixture.cluster["instance"]["receipt_directory"]) / "highway"
        path.parent.mkdir(parents=True)
        target = self.root / "elsewhere"; target.mkdir()
        path.symlink_to(target, target_is_directory=True)
        with self.assertRaisesRegex(h.HighwayActivationError, "symlink"):
            self.execute()
        path.unlink(); path.mkdir(); (path / "unknown.json").write_text("{}")
        with self.assertRaisesRegex(h.HighwayActivationError, "unknown evidence"):
            self.execute()

    def test_wrong_contract_or_system_identity_fails_before_native_revalidation(self):
        wrong = copy.deepcopy(self.contract); wrong["selection"]["require_original_request_absent"] = False
        with self.assertRaisesRegex(h.HighwayActivationError, "contract differs"):
            self.execute(selected_contract=wrong)
        with self.assertRaisesRegex(h.HighwayActivationError, "outside the selected"):
            self.execute(system_identifier="12345")
        self.assertFalse(any("revalidate-committed" in label for label, _, _ in self.calls))

    def test_revalidation_evidence_symlink_fails_before_native_proof(self):
        family = h.unicodectl.prefixed(self.root, self.fixture.cluster["instance"]["receipt_directory"]) / "highway-revalidation"
        family.parent.mkdir(parents=True)
        outside = self.root / "outside"; outside.mkdir()
        family.symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(h.HighwayActivationError, "not physical"):
            self.execute()
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(list(outside.iterdir()), [])

    def test_new_evidence_publication_preserves_existing_bytes_and_rejects_redirects(self):
        path = self.root / "proof/request.json"
        r.write_immutable(h, path, {"proof": 1})
        original = path.read_bytes()
        inode = path.stat().st_ino
        r.write_immutable(h, path, {"proof": 1})
        self.assertEqual(path.stat().st_ino, inode)
        with self.assertRaisesRegex(h.HighwayActivationError, "immutable evidence differs"):
            r.write_immutable(h, path, {"proof": 2})
        self.assertEqual(path.read_bytes(), original)
        path.unlink(); target = self.root / "target.json"; target.write_bytes(original)
        path.symlink_to(target)
        with self.assertRaisesRegex(h.HighwayActivationError, "publication failed"):
            r.write_immutable(h, path, {"proof": 2})
        self.assertEqual(target.read_bytes(), original)
        self.assertFalse(list(path.parent.glob(".publish-*")))

    def test_concurrent_conflicting_receipts_publish_one_complete_document(self):
        path = self.root / "concurrent/request.json"
        barrier = threading.Barrier(2)
        native_link = os.link

        def synchronized_link(*args, **kwargs):
            barrier.wait(timeout=10)
            return native_link(*args, **kwargs)

        def publish(value):
            try:
                r.write_immutable(h, path, value)
                return "published", value
            except h.HighwayActivationError as error:
                return str(error), value

        values = [{"proof": "a" * 65536}, {"proof": "b" * 65536}]
        with mock.patch.object(os, "link", side_effect=synchronized_link), ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(publish, values))
        winners = [value for outcome, value in results if outcome == "published"]
        self.assertEqual(len(winners), 1)
        self.assertEqual(path.read_bytes(), h.unicodectl.canonical_bytes(winners[0]))
        self.assertEqual(sum("immutable evidence differs" in outcome for outcome, _ in results), 1)
        self.assertFalse(list(path.parent.glob(".publish-*")))

    def test_failure_receipt_cannot_follow_a_rejected_retention_symlink(self):
        f = self.fixture
        directory = h.unicodectl.prefixed(self.root, f.cluster["instance"]["receipt_directory"]) / "highway"
        directory.parent.mkdir(parents=True)
        outside = self.root / "outside-failure"; outside.mkdir()
        directory.symlink_to(outside, target_is_directory=True)
        args = (f.contract, f.cluster, f.unicode_contract, f.registry, f.previous_registry,
                {"package_id": "ab" * 32}, {"plan_sha256": "cd" * 32},
                {"activation_receipt_sha256": "ef" * 32}, f.unicode_receipt, self.root, False)
        with mock.patch.object(h, "execute_highway_activation", side_effect=h.HighwayActivationError("Highway retention boundary contains a symlink")):
            with self.assertRaisesRegex(h.unicodectl.UnicodeActivationError, "not physical"):
                h.execute_highway_activation_receipted(*args, committed_revalidation_contract=self.contract)
        self.assertEqual(list(outside.iterdir()), [])

    def test_native_wrong_input_or_activation_claim_is_rejected(self):
        self.input_hash_wrong = True
        with self.assertRaisesRegex(h.HighwayActivationError, "input identity"):
            self.execute()
        self.input_hash_wrong = False; self.proof["activation_performed"] = True
        with self.assertRaisesRegex(h.HighwayActivationError, "result differs"):
            self.execute()

    def test_native_missing_hash_or_different_current_epoch_is_rejected(self):
        self.proof["verification_receipt"] = "0" * 64
        with self.assertRaisesRegex(h.HighwayActivationError, "invalid verification_receipt"):
            self.execute()
        self.proof["verification_receipt"] = "b2" * 32
        self.proof["unicode_activation_epoch_fingerprint"] = "f1" * 32
        with self.assertRaisesRegex(h.HighwayActivationError, "result differs"):
            self.execute()

    def test_cold_readback_and_post_restart_state_must_match(self):
        self.readback["activation_receipt"] = "f2" * 32
        with self.assertRaisesRegex(h.HighwayActivationError, "readback differs"):
            self.execute()
        self.readback["activation_receipt"] = self.proof["stored_activation_receipt"]
        self.after_inspection["event_count"] += 1
        with self.assertRaisesRegex(h.HighwayActivationError, "state changed"):
            self.execute()

    def test_cold_native_projection_proof_must_equal_pre_restart_proof(self):
        self.cold_proof_override = {**self.proof, "verification_receipt": "d4" * 32}
        with self.assertRaisesRegex(h.HighwayActivationError, "cold native"):
            self.execute()
        self.cold_proof_override = {**self.proof, "status": False}
        with self.assertRaisesRegex(h.HighwayActivationError, "cold native"):
            self.execute()

    def test_cold_native_stored_references_and_coverage_must_match_exactly(self):
        for name, value in (("stored_working_set_receipt", "d4" * 32),
                            ("stored_producer_receipt", "d4" * 32),
                            ("historical_composition_receipt_present", True),
                            ("historical_intermediate_receipts_verified", True)):
            with self.subTest(name=name):
                self.cold_proof_override = {**self.proof, name: value}
                with self.assertRaisesRegex(h.HighwayActivationError, "cold native"):
                    self.execute()


if __name__ == "__main__": unittest.main(verbosity=2)
