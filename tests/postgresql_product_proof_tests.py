#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/postgresql/prove-product.py"
SPEC = importlib.util.spec_from_file_location(
    "laplace_postgresql_product_proof_tests", MODULE_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load PostgreSQL-product proof module")
proof = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = proof
SPEC.loader.exec_module(proof)


EXPECTED_LOADED = [
    "pgsql-18/bin/postgres",
    "pgsql-18/lib/laplace_pg.so",
    "pgsql-18/lib/pg_stat_statements.so",
    "lib/liblaplace_engine.so.2.0.0",
]


def cluster_contract() -> dict:
    return {
        "package": {
            "postgresql_version": "18.6",
            "required_loaded_objects": list(EXPECTED_LOADED),
        },
        "instance": {
            "os_user": "laplace-runner",
            "admin_role": "laplace_admin",
            "app_role": "laplace_app",
            "port": 55433,
            "socket_directory": "/run/laplace-refactor-postgresql",
            "database": "laplace_refactor",
        },
        "security": {
            "listen_addresses": "",
            "socket_mode": "0770",
            "admin_map": "laplace_refactor_admin",
            "app_map": "laplace_refactor_app",
            "allowed_preload_libraries": ["pg_stat_statements"],
            "forbid_environment": ["LD_LIBRARY_PATH", "LD_PRELOAD"],
        },
    }


def manifest() -> dict:
    return {
        "postgresql": {"version": "18.6"},
        "loaded_objects": list(EXPECTED_LOADED),
        "loader_environment": {"LD_LIBRARY_PATH": None, "LD_PRELOAD": None},
        "files": [
            {
                "path": path,
                "kind": "file",
                "sha256": chr(ord("a") + index) * 64,
            }
            for index, path in enumerate(EXPECTED_LOADED)
        ],
    }


def metadata() -> dict:
    return {
        "server_version": "18.6",
        "port": "55433",
        "socket_directory": "/run/laplace-refactor-postgresql",
        "database": "laplace_refactor",
        "system_identifier": "7451179018073440222",
        "laplace_extension_version": "1.0.0",
        "pg_stat_statements_extension_version": "1.11",
        "admin_role_count": 1,
        "app_role_count": 1,
        "schema_count": 1,
        "active_epoch_count": 0,
        "data_checksums": "on",
        "listen_addresses": "",
        "shared_preload_libraries": "pg_stat_statements",
    }


class PostgreSQLProductProofTests(unittest.TestCase):
    def test_exact_postgresql_selection_is_admitted(self) -> None:
        selected = proof.validate_postgresql_selection(manifest(), cluster_contract())
        self.assertEqual(selected, EXPECTED_LOADED)

    def test_wrong_postgresql_minor_is_rejected(self) -> None:
        for target in ("contract", "manifest"):
            with self.subTest(target=target):
                candidate_contract = cluster_contract()
                candidate_manifest = manifest()
                if target == "contract":
                    candidate_contract["package"]["postgresql_version"] = "18.5"
                else:
                    candidate_manifest["postgresql"]["version"] = "18.5"
                with self.assertRaisesRegex(
                    proof.PostgreSQLProductProofError, "PostgreSQL 18.6"
                ):
                    proof.validate_postgresql_selection(
                        candidate_manifest, candidate_contract
                    )

    def test_missing_or_reordered_loaded_object_is_rejected(self) -> None:
        for loaded in (
            EXPECTED_LOADED[:-1],
            list(reversed(EXPECTED_LOADED)),
        ):
            with self.subTest(loaded=loaded):
                candidate = manifest()
                candidate["loaded_objects"] = loaded
                with self.assertRaisesRegex(
                    proof.PostgreSQLProductProofError, "loaded-object set"
                ):
                    proof.validate_postgresql_selection(candidate, cluster_contract())

    def test_forbidden_loader_environment_is_rejected(self) -> None:
        for variable in ("LD_LIBRARY_PATH", "LD_PRELOAD"):
            with self.subTest(variable=variable):
                candidate = manifest()
                candidate["loader_environment"][variable] = "/tmp/not-product"
                with self.assertRaisesRegex(
                    proof.PostgreSQLProductProofError, "ambient loader"
                ):
                    proof.validate_postgresql_selection(candidate, cluster_contract())

    def test_source_identity_rejects_stale_commit_or_tree(self) -> None:
        commit = "a" * 40
        tree = "b" * 40
        package = {
            "schema": proof.PACKAGE_PROOF_SCHEMA,
            "phase": "composed-installed-retained",
            "repository_commit": commit,
            "repository_tree": tree,
        }
        proof.validate_source_identity(package, commit, tree)
        for expected_commit, expected_tree in (
            ("c" * 40, tree),
            (commit, "d" * 40),
        ):
            with self.subTest(commit=expected_commit, tree=expected_tree):
                with self.assertRaisesRegex(
                    proof.PostgreSQLProductProofError, "source identity"
                ):
                    proof.validate_source_identity(
                        package, expected_commit, expected_tree
                    )

    def test_hba_is_peer_mapped_and_has_no_trust_escape(self) -> None:
        content = proof.hba_content(cluster_contract(), "laplace-runner")
        self.assertIn(
            "local all laplace_admin peer map=laplace_refactor_admin", content
        )
        self.assertIn(
            "local all laplace_app peer map=laplace_refactor_app", content
        )
        self.assertIn("local all all reject", content)
        self.assertNotIn("trust", content.lower())
        with self.assertRaisesRegex(
            proof.PostgreSQLProductProofError, "service identity"
        ):
            proof.hba_content(cluster_contract(), "wrong-runner")

    def test_runtime_configuration_matches_product_boundary(self) -> None:
        content = proof.postgresql_conf_content(cluster_contract())
        self.assertIn("listen_addresses = ''", content)
        self.assertIn("port = 55433", content)
        self.assertIn(
            "unix_socket_directories = '/run/laplace-refactor-postgresql'", content
        )
        self.assertIn("unix_socket_permissions = '0770'", content)
        self.assertIn(
            "shared_preload_libraries = 'pg_stat_statements'", content
        )
        self.assertIn("dynamic_library_path = '$libdir'", content)

    def test_sandbox_rebinds_exact_release_and_clears_environment(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "bwrap"
            executable.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            executable.chmod(0o755)
            release = root / "installed-release"
            release.mkdir()
            paths = {
                "laplace": root / "laplace",
                "run": root / "run",
                "tmp": root / "tmp",
                "release": release,
            }
            for name in ("laplace", "run", "tmp"):
                paths[name].mkdir()
            prefix = proof.sandbox_prefix(paths, executable)
            self.assertIn("--clearenv", prefix)
            release_index = prefix.index(str(release))
            self.assertEqual(prefix[release_index - 1], "--ro-bind")
            self.assertEqual(prefix[release_index + 1], "/opt/laplace/current")
            self.assertNotIn("LD_LIBRARY_PATH", prefix)
            self.assertNotIn("LD_PRELOAD", prefix)

    def test_runtime_metadata_round_trip_is_exact(self) -> None:
        row = (
            "18.6|55433|/run/laplace-refactor-postgresql|laplace_refactor|"
            "7451179018073440222|1.0.0|1.11|1|1|1|0|on|;pg_stat_statements"
        )
        parsed = proof.parse_runtime_metadata(row)
        self.assertEqual(parsed, metadata())
        proof.validate_runtime_metadata(parsed, cluster_contract())
        sql = proof.runtime_metadata_sql(cluster_contract())
        self.assertIn("rolname='laplace_admin'", sql)
        self.assertIn("rolname='laplace_app'", sql)

    def test_runtime_metadata_mutations_fail_closed(self) -> None:
        mutations = {
            "server_version": "18.5",
            "port": "5432",
            "socket_directory": "/tmp",
            "database": "postgres",
            "admin_role_count": 0,
            "app_role_count": 0,
            "schema_count": 0,
            "active_epoch_count": 1,
            "data_checksums": "off",
            "listen_addresses": "localhost",
            "shared_preload_libraries": "",
            "system_identifier": "0",
        }
        for field, value in mutations.items():
            with self.subTest(field=field):
                candidate = metadata()
                candidate[field] = value
                with self.assertRaises(proof.PostgreSQLProductProofError):
                    proof.validate_runtime_metadata(candidate, cluster_contract())

    def test_loaded_object_identity_requires_hash_inode_device_and_process(self) -> None:
        expected = [
            {
                "path": path,
                "sha256": chr(ord("a") + index) * 64,
                "device_major": 8,
                "device_minor": index,
                "inode": 100 + index,
                "processes": ["backend"],
            }
            for index, path in enumerate(EXPECTED_LOADED)
        ]
        observed = copy.deepcopy(expected)
        proof.validate_loaded_observations(expected, observed)
        for field, value in (
            ("sha256", "f" * 64),
            ("inode", 999),
            ("device_minor", 99),
            ("processes", []),
        ):
            with self.subTest(field=field):
                candidate = copy.deepcopy(observed)
                candidate[0][field] = value
                with self.assertRaises(proof.PostgreSQLProductProofError):
                    proof.validate_loaded_observations(expected, candidate)
        with self.assertRaises(proof.PostgreSQLProductProofError):
            proof.validate_loaded_observations(expected, observed[:-1])


if __name__ == "__main__":
    unittest.main()
