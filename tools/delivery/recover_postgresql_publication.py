#!/usr/bin/env python3
"""Recover the exact authority-selected PostgreSQL publication fail-closed.

The selected publication predates later changes to the publication program and
contract. Recovery therefore binds to the exact historical authority that created
the selected publication identity instead of silently generating a new identity
with current code. Existing published bytes are preferred; private build evidence
is only a fallback when the immutable published root itself is absent.
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
import subprocess
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
AUTHORITY_COMMIT = "7ba1b329e54276f2078bef8b2b5771a5ad3ed262"
AUTHORITY_PUBLISHER_PATH = "tools/delivery/postgresql_package_publication.py"
AUTHORITY_CONTRACT_PATH = "contracts/postgresql-package-publication.json"


class RecoveryError(RuntimeError):
    pass


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def git_blob(commit: str, path: str) -> bytes:
    completed = subprocess.run(
        ["git", "-C", str(REPOSITORY), "show", f"{commit}:{path}"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace").strip()
        raise RecoveryError(f"cannot read publication authority {commit}:{path}: {detail}")
    return completed.stdout


def load_authority() -> tuple[dict[str, Any], str]:
    publisher_bytes = git_blob(AUTHORITY_COMMIT, AUTHORITY_PUBLISHER_PATH)
    contract_bytes = git_blob(AUTHORITY_COMMIT, AUTHORITY_CONTRACT_PATH)
    try:
        contract = json.loads(
            contract_bytes.decode("utf-8"),
            object_pairs_hook=PUBLICATION.reject_duplicate_keys,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RecoveryError(f"historical publication contract is invalid: {error}") from error
    if not isinstance(contract, dict):
        raise RecoveryError("historical publication contract root differs")
    if (
        contract.get("schema") != PUBLICATION.CONTRACT_SCHEMA
        or contract.get("publication_receipt_schema") != PUBLICATION.PUBLICATION_SCHEMA
        or contract.get("source_receipt_schema") != SOURCE_SCHEMA
        or contract.get("publication_root") != "/opt/laplace/package-inputs/postgresql"
        or contract.get("receipt_root") != "/opt/laplace/receipts/postgresql"
        or contract.get("consumer_group") != "laplace-runner"
        or contract.get("directory_mode") != "2750"
        or contract.get("receipt_mode") != "0640"
    ):
        raise RecoveryError("historical publication authority boundary differs")
    return contract, sha256_bytes(publisher_bytes)


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


def authority_receipt(
    authority_contract: Mapping[str, Any],
    publisher_sha256: str,
    *,
    source_receipt_sha256: str,
    source: Mapping[str, Any],
    postgresql_tree: Mapping[str, Any],
    toolchain_receipt_sha256: str,
    toolchain_tree: Mapping[str, Any],
    host_receipt_sha256: str,
) -> dict[str, Any]:
    build_input_id = PUBLICATION.require_sha256(source.get("build_input_id"), "build_input_id")
    toolchain = source.get("build_toolchain")
    if not isinstance(toolchain, dict):
        raise RecoveryError("accepted source omits build_toolchain")
    source_prefix = PUBLICATION.require_absolute(toolchain.get("prefix"), "toolchain.prefix")
    identity = {
        "schema": PUBLICATION.PUBLICATION_SCHEMA,
        "contract_sha256": PUBLICATION.canonical_sha256(authority_contract),
        "publisher_sha256": publisher_sha256,
        "source_receipt_sha256": source_receipt_sha256,
        "build_input_id": build_input_id,
        "postgresql_tree_sha256": postgresql_tree["tree_sha256"],
        "toolchain_receipt_sha256": toolchain_receipt_sha256,
        "toolchain_tree_sha256": toolchain_tree["tree_sha256"],
        "host_provider_receipt_sha256": host_receipt_sha256,
    }
    publication_id = PUBLICATION.canonical_sha256(identity)
    publication_root = Path(str(authority_contract["publication_root"])) / publication_id
    receipt_path = Path(str(authority_contract["receipt_root"])) / f"{publication_id}.json"
    receipt = {
        **identity,
        "publication_id": publication_id,
        "publication_root": str(publication_root),
        "source_receipt": {
            "path": str(publication_root / "evidence/postgresql-package-receipt.json"),
            "sha256": source_receipt_sha256,
        },
        "postgresql": {
            "prefix": str(publication_root / "postgresql"),
            **dict(postgresql_tree),
        },
        "toolchain": {
            "prefix": str(publication_root / "toolchain"),
            **dict(toolchain_tree),
            "source_receipt": str(publication_root / "evidence/toolchain-package-receipt.json"),
            "source_receipt_sha256": toolchain_receipt_sha256,
            "source_prefix": str(source_prefix),
        },
        "host_provider": {
            "source_receipt": str(publication_root / "evidence/host-provider-receipt.json"),
            "source_receipt_sha256": host_receipt_sha256,
        },
        "consumer_group": authority_contract["consumer_group"],
        "publication_complete": True,
        "source_product_activation_occurred": False,
        "receipt_path": str(receipt_path),
    }
    receipt["receipt_sha256"] = hashlib.sha256(
        PUBLICATION.canonical_bytes(
            {key: value for key, value in receipt.items() if key != "receipt_sha256"}
        )
    ).hexdigest()
    return receipt


def plan_from_private_source(
    current_contract: Mapping[str, Any],
    authority_contract: Mapping[str, Any],
    publisher_sha256: str,
    source_receipt_path: Path,
) -> dict[str, Any]:
    source_state = PUBLICATION.accepted_source(current_contract, source_receipt_path)
    receipt = authority_receipt(
        authority_contract,
        publisher_sha256,
        source_receipt_sha256=source_state["source_receipt_sha256"],
        source=source_state["source"],
        postgresql_tree=source_state["postgresql_tree"],
        toolchain_receipt_sha256=source_state["toolchain_receipt_sha256"],
        toolchain_tree=source_state["toolchain_tree"],
        host_receipt_sha256=source_state["host_receipt_sha256"],
    )
    return {"source": source_state, "receipt": receipt}


def plan_from_published_root(
    authority_contract: Mapping[str, Any],
    publisher_sha256: str,
    selected_receipt: Path,
) -> dict[str, Any]:
    publication_id = selected_receipt.stem
    if PUBLICATION.HEX_256.fullmatch(publication_id) is None:
        raise RecoveryError("selected publication id differs")
    publication_root = Path(str(authority_contract["publication_root"])) / publication_id
    if not publication_root.is_dir() or publication_root.is_symlink():
        raise RecoveryError("selected publication root is absent")
    evidence = publication_root / "evidence"
    source_path = evidence / "postgresql-package-receipt.json"
    toolchain_path = evidence / "toolchain-package-receipt.json"
    host_path = evidence / "host-provider-receipt.json"
    try:
        PUBLICATION.require_physical_file(source_path, "published PostgreSQL receipt")
        PUBLICATION.require_physical_file(toolchain_path, "published toolchain receipt")
        PUBLICATION.require_physical_file(host_path, "published host-provider receipt")
    except PUBLICATION.PublicationError as error:
        raise RecoveryError(str(error)) from error
    source = PUBLICATION.load_json(source_path)
    if (
        source.get("schema") != SOURCE_SCHEMA
        or source.get("activation_eligible") is not True
        or source.get("version") != "PostgreSQL 18.6"
    ):
        raise RecoveryError("published PostgreSQL source receipt identity differs")
    postgresql_tree = PUBLICATION.tree_receipt(publication_root / "postgresql")
    toolchain_tree = PUBLICATION.tree_receipt(publication_root / "toolchain")
    if (
        postgresql_tree["tree_sha256"] != source.get("tree_sha256")
        or postgresql_tree["file_count"] != source.get("file_count")
        or postgresql_tree["total_file_bytes"] != source.get("total_file_bytes")
    ):
        raise RecoveryError("published PostgreSQL tree differs from source evidence")
    toolchain = source.get("build_toolchain")
    host_provider = source.get("host_build_provider")
    if not isinstance(toolchain, dict) or not isinstance(host_provider, dict):
        raise RecoveryError("published source omits provider evidence")
    toolchain_sha = PUBLICATION.sha256_file(toolchain_path)
    host_sha = PUBLICATION.sha256_file(host_path)
    if (
        toolchain_sha != toolchain.get("receipt_sha256")
        or host_sha != host_provider.get("receipt_sha256")
    ):
        raise RecoveryError("published provider evidence differs from source receipt")
    receipt = authority_receipt(
        authority_contract,
        publisher_sha256,
        source_receipt_sha256=PUBLICATION.sha256_file(source_path),
        source=source,
        postgresql_tree=postgresql_tree,
        toolchain_receipt_sha256=toolchain_sha,
        toolchain_tree=toolchain_tree,
        host_receipt_sha256=host_sha,
    )
    if (
        receipt["publication_id"] != publication_id
        or Path(receipt["publication_root"]) != publication_root
        or Path(receipt["receipt_path"]) != selected_receipt
    ):
        raise RecoveryError("published root does not reproduce selected publication identity")
    return {"source": None, "receipt": receipt}


def exact_private_source_plan(
    current_contract: Mapping[str, Any],
    authority_contract: Mapping[str, Any],
    publisher_sha256: str,
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
            plan = plan_from_private_source(
                current_contract,
                authority_contract,
                publisher_sha256,
                source_path,
            )
            payload = PUBLICATION.receipt_bytes(plan["receipt"])
        except (PUBLICATION.PublicationError, RecoveryError):
            continue
        if (
            Path(plan["receipt"]["receipt_path"]) == selected_receipt
            and sha256_bytes(payload) == selected_sha256
        ):
            matches[source_path.resolve()] = plan
    if not matches:
        raise RecoveryError(
            "no accepted private PostgreSQL source reproduces the selected publication exactly"
        )
    if len(matches) != 1:
        raise RecoveryError("selected PostgreSQL publication recovery source is ambiguous")
    return next(iter(matches.values()))


def write_selected_receipt(
    authority_contract: Mapping[str, Any],
    receipt: Mapping[str, Any],
    selected_receipt: Path,
) -> None:
    payload = PUBLICATION.receipt_bytes(receipt)
    group_id = grp.getgrnam(str(authority_contract["consumer_group"])).gr_gid
    directory_mode = PUBLICATION.parse_mode(authority_contract["directory_mode"], "directory_mode")
    receipt_mode = PUBLICATION.parse_mode(authority_contract["receipt_mode"], "receipt_mode")
    PUBLICATION.ensure_shared_directory(selected_receipt.parent, directory_mode, group_id)
    temporary = selected_receipt.with_name(f".{selected_receipt.name}.{os.getpid()}.recovery")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, receipt_mode)
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
    if stat.S_IMODE(metadata.st_mode) != receipt_mode or metadata.st_gid != group_id:
        raise RecoveryError("recovered publication receipt authority differs")


def materialize_selected_root(
    authority_contract: Mapping[str, Any], plan: Mapping[str, Any]
) -> None:
    source = plan.get("source")
    if not isinstance(source, dict):
        raise RecoveryError("private source evidence is unavailable for root reconstruction")
    receipt = plan["receipt"]
    root = Path(receipt["publication_root"])
    if root.exists() or root.is_symlink():
        raise RecoveryError("selected publication root unexpectedly exists")
    group_id = grp.getgrnam(str(authority_contract["consumer_group"])).gr_gid
    directory_mode = PUBLICATION.parse_mode(authority_contract["directory_mode"], "directory_mode")
    receipt_mode = PUBLICATION.parse_mode(authority_contract["receipt_mode"], "receipt_mode")
    PUBLICATION.ensure_shared_directory(root.parent, directory_mode, group_id)
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
        shutil.copytree(source["toolchain_physical_prefix"], temporary / "toolchain", symlinks=True)
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


def restore_publication_modes(plan: Mapping[str, Any]) -> int:
    """Restore only metadata from an exact verified source, never replace bytes."""
    repairs: list[tuple[Path, int]] = []
    source = plan["source"]
    for name, source_key in (("postgresql", "postgresql_prefix"),
                             ("toolchain", "toolchain_physical_prefix")):
        original = Path(source[source_key])
        published = Path(plan["receipt"][name]["prefix"])
        PUBLICATION.require_physical_directory(published, "published tree")
        originals = {p.relative_to(original): p for p in original.rglob("*")}
        targets = {p.relative_to(published): p for p in published.rglob("*")}
        if originals.keys() != targets.keys():
            raise RecoveryError("published tree entries differ; cannot repair modes")
        for relative, path in originals.items():
            target = targets[relative]
            before, current = path.lstat(), target.lstat()
            if stat.S_IFMT(before.st_mode) != stat.S_IFMT(current.st_mode):
                raise RecoveryError(f"published object type differs: {target}")
            if path.is_symlink():
                if os.readlink(path) != os.readlink(target):
                    raise RecoveryError(f"published symlink differs: {target}")
                continue
            if path.is_file() and (before.st_size != current.st_size or
                    PUBLICATION.sha256_file(path) != PUBLICATION.sha256_file(target)):
                raise RecoveryError(f"published bytes differ: {target}")
            mode = stat.S_IMODE(before.st_mode)
            if mode != stat.S_IMODE(current.st_mode):
                repairs.append((target, mode))
    # Validate every object in both trees before changing any metadata.
    for target, mode in repairs:
        os.chmod(target, mode, follow_symlinks=False)
    return len(repairs)


def recover_selected(
    current_contract_path: Path,
    selection_path: Path,
    continuation_path: Path,
) -> dict[str, Any]:
    current_contract = PUBLICATION.load_json(current_contract_path)
    PUBLICATION.validate_contract(current_contract)
    authority_contract, publisher_sha256 = load_authority()
    selection = PUBLICATION.load_json(selection_path)
    selected_receipt, selected_sha256 = require_selected_state(selection)

    authority_receipt_root = Path(str(authority_contract["receipt_root"]))
    if selected_receipt.parent != authority_receipt_root:
        raise RecoveryError("selected receipt escapes historical publication authority")

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
                "publication_authority_commit": AUTHORITY_COMMIT,
                "publication_id": verified["publication_id"],
                "receipt": str(selected_receipt),
                "receipt_sha256": selected_sha256,
            }
        except PUBLICATION.PublicationError:
            pass

    publication_root = Path(str(authority_contract["publication_root"])) / selected_receipt.stem
    plan: dict[str, Any]
    if publication_root.exists() or publication_root.is_symlink():
        if selected_receipt.exists():
            plan = exact_private_source_plan(
                current_contract, authority_contract, publisher_sha256,
                PUBLICATION.load_json(continuation_path), selected_receipt, selected_sha256,
            )
            restore_publication_modes(plan)
        else:
            plan = plan_from_published_root(
                authority_contract, publisher_sha256, selected_receipt,
            )
    else:
        continuation = PUBLICATION.load_json(continuation_path)
        plan = exact_private_source_plan(
            current_contract,
            authority_contract,
            publisher_sha256,
            continuation,
            selected_receipt,
            selected_sha256,
        )
        if selected_receipt.exists():
            if selected_receipt.read_bytes() != PUBLICATION.receipt_bytes(plan["receipt"]):
                raise RecoveryError("existing selected publication receipt differs from authority")
        materialize_selected_root(authority_contract, plan)

    payload = PUBLICATION.receipt_bytes(plan["receipt"])
    if sha256_bytes(payload) != selected_sha256:
        raise RecoveryError("historical authority does not reproduce selected receipt bytes")
    if not selected_receipt.exists():
        write_selected_receipt(authority_contract, plan["receipt"], selected_receipt)
    if PUBLICATION.sha256_file(selected_receipt) != selected_sha256:
        raise RecoveryError("recovered selected publication receipt SHA-256 differs")
    try:
        verified = PUBLICATION.verify_publication(selected_receipt)
    except PUBLICATION.PublicationError as error:
        raise RecoveryError(str(error)) from error
    return {
        "schema": "laplace.postgresql-publication-recovery/v1",
        "state": "recovered-exact-selection",
        "publication_authority_commit": AUTHORITY_COMMIT,
        "publisher_sha256": publisher_sha256,
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
