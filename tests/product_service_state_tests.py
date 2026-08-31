#!/usr/bin/env python3
"""Acceptance tests for boot-enabled, identity-preserving product service state."""

from __future__ import annotations

import ast
import copy
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/product_service_state.py"
SPEC = importlib.util.spec_from_file_location(
    "laplace_product_service_state_tests", MODULE_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product service-state controller")
service_state = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = service_state
SPEC.loader.exec_module(service_state)

PACKAGE_ID = "51" * 32
SYSTEM_IDENTIFIER = "7418529630741852963"
BOOT_A = "11111111-1111-4111-8111-111111111111"
BOOT_B = "22222222-2222-4222-8222-222222222222"


class ClusterModuleStub:
    @staticmethod
    def validate_plan(plan: dict, _contract: dict) -> None:
        if plan.get("package_id") != PACKAGE_ID:
            raise RuntimeError("fixture plan package differs")

    @staticmethod
    def verify_loaded(_plan: dict, _contract: dict, loaded: dict) -> None:
        if loaded.get("system_identifier") != SYSTEM_IDENTIFIER:
            raise RuntimeError("fixture loaded identity differs")


class ProductServiceStateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.gateway = json.loads(
            (REPOSITORY / "contracts/product-activation-gateway.json").read_text(
                encoding="utf-8"
            )
        )
        self.cluster = json.loads(
            (REPOSITORY / "contracts/postgresql-cluster.json").read_text(
                encoding="utf-8"
            )
        )

    def physical(self, root: Path, logical: str | Path) -> Path:
        return service_state.prefixed(root, logical)

    def producer_receipt(self, document: dict, field: str) -> dict:
        result = copy.deepcopy(document)
        result[field] = service_state.producer_document_identity(result, field)
        return result

    def make_fixture(self, root: Path) -> None:
        product = self.gateway["product"]
        instance = self.cluster["instance"]
        release_root = self.cluster["package"]["release_root"]

        release = self.physical(root, f"{release_root}/{PACKAGE_ID}")
        release.mkdir(parents=True)
        active = self.physical(root, self.cluster["package"]["active_link"])
        active.parent.mkdir(parents=True, exist_ok=True)
        active.symlink_to(f"releases/{PACKAGE_ID}")

        plan_logical = (
            Path(product["cluster_activation_root"])
            / PACKAGE_ID
            / "cluster-plan.json"
        )
        plan = self.physical(root, plan_logical)
        plan.parent.mkdir(parents=True)
        plan.write_text(
            json.dumps(
                {
                    "package_id": PACKAGE_ID,
                    "plan_sha256": "52" * 32,
                },
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        cluster_result = self.producer_receipt(
            {
                "schema": "laplace.postgresql-activation-receipt/v1",
                "phase": "activated",
                "package_id": PACKAGE_ID,
                "plan_sha256": "52" * 32,
                "restart_proven": True,
                "active_target": f"releases/{PACKAGE_ID}",
                "cluster_plan_path": str(plan_logical),
                "system_identifier": SYSTEM_IDENTIFIER,
            },
            "activation_receipt_sha256",
        )
        cluster_result_path = self.physical(
            root,
            Path(product["cluster_activation_root"])
            / PACKAGE_ID
            / "activation-result.json",
        )
        cluster_result_path.write_text(
            json.dumps(cluster_result, sort_keys=True) + "\n", encoding="utf-8"
        )

        unicode = self.producer_receipt(
            {
                "schema": "laplace.unicode-product-activation-receipt/v1",
                "phase": "product-activated",
                "package_id": PACKAGE_ID,
                "cluster_activation_receipt_sha256": cluster_result[
                    "activation_receipt_sha256"
                ],
                "system_identifier": SYSTEM_IDENTIFIER,
                "activation_epoch_id": "53" * 16,
                "activation_epoch_fingerprint": "54" * 32,
                "restart_proven": True,
                "cold_public_readback_proven": True,
                "reverse_inversion_proven": True,
            },
            "receipt_sha256",
        )
        unicode_path = self.physical(root, product["unicode_result"])
        unicode_path.parent.mkdir(parents=True, exist_ok=True)
        unicode_path.write_text(
            json.dumps(unicode, sort_keys=True) + "\n", encoding="utf-8"
        )

        highway = self.producer_receipt(
            {
                "schema": "laplace.highway-product-activation-receipt/v1",
                "phase": "product-activated",
                "package_id": PACKAGE_ID,
                "cluster_activation_receipt_sha256": cluster_result[
                    "activation_receipt_sha256"
                ],
                "unicode_activation_receipt_sha256": unicode["receipt_sha256"],
                "system_identifier": SYSTEM_IDENTIFIER,
                "activation": {
                    "registry_epoch_id": "55" * 16,
                    "registry_epoch_fingerprint": "56" * 32,
                },
                "restart_proven": True,
                "cold_application_readback_proven": True,
            },
            "receipt_sha256",
        )
        highway_path = self.physical(root, product["highway_result"])
        highway_path.write_text(
            json.dumps(highway, sort_keys=True) + "\n", encoding="utf-8"
        )

        unit = self.physical(root, self.gateway["service_state"]["cluster_unit"])
        unit.parent.mkdir(parents=True, exist_ok=True)
        unit.write_text(
            "\n".join(
                (
                    "[Unit]",
                    f"RequiresMountsFor={instance['data_directory']} {instance['wal_directory']} {instance['temp_directory']}",
                    "[Service]",
                    f"User={instance['os_user']}",
                    f"Group={instance['os_group']}",
                    f"ExecStart={release_root}/{PACKAGE_ID}/pgsql-18/bin/postgres -D {instance['data_directory']} -c config_file={instance['config_directory']}/postgresql.conf",
                    "[Install]",
                    "WantedBy=multi-user.target",
                    "",
                )
            ),
            encoding="utf-8",
        )
        boot_id = self.physical(root, self.gateway["service_state"]["boot_id_path"])
        boot_id.parent.mkdir(parents=True, exist_ok=True)
        boot_id.write_text(BOOT_A + "\n", encoding="ascii")

    @staticmethod
    def loaded_observer(_plan: dict, _contract: dict, _root: Path) -> dict:
        return {
            "system_identifier": SYSTEM_IDENTIFIER,
            "observation_sha256": "57" * 32,
            "postmaster_pid": 1234,
            "loaded_objects": [
                {"path": "/opt/laplace/releases/x/pgsql-18/bin/postgres", "sha256": "58" * 32}
            ],
            "config_files": [
                {"path": "/etc/laplace/instances/refactor/postgresql.conf", "sha256": "59" * 32}
            ],
        }

    @staticmethod
    def runner(label: str, command: list[str], _timeout: int):
        if command[1] == "show":
            unit = command[-1]
            active = (
                "active"
                if unit == "laplace-refactor-postgresql.service"
                else "inactive"
            )
            sub = "running" if active == "active" else "dead"
            pid = "1234" if active == "active" else "0"
            output = (
                "LoadState=loaded\n"
                "UnitFileState=enabled\n"
                f"ActiveState={active}\n"
                f"SubState={sub}\n"
                f"MainPID={pid}\n"
            )
        else:
            output = ""
        receipt = {
            "label": label,
            "argv": command,
            "exit_code": 0,
            "stdout_sha256": "60" * 32,
            "stderr_sha256": "61" * 32,
        }
        return output, receipt

    def test_enablement_and_different_boot_readback_are_durable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-service-state-") as temporary:
            root = Path(temporary)
            self.make_fixture(root)
            enabled = service_state.enable_product_service(
                self.gateway,
                self.cluster,
                "62" * 32,
                root,
                ClusterModuleStub,
                self.loaded_observer,
                self.runner,
            )
            self.assertTrue(enabled["enabled_for_boot"])
            self.assertFalse(enabled["cold_boot_proven"])
            enablement_path = self.physical(
                root, self.gateway["service_state"]["enablement_receipt"]
            )
            self.assertTrue(enablement_path.is_file())
            persisted = service_state.load_json(enablement_path)
            service_state.validate_enablement_receipt(persisted)
            self.assertEqual(
                persisted["product_identity"]["package_id"], PACKAGE_ID
            )

            with self.assertRaisesRegex(
                service_state.ServiceStateError, "activation boot"
            ):
                service_state.cold_boot_readback(
                    self.gateway,
                    self.cluster,
                    "62" * 32,
                    root,
                    ClusterModuleStub,
                    self.loaded_observer,
                    self.runner,
                )

            boot_id = self.physical(
                root, self.gateway["service_state"]["boot_id_path"]
            )
            boot_id.write_text(BOOT_B + "\n", encoding="ascii")
            readback = service_state.cold_boot_readback(
                self.gateway,
                self.cluster,
                "62" * 32,
                root,
                ClusterModuleStub,
                self.loaded_observer,
                self.runner,
            )
            self.assertTrue(readback["cold_boot_proven"])
            self.assertNotEqual(
                readback["activation_boot_id"], readback["observed_boot_id"]
            )
            boot_receipt = self.physical(
                root, self.gateway["service_state"]["boot_readback_receipt"]
            )
            persisted_boot = service_state.load_json(boot_receipt)
            self.assertEqual(
                persisted_boot["receipt_sha256"],
                service_state.document_identity(persisted_boot, "receipt_sha256"),
            )

    def test_receipt_or_active_pointer_drift_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-service-drift-") as temporary:
            root = Path(temporary)
            self.make_fixture(root)
            active = self.physical(root, self.cluster["package"]["active_link"])
            active.unlink()
            active.symlink_to("releases/" + "63" * 32)
            with self.assertRaisesRegex(
                service_state.ServiceStateError, "active product pointer"
            ):
                service_state.inspect_product_state(
                    self.gateway,
                    self.cluster,
                    root,
                    ClusterModuleStub,
                    self.loaded_observer,
                )

    def test_gateway_bundle_contains_runtime_adapter_and_service_controller(self) -> None:
        contract_files = set(self.gateway["trusted_bundle"]["files"])
        installer_path = (
            REPOSITORY / "tools/delivery/install_product_activation_gateway.py"
        )
        tree = ast.parse(installer_path.read_text(encoding="utf-8"))
        source_map = None
        for node in tree.body:
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "SOURCE_MAP"
                for target in node.targets
            ):
                source_map = ast.literal_eval(node.value)
                break
        self.assertIsInstance(source_map, dict)
        self.assertEqual(contract_files, set(source_map))
        self.assertEqual(
            source_map["bin/laplace-product-activate"],
            "tools/delivery/product_activation_gateway.py",
        )
        self.assertEqual(
            source_map["controllers/product_activation_impl.py"],
            "tools/delivery/product_activation.py",
        )
        self.assertEqual(
            source_map["controllers/product_service_state.py"],
            "tools/delivery/product_service_state.py",
        )

    def test_installer_owns_path_trigger_and_boot_readback_units(self) -> None:
        installer = (
            REPOSITORY / "tools/delivery/install_product_activation_gateway.py"
        ).read_text(encoding="utf-8")
        state = self.gateway["service_state"]
        path_unit = installer.split('path_unit = f"""', 1)[1].split('"""', 1)[0]
        self.assertNotIn("PathExists={highway_result}", path_unit)
        self.assertIn("PathChanged={highway_result}", path_unit)
        self.assertIn('"enable",\n                "--now",', installer)
        self.assertIn("boot-readback", installer)
        self.assertEqual(
            state["cluster_service"], self.cluster["instance"]["service"]
        )
        self.assertEqual(
            state["cluster_unit"],
            f"/etc/systemd/system/{self.cluster['instance']['service']}",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
