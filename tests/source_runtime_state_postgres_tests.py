#!/usr/bin/env python3
"""Actual PostgreSQL controls for source runtime-state SQL against owned DDL.

This creates only a private schema fixture, using the real PostgreSQL binding
generator and exact table/domain definitions. It does not load the native
extension or claim Unicode deposition, canonical content or installed activation.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tools"))
from sources.runtime_state import LIVE_RUNTIME_SQL, RuntimeStateError, parse_live_state

arguments = argparse.ArgumentParser(description=__doc__)
arguments.add_argument("--pg-bindir", type=Path, required=True)
args, remaining = arguments.parse_known_args()
sys.argv = [sys.argv[0], *remaining]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    sys.modules[name] = value
    spec.loader.exec_module(value)
    return value


TABLES = ("entity", "physicality", "execution_receipt", "unicode_root_generation",
          "unicode_root_deposit_receipt", "perfcache_active_control",
          "highway_registry_generation", "highway_registry_active_control")


def selected_ddl(sql):
    definitions = ["CREATE SCHEMA laplace;"]
    for name in ("content_id_128", "record_id_256"):
        matches = re.findall(r"^CREATE DOMAIN laplace\." + name + r" AS bytea\n[^;]+;",
                             sql, re.MULTILINE)
        if len(matches) != 1:
            raise ValueError("missing or ambiguous authoritative domain: " + name)
        definitions.extend(matches)
    for name in TABLES:
        matches = re.findall(r"^CREATE TABLE laplace\." + name + r" \(\n.*?^\);",
                             sql, re.MULTILINE | re.DOTALL)
        if len(matches) != 1:
            raise ValueError("missing or ambiguous authoritative table: " + name)
        definitions.extend(matches)
    result = "\n".join(definitions)
    if "@LAPLACE_" in result:
        raise ValueError("PostgreSQL binding generator left unresolved placeholders")
    return result


def bytea(byte, width=32):
    return "decode('" + byte * width + "','hex')"


class SourceRuntimeSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.geteuid() == 0:
            raise RuntimeError("private PostgreSQL fixture must run as an ordinary user")
        scratch = Path(os.environ.get("TMPDIR", ""))
        if (not scratch.is_absolute() or not scratch.is_dir()
                or not scratch.resolve().is_relative_to(Path("/build"))):
            raise RuntimeError("TMPDIR must be an existing permanent /build workspace")
        cls.pg = args.pg_bindir.resolve(strict=True)
        for name in ("initdb", "pg_ctl", "postgres", "psql"):
            if not (cls.pg / name).is_file() or not os.access(cls.pg / name, os.X_OK):
                raise RuntimeError("missing selected PostgreSQL executable: " + name)
        cls.root = Path(tempfile.mkdtemp(prefix="laplace-postgres-test.", dir=scratch))
        cls.data, cls.socket = cls.root / "data", cls.root / "s"
        cls.socket.mkdir(mode=0o700)
        if len(os.fsencode(cls.socket)) > 80:
            raise RuntimeError("selected private PostgreSQL socket path is too long")
        cls.started = False
        cls.addClassCleanup(cls.stop)
        cls.environment = dict(os.environ)
        for key in list(cls.environment):
            if key.startswith("PG"):
                cls.environment.pop(key)
        cls.environment["LD_LIBRARY_PATH"] = str(cls.pg.parent / "lib") + (
            ":" + cls.environment["LD_LIBRARY_PATH"] if cls.environment.get("LD_LIBRARY_PATH") else "")
        cls.execute([cls.pg / "initdb", "-D", cls.data, "--username=source_state_fixture",
                     "--auth=trust", "--locale=C", "--no-sync"], timeout=30)
        cls.started = True
        cls.execute([cls.pg / "pg_ctl", "-D", cls.data, "-l", cls.root / "postgres.log",
                     "-o", "-c listen_addresses='' -c unix_socket_directories='"
                     + str(cls.socket) + "' -c port=55439 -c max_connections=8 "
                     "-c shared_buffers=16MB -c fsync=off -c statement_timeout=10000",
                     "-w", "-t", "20", "start"], timeout=25)
        cls.command = [str(cls.pg / "psql"), "--no-psqlrc", "--host", str(cls.socket),
                       "--port", "55439", "--username", "source_state_fixture",
                       "--dbname", "postgres", "--set", "ON_ERROR_STOP=1",
                       "--quiet", "--tuples-only", "--no-align"]
        generator = module("source_state_real_binding_generator",
                           ROOT / "tests/postgresql_public_readback_tests.py")
        cls.generated = cls.root / "generated"
        cls.generated.mkdir()
        generator.render(cls.generated)
        cls.full_sql = (cls.generated / "laplace--1.0.0.sql").read_text()
        cls.ddl = selected_ddl(cls.full_sql)
        cls.query(cls.ddl)
        cls.columns = {}
        for table in TABLES:
            cls.columns[table] = json.loads(cls.query(
                "SELECT json_agg(json_build_object('name',column_name,'type',data_type,"
                "'domain',domain_name,'default',column_default) ORDER BY ordinal_position) "
                "FROM information_schema.columns WHERE table_schema='laplace' AND table_name='"
                + table + "';"))
        cls.guard = module("source_state_actual_guard", ROOT / "tools/admit_source_guard.py")
        cls.core = module("source_state_actual_core", ROOT / "tools/admit_source.py")
        cls.tools = cls.root / "actual-client"
        (cls.tools / "pgsql-18/bin").mkdir(parents=True)
        # A real selected client, not a fake executable or native substitution.
        shutil.copy2(cls.pg / "psql", cls.tools / "pgsql-18/bin/psql")
        print(json.dumps({"scope": "PostgreSQL schema/query controls only",
                          "server_version": cls.query("SHOW server_version;").strip(),
                          "pg_bindir": str(cls.pg),
                          "ddl_source": "integrations/postgresql/extension/laplace--version.sql.in",
                          "tables": list(TABLES)}))

    @classmethod
    def execute(cls, argv, *, timeout=10, input=None, check=True):
        result = subprocess.run([str(value) for value in argv], input=input, text=True,
                                capture_output=True, timeout=timeout, env=cls.environment)
        if check and result.returncode:
            raise RuntimeError(result.stderr[-8000:])
        return result

    @classmethod
    def query(cls, sql, *, check=True):
        result = cls.execute(cls.command, input=sql, check=check)
        return result.stdout if check else result

    @classmethod
    def stop(cls):
        if getattr(cls, "started", False):
            result = cls.execute([cls.pg / "pg_ctl", "-D", cls.data, "-m", "fast",
                                  "-w", "-t", "5", "stop"], timeout=10, check=False)
            if result.returncode:
                result = cls.execute([cls.pg / "pg_ctl", "-D", cls.data, "-m", "immediate",
                                      "-w", "-t", "5", "stop"], timeout=10, check=False)
            if result.returncode:
                raise RuntimeError("owned PostgreSQL fixture did not stop; workspace retained")
            cls.started = False
        shutil.rmtree(cls.root)

    def insert(self, table, overrides):
        values = {}
        for column in self.columns[table]:
            if column["default"] is not None:
                continue
            if column["domain"] == "record_id_256":
                value = bytea("aa")
            elif column["domain"] == "content_id_128":
                value = bytea("bb", 16)
            elif column["type"] == "bytea":
                value = bytea("aa")
            elif column["type"] == "boolean":
                value = "true"
            elif column["type"] in ("integer", "bigint", "smallint", "numeric", "double precision"):
                value = "1"
            else:
                self.fail("unhandled authoritative fixture column: " + str(column))
            values[column["name"]] = value
        self.assertFalse(set(overrides) - set(values))
        values.update(overrides)
        self.query("INSERT INTO laplace." + table + " ("
                   + ",".join(values) + ") VALUES (" + ",".join(values.values()) + ");")

    def setUp(self):
        self.query("TRUNCATE " + ",".join("laplace." + name for name in reversed(TABLES)) + ";")
        persistent = json.loads((ROOT / "contracts/persistence.json").read_text())["semantic_enums"]
        stream = json.loads((ROOT / "contracts/unicode-root-stream.json").read_text())
        population = stream["frame_kinds"][0]["count"]
        minimum_frames = population + stream["frame_kinds"][1]["count"] + stream["frame_kinds"][4]["count"]
        manifest_bytes = stream["payload_contracts"]["root-manifest-v2"]["record_bytes"]
        self.insert("entity", {"entity_id": bytea("ab", 16), "identity_witness": bytea("ab")})
        self.insert("physicality", {
            "physicality_id": bytea("ac"), "entity_id": bytea("ab", 16),
            "physicality_type": str(persistent["physicality_type"]["atomic_point"]),
            "vertex_class": str(persistent["vertex_class"]["none"]),
            "structural_form": str(persistent["structural_form"]["atomic_point"]),
            "dimension_count": "4", "flags": str(persistent["physicality_flags"]["none"]),
            "trajectory_fingerprint": bytea("00"), "centroid_x": "0", "centroid_y": "0",
            "centroid_z": "0", "centroid_m": "0", "radius": "0",
            "vertex_count": "0", "trajectory": "''::bytea"})
        self.insert("execution_receipt", {"receipt_id": bytea("ad")})
        self.insert("unicode_root_generation", {
            "root_receipt": bytea("55"), "geometry_epoch": bytea("33"),
            "plan_manifest_fingerprint": bytea("66"),
            "postgresql_artifact_fingerprint": bytea("77"),
            "atom_count": str(population), "ducet_position_count": str(population),
            "total_frame_count": str(minimum_frames),
            "canonical_manifest": "decode(repeat('00'," + str(manifest_bytes) + "),'hex')"})
        self.insert("unicode_root_deposit_receipt", {
            "receipt_id": bytea("de"), "root_receipt": bytea("55"),
            "activation_epoch_id": bytea("11", 16),
            "activation_epoch_fingerprint": bytea("22"),
            "perfcache_manifest_fingerprint": bytea("88"),
            "plan_manifest_fingerprint": bytea("66"),
            "postgresql_artifact_fingerprint": bytea("77"),
            "total_frame_count": str(minimum_frames)})
        self.insert("perfcache_active_control", {
            "activation_epoch_id": bytea("11", 16), "epoch_fingerprint": bytea("22"),
            "manifest_fingerprint": bytea("88")})
        self.insert("highway_registry_generation", {
            "activation_epoch_id": bytea("99", 16),
            "activation_epoch_fingerprint": bytea("44"),
            "root_entity_id": bytea("ab", 16), "root_physicality_id": bytea("ac"),
            "isa_receipt": bytea("ad")})
        self.insert("highway_registry_active_control", {
            "activation_epoch_id": bytea("99", 16), "activation_epoch_fingerprint": bytea("44")})
        self.expected = {"activation_epoch_id": "11" * 16,
                         "activation_epoch_fingerprint": "22" * 32,
                         "geometry_epoch": "33" * 32,
                         "perfcache_epoch": "22" * 32, "numeric_epoch": "44" * 32}

    def observed(self, sql=LIVE_RUNTIME_SQL):
        return parse_live_state(self.query(sql))

    def test_real_schema_shared_query_and_both_callers_bind_exact_live_context(self):
        self.assertEqual(self.expected, self.observed())
        self.assertEqual(self.expected, parse_live_state(self.core.run_scalar(
            self.command, LIVE_RUNTIME_SQL, "source schema fixture")))
        original = dict(os.environ)
        try:
            os.environ.clear()
            os.environ.update(self.environment)
            self.assertEqual(self.expected, self.guard._live_runtime_state(
                self.tools, self.socket, 55439, "postgres", "source_state_fixture"))
        finally:
            os.environ.clear()
            os.environ.update(original)

    def test_inactive_unicode_or_highway_returns_no_context(self):
        for table in ("perfcache_active_control", "highway_registry_active_control"):
            with self.subTest(table=table), self.assertRaisesRegex(RuntimeStateError, "0 rows"):
                self.observed("BEGIN; UPDATE laplace." + table
                              + " SET active_present=false;\n" + LIVE_RUNTIME_SQL + "\nROLLBACK;")

    def test_each_actual_join_binding_mismatch_returns_no_context(self):
        variants = (
            ("unicode_root_deposit_receipt", "activation_epoch_id", 16),
            ("unicode_root_deposit_receipt", "activation_epoch_fingerprint", 32),
            ("unicode_root_deposit_receipt", "perfcache_manifest_fingerprint", 32),
            ("unicode_root_deposit_receipt", "plan_manifest_fingerprint", 32),
            ("unicode_root_deposit_receipt", "postgresql_artifact_fingerprint", 32),
            ("highway_registry_active_control", "activation_epoch_id", 16),
            ("highway_registry_active_control", "activation_epoch_fingerprint", 32),
        )
        for table, column, width in variants:
            with self.subTest(table=table, column=column), self.assertRaisesRegex(RuntimeStateError, "0 rows"):
                self.observed("BEGIN; UPDATE laplace." + table + " SET " + column + "="
                              + bytea("ef", width) + ";\n" + LIVE_RUNTIME_SQL + "\nROLLBACK;")

    def test_missing_deposit_and_foreign_root_cannot_supply_context(self):
        result = self.query("UPDATE laplace.unicode_root_deposit_receipt SET root_receipt="
                            + bytea("ef") + ";", check=False)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("foreign key constraint", result.stderr)
        self.query("DELETE FROM laplace.unicode_root_deposit_receipt;")
        with self.assertRaisesRegex(RuntimeStateError, "0 rows"):
            self.observed()

    def test_distinct_deposit_receipts_are_ambiguous_not_first_row_selected(self):
        columns = [item["name"] for item in self.columns["unicode_root_deposit_receipt"]]
        projection = [bytea("df") if name == "receipt_id" else name for name in columns]
        self.query("INSERT INTO laplace.unicode_root_deposit_receipt ("
                   + ",".join(columns) + ") SELECT " + ",".join(projection)
                   + " FROM laplace.unicode_root_deposit_receipt;")
        with self.assertRaisesRegex(RuntimeStateError, "2 rows"):
            self.observed()

    def test_stale_generation_columns_fail_on_authoritative_tables(self):
        for query in (
                "SELECT g.activation_epoch_id FROM laplace.unicode_root_generation g;",
                "SELECT hg.unicode_activation_epoch_id FROM laplace.highway_registry_generation hg;"):
            with self.subTest(query=query):
                result = self.query("\\set VERBOSITY verbose\n" + query, check=False)
                self.assertNotEqual(0, result.returncode)
                self.assertIn("42703", result.stderr)

    def test_deposition_sequence_and_live_successor_numeric_epoch_remain_distinct(self):
        self.query("UPDATE laplace.unicode_root_deposit_receipt SET plan_sequence_fingerprint="
                   + bytea("ee") + ";")
        self.assertEqual(self.expected, self.observed())
        self.query("UPDATE laplace.highway_registry_generation SET activation_epoch_fingerprint="
                   + bytea("45") + "; UPDATE laplace.highway_registry_active_control "
                   "SET activation_epoch_fingerprint=" + bytea("45") + ";")
        self.assertEqual({**self.expected, "numeric_epoch": "45" * 32}, self.observed())


    def readback_transaction(self, statement):
        # Generate the production SQL through its existing caller fixture, then
        # replace only the native SELECT: these tests prove PostgreSQL lifecycle,
        # not native materialization or corpus acceptance.
        cases = module("source_readback_transaction_fixture",
                       ROOT / "tests/verified_git_source_tests.py")
        case = cases.GitReadbackReceiptTests()
        case.execute()
        prefix, selected, unused = case.readback_sql.partition("WITH selected AS MATERIALIZED (")
        unused, selected_end, suffix = case.readback_sql.rpartition(")::text;")
        self.assertTrue(selected and selected_end)
        self.assertEqual(case.readback_client_timeout, 1000)
        return prefix + statement + suffix

    def test_readback_transaction_is_read_only_and_timeout_is_local(self):
        original_timeout = self.query("SHOW statement_timeout;").strip()
        sql = self.readback_transaction(
            "SELECT json_build_object('read_only',current_setting('transaction_read_only'),"
            "'timeout',current_setting('statement_timeout'))::text;")
        result = json.loads(self.core.run_scalar(self.command, sql, "readback transaction control"))
        self.assertEqual(result, {"read_only": "on", "timeout": "15min"})
        rows = self.query(sql + "\nSHOW statement_timeout;").splitlines()
        self.assertEqual(json.loads(rows[0]), result)
        self.assertEqual(rows[1:], [original_timeout])

    def test_readback_server_timeout_cancels_before_client_and_leaves_no_backend(self):
        sql = self.readback_transaction("SELECT pg_sleep(1);")
        self.assertEqual(sql.count("SET LOCAL statement_timeout = '15min';"), 1)
        sql = sql.replace("SET LOCAL statement_timeout = '15min';",
                          "SET LOCAL statement_timeout = '50ms';")
        application = "source_readback_timeout_" + str(os.getpid())
        with patch.dict(os.environ, {"PGAPPNAME": application}):
            with self.assertRaisesRegex(self.core.AdmissionError,
                    "readback timeout control failed:.*canceling statement due to statement timeout"):
                self.core.run_scalar(self.command, sql, "readback timeout control", timeout=3)
        deadline = time.monotonic() + 3
        while self.query("SELECT count(*) FROM pg_stat_activity WHERE application_name='"
                        + application + "';").strip() != "0":
            if time.monotonic() >= deadline:
                self.fail("timed-out readback backend remained after its client returned")
            time.sleep(0.01)

    def test_readback_transaction_refuses_persistent_writes(self):
        before = self.query("SELECT encode(identity_witness,'hex') FROM laplace.entity;")
        sql = self.readback_transaction("UPDATE laplace.entity SET identity_witness=identity_witness;")
        with self.assertRaisesRegex(self.core.AdmissionError,
                "cannot execute UPDATE in a read-only transaction"):
            self.core.run_scalar(self.command, sql, "readback write control")
        self.assertEqual(self.query("SELECT encode(identity_witness,'hex') FROM laplace.entity;"), before)


if __name__ == "__main__":
    unittest.main()
