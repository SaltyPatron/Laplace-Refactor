#!/usr/bin/env python3
"""Behavior and deliberate-break tests for PostgreSQL resource ceilings."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/tests/postgres_resource_guard.py"
RUN_SPI = REPOSITORY / "tests/postgres/run_spi_test.sh"
SPEC = importlib.util.spec_from_file_location("laplace_postgres_resource_guard", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
GUARD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GUARD)


class PostgreSQLResourceGuardTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.data = self.root / "data"
        self.wal = self.data / "pg_wal"
        self.wal.mkdir(parents=True)
        self.receipt = self.root / "receipt.json"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def arguments(self, command: list[str], **overrides: int | float) -> object:
        values: dict[str, int | float] = {
            "max_wall_seconds": 2.0,
            "max_data_bytes": 1048576,
            "max_wal_bytes": 524288,
            "max_workspace_bytes": 2097152,
            "max_rss_bytes": 1073741824,
            "sample_seconds": 0.01,
        }
        values.update(overrides)
        raw = [
            "--data-directory", str(self.data),
            "--workspace-directory", str(self.root),
            "--postmaster-pid", str(__import__("os").getpid()),
        ]
        for name, value in values.items():
            raw.extend(("--" + name.replace("_", "-"), str(value)))
        raw.extend(("--receipt", str(self.receipt), "--", *command))
        return GUARD.parse_args(raw)

    def test_completed_command_records_bounded_receipt(self) -> None:
        result = GUARD.run(self.arguments([sys.executable, "-c", "pass"]))
        receipt = json.loads(self.receipt.read_text(encoding="utf-8"))
        self.assertEqual(result, 0)
        self.assertEqual(receipt["result"], "completed")
        self.assertIsNone(receipt["breach"])

    def test_deliberate_wal_growth_is_cancelled(self) -> None:
        command = [
            sys.executable,
            "-c",
            "from pathlib import Path; import time; "
            f"Path({str(self.wal / 'segment')!r}).write_bytes(b'x' * 8192); time.sleep(2)",
        ]
        result = GUARD.run(self.arguments(command, max_wal_bytes=4096))
        receipt = json.loads(self.receipt.read_text(encoding="utf-8"))
        self.assertEqual(result, 90)
        self.assertEqual(receipt["breach"]["dimension"], "wal_bytes")

    def test_deliberate_wall_overrun_is_cancelled(self) -> None:
        result = GUARD.run(
            self.arguments(
                [sys.executable, "-c", "import time; time.sleep(2)"],
                max_wall_seconds=0.05,
            )
        )
        receipt = json.loads(self.receipt.read_text(encoding="utf-8"))
        self.assertEqual(result, 90)
        self.assertEqual(receipt["breach"]["dimension"], "wall_seconds")

    def test_preexisting_database_bytes_are_not_charged_to_the_next_phase(self) -> None:
        (self.data / "existing").write_bytes(b"x" * 8192)
        result = GUARD.run(
            self.arguments(
                [sys.executable, "-c", "pass"],
                max_data_bytes=4096,
            )
        )
        receipt = json.loads(self.receipt.read_text(encoding="utf-8"))
        self.assertEqual(result, 0)
        self.assertEqual(receipt["maxima"]["data_bytes"], 0)
        self.assertGreaterEqual(receipt["baseline"]["data_bytes"], 8192)
        self.assertEqual(
            receipt["accounting"]["data_bytes"],
            "allocated growth from pre-command baseline",
        )

    def test_source_admission_harness_applies_every_guard_dimension(self) -> None:
        harness = RUN_SPI.read_text(encoding="utf-8")
        for boundary in (
            "LAPLACE_POSTGRES_STATEMENT_TIMEOUT_MS:-60000",
            "LAPLACE_POSTGRES_TEMP_FILE_LIMIT_KB:-524288",
            "LAPLACE_POSTGRES_UNICODE_BOOTSTRAP_TIMEOUT_MS:-300000",
            "LAPLACE_POSTGRES_UNICODE_MAX_WALL_SECONDS:-300",
            "LAPLACE_POSTGRES_UNICODE_MAX_DATA_BYTES:-8589934592",
            "LAPLACE_POSTGRES_UNICODE_MAX_WAL_BYTES:-8589934592",
            "LAPLACE_POSTGRES_UNICODE_MAX_WORKSPACE_BYTES:-12884901888",
            "LAPLACE_POSTGRES_MAX_WALL_SECONDS:-60",
            "LAPLACE_POSTGRES_MAX_DATA_BYTES:-1073741824",
            "LAPLACE_POSTGRES_MAX_WAL_BYTES:-536870912",
            "LAPLACE_POSTGRES_MAX_WORKSPACE_BYTES:-2147483648",
            "LAPLACE_POSTGRES_MAX_RSS_BYTES:-12884901888",
        ):
            self.assertIn(boundary, harness)

    def test_deliberate_guard_omission_is_detected(self) -> None:
        harness = RUN_SPI.read_text(encoding="utf-8")
        mutant = harness.replace(
            "LAPLACE_POSTGRES_MAX_WAL_BYTES:-536870912", "unbounded", 1
        )
        self.assertNotIn("LAPLACE_POSTGRES_MAX_WAL_BYTES:-536870912", mutant)


if __name__ == "__main__":
    unittest.main()
