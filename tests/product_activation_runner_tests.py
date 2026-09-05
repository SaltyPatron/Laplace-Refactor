#!/usr/bin/env python3
"""Regression tests for the persistent laplace-runner activation boundary."""

from __future__ import annotations

import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/product-activation.yml"
SETUP = ROOT / "scripts/setup-host.sh"
SERVICE = ROOT / "packaging/systemd/laplace-refactor-postgresql.service"
CLUSTER = ROOT / "contracts/postgresql-cluster.json"
RUNNER = ROOT / "tools/delivery/product_activation_runner.py"
CLUSTERCTL = ROOT / "tools/postgresql/clusterctl.py"
RESOURCECTL = ROOT / "tools/postgresql/resourcectl.py"
UNICODECTL = ROOT / "tools/postgresql/unicodectl.py"
HIGHWAYCTL = ROOT / "tools/postgresql/highwayctl.py"


class ProductActivationRunnerTests(unittest.TestCase):
    def test_workflow_selects_runner_provider_not_root_or_systemd(self) -> None:
        source = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("product_activation_runner.py", source)
        self.assertIn("tools/postgresql/resourcectl.py observe-resources", source)
        self.assertNotIn("tools/postgresql/clusterctl.py observe-resources", source)
        self.assertIn('execution_owner == "laplace-runner"', source)
        self.assertIn("root_product_executor == false", source)
        self.assertIn("pg_ctl", source)
        self.assertIn("lifecycle_provider", source)
        for forbidden in (
            "laplace-product-activate",
            "LAPLACE_ACTIVATION_HMAC_KEY_B64",
            "compile_gateway_upgrade.py",
            "product_activation.py create-request",
            "gateway-upgrade-request",
            "root gateway",
            "sudo -n",
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
