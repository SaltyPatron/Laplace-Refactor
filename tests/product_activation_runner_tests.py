#!/usr/bin/env python3
"""Regression tests for the persistent laplace-runner activation boundary."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/product-activation.yml"
SETUP = ROOT / "scripts/setup-host.sh"
SERVICE = ROOT / "packaging/systemd/laplace-refactor-postgresql.service"
CLUSTER = ROOT / "contracts/postgresql-cluster.json"
RUNNER = ROOT / "tools/delivery/product_activation_runner.py"
RECONCILER = ROOT / "tools/delivery/product_activation_reconcile.py"
CLUSTERCTL = ROOT / "tools/postgresql/clusterctl.py"
RESOURCECTL = ROOT / "tools/postgresql/resourcectl.py"
UNICODECTL = ROOT / "tools/postgresql/unicodectl_core.py"
HIGHWAYCTL = ROOT / "tools/postgresql/highwayctl.py"


class ProductActivationRunnerTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("jq"), "jq is required to execute delivery result predicates")
    def test_workflow_and_setup_accept_only_distinct_complete_revalidation_variant(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")
        workflow_match = re.search(r"--arg repository_commit \"\$GITHUB_SHA\" '\n(.*?)\n          ' \"\$result\"", workflow, re.S)
        setup = (ROOT / "scripts/setup-product.sh").read_text(encoding="utf-8")
        setup_match = re.search(r"jq -e --arg package \"\$package\" '\n(.*?)\n' \"\$work/activation.json\"", setup, re.S)
        self.assertIsNotNone(workflow_match)
        self.assertIsNotNone(setup_match)
        base = {"schema": "laplace.product-activation-result/v1",
                "phase": "product-unicode-activated-and-highway-revalidated",
                "execution_owner": "laplace-runner", "root_product_executor": False,
                "package_id": "42" * 32, "repository_commit": "43" * 20,
                "package_installation_receipt_sha256": "44" * 32,
                "cluster_activation_receipt_sha256": "45" * 32,
                "unicode_activation_receipt_sha256": "46" * 32,
                "highway_revalidation_receipt_sha256": "47" * 32,
                "highway_activation_performed": False,
                "highway_historical_request_present": False,
                "highway_stored_working_set_receipt": "51" * 32,
                "highway_stored_producer_receipt": "52" * 32,
                "highway_historical_composition_receipt_present": False,
                "highway_historical_intermediate_receipts_verified": False,
                "highway_activation_sequence": 1,
                "highway_retained_expected_epoch_count": 1,
                "highway_recovered_expected_epoch_count": 0,
                "result_sha256": "48" * 32}
        normal = copy.deepcopy(base)
        normal["phase"] = "product-unicode-and-highway-activated"
        normal["highway_activation_receipt_sha256"] = normal.pop("highway_revalidation_receipt_sha256")
        normal.pop("highway_activation_performed")
        normal.pop("highway_historical_request_present")
        for field in ("highway_stored_working_set_receipt", "highway_stored_producer_receipt",
                      "highway_historical_composition_receipt_present", "highway_historical_intermediate_receipts_verified",
                      "highway_activation_sequence", "highway_retained_expected_epoch_count", "highway_recovered_expected_epoch_count"):
            normal.pop(field)
        present_composition = {**base, "highway_historical_composition_receipt_present": True}
        mutants = []
        for field, value in (("highway_activation_performed", True),
                ("highway_historical_request_present", True),
                ("highway_historical_composition_receipt_present", 0),
                ("highway_historical_intermediate_receipts_verified", True),
                ("highway_historical_intermediate_receipts_verified", 0),
                ("highway_activation_sequence", True),
                ("highway_activation_sequence", 0),
                ("highway_activation_sequence", 1025),
                ("highway_retained_expected_epoch_count", True),
                ("highway_recovered_expected_epoch_count", "0"),
                ("highway_retained_expected_epoch_count", -1),
                ("highway_recovered_expected_epoch_count", 0.5),
                ("highway_recovered_expected_epoch_count", 1),
                ("highway_stored_working_set_receipt", "00" * 32),
                ("highway_stored_producer_receipt", "00" * 32),
                ("highway_stored_working_set_receipt", "51" * 16),
                ("highway_stored_producer_receipt", "52" * 16),
                ("highway_activation_receipt_sha256", "49" * 32),
                ("highway_revalidation_receipt_sha256", ""),
                ("phase", "product-unicode-and-highway-activated"),
                ("root_product_executor", True), ("package_id", "50" * 32)):
            invalid = copy.deepcopy(base)
            invalid[field] = value
            mutants.append(invalid)
        for field in ("highway_activation_performed", "highway_stored_working_set_receipt",
                      "highway_stored_producer_receipt", "highway_historical_composition_receipt_present",
                      "highway_historical_intermediate_receipts_verified",
                      "highway_activation_sequence", "highway_retained_expected_epoch_count", "highway_recovered_expected_epoch_count"):
            missing = copy.deepcopy(base)
            missing.pop(field)
            mutants.append(missing)
        for source, predicate in (("workflow", workflow_match[1]), ("setup", setup_match[1])):
            mixed = {**base, "highway_activation_sequence": 5,
                     "highway_retained_expected_epoch_count": 2, "highway_recovered_expected_epoch_count": 3}
            recovered = {**base, "highway_retained_expected_epoch_count": 0, "highway_recovered_expected_epoch_count": 1}
            for expected, document in [(True, base), (True, present_composition), (True, normal),
                                       (True, recovered), (True, mixed)] + [(False, mutant) for mutant in mutants]:
                with self.subTest(source=source, phase=document["phase"], expected=expected, document=document):
                    completed = subprocess.run(["jq", "-e", "--arg", "package_id", base["package_id"],
                        "--arg", "package", base["package_id"], "--arg", "repository_commit", base["repository_commit"], predicate],
                        input=json.dumps(document), text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
                    self.assertEqual(completed.returncode == 0, expected, completed.stderr)

    def test_workflow_selects_runner_provider_not_root_or_systemd(self) -> None:
        source = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("product_activation_reconcile.py", source)
        reconciler = RECONCILER.read_text(encoding="utf-8")
        self.assertIn("product_activation_runner.py", reconciler)
        self.assertIn("product_cluster_upgrade.py", reconciler)
        self.assertIn("tools/postgresql/resourcectl.py observe-resources", source)
        self.assertNotIn("tools/postgresql/clusterctl.py observe-resources", source)
        self.assertIn('execution_owner == "laplace-runner"', source)
        self.assertIn("root_product_executor == false", source)
        self.assertIn("pg_ctl", source)
        self.assertIn("sudo -n /usr/bin/systemctl restart laplace-refactor-cognition.service", source)
        self.assertIn("lifecycle_provider", source)
        for forbidden in (
            "laplace-product-activate",
            "LAPLACE_ACTIVATION_HMAC_KEY_B64",
            "compile_gateway_upgrade.py",
            "product_activation.py create-request",
            "gateway-upgrade-request",
            "root gateway",
            "systemctl is-active",
            "systemctl is-enabled",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)

    def test_bootstrap_service_is_optional_integration_not_product_executor(self) -> None:
        source = SETUP.read_text(encoding="utf-8")
        self.assertIn('SERVICE_SOURCE="$REPOSITORY/packaging/systemd/$SERVICE"', source)
        self.assertIn('"$SYSTEMCTL_BIN" daemon-reload', source)
        self.assertIn('"$SYSTEMCTL_BIN" enable "$SERVICE"', source)
        self.assertIn('"started_by_bootstrap": false', source)
        for forbidden in (
            "build-package.py",
            "clusterctl.py activate-product",
            "unicodectl.py",
            "highwayctl.py",
            "laplace-product-activate",
            "execute-request",
            "initdb",
            "LAPLACE_ACTIVATION_HMAC_KEY_B64",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)

    def test_static_service_is_optional_boot_integration(self) -> None:
        service = SERVICE.read_text(encoding="utf-8")
        self.assertIn("User=laplace-runner", service)
        self.assertIn("Group=laplace-runner", service)
        self.assertIn(
            "ExecStart=/opt/laplace/runtime/refactor/pgsql-18/bin/postgres",
            service,
        )
        self.assertNotIn("ExecStart=/opt/laplace/current/", service)
        self.assertNotIn("/opt/laplace/releases/", service)
        self.assertNotIn("AllowedCPUs=", service)
        self.assertNotIn("MemoryHigh=", service)
        self.assertNotIn("MemoryMax=", service)

    def test_cluster_contract_has_one_recurring_os_owner_and_runner_socket(self) -> None:
        contract = json.loads(CLUSTER.read_text(encoding="utf-8"))
        instance = contract["instance"]
        security = contract["security"]
        self.assertEqual(instance["os_user"], "laplace-runner")
        self.assertEqual(instance["os_group"], "laplace-runner")
        self.assertEqual(security["admin_os_user"], "laplace-runner")
        self.assertEqual(security["app_os_user"], "laplace-runner")
        self.assertEqual(instance["admin_role"], "laplace_admin")
        self.assertEqual(instance["app_role"], "laplace_app")
        self.assertTrue(instance["socket_directory"].startswith("/opt/laplace/runtime/"))

    def test_runner_provider_declares_pg_ctl_and_no_root_product_executor(self) -> None:
        provider = RUNNER.read_text(encoding="utf-8")
        controller = CLUSTERCTL.read_text(encoding="utf-8")
        self.assertIn('"execution_owner": RUNNER_USER', provider)
        self.assertIn('"root_product_executor": False', provider)
        self.assertIn('"postgresql_lifecycle_provider": clusterctl.LIFECYCLE_PROVIDER', provider)
        self.assertIn('LIFECYCLE_PROVIDER = "pg_ctl"', controller)
        self.assertIn("RUNNER_USER = \"laplace-runner\"", controller)
        self.assertIn("RUNTIME_LINK = \"/opt/laplace/runtime/refactor\"", controller)
        self.assertIn("postmaster.pid", controller)
        self.assertNotIn("laplace-product-activate", provider)
        self.assertNotIn("execute-request", provider)
        self.assertNotIn("runuser", provider)
        self.assertNotIn("sudo", controller)
        self.assertNotIn("systemctl", controller)
        self.assertNotIn('["/usr/sbin/runuser"', controller)
        self.assertNotIn('["runuser"', controller)

    def test_runner_loaded_object_probe_ignores_deleted_pseudo_maps_fail_closed(self) -> None:
        controller = CLUSTERCTL.read_text(encoding="utf-8")
        self.assertIn('if executable.endswith(" (deleted)"):', controller)
        self.assertIn(
            'raise _core.ClusterError(f"process {pid} executable was deleted after start")',
            controller,
        )
        self.assertIn('if path.endswith(" (deleted)"):', controller)
        self.assertIn("required package objects remain fail-closed", controller)
        self.assertIn("missing = sorted(expected_paths - process_paths)", controller)
        self.assertIn("live PostgreSQL backend omits required package objects", controller)

    def test_resource_observer_uses_real_runner_contract_without_legacy_cli(self) -> None:
        source = RESOURCECTL.read_text(encoding="utf-8")
        self.assertIn("clusterctl.validate_contract(contract)", source)
        self.assertIn("clusterctl.verify_package(", source)
        self.assertIn("clusterctl.finalize_native_resource_observation(", source)
        self.assertIn("clusterctl.validate_resource_observation(result, contract)", source)
        self.assertIn("clusterctl.validate_resource_package_binding(result, package)", source)
        self.assertIn("RESOURCE_OBSERVER_PATH", source)
        self.assertNotIn("_validation_contract", source)
        self.assertNotIn("cluster_core", source)
        self.assertNotIn("runuser", source)
        self.assertNotIn("sudo", source)
        self.assertNotIn("systemctl", source)

    def test_unicode_and_highway_restart_requests_are_intercepted_by_provider(self) -> None:
        provider = RUNNER.read_text(encoding="utf-8")
        unicode = UNICODECTL.read_text(encoding="utf-8")
        highway = HIGHWAYCTL.read_text(encoding="utf-8")
        self.assertIn("runner_command", provider)
        self.assertIn("unsupported systemd operation", provider)
        self.assertIn('Path(values[0]).name == "systemctl"', provider)
        self.assertIn("restart-after-unicode-activation", unicode)
        self.assertIn("restart-after-highway-activation", highway)
        self.assertIn('plan["commands"]["stop_candidate"]', provider)
        self.assertIn('plan["commands"]["start_candidate"]', provider)


class IndexedCognitionSuccessorReconciliationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        spec = importlib.util.spec_from_file_location(
            "laplace_indexed_successor_reconcile_tests", RECONCILER)
        if spec is None or spec.loader is None:
            raise RuntimeError("cannot load actual product reconciliation owner")
        cls.owner = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = cls.owner
        spec.loader.exec_module(cls.owner)

    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="laplace-indexed-successor-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        relative = "pgsql-18/share/extension/laplace--1.0.0--1.0.1.sql"
        self.migration = self.root / relative
        self.migration.parent.mkdir(parents=True)
        self.migration.write_bytes((ROOT / "integrations/postgresql/extension/observation_cognition_persisted.sql.in").read_bytes())
        self.package = {"package_id": "42" * 32, "files": [{
            "path": relative, "kind": "file",
            "sha256": hashlib.sha256(self.migration.read_bytes()).hexdigest()}]}
        self.plan = {"postgresql_major": 18, "package_root": str(self.root)}
        self.contract = {"instance": {"admin_role": "laplace_admin"}}

    def observations(self, version: str) -> list:
        return [({"version": version, "owner": "laplace_admin"}, {}), ({
            "schema": "laplace.indexed-cognition-upgrade/v1", "version": version,
            "owner": "laplace_admin", "native_bindings": 2, "ready_indexes": 2},
            {"fixture": "acknowledged server verification"})]

    def test_supported_successors_verify_inherited_native_bindings_without_downgrade(self) -> None:
        for version in ("1.0.2", "1.0.3", "1.0.4"):
            with self.subTest(version=version), mock.patch.object(
                self.owner.runner, "runner_sql", side_effect=self.observations(version)
            ) as sql, mock.patch.object(self.owner, "fresh_reconcile_indexed_cognition") as predecessor:
                receipt = self.owner.reconcile_indexed_cognition_after_generation_upgrade(
                    self.plan, self.contract, self.package)
                predecessor.assert_not_called()
                self.assertEqual(sql.call_count, 2)
                self.assertEqual(receipt["version"], version)
                self.assertEqual(receipt["script_sha256"], self.package["files"][0]["sha256"])
                self.assertEqual(receipt["package_id"], self.package["package_id"])
                self.assertEqual(receipt["receipt_sha256"],
                    self.owner.runner.document_identity(receipt, "receipt_sha256"))

    def test_new_successor_refuses_changed_package_migration_before_database_reconciliation(self) -> None:
        self.migration.write_bytes(self.migration.read_bytes() + b"\n-- changed package bytes\n")
        with mock.patch.object(self.owner.runner, "runner_sql",
                side_effect=self.observations("1.0.4")) as sql:
            with self.assertRaisesRegex(self.owner.runner.RunnerActivationError, "migration bytes differ"):
                self.owner.reconcile_indexed_cognition_after_generation_upgrade(
                    self.plan, self.contract, self.package)
            self.assertEqual(sql.call_count, 1)

    def test_new_successor_refuses_version_or_native_proof_drift(self) -> None:
        for field, changed in (("version", "1.0.5"), ("owner", "other_role"),
                ("native_bindings", 1), ("ready_indexes", 1)):
            observed = self.observations("1.0.4")
            observed[1][0][field] = changed
            with self.subTest(field=field), mock.patch.object(
                self.owner.runner, "runner_sql", side_effect=observed):
                with self.assertRaisesRegex(self.owner.runner.RunnerActivationError, "result differs"):
                    self.owner.reconcile_indexed_cognition_after_generation_upgrade(
                        self.plan, self.contract, self.package)

    def test_unknown_version_uses_existing_version_admission_owner(self) -> None:
        with mock.patch.object(self.owner.runner, "runner_sql",
                return_value=({"version": "1.0.5"}, {})) as sql, mock.patch.object(
                self.owner, "fresh_reconcile_indexed_cognition", return_value={"deferred": True}) as predecessor:
            result = self.owner.reconcile_indexed_cognition_after_generation_upgrade(
                self.plan, self.contract, self.package)
            predecessor.assert_called_once_with(self.plan, self.contract, self.package)
            self.assertEqual(sql.call_count, 1)
            self.assertEqual(result, {"deferred": True})


if __name__ == "__main__":
    unittest.main(verbosity=2)
