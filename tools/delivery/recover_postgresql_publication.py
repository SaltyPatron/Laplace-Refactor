#!/usr/bin/env python3
"""Recover the exact authority-selected PostgreSQL publication from accepted source evidence.

This utility never selects a new publication. It may only reconstruct the publication
already named by state/product-publication-selection.json when one accepted source
receipt recorded in continuation state reproduces the selected receipt path and bytes
exactly. Any ambiguity, mutation, or partial-state mismatch fails closed.
"""

from __future__ import annotations

import argparse
import grp
import hashlib
import importlib.util
import json
import os
import shutil
import stat
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


REPOSITORY = Path(__file__).resolve().parents[2]
PUBLICATION_PATH = Path(__file__).with_name("postgresql_package_publication.py")
SPEC = importlib.util.spec_from_file_location(
    "laplace_postgresql_package_publication_recovery_owner", PUBLICATION_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load PostgreSQL publication owner")
PUBLICATION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PUBLICATION)

SELECTION_SCHEMA = "laplace.product-publication-selection/v1"
SELECTION_CLASSIFICATION = "authority-selected-development-publication"
SOURCE_SCHEMA = "laplace.postgresql-package-receipt/v2"


class RecoveryError(RuntimeError):
    pass


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def require_selected_state(selection: Mapping[str, Any]) -> tuple[Path, str]:
    if (
        selection.get("schema") != SELECTION_SCHEMA
        or selection.get("classification") != SELECTION_CLASSIFICATION
        or set(selection) != {"schema", "classification", "postgresql_publication"}
    ):
        raise RecoveryError("selected PostgreSQL publication state differs")
    record = selection.get("postgresql_publication")
    if not isinstance(record, dict) or set(record) != {"receipt", "receipt_sha256"}:
        raise RecoveryError("selected PostgreSQL publication record differs")
    receipt = Path(str(record.get("receipt", "")))
    if not receipt.is_absolute() or ".." in receipt.parts:
        raise RecoveryError("selected publication receipt path is not normalized absolute")
    expected_sha = str(record.get("receipt_sha256", ""))
    if PUBLICATION.HEX_256.fullmatch(expected_sha) is None:
        raise RecoveryError("selected publication receipt SHA-256 differs")
    return receipt, expected_sha


def candidate_records(value: Any) -> Iterable[tuple[Path, str]]:
    if isinstance(value, dict):
        if (
            value.get("receipt_schema") == SOURCE_SCHEMA
            and isinstance(value.get("package_receipt"), str)
            and isinstance(value.get("package_receipt_sha256"), str)
        ):
            yield Path(value["package_receipt"]), value["package_receipt_sha256"]
        for child in value.values():
            yield from candidate_records(child)
    elif isinstance(value, list):
        for child in value:
            yield from candidate_records(child)


def exact_source_plan(
    contract: Mapping[str, Any],
    continuation: Mapping[str, Any],
    selected_receipt: Path,
    selected_sha256: str,
) -> dict[str, Any]:
    matches: dict[Path, dict[str, Any]] = {}
    for source_path, source_sha256 in candidate_records(continuation):
        if (
            not source_path.is_absolute()
            or ".." in source_path.parts
            or PUBLICATION.HEX_256.fullmatch(source_sha256) is None
            or not source_path.is_file()
            or source_path.is_symlink()
        ):
            continue
        try:
            if PUBLICATION.sha256_file(source_path) != source_sha256:
                continue
            plan = PUBLICATION.publication_plan(contract, source_path)
            receipt = plan["receipt"]
            payload = PUBLICATION.receipt_bytes(receipt)
        except PUBLICATION.PublicationError:
            continue
        if (
            Path(receipt["receipt_path"]) == selected_receipt
            and sha256_bytes(payload) == selected_sha256
        ):
            matches[source_path.resolve()] = plan
    if not matches:
        raise RecoveryError(
            "no accepted PostgreSQL source receipt reproduces the selected publication exactly"
        )
    if len(matches) != 1:
        raise RecoveryError("selected PostgreSQL publication recovery source is ambiguous")
    return next(iter(matches.values()))


def expected_tree(record: Mapping[str, Any]) -> dict[str, Any]:
    return {
        field: record[field]
        for field in (
            "tree_sha256",
            "file_count",
            "symlink_count",
            "directory_count",
            "total_file_bytes",
        )
    }


def validate_existing_root(plan: Mapping[str, Any]) -> None:
    receipt = plan["receipt"]
    root = Path(receipt["publication_root"])
    if not root.is_dir() or root.is_symlink():
        raise RecoveryError("selected publication root is not one physical directory")
    for name in ("postgresql", "toolchain"):
        record = receipt[name]
        prefix = Path(record["prefix"])
        if PUBLICATION.tree_receipt(prefix) != expected_tree(record):
            raise RecoveryError(f"existing selected publication {name} tree differs")
    evidence = (
        (
            Path(receipt["source_receipt"]["path"]),
            receipt["source_receipt"]["sha256"],
            "PostgreSQL source receipt",
        ),
        (
            Path(receipt["toolchain"]["source_receipt"]),
            receipt["toolchain"]["source_receipt_sha256"],
            "toolchain source receipt",
        ),
        (
            Path(receipt["host_provider"]["source_receipt"]),
            receipt["host_provider"]["source_receipt_sha256"],
            "host-provider source receipt",
        ),
    )
    for path, expected_sha, label in evidence:
        try:
            PUBLICATION.require_physical_file(path, label, expected_sha)
        except PUBLICATION.PublicationError as error:
            raise RecoveryError(str(error)) from error


def write_selected_receipt(
    contract: Mapping[str, Any], plan: Mapping[str, Any], selected_receipt: Path
) -> None:
    receipt = plan["receipt"]
    payload = PUBLICATION.receipt_bytes(receipt)
    group_id = grp.getgrnam(contract["consumer_group"]).gr_gid
    directory_mode = PUBLICATION.parse_mode(contract["directory_mode"], "directory_mode")
    receipt_mode = PUBLICATION.parse_mode(contract["receipt_mode"], "receipt_mode")
    try:
        PUBLICATION.ensure_shared_directory(selected_receipt.parent, directory_mode, group_id)
    except PUBLICATION.PublicationError as error:
        raise RecoveryError(str(error)) from error
    temporary = selected_receipt.with_name(
        f".{selected_receipt.name}.{os.getpid()}.recovery"
    )
    descriptor = os.open(
        temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, receipt_mode
    )
    try:
        with os.fdopen(descriptor, "wb", closefd=False) as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
    finally:
        os.close(descriptor)
    os.replace(temporary, selected_receipt)
    directory = os.open(selected_receipt.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)
    metadata = selected_receipt.stat()
    if (
        stat.S_IMODE(metadata.st_mode) != receipt_mode
        or metadata.st_gid != group_id
    ):
        raise RecoveryError("recovered publication receipt authority differs")


def materialize_selected_root(
    contract: Mapping[str, Any], plan: Mapping[str, Any]
) -> None:
    source = plan["source"]
    receipt = plan["receipt"]
    root = Path(receipt["publication_root"])
    if root.exists() or root.is_symlink():
        raise RecoveryError("selected publication root unexpectedly exists")
    group_id = grp.getgrnam(contract["consumer_group"]).gr_gid
    directory_mode = PUBLICATION.parse_mode(contract["directory_mode"], "directory_mode")
    receipt_mode = PUBLICATION.parse_mode(contract["receipt_mode"], "receipt_mode")
    try:
        PUBLICATION.ensure_shared_directory(root.parent, directory_mode, group_id)
    except PUBLICATION.PublicationError as error:
        raise RecoveryError(str(error)) from error
    temporary = Path(
        tempfile.mkdtemp(prefix=f".{receipt['publication_id']}.recovery.", dir=root.parent)
    )
    try:
        os.chmod(temporary, directory_mode)
        evidence = temporary / "evidence"
        evidence.mkdir(mode=0o750)
        shutil.copy2(source["source_receipt"], evidence / "postgresql-package-receipt.json")
        shutil.copy2(source["toolchain_receipt"], evidence / "toolchain-package-receipt.json")
        shutil.copy2(source["host_receipt"], evidence / "host-provider-receipt.json")
        for path in evidence.iterdir():
            os.chmod(path, receipt_mode)
        shutil.copytree(source["postgresql_prefix"], temporary / "postgresql", symlinks=True)
        shutil.copytree(
            source["toolchain_physical_prefix"], temporary / "toolchain", symlinks=True
        )
        os.chmod(temporary / "postgresql", 0o750)
        os.chmod(temporary / "toolchain", 0o750)
        if PUBLICATION.tree_receipt(temporary / "postgresql") != source["postgresql_tree"]:
            raise RecoveryError("recovered PostgreSQL package tree differs")
        if PUBLICATION.tree_receipt(temporary / "toolchain") != source["toolchain_tree"]:
            raise RecoveryError("recovered toolchain package tree differs")
        PUBLICATION.fsync_tree(temporary)
        os.replace(temporary, root)
        parent = os.open(root.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(parent)
        finally:
            os.close(parent)
    except BaseException:
        if temporary.exists():
            shutil.rmtree(temporary)
        raise


def recover_selected(
    contract_path: Path, selection_path: Path, continuation_path: Path
) -> dict[str, Any]:
    contract = PUBLICATION.load_json(contract_path)
    PUBLICATION.validate_contract(contract)
    selection = PUBLICATION.load_json(selection_path)
    selected_receipt, selected_sha256 = require_selected_state(selection)

    if selected_receipt.exists() or selected_receipt.is_symlink():
        if not selected_receipt.is_file() or selected_receipt.is_symlink():
            raise RecoveryError("selected publication receipt is not one physical file")
        if PUBLICATION.sha256_file(selected_receipt) != selected_sha256:
            raise RecoveryError("selected publication receipt bytes differ")
        try:
            verified = PUBLICATION.verify_publication(selected_receipt)
            return {
                "schema": "laplace.postgresql-publication-recovery/v1",
                "state": "already-verified",
                "publication_id": verified["publication_id"],
                "receipt": str(selected_receipt),
                "receipt_sha256": selected_sha256,
            }
        except PUBLICATION.PublicationError:
            pass

    continuation = PUBLICATION.load_json(continuation_path)
    plan = exact_source_plan(
        contract, continuation, selected_receipt, selected_sha256
    )
    planned_receipt = plan["receipt"]
    planned_payload = PUBLICATION.receipt_bytes(planned_receipt)
    root = Path(planned_receipt["publication_root"])

    if selected_receipt.exists():
        if selected_receipt.read_bytes() != planned_payload:
            raise RecoveryError("existing selected publication receipt differs from exact plan")
        if root.exists() or root.is_symlink():
            raise RecoveryError("existing selected publication failed verification; refusing mutation")
        materialize_selected_root(contract, plan)
    elif root.exists() or root.is_symlink():
        validate_existing_root(plan)
        write_selected_receipt(contract, plan, selected_receipt)
    else:
        try:
            PUBLICATION.publish(contract, Path(plan["source"]["source_receipt"]))
        except PUBLICATION.PublicationError as error:
            raise RecoveryError(str(error)) from error

    if PUBLICATION.sha256_file(selected_receipt) != selected_sha256:
        raise RecoveryError("recovered selected publication receipt SHA-256 differs")
    try:
        verified = PUBLICATION.verify_publication(selected_receipt)
    except PUBLICATION.PublicationError as error:
        raise RecoveryError(str(error)) from error
    return {
        "schema": "laplace.postgresql-publication-recovery/v1",
        "state": "recovered-exact-selection",
        "publication_id": verified["publication_id"],
        "receipt": str(selected_receipt),
        "receipt_sha256": selected_sha256,
        "source_receipt_sha256": verified["source_receipt_sha256"],
    }


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--contract",
        default=str(REPOSITORY / "contracts/postgresql-package-publication.json"),
    )
    parser.add_argument(
        "--selection",
        default=str(REPOSITORY / "state/product-publication-selection.json"),
    )
    parser.add_argument(
        "--continuation",
        default=str(REPOSITORY / "state/continuation.json"),
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_arguments(argv)
    result = recover_selected(
        Path(arguments.contract),
        Path(arguments.selection),
        Path(arguments.continuation),
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (RecoveryError, PUBLICATION.PublicationError, OSError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
