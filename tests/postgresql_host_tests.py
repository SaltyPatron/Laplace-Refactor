#!/usr/bin/env python3
"""Executable and deliberate-defect tests for PostgreSQL host topology control."""

from __future__ import annotations

import copy
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/postgresql/hostctl.py"
SPEC = importlib.util.spec_from_file_location("laplace_postgresql_host_tests", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
hostctl = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(hostctl)


class PostgreSQLHostTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-postgresql-host-")
        self.root = Path(self.temporary.name)
        self.contract = hostctl.load_json(REPOSITORY / "contracts/postgresql-host.json")
        self.contract["postgresql"]["binary_candidates"] = ["/postgres/*/bin"]
        self.contract["discovery"]["process_root"] = "/proc"
        self.contract["discovery"]["mountinfo"] = "/proc/self/mountinfo"
        self.contract["discovery"]["systemd_unit_roots"] = ["/etc/systemd/system"]
        for provider in self.contract["discovery"]["routing_providers"].values():
            provider["binary_candidates"] = []
        self.contract["discovery"]["routing_providers"]["haproxy"]["binary_candidates"] = [
            "/usr/sbin/haproxy"
        ]
        self.make_installation("18.3")
        self.make_installation("18.6")
        (self.root / "proc/42").mkdir(parents=True)
        (self.root / "proc/42/cmdline").write_bytes(
            b"/postgres/18.3/bin/postgres\0-D\0/cluster/legacy\0"
        )
        (self.root / "proc/42/exe").symlink_to("/postgres/18.3/bin/postgres")
        (self.root / "cluster/legacy").mkdir(parents=True)
        (self.root / "cluster/legacy/postmaster.pid").write_text(
            "42\n/cluster/legacy\n1\n5432\n/run/postgresql\n\n\nready\n",
            encoding="utf-8",
        )
        (self.root / "proc/self").mkdir(parents=True)
        (self.root / "proc/self/mountinfo").write_text(
            "36 25 253:1 / /opt/laplace/pgdata rw,relatime - ext4 /dev/mapper/vg-data rw\n",
            encoding="utf-8",
        )
        units = self.root / "etc/systemd/system"
        units.mkdir(parents=True)
        (units / "laplace-postgresql.service").write_text("[Service]\n", encoding="utf-8")
        haproxy = self.root / "usr/sbin/haproxy"
        haproxy.parent.mkdir(parents=True)
        haproxy.write_text("fixture\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def make_installation(self, version: str) -> None:
        binary = self.root / f"postgres/{version}/bin"
        binary.mkdir(parents=True)
        for name, output in (
            ("pg_config", f"PostgreSQL {version}"),
            ("postgres", f"postgres (PostgreSQL) {version}"),
        ):
            script = binary / name
            script.write_text(f"#!/bin/sh\nprintf '%s\\n' '{output}'\n", encoding="utf-8")
            script.chmod(0o755)

    @staticmethod
    def request() -> dict[str, object]:
        return hostctl.load_json(REPOSITORY / "contracts/instances/refactor.json")

    @staticmethod
    def package(version: str = "18.6") -> dict[str, object]:
        return {
            "schema": hostctl.PACKAGE_SCHEMA,
            "package_id": "a" * 64,
            "postgresql": {"version": version},
            "activation_eligible": True,
        }

    def test_inventory_finds_versions_cluster_service_mount_and_router(self) -> None:
        observed = hostctl.inventory(self.contract, self.root)
        hostctl.validate_inventory(observed)
        self.assertEqual(
            [item["version"] for item in observed["postgresql_installations"]],
            ["18.3", "18.6"],
        )
        self.assertEqual(observed["postgresql_processes"][0]["data_directory"], "/cluster/legacy")
        self.assertEqual(observed["postgresql_processes"][0]["postmaster"]["port"], 5432)
        self.assertEqual(observed["mounts"][0]["mount_point"], "/opt/laplace/pgdata")
        self.assertIn(
            "laplace-postgresql.service",
            [item["name"] for item in observed["systemd_services"]],
        )
        haproxy = next(item for item in observed["routing_providers"] if item["provider"] == "haproxy")
        self.assertTrue(haproxy["available"])

    def test_new_cluster_selection_binds_exact_package_and_route(self) -> None:
        observed = hostctl.inventory(self.contract, self.root)
        selected = hostctl.select_host(
            self.contract, observed, self.request(), self.package()
        )
        self.assertEqual(selected["activation_disposition"], "eligible-for-new-cluster-plan")
        self.assertEqual(selected["postgresql"]["version"], "18.6")
        self.assertFalse(selected["postgresql"]["patch_binary_compatibility_assumed"])
        self.assertEqual(selected["routing"]["mode"], "direct-unix")

    def test_existing_port_collision_is_detected(self) -> None:
        observed = hostctl.inventory(self.contract, self.root)
        request = self.request()
        request["instance"]["port"] = 5432
        request["routing"]["endpoint"]["port"] = 5432
        with self.assertRaisesRegex(hostctl.HostControlError, "collides"):
            hostctl.select_host(self.contract, observed, request, self.package())

    def test_patch_version_substitution_is_detected(self) -> None:
        observed = hostctl.inventory(self.contract, self.root)
        with self.assertRaisesRegex(hostctl.HostControlError, "versions differ"):
            hostctl.select_host(
                self.contract, observed, self.request(), self.package("18.3")
            )

    def test_external_router_requires_provider_and_authority_receipt(self) -> None:
        observed = hostctl.inventory(self.contract, self.root)
        request = self.request()
        request["routing"] = {
            "mode": "external-haproxy",
            "endpoint": {"host": "127.0.0.1", "port": 6432},
        }
        with self.assertRaisesRegex(hostctl.HostControlError, "authority receipt"):
            hostctl.select_host(self.contract, observed, request, self.package())
        request["routing"]["authority_receipt_sha256"] = "b" * 64
        selected = hostctl.select_host(self.contract, observed, request, self.package())
        self.assertEqual(selected["routing_provider_observation"]["provider"], "haproxy")

    def test_contract_mutant_cannot_assume_patch_compatibility(self) -> None:
        mutant = copy.deepcopy(self.contract)
        mutant["postgresql"]["patch_binary_compatibility_assumed"] = True
        with self.assertRaisesRegex(hostctl.HostControlError, "cannot be assumed"):
            hostctl.validate_contract(mutant)

    def test_inventory_digest_mutation_is_detected(self) -> None:
        observed = hostctl.inventory(self.contract, self.root)
        observed["mounts"][0]["source"] = "/dev/wrong"
        with self.assertRaisesRegex(hostctl.HostControlError, "digest differs"):
            hostctl.validate_inventory(observed)


if __name__ == "__main__":
    unittest.main(verbosity=2)
