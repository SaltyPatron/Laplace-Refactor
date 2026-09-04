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


class ProductActivationRunnerTests(unittest.TestCase):
    def test_workflow_selects_runner_provider_not_root_gateway(self) -> None:
        source = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("product_activation_runner.py", source)
        self.assertIn('execution_owner == "laplace-runner"', source)
        self.assertIn("root_product_executor == false", source)
        for forbidden in (
            "laplace-product-activate",
            "LAPLACE_ACTIVATION_HMAC_KEY_B64",
            "compile_gateway_upgrade.py",
            "product_activation.py create-request",
            "gateway-upgrade-request",
            "root gateway",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)

    def test_bootstrap_installs_only_static_service_and_narrow_sudo(self) -> None:
        source = SETUP.read_text(encoding="utf-8")
        self.assertIn('SERVICE_SOURCE="$REPOSITORY/packaging/systemd/$SERVICE"', source)
        self.assertIn('"$SYSTEMCTL_BIN" daemon-reload', source)
        self.assertIn('"$SYSTEMCTL_BIN" enable "$SERVICE"', source)
        self.assertIn('"started_by_bootstrap": false', source)
        for action in ("start", "stop", "restart"):
            self.assertIn(
                f'$RUNNER_USER ALL=(root) NOPASSWD: $SYSTEMCTL_BIN {action} $SERVICE',
                source,
            )
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

    def test_static_service_uses_runner_owned_runtime_link(self) -> None:
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

    def test_cluster_contract_has_one_recurring_os_owner(self) -> None:
        contract = json.loads(CLUSTER.read_text(encoding="utf-8"))
        instance = contract["instance"]
        security = contract["security"]
        self.assertEqual(instance["os_user"], "laplace-runner")
        self.assertEqual(instance["os_group"], "laplace-runner")
        self.assertEqual(security["admin_os_user"], "laplace-runner")
        self.assertEqual(security["app_os_user"], "laplace-runner")
        self.assertEqual(instance["admin_role"], "laplace_admin")
        self.assertEqual(instance["app_role"], "laplace_app")

    def test_runner_provider_declares_no_root_product_executor(self) -> None:
        provider = RUNNER.read_text(encoding="utf-8")
        controller = CLUSTERCTL.read_text(encoding="utf-8")
        self.assertIn('"execution_owner": RUNNER_USER', provider)
        self.assertIn('"root_product_executor": False', provider)
        self.assertIn("RUNNER_USER = \"laplace-runner\"", controller)
        self.assertIn("RUNTIME_LINK = \"/opt/laplace/runtime/refactor\"", controller)
        self.assertIn("system product lifecycle requires", controller)
        self.assertNotIn("laplace-product-activate", provider)
        self.assertNotIn("execute-request", provider)
        self.assertNotIn("runuser", provider)
        self.assertNotIn("runuser", controller)

    def test_only_system_service_actions_cross_sudo(self) -> None:
        controller = CLUSTERCTL.read_text(encoding="utf-8")
        self.assertIn('if action not in {"start", "stop", "restart"}', controller)
        self.assertIn('return [sudo, "-n", systemctl, action, service]', controller)
        for forbidden in (
            "sudo -n /opt/laplace",
            "sudo -n python",
            "sudo -n psql",
            "sudo -n initdb",
        ):
            self.assertNotIn(forbidden, controller)


if __name__ == "__main__":
    unittest.main(verbosity=2)
