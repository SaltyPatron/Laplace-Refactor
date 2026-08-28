#!/usr/bin/env python3
from __future__ import annotations

import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "sources" / "acquire-tabular-source.py"
SPEC = importlib.util.spec_from_file_location("laplace_tabular_acquisition", TOOL)
assert SPEC is not None and SPEC.loader is not None
ACQUISITION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ACQUISITION)


def digest(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def fixture() -> tuple[dict, bytes, bytes]:
    selected = b"Id\tName\neng\tEnglish\njpn\tJapanese\n"
    archive_buffer = io.BytesIO()
    with zipfile.ZipFile(archive_buffer, "w") as archive:
        member = zipfile.ZipInfo("fixture/table.tab", (2026, 4, 15, 0, 0, 0))
        member.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(member, selected)
    archive_bytes = archive_buffer.getvalue()
    document = {
        "schema": "laplace.tabular-source-profile/v1",
        "artifacts": [
            {
                "name": "fixture.zip",
                "local_discovery_path": "fixture.zip",
                "byte_count": len(archive_bytes),
                "sha256": digest(archive_bytes),
                "parent": None,
                "acquisition": {
                    "transport": "https",
                    "url": "https://authority.invalid/fixture.zip",
                    "retry_attempts": 3,
                },
            },
            {
                "name": "fixture/table.tab",
                "local_discovery_path": "table.tab",
                "archive_member": "fixture/table.tab",
                "byte_count": len(selected),
                "sha256": digest(selected),
                "parent": "fixture.zip",
            },
        ],
    }
    return document, archive_bytes, selected


class LockedTabularSourceAcquisition(unittest.TestCase):
    def test_acquires_verifies_extracts_receipts_and_replays_exactly(self) -> None:
        document, archive_bytes, selected = fixture()

        def fetch(url: str, destination: Path, attempts: int) -> None:
            self.assertEqual(url, document["artifacts"][0]["acquisition"]["url"])
            self.assertEqual(attempts, 3)
            destination.write_bytes(archive_bytes)

        with tempfile.TemporaryDirectory(prefix="laplace-source-acquisition-") as root:
            destination = Path(root) / "accepted"
            receipt = ACQUISITION.acquire(document, destination, fetch)
            self.assertEqual((destination / "fixture.zip").read_bytes(), archive_bytes)
            self.assertEqual((destination / "table.tab").read_bytes(), selected)
            self.assertEqual(receipt["schema"], ACQUISITION.RECEIPT_SCHEMA)
            self.assertEqual(len(receipt["artifacts"]), 2)
            self.assertEqual(len(receipt["receipt_id"]), 64)

            def must_not_fetch(url: str, path: Path, attempts: int) -> None:
                self.fail(f"exact replay attempted acquisition: {url} {path} {attempts}")

            self.assertEqual(
                ACQUISITION.acquire(document, destination, must_not_fetch), receipt)

    def test_digest_path_member_and_transport_mutations_fail_closed(self) -> None:
        document, archive_bytes, _ = fixture()

        def fetch(url: str, destination: Path, attempts: int) -> None:
            del url, attempts
            destination.write_bytes(archive_bytes)

        mutations = []
        changed_container = copy.deepcopy(document)
        changed_container["artifacts"][0]["sha256"] = "00" * 32
        mutations.append(changed_container)

        changed_member = copy.deepcopy(document)
        changed_member["artifacts"][1]["sha256"] = "11" * 32
        mutations.append(changed_member)

        escaping_path = copy.deepcopy(document)
        escaping_path["artifacts"][1]["local_discovery_path"] = "../escape.tab"
        mutations.append(escaping_path)

        insecure_transport = copy.deepcopy(document)
        insecure_transport["artifacts"][0]["acquisition"] = {
            "transport": "http",
            "url": "http://authority.invalid/fixture.zip",
            "retry_attempts": 3,
        }
        mutations.append(insecure_transport)

        with tempfile.TemporaryDirectory(prefix="laplace-source-mutations-") as root:
            for ordinal, mutation in enumerate(mutations):
                destination = Path(root) / str(ordinal)
                with self.assertRaises(ACQUISITION.AcquisitionError):
                    ACQUISITION.acquire(mutation, destination, fetch)
                self.assertFalse(destination.exists())


if __name__ == "__main__":
    unittest.main()
