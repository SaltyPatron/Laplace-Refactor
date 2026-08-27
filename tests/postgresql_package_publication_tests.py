#!/usr/bin/env python3
"""Acceptance and deliberate-defect tests for shared PostgreSQL package publication."""

from __future__ import annotations

import copy
import grp
import importlib.util
import json
import os
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/postgresql_package_publication.py"
SPEC = importlib.util.spec_from_file_location("postgresql_package_publication", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
PUBLICATION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PUBLICATION)


class PostgreSQLPackagePublicationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="laplace-postgresql-publication-"
        )
        self.root = Path(self.temporary.name)
        self.contract = copy.deepcopy(
            PUBLICATION.load_json(
                REPOSITORY / "contracts/postgresql-package-publication.json"
            )
        )
        self.contract["publication_root"] = str(self.root / "published")
        self.contract["receipt_root"] = str(self.root / "receipts")
        self.contract["consumer_group"] = grp.getgrgid(os.getgid()).gr_name
        self.source_receipt = self.make_source()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_json(self, path: Path, value: object) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    def make_source(self) -> Path:
        postgresql = self.root / "private/postgresql"
        postgres = postgresql / "pgsql-18/bin/postgres"
        postgres.parent.mkdir(parents=True)
        postgres.write_bytes(b"accepted PostgreSQL\n")
        postgres.chmod(0o755)
        (postgresql / "lib").mkdir()
        (postgresql / "lib/libaccepted.so.1").write_bytes(b"accepted runtime\n")
        (postgresql / "lib/libaccepted.so").symlink_to("libaccepted.so.1")

        toolchain = self.root / "private/toolchain"
        readelf = toolchain / "bin/readelf"
        readelf.parent.mkdir(parents=True)
        readelf.write_bytes(b"accepted readelf\n")
        readelf.chmod(0o755)
        toolchain_receipt = self.root / "private/evidence/toolchain.json"
        toolchain_value = {
            "schema": "laplace.toolchain-package-receipt/v1",
            "build_input_id": "2" * 64,
            "package": {"prefix": str(toolchain)},
        }
        self.write_json(toolchain_receipt, toolchain_value)
        host_receipt = self.root / "private/evidence/host.json"
        self.write_json(host_receipt, {"provider_id": "host-provider"})
        postgresql_tree = PUBLICATION.tree_receipt(postgresql)
        receipt = {
            "schema": "laplace.postgresql-package-receipt/v2",
            "build_input_id": "9" * 64,
            "prefix": str(postgresql),
            "version": "PostgreSQL 18.6",
            "tree_sha256": postgresql_tree["tree_sha256"],
            "file_count": postgresql_tree["file_count"],
            "total_file_bytes": postgresql_tree["total_file_bytes"],
            "build_input_closure_complete": True,
            "recursive_elf_closure_verified": True,
            "runtime_provider_qualification_complete": True,
            "activation_eligible": True,
            "build_toolchain": {
                "build_input_id": "2" * 64,
                "prefix": str(toolchain),
                "receipt_path": str(toolchain_receipt),
                "receipt_sha256": PUBLICATION.sha256_file(toolchain_receipt),
            },
            "host_build_provider": {
                "provider_id": "host-provider",
                "receipt_path": str(host_receipt),
                "receipt_sha256": PUBLICATION.sha256_file(host_receipt),
            },
        }
        source_receipt = self.root / "private/evidence/postgresql.json"
        self.write_json(source_receipt, receipt)
        return source_receipt

    def test_exact_publication_is_runner_readable_and_replayable(self) -> None:
        first = PUBLICATION.publish(self.contract, self.source_receipt)
        second = PUBLICATION.publish(self.contract, self.source_receipt)
        self.assertEqual(first, second)
        self.assertEqual(first["schema"], PUBLICATION.PUBLICATION_SCHEMA)
        self.assertTrue(first["publication_complete"])
        self.assertEqual(
            first["postgresql"]["tree_sha256"],
            PUBLICATION.load_json(self.source_receipt)["tree_sha256"],
        )
        receipt = Path(first["receipt_path"])
        self.assertEqual(receipt.stat().st_mode & 0o777, 0o640)
        self.assertEqual(Path(first["publication_root"]).stat().st_mode & 0o7777, 0o2750)

    def test_incomplete_source_proof_state_is_rejected(self) -> None:
        source = PUBLICATION.load_json(self.source_receipt)
        source["activation_eligible"] = False
        self.write_json(self.source_receipt, source)
        with self.assertRaisesRegex(PUBLICATION.PublicationError, "activation_eligible"):
            PUBLICATION.publication_plan(self.contract, self.source_receipt)

    def test_source_tree_mutation_is_rejected_before_publication(self) -> None:
        source = PUBLICATION.load_json(self.source_receipt)
        (Path(source["prefix"]) / "pgsql-18/bin/postgres").write_bytes(b"mutated\n")
        with self.assertRaisesRegex(PUBLICATION.PublicationError, "tree differs"):
            PUBLICATION.publication_plan(self.contract, self.source_receipt)

    def test_published_tree_mutation_is_rejected(self) -> None:
        receipt = PUBLICATION.publish(self.contract, self.source_receipt)
        (Path(receipt["postgresql"]["prefix"]) / "pgsql-18/bin/postgres").write_bytes(
            b"mutated publication\n"
        )
        with self.assertRaisesRegex(PUBLICATION.PublicationError, "tree differs"):
            PUBLICATION.verify_publication(Path(receipt["receipt_path"]))

    def test_publication_receipt_self_digest_mutation_is_rejected(self) -> None:
        receipt = PUBLICATION.publish(self.contract, self.source_receipt)
        path = Path(receipt["receipt_path"])
        mutant = PUBLICATION.load_json(path)
        mutant["publication_complete"] = False
        self.write_json(path, mutant)
        with self.assertRaisesRegex(PUBLICATION.PublicationError, "self-digest"):
            PUBLICATION.verify_publication(path)

    def test_absolute_package_symlink_is_rejected(self) -> None:
        source = PUBLICATION.load_json(self.source_receipt)
        (Path(source["prefix"]) / "absolute").symlink_to("/tmp/outside")
        with self.assertRaisesRegex(PUBLICATION.PublicationError, "absolute symlink"):
            PUBLICATION.publication_plan(self.contract, self.source_receipt)


if __name__ == "__main__":
    unittest.main()
