#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/postgresql/highwayctl.py"
SPEC = importlib.util.spec_from_file_location("laplace_highwayctl_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load Highway activation controller")
highway = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = highway
SPEC.loader.exec_module(highway)


def load(name: str) -> dict:
    return json.loads((REPOSITORY / name).read_text(encoding="utf-8"))


class HighwayProductActivationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = load("contracts/highway-product-activation.json")
        self.cluster = load("contracts/postgresql-cluster.json")
        self.unicode_contract = load("contracts/unicode-product-activation.json")
        self.registry = load("contracts/highway.json")
        self.previous_registry = load("contracts/history/highway-v1.json")
        self.identities = {
            "schema": highway.unicodectl.IDENTITY_SCHEMA,
            **{
                name: ("10" * specification["bytes"])
                for name, specification in self.unicode_contract["identity_provider"]["fields"].items()
            },
        }
        self.identities["activation_epoch_id"] = "42" * 16
        self.identities["activation_epoch_fingerprint"] = "43" * 32
        self.unicode_receipt = {
            "schema": highway.unicodectl.RECEIPT_SCHEMA,
            "phase": "product-activated",
            "package_id": "60" * 32,
            "request_fingerprint": "61" * 32,
            "activation_epoch_id": self.identities["activation_epoch_id"],
            "activation_epoch_fingerprint": self.identities["activation_epoch_fingerprint"],
            "restart_proven": True,
            "cold_public_readback_proven": True,
            "reverse_inversion_proven": True,
        }
        self.unicode_receipt["receipt_sha256"] = highway.unicodectl.document_identity(
            self.unicode_receipt, "receipt_sha256"
        )

    def activation_result(self, inserted: int = 1, sequence: int | None = None) -> dict:
        expected = self.contract["expected_result"]
        if sequence is None:
            sequence = expected["fresh_activation_sequence"]
        result = {
            "registry_version": expected["registry_version"],
            "registry_fingerprint": expected["registry_fingerprint"],
            "registry_epoch_id": "71" * 16,
            "registry_epoch_fingerprint": "72" * 32,
            "root_entity_id": "73" * 16,
            "root_physicality_id": "74" * 32,
            "isa_receipt": "75" * 32,
            "unicode_root_receipt": "76" * 32,
            "unicode_activation_epoch_id": self.unicode_receipt["activation_epoch_id"],
            "unicode_activation_epoch_fingerprint": self.unicode_receipt["activation_epoch_fingerprint"],
            "working_set_receipt": "77" * 32,
            "presence_semantic_receipt": "78" * 32,
            "presence_execution_receipt": "79" * 32,
            "producer_receipt": "7a" * 32,
            "staged_stream_receipt": "7b" * 32,
            "sink_artifacts_fingerprint": "7c" * 32,
            "admission_receipt": "7d" * 32,
            "activation_receipt": "7e" * 32,
            "activation_fingerprint": "7f" * 32,
            "activation_sequence": sequence,
            "effect_disposition": expected["effect_disposition"],
            "kind_count": expected["kind_count"],
            "alias_count": expected["alias_count"],
            "disposition_count": expected["disposition_count"],
            "entity_inserted": inserted,
            "physicality_inserted": inserted,
            "trajectory_vertex_inserted": inserted,
            "occurrence_inserted": inserted,
            "plan_sequence_fingerprint": "80" * 32,
            "plan_count": 11,
            "status": expected["status"],
        }
        return result

    def inspection(self, mode: str) -> dict:
        authority = self.contract["authority"]
        expected = self.contract["expected_result"]
        base = {
            "unicode_present": True,
            "unicode_epoch_id": self.unicode_receipt["activation_epoch_id"],
            "unicode_epoch_fingerprint": self.unicode_receipt["activation_epoch_fingerprint"],
        }
        if mode == "fresh":
            return {
                **base,
                "highway_sequence": 0,
                "highway_present": False,
                "highway_epoch_id": "00" * 16,
                "highway_epoch_fingerprint": "00" * 32,
                "active_registry_version": None,
                "active_registry_fingerprint": None,
                "active_kind_count": None,
                "active_alias_count": None,
                "active_disposition_count": None,
                "active_kind_projection_count": 0,
                "active_alias_projection_count": 0,
                "active_disposition_projection_count": 0,
                "generation_count": 0,
                "event_count": 0,
            }
        if mode == "predecessor":
            version = authority["predecessor_registry_version"]
            fingerprint = authority["predecessor_registry_fingerprint"]
            kinds = authority["predecessor_kind_count"]
            aliases = authority["predecessor_alias_count"]
            dispositions = authority["predecessor_disposition_count"]
            sequence = self.contract["operation"]["successor_expected_old_sequence"]
            epoch = "51" * 32
        elif mode in {"target-fresh", "target-successor"}:
            version = expected["registry_version"]
            fingerprint = expected["registry_fingerprint"]
            kinds = expected["kind_count"]
            aliases = expected["alias_count"]
            dispositions = expected["disposition_count"]
            sequence = expected[
                "fresh_activation_sequence" if mode == "target-fresh"
                else "successor_activation_sequence"
            ]
            epoch = "52" * 32
        else:
            raise AssertionError(f"unsupported inspection fixture {mode}")
        return {
            **base,
            "highway_sequence": sequence,
            "highway_present": True,
            "highway_epoch_id": "50" * 16,
            "highway_epoch_fingerprint": epoch,
            "active_registry_version": version,
            "active_registry_fingerprint": fingerprint,
            "active_kind_count": kinds,
            "active_alias_count": aliases,
            "active_disposition_count": dispositions,
            "active_kind_projection_count": kinds,
            "active_alias_projection_count": aliases,
            "active_disposition_projection_count": dispositions,
            "generation_count": sequence,
            "event_count": sequence,
        }

    def readback(self, activation: dict) -> dict:
        expected = self.contract["expected_result"]
        return {
            "registry_version": activation["registry_version"],
            "registry_fingerprint": activation["registry_fingerprint"],
            "registry_epoch_id": activation["registry_epoch_id"],
            "registry_epoch_fingerprint": activation["registry_epoch_fingerprint"],
            "root_entity_id": activation["root_entity_id"],
            "root_physicality_id": activation["root_physicality_id"],
            "activation_sequence": activation["activation_sequence"],
            "activation_receipt": activation["activation_receipt"],
            "activation_fingerprint": activation["activation_fingerprint"],
            "isa_receipt": "81" * 32,
            "kind_ids": list(range(1, expected["kind_count"] + 1)),
            "kind_name_entity_ids": ["82" * 16] * expected["kind_count"],
            "alias_kind_ids": [],
            "alias_name_entity_ids": [],
            "disposition_ids": list(range(1, expected["disposition_count"] + 1)),
            "disposition_name_entity_ids": ["83" * 16] * expected["disposition_count"],
            "unicode_activation_epoch_id": self.unicode_receipt["activation_epoch_id"],
            "unicode_activation_epoch_fingerprint": self.unicode_receipt["activation_epoch_fingerprint"],
            "status": expected["status"],
        }

    def test_contract_and_sql_bind_the_real_shared_lifecycle(self) -> None:
        highway.validate_contract(
            self.contract, self.cluster, self.unicode_contract, self.registry,
            self.previous_registry,
        )
        state = highway.validate_inspection(
            self.inspection("fresh"), self.unicode_receipt, self.contract
        )
        sql = highway.render_activation_sql(
            self.contract, self.identities, self.unicode_receipt, state
        )
        for required in (
            "highway_registry_admit_and_activate",
            "canonical_entity",
            "physicality",
            "highway_registry_active_control",
            "highway_registry_kind_projection",
            "COMMIT",
        ):
            self.assertIn(required, sql)
        readback = highway.render_readback_sql(
            self.contract, self.identities, self.unicode_receipt,
            self.activation_result(),
        )
        self.assertIn("highway_registry_resolve_active", readback)
        self.assertIn(self.activation_result()["registry_epoch_fingerprint"], readback)

    def test_partial_state_and_zero_write_fresh_result_are_rejected(self) -> None:
        partial = {
            "unicode_present": True,
            "unicode_epoch_id": self.unicode_receipt["activation_epoch_id"],
            "unicode_epoch_fingerprint": self.unicode_receipt["activation_epoch_fingerprint"],
            "highway_sequence": 1,
            "highway_present": False,
            "highway_epoch_id": "00" * 16,
            "highway_epoch_fingerprint": "00" * 32,
            "generation_count": 1,
            "event_count": 0,
        }
        with self.assertRaisesRegex(highway.HighwayActivationError, "partial"):
            highway.validate_inspection(partial, self.unicode_receipt, self.contract)
        with self.assertRaisesRegex(highway.HighwayActivationError, "did not write"):
            highway.validate_activation_result(
                self.activation_result(inserted=0), self.contract,
                self.unicode_receipt,
                highway.validate_inspection(
                    self.inspection("fresh"), self.unicode_receipt, self.contract
                ),
            )

    def test_exact_v1_successor_and_v2_replay_are_distinct_machine_states(self) -> None:
        successor = highway.validate_inspection(
            self.inspection("predecessor"), self.unicode_receipt, self.contract
        )
        self.assertEqual(successor["mode"], "successor")
        self.assertEqual(
            successor["expected_activation_sequence"],
            self.contract["expected_result"]["successor_activation_sequence"],
        )
        sql = highway.render_activation_sql(
            self.contract, self.identities, self.unicode_receipt, successor
        )
        self.assertIn(successor["numeric_epoch"], sql)
        highway.validate_activation_result(
            self.activation_result(sequence=successor["expected_activation_sequence"]),
            self.contract, self.unicode_receipt, successor,
        )

        replay = highway.validate_inspection(
            self.inspection("target-successor"), self.unicode_receipt, self.contract
        )
        self.assertEqual(replay["mode"], "replay")
        highway.validate_activation_result(
            self.activation_result(inserted=0, sequence=replay["expected_activation_sequence"]),
            self.contract, self.unicode_receipt, replay,
        )
        with self.assertRaisesRegex(highway.HighwayActivationError, "duplicate"):
            highway.validate_activation_result(
                self.activation_result(inserted=1, sequence=replay["expected_activation_sequence"]),
                self.contract, self.unicode_receipt, replay,
            )

    def test_predecessor_history_rewrite_is_rejected(self) -> None:
        rewritten = copy.deepcopy(self.previous_registry)
        rewritten["kinds"][0]["name"] = "rewritten"
        with self.assertRaisesRegex(highway.HighwayActivationError, "predecessor.*bytes"):
            highway.validate_contract(
                self.contract, self.cluster, self.unicode_contract, self.registry,
                rewritten,
            )

    def test_complete_controller_restarts_and_reads_as_application_role(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-highway-product-") as temporary:
            root = Path(temporary)
            cluster = copy.deepcopy(self.cluster)
            package = {"package_id": self.unicode_receipt["package_id"]}
            plan = {
                "plan_sha256": "91" * 32,
                "instance": cluster["instance"],
                "commands": {"probe_readiness": ["/bin/true"]},
            }
            cluster_receipt = {
                "activation_receipt_sha256": "92" * 32,
                "system_identifier": "123456789",
            }
            activation = self.activation_result()
            readback = self.readback(activation)
            labels: list[tuple[str, str, str]] = []

            def fake_sql(_plan, _contract, _sql, label, os_user, db_role, _timeout):
                labels.append((label, os_user, db_role))
                if label == "inspect-highway-product-state":
                    return (self.inspection("fresh"), {"label": label})
                if label == "admit-and-activate-highway-product-registry":
                    return activation, {"label": label}
                return readback, {"label": label}

            observations = iter([
                {
                    "system_identifier": "123456789", "postmaster_pid": 10,
                    "loaded_objects": ["a"], "config_files": ["b"],
                    "observation_sha256": "93" * 32,
                },
                {
                    "system_identifier": "123456789", "postmaster_pid": 11,
                    "loaded_objects": ["a"], "config_files": ["b"],
                    "observation_sha256": "94" * 32,
                },
            ])
            with mock.patch.object(highway.unicodectl, "validate_product_boundary"), mock.patch.object(
                highway, "load_unicode_identities", return_value=self.identities
            ), mock.patch.object(highway.clusterctl, "verify_loaded"):
                receipt = highway.execute_highway_activation(
                    self.contract, cluster, self.unicode_contract, self.registry,
                    self.previous_registry, package, plan, cluster_receipt,
                    self.unicode_receipt, root, False,
                    sql_runner=fake_sql,
                    loaded_observer=lambda *_args: next(observations),
                    command_runner=lambda label, _command, _timeout: {"label": label},
                    readiness_runner=lambda label, _command, _timeout: {"label": label},
                )
            self.assertTrue(receipt["restart_proven"])
            self.assertTrue(receipt["cold_application_readback_proven"])
            self.assertEqual(labels[-1][1], cluster["security"]["app_os_user"])
            self.assertEqual(labels[-1][2], cluster["instance"]["app_role"])
            receipt_path = root.joinpath(
                *Path(cluster["instance"]["receipt_directory"]).parts[1:],
                "highway", receipt["request_sha256"], "receipt.json",
            )
            self.assertTrue(receipt_path.is_file())


if __name__ == "__main__":
    unittest.main(verbosity=2)
