#!/usr/bin/env python3

from __future__ import annotations

import base64
import copy
import datetime as dt
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/product_activation.py"
SPEC = importlib.util.spec_from_file_location("laplace_product_activation_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product activation module")
activation = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = activation
SPEC.loader.exec_module(activation)

INSTALLER_PATH = REPOSITORY / "tools/delivery/install_product_activation_gateway.py"
INSTALLER_SPEC = importlib.util.spec_from_file_location(
    "laplace_product_activation_installer_tests", INSTALLER_PATH
)
if INSTALLER_SPEC is None or INSTALLER_SPEC.loader is None:
    raise RuntimeError("cannot load product activation installer")
installer = importlib.util.module_from_spec(INSTALLER_SPEC)
sys.modules[INSTALLER_SPEC.name] = installer
INSTALLER_SPEC.loader.exec_module(installer)


NOW = dt.datetime(2026, 8, 27, 16, 0, 0, tzinfo=dt.timezone.utc)
KEY = bytes(range(32))
PACKAGE_ID = "60" * 32
BUILD_ID = "e6" * 32
COMMIT = "0e" * 20


class ProductActivationGatewayTests(unittest.TestCase):
    def setUp(self) -> None:
        self.base_contract = activation.load_json(
            REPOSITORY / "contracts/product-activation-gateway.json"
        )

    def fixture(self, root: Path) -> tuple[dict, dict, dict, Path, Path]:
        contract = copy.deepcopy(self.base_contract)
        product = contract["product"]
        gateway = contract["gateway"]
        request = contract["request"]
        product["package_manifest_root"] = str(root / "build")
        product["package_release_root"] = str(root / "releases")
        product["plan_receipt_root"] = str(root / "plans")
        product["cluster_activation_root"] = str(root / "cluster-activation")
        product["unicode_source_root"] = str(root / "unicode-source")
        product["unicode_result"] = str(root / "unicode-result.json")
        gateway["receipt_root"] = str(root / "deployment-receipts")
        gateway["release_root"] = str(root / "gateway-releases")
        gateway["active_link"] = str(root / "gateway-current")
        gateway["executable"] = str(root / "gateway-current/bin/laplace-product-activate")
        gateway["sudoers_path"] = str(root / "sudoers/laplace-product-activation")
        request["secret_path"] = str(root / "etc/product-activation.key")

        manifest = root / "build" / BUILD_ID / "package-manifest.json"
        manifest.parent.mkdir(parents=True)
        manifest_document = {
            "schema": "laplace.package-manifest/v1",
            "package_id": PACKAGE_ID,
            "root": f"{product['package_release_root']}/{PACKAGE_ID}",
        }
        manifest.write_bytes(activation.canonical_bytes(manifest_document))
        resource = root / "plans" / PACKAGE_ID / "resource-observation.json"
        resource.parent.mkdir(parents=True)
        resource.write_bytes(
            activation.canonical_bytes(
                {"schema": "laplace.execution-resource-observation/v1", "observation_sha256": "31" * 32}
            )
        )
        (root / "unicode-source").mkdir()
        continuation = {
            "active_work": {
                "implementation_progress": {
                    "successor_product_package": {
                        "activation_eligible": True,
                        "immutable_release_installed": True,
                        "package_id": PACKAGE_ID,
                        "package_manifest": str(manifest),
                        "package_manifest_sha256": activation.sha256_file(manifest),
                        "installation": {"installed_package_verified": True},
                        "resource_observation": {
                            "receipt": str(resource),
                            "receipt_sha256": activation.sha256_file(resource),
                            "observation_sha256": "31" * 32,
                        },
                    }
                }
            }
        }
        environment = {
            "GITHUB_REPOSITORY": "SaltyPatron/Laplace-Refactor",
            "GITHUB_REF": "refs/heads/main",
            "GITHUB_EVENT_NAME": "workflow_dispatch",
            "GITHUB_SHA": COMMIT,
            "GITHUB_RUN_ID": "12345",
            "GITHUB_RUN_ATTEMPT": "1",
            "GITHUB_ACTOR": "SaltyPatron",
        }
        return contract, continuation, environment, manifest, resource

    def test_contract_and_exact_request_round_trip(self) -> None:
        activation.validate_contract(self.base_contract)
        with tempfile.TemporaryDirectory(prefix="laplace-activation-request-") as temporary:
            contract, continuation, environment, _manifest, _resource = self.fixture(Path(temporary))
            request = activation.build_request(contract, continuation, environment, KEY, NOW)
            payload = activation.validate_request(contract, request, KEY, NOW)
            self.assertEqual(payload["package"]["id"], PACKAGE_ID)
            self.assertEqual(request["request_id"], activation.sha256_bytes(activation.canonical_bytes(payload)))

    def test_wrong_route_stale_request_and_changed_hmac_are_distinct_failures(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-activation-negative-") as temporary:
            contract, continuation, environment, _manifest, _resource = self.fixture(Path(temporary))
            wrong = dict(environment)
            wrong["GITHUB_EVENT_NAME"] = "pull_request"
            with self.assertRaisesRegex(activation.ActivationGatewayError, "declared deployment event"):
                activation.build_request(contract, continuation, wrong, KEY, NOW)
            request = activation.build_request(contract, continuation, environment, KEY, NOW)
            extra = copy.deepcopy(request)
            extra["payload"]["undeclared"] = True
            extra["request_id"] = activation.sha256_bytes(
                activation.canonical_bytes(extra["payload"])
            )
            extra["hmac_sha256"] = activation.request_mac(
                contract, extra["payload"], KEY
            )
            with self.assertRaisesRegex(
                activation.ActivationGatewayError, "payload fields differ"
            ):
                activation.validate_request(contract, extra, KEY, NOW)
            changed = copy.deepcopy(request)
            changed["payload"]["package"]["id"] = "61" * 32
            changed["request_id"] = activation.sha256_bytes(
                activation.canonical_bytes(changed["payload"])
            )
            with self.assertRaisesRegex(activation.ActivationGatewayError, "authentication failed"):
                activation.validate_request(contract, changed, KEY, NOW)
            with self.assertRaisesRegex(activation.ActivationGatewayError, "time window"):
                activation.validate_request(contract, request, KEY, NOW + dt.timedelta(hours=2))

    def test_path_escape_and_changed_input_bytes_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-activation-path-") as temporary:
            root = Path(temporary)
            contract, continuation, environment, manifest, resource = self.fixture(root)
            request = activation.build_request(contract, continuation, environment, KEY, NOW)
            resource.write_text("changed\n", encoding="utf-8")
            with self.assertRaisesRegex(activation.ActivationGatewayError, "resource observation bytes"):
                activation.validate_request(contract, request, KEY, NOW)
            continuation["active_work"]["implementation_progress"]["successor_product_package"]["package_manifest"] = str(root / "outside.json")
            with self.assertRaisesRegex(activation.ActivationGatewayError, "escapes"):
                activation.build_request(contract, continuation, environment, KEY, NOW)
            self.assertTrue(manifest.is_file())

    def test_duplicate_json_key_is_rejected(self) -> None:
        with self.assertRaisesRegex(activation.ActivationGatewayError, "duplicate JSON key"):
            activation.parse_json(b'{"schema":"a","schema":"b"}')

    def test_fixture_gateway_install_is_immutable_and_replayable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-gateway-install-") as temporary:
            root = Path(temporary)
            key = root / "key"
            key.write_text(base64.b64encode(KEY).decode("ascii") + "\n", encoding="ascii")
            first = installer.install_gateway(
                REPOSITORY,
                REPOSITORY / "contracts/product-activation-gateway.json",
                key,
                root,
                False,
            )
            second = installer.install_gateway(
                REPOSITORY,
                REPOSITORY / "contracts/product-activation-gateway.json",
                key,
                root,
                False,
            )
            self.assertTrue(first["installed_new"])
            self.assertFalse(second["installed_new"])
            self.assertEqual(first["bundle_id"], second["bundle_id"])
            active = root / "opt/laplace/deployment/current"
            self.assertEqual((active.resolve()).stat().st_mode & 0o777, 0o755)
            bundle, manifest = activation.verify_installed_bundle(active / "bin/laplace-product-activate")
            self.assertEqual(bundle.name, manifest["bundle_id"])
            sudoers = (root / "etc/sudoers.d/laplace-product-activation").read_text(
                encoding="utf-8"
            )
            self.assertEqual(sudoers.count("NOPASSWD:"), 2)
            self.assertNotIn("*", sudoers)
            self.assertNotIn(" ALL\n", sudoers)
            other = root / "other-key"
            other.write_text(base64.b64encode(b"x" * 32).decode("ascii") + "\n", encoding="ascii")
            with self.assertRaisesRegex(installer.InstallError, "key differs"):
                installer.install_gateway(
                    REPOSITORY,
                    REPOSITORY / "contracts/product-activation-gateway.json",
                    other,
                    root,
                    False,
                )

    def test_workflow_keeps_pull_requests_out_of_the_activation_job(self) -> None:
        workflow = (REPOSITORY / ".github/workflows/product-activation.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("if: github.event_name == 'workflow_dispatch'", workflow)
        self.assertIn("environment: product", workflow)
        self.assertIn("test \"$GITHUB_REF\" = refs/heads/main", workflow)
        self.assertIn("execute-request < \"$LAPLACE_ACTIVATION_REQUEST\"", workflow)

    def test_execute_chains_cluster_unicode_and_exact_replay(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-gateway-execute-") as temporary:
            root = Path(temporary)
            contract, continuation, environment, _manifest, _resource = self.fixture(root)
            request = activation.build_request(contract, continuation, environment, KEY, NOW)
            bundle = root / "bundle"
            (bundle / "controllers").mkdir(parents=True)
            (bundle / "contracts").mkdir()
            evidence = root / "cluster-activation" / PACKAGE_ID
            cluster_result_path = evidence / "activation-result.json"
            unicode_result_path = Path(contract["product"]["unicode_result"])
            calls: list[str] = []

            def fake_run(label: str, command: list[str], _timeout: int) -> dict:
                calls.append(label)
                if label == "activate-product-cluster":
                    evidence.mkdir(parents=True, exist_ok=True)
                    plan = evidence / "cluster-plan-fixture.json"
                    plan.write_text("{}\n", encoding="utf-8")
                    result = {
                        "schema": contract["operation"]["cluster_success_schema"],
                        "phase": "activated",
                        "package_id": PACKAGE_ID,
                        "restart_proven": True,
                        "active_target": f"releases/{PACKAGE_ID}",
                        "cluster_plan_path": str(plan),
                    }
                    result["activation_receipt_sha256"] = activation.document_identity(result, "activation_receipt_sha256")
                    cluster_result_path.write_bytes(activation.canonical_bytes(result))
                else:
                    result = {
                        "schema": contract["operation"]["unicode_success_schema"],
                        "phase": "product-activated",
                        "package_id": PACKAGE_ID,
                        "restart_proven": True,
                        "cold_public_readback_proven": True,
                        "reverse_inversion_proven": True,
                    }
                    result["receipt_sha256"] = activation.document_identity(result, "receipt_sha256")
                    unicode_result_path.write_bytes(activation.canonical_bytes(result))
                return {"label": label, "argv": command, "exit_code": 0, "stdout_sha256": "00", "stderr_sha256": "00"}

            with mock.patch.object(activation.os, "geteuid", return_value=0), mock.patch.object(
                activation, "run_fixed", side_effect=fake_run
            ):
                result = activation.execute_request(contract, request, KEY, bundle, NOW)
                replay = activation.execute_request(contract, request, KEY, bundle, NOW)
            self.assertEqual(
                calls,
                [
                    "activate-product-cluster",
                    "activate-product-unicode",
                    "activate-product-unicode",
                ],
            )
            self.assertEqual(result, replay)
            self.assertEqual(result["phase"], "product-and-unicode-activated")

    def test_incomplete_success_receipts_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-gateway-receipt-") as temporary:
            root = Path(temporary)
            contract, _continuation, _environment, _manifest, _resource = self.fixture(root)
            plan = root / "cluster-activation" / PACKAGE_ID / "cluster-plan.json"
            plan.parent.mkdir(parents=True, exist_ok=True)
            plan.write_text("{}\n", encoding="utf-8")
            cluster = {
                "schema": contract["operation"]["cluster_success_schema"],
                "phase": "activated",
                "package_id": PACKAGE_ID,
                "restart_proven": False,
                "active_target": f"releases/{PACKAGE_ID}",
                "cluster_plan_path": str(plan),
            }
            cluster["activation_receipt_sha256"] = activation.document_identity(cluster, "activation_receipt_sha256")
            with self.assertRaisesRegex(activation.ActivationGatewayError, "not exact and complete"):
                activation.validate_cluster_success(contract, cluster, PACKAGE_ID)


if __name__ == "__main__":
    unittest.main(verbosity=2)
