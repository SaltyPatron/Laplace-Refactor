#!/usr/bin/env python3
from __future__ import annotations
import importlib.util
from pathlib import Path
import sys
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/product/prove-postgresql-product.py"
SPEC = importlib.util.spec_from_file_location("laplace_postgresql_product_proof_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load PostgreSQL product proof module")
proof = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = proof
SPEC.loader.exec_module(proof)

COMMIT = "1a" * 20
TREE = "2b" * 20
PACKAGE = "3c" * 32

def package_proof() -> dict:
    return {
        "schema": proof.PACKAGE_PROOF_SCHEMA,
        "phase": "composed-installed-retained",
        "repository_commit": COMMIT,
        "repository_tree": TREE,
        "package_id": PACKAGE,
        "binding_receipt_blake3": "4d" * 32,
    }

def live_probe() -> dict:
    value = {
        "schema": "laplace.product-activation-gateway-probe/v2",
        "bundle_id": "5e" * 32,
        "package_id": PACKAGE,
        "active_target": f"releases/{PACKAGE}",
        "postgresql_version": "18.6",
        "cluster_activation_receipt_sha256": "6f" * 32,
        "cluster_plan_sha256": "70" * 32,
        "live_loaded_observation_sha256": "81" * 32,
        "probe_sql_sha256": "92" * 32,
        "system_identifier": "1234567890123456789",
        "service": "laplace-refactor-postgresql.service",
        "service_state": "active",
        "loaded_objects": [{"path": "/opt/laplace/current/lib/laplace_pg.so", "sha256": "a3" * 32}],
        "config_files": [{"path": "/etc/laplace/refactor/postgresql.conf", "sha256": "b4" * 32}],
    }
    value["probe_sha256"] = proof.probe_identity(value)
    return value

class PostgreSQLProductProofTests(unittest.TestCase):
    def test_exact_source_package_and_live_probe_match(self) -> None:
        proof.validate_probe(live_probe(), package_proof(), COMMIT, TREE, "laplace.product-activation-gateway-probe/v2")

    def test_stale_active_package_is_rejected(self) -> None:
        probe = live_probe()
        probe["package_id"] = "cc" * 32
        probe["active_target"] = f"releases/{probe['package_id']}"
        probe["probe_sha256"] = proof.probe_identity(probe)
        with self.assertRaisesRegex(proof.PostgreSQLProductProofError, "current package/product state"):
            proof.validate_probe(probe, package_proof(), COMMIT, TREE, "laplace.product-activation-gateway-probe/v2")

    def test_stale_source_package_proof_is_rejected(self) -> None:
        package = package_proof()
        package["repository_commit"] = "dd" * 20
        with self.assertRaisesRegex(proof.PostgreSQLProductProofError, "different source"):
            proof.validate_probe(live_probe(), package, COMMIT, TREE, "laplace.product-activation-gateway-probe/v2")

    def test_mutated_probe_self_identity_is_rejected(self) -> None:
        probe = live_probe()
        probe["service_state"] = "inactive"
        with self.assertRaisesRegex(proof.PostgreSQLProductProofError, "current package/product state"):
            proof.validate_probe(probe, package_proof(), COMMIT, TREE, "laplace.product-activation-gateway-probe/v2")

if __name__ == "__main__":
    unittest.main()
