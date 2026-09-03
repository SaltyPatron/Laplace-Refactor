#!/usr/bin/env python3
"""Publish one accepted private PostgreSQL package as an immutable shared build input."""

from __future__ import annotations

import argparse
import grp
import hashlib
import json
import os
import re
import shutil
import stat
import tempfile
from pathlib import Path
from typing import Any, Mapping, Sequence


CONTRACT_SCHEMA = "laplace.postgresql-package-publication-contract/v1"
PUBLICATION_SCHEMA = "laplace.postgresql-package-publication-receipt/v1"
HEX_256 = re.compile(r"^[0-9a-f]{64}$")


class PublicationError(RuntimeError):
    pass


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise PublicationError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise PublicationError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise PublicationError(f"JSON root must be an object: {path}")
    return value


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def canonical_sha256(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def require_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise PublicationError(f"{field} must be a non-empty string")
    return value


def require_sha256(value: Any, field: str) -> str:
    text = require_string(value, field)
    if HEX_256.fullmatch(text) is None:
        raise PublicationError(f"{field} must be lowercase SHA-256")
    return text


def require_absolute(value: Any, field: str) -> Path:
    path = Path(require_string(value, field))
    if not path.is_absolute() or ".." in path.parts:
        raise PublicationError(f"{field} must be a normalized absolute path")
    return path


def parse_mode(value: Any, field: str) -> int:
    text = require_string(value, field)
    if re.fullmatch(r"[0-7]{4}", text) is None:
        raise PublicationError(f"{field} must be a four-digit octal mode")
    return int(text, 8)


def validate_contract(contract: Mapping[str, Any]) -> None:
    if contract.get("schema") != CONTRACT_SCHEMA:
        raise PublicationError(f"contract schema must be {CONTRACT_SCHEMA}")
    if contract.get("publication_receipt_schema") != PUBLICATION_SCHEMA:
        raise PublicationError("publication receipt schema differs")
    if contract.get("source_receipt_schema") != "laplace.postgresql-package-receipt/v2":
        raise PublicationError("source receipt schema differs")
    publication_root = require_absolute(
        contract.get("publication_root"), "publication_root"
    )
    receipt_root = require_absolute(contract.get("receipt_root"), "receipt_root")
    if publication_root == receipt_root or publication_root.is_relative_to(receipt_root):
        raise PublicationError("publication and receipt roots must be separate")
    require_string(contract.get("consumer_group"), "consumer_group")
    if parse_mode(contract.get("directory_mode"), "directory_mode") != 0o2750:
        raise PublicationError("publication directory mode must remain 2750")
    if parse_mode(contract.get("receipt_mode"), "receipt_mode") != 0o640:
        raise PublicationError("publication receipt mode must remain 0640")
    required = contract.get("required_source_state")
    if not isinstance(required, dict) or set(required) != {
        "version",
        "major",
        "build_input_closure_complete",
        "recursive_elf_closure_verified",
        "runtime_provider_qualification_complete",
        "activation_eligible",
    }:
        raise PublicationError("required accepted PostgreSQL proof state differs")
    version = required.get("version")
    major = required.get("major")
    match = (
        re.fullmatch(r"PostgreSQL ([0-9]+(?:\.[0-9]+)+)", version)
        if isinstance(version, str)
        else None
    )
    if (
        match is None
        or not isinstance(major, int)
        or major <= 0
        or int(match.group(1).split(".", 1)[0]) != major
    ):
        raise PublicationError("required PostgreSQL version and major differ")
    for field in (
        "build_input_closure_complete",
        "recursive_elf_closure_verified",
        "runtime_provider_qualification_complete",
        "activation_eligible",
    ):
        if required.get(field) is not True:
            raise PublicationError("required accepted PostgreSQL proof state differs")


def require_physical_file(path: Path, field: str, expected_sha256: str | None = None) -> None:
    if not path.is_file() or path.is_symlink():
        raise PublicationError(f"{field} must be a physical file: {path}")
    if expected_sha256 is not None and sha256_file(path) != expected_sha256:
        raise PublicationError(f"{field} bytes differ: {path}")


def require_physical_directory(path: Path, field: str) -> None:
    if not path.is_dir() or path.is_symlink():
        raise PublicationError(f"{field} must be a physical directory: {path}")


def tree_receipt(root: Path) -> dict[str, Any]:
    require_physical_directory(root, "package tree")
    digest = hashlib.sha256()
    file_count = 0
    symlink_count = 0
    directory_count = 0
    total_file_bytes = 0
    for path in sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix()):
        relative_text = path.relative_to(root).as_posix()
        relative = relative_text.encode("utf-8")
        mode = stat.S_IMODE(path.lstat().st_mode)
        if path.is_symlink():
            kind = b"symlink"
            target = os.readlink(path)
            target_path = Path(target)
            if target_path.is_absolute():
                raise PublicationError(f"package tree has an absolute symlink: {relative_text}")
            resolved = (path.parent / target_path).resolve(strict=False)
            try:
                resolved.relative_to(root.resolve())
            except ValueError as error:
                raise PublicationError(
                    f"package tree symlink escapes its root: {relative_text}"
                ) from error
            if not resolved.exists():
                raise PublicationError(f"package tree symlink is broken: {relative_text}")
            content = target.encode("utf-8")
            symlink_count += 1
        elif path.is_file():
            kind = b"file"
            content = sha256_file(path).encode("ascii")
            file_count += 1
            total_file_bytes += path.stat().st_size
        elif path.is_dir():
            kind = b"directory"
            content = b""
            directory_count += 1
        else:
            raise PublicationError(f"package tree has an unsupported object: {relative_text}")
        for field in (relative, kind, str(mode).encode("ascii"), content):
            digest.update(len(field).to_bytes(8, "big"))
            digest.update(field)
    return {
        "tree_sha256": digest.hexdigest(),
        "file_count": file_count,
        "symlink_count": symlink_count,
        "directory_count": directory_count,
        "total_file_bytes": total_file_bytes,
    }


def accepted_source(
    contract: Mapping[str, Any], source_receipt_path: Path
) -> dict[str, Any]:
    require_physical_file(source_receipt_path, "PostgreSQL source receipt")
    receipt = load_json(source_receipt_path)
    if receipt.get("schema") != contract["source_receipt_schema"]:
        raise PublicationError("PostgreSQL source receipt schema differs")
    for field, expected in contract["required_source_state"].items():
        if field == "major":
            continue
        if receipt.get(field) != expected:
            raise PublicationError(f"PostgreSQL source receipt is not accepted: {field}")
    build_input_id = require_sha256(receipt.get("build_input_id"), "build_input_id")
    postgresql_prefix = require_absolute(receipt.get("prefix"), "postgresql.prefix")
    require_physical_directory(postgresql_prefix, "PostgreSQL package prefix")
    postgresql_tree = tree_receipt(postgresql_prefix)
    if postgresql_tree["tree_sha256"] != require_sha256(
        receipt.get("tree_sha256"), "postgresql.tree_sha256"
    ):
        raise PublicationError("PostgreSQL package tree differs from its receipt")
    if postgresql_tree["file_count"] != receipt.get("file_count"):
        raise PublicationError("PostgreSQL package file count differs")
    if postgresql_tree["total_file_bytes"] != receipt.get("total_file_bytes"):
        raise PublicationError("PostgreSQL package byte count differs")

    toolchain = receipt.get("build_toolchain")
    host_provider = receipt.get("host_build_provider")
    if not isinstance(toolchain, dict) or not isinstance(host_provider, dict):
        raise PublicationError("PostgreSQL source receipt omits build providers")
    toolchain_prefix = require_absolute(toolchain.get("prefix"), "toolchain.prefix")
    require_physical_directory(toolchain_prefix, "toolchain prefix")
    toolchain_receipt_path = require_absolute(
        toolchain.get("receipt_path"), "toolchain.receipt_path"
    )
    toolchain_receipt_sha256 = require_sha256(
        toolchain.get("receipt_sha256"), "toolchain.receipt_sha256"
    )
    require_physical_file(
        toolchain_receipt_path, "toolchain receipt", toolchain_receipt_sha256
    )
    toolchain_receipt = load_json(toolchain_receipt_path)
    if (
        toolchain_receipt.get("schema") != "laplace.toolchain-package-receipt/v1"
        or toolchain_receipt.get("build_input_id") != toolchain.get("build_input_id")
    ):
        raise PublicationError("toolchain receipt identity differs")
    package = toolchain_receipt.get("package")
    if not isinstance(package, dict) or Path(str(package.get("prefix", ""))) != toolchain_prefix:
        raise PublicationError("toolchain receipt prefix differs")
    toolchain_tree = tree_receipt(toolchain_prefix)

    host_receipt_path = require_absolute(
        host_provider.get("receipt_path"), "host_provider.receipt_path"
    )
    host_receipt_sha256 = require_sha256(
        host_provider.get("receipt_sha256"), "host_provider.receipt_sha256"
    )
    require_physical_file(host_receipt_path, "host provider receipt", host_receipt_sha256)
    if load_json(host_receipt_path).get("provider_id") != host_provider.get("provider_id"):
        raise PublicationError("host provider receipt identity differs")

    return {
        "source_receipt": source_receipt_path.resolve(),
        "source_receipt_sha256": sha256_file(source_receipt_path),
        "source": receipt,
        "build_input_id": build_input_id,
        "postgresql_prefix": postgresql_prefix.resolve(),
        "postgresql_tree": postgresql_tree,
        "toolchain_prefix": toolchain_prefix,
        "toolchain_physical_prefix": toolchain_prefix.resolve(),
        "toolchain_tree": toolchain_tree,
        "toolchain_receipt": toolchain_receipt_path.resolve(),
        "toolchain_receipt_sha256": toolchain_receipt_sha256,
        "host_receipt": host_receipt_path.resolve(),
        "host_receipt_sha256": host_receipt_sha256,
    }


def publication_plan(
    contract: Mapping[str, Any], source_receipt_path: Path
) -> dict[str, Any]:
    validate_contract(contract)
    source = accepted_source(contract, source_receipt_path)
    identity = {
        "schema": PUBLICATION_SCHEMA,
        "contract_sha256": canonical_sha256(contract),
        "publisher_sha256": sha256_file(Path(__file__).resolve()),
        "source_receipt_sha256": source["source_receipt_sha256"],
        "build_input_id": source["build_input_id"],
        "postgresql_tree_sha256": source["postgresql_tree"]["tree_sha256"],
        "toolchain_receipt_sha256": source["toolchain_receipt_sha256"],
        "toolchain_tree_sha256": source["toolchain_tree"]["tree_sha256"],
        "host_provider_receipt_sha256": source["host_receipt_sha256"],
    }
    publication_id = canonical_sha256(identity)
    publication_root = Path(contract["publication_root"]) / publication_id
    receipt_path = Path(contract["receipt_root"]) / f"{publication_id}.json"
    postgresql_prefix = publication_root / "postgresql"
    toolchain_prefix = publication_root / "toolchain"
    receipt = {
        **identity,
        "publication_id": publication_id,
        "publication_root": str(publication_root),
        "source_receipt": {
            "path": str(publication_root / "evidence/postgresql-package-receipt.json"),
            "sha256": source["source_receipt_sha256"],
        },
        "postgresql": {
            "prefix": str(postgresql_prefix),
            **source["postgresql_tree"],
        },
        "toolchain": {
            "prefix": str(toolchain_prefix),
            **source["toolchain_tree"],
            "source_receipt": str(publication_root / "evidence/toolchain-package-receipt.json"),
            "source_receipt_sha256": source["toolchain_receipt_sha256"],
            "source_prefix": str(source["toolchain_prefix"]),
        },
        "host_provider": {
            "source_receipt": str(publication_root / "evidence/host-provider-receipt.json"),
            "source_receipt_sha256": source["host_receipt_sha256"],
        },
        "consumer_group": contract["consumer_group"],
        "publication_complete": True,
        "source_product_activation_occurred": False,
        "receipt_path": str(receipt_path),
    }
    receipt["receipt_sha256"] = hashlib.sha256(
        canonical_bytes({key: value for key, value in receipt.items() if key != "receipt_sha256"})
    ).hexdigest()
    return {"source": source, "receipt": receipt}


def receipt_bytes(receipt: Mapping[str, Any]) -> bytes:
    payload = dict(receipt)
    expected = payload.pop("receipt_sha256", None)
    actual = hashlib.sha256(canonical_bytes(payload)).hexdigest()
    if expected != actual:
        raise PublicationError("publication receipt self-digest differs")
    return canonical_bytes(receipt)


def verify_publication(receipt_path: Path) -> dict[str, Any]:
    require_physical_file(receipt_path, "publication receipt")
    receipt = load_json(receipt_path)
    if receipt.get("schema") != PUBLICATION_SCHEMA:
        raise PublicationError("publication receipt schema differs")
    if Path(str(receipt.get("receipt_path", ""))) != receipt_path:
        raise PublicationError("publication receipt path differs")
    receipt_bytes(receipt)
    publication_root = require_absolute(receipt.get("publication_root"), "publication_root")
    require_physical_directory(publication_root, "publication root")
    source_record = receipt.get("source_receipt")
    postgresql = receipt.get("postgresql")
    toolchain = receipt.get("toolchain")
    host = receipt.get("host_provider")
    if not all(isinstance(item, dict) for item in (source_record, postgresql, toolchain, host)):
        raise PublicationError("publication receipt is incomplete")
    source_receipt = require_absolute(source_record.get("path"), "source_receipt.path")
    require_physical_file(
        source_receipt,
        "published PostgreSQL receipt",
        require_sha256(source_record.get("sha256"), "source_receipt.sha256"),
    )
    source = load_json(source_receipt)
    if (
        source.get("schema") != "laplace.postgresql-package-receipt/v2"
        or source.get("build_input_id") != receipt.get("build_input_id")
        or source.get("tree_sha256") != receipt.get("postgresql_tree_sha256")
        or source.get("activation_eligible") is not True
    ):
        raise PublicationError("published PostgreSQL source receipt identity differs")
    for name, record in (("postgresql", postgresql), ("toolchain", toolchain)):
        prefix = require_absolute(record.get("prefix"), f"{name}.prefix")
        if not prefix.is_relative_to(publication_root):
            raise PublicationError(f"published {name} prefix escaped publication root")
        observed = tree_receipt(prefix)
        for field in (
            "tree_sha256",
            "file_count",
            "symlink_count",
            "directory_count",
            "total_file_bytes",
        ):
            if observed[field] != record.get(field):
                raise PublicationError(f"published {name} tree differs: {field}")
    if postgresql.get("tree_sha256") != source.get("tree_sha256"):
        raise PublicationError("published PostgreSQL tree differs from source receipt")
    for name, record in (("toolchain", toolchain), ("host provider", host)):
        path = require_absolute(record.get("source_receipt"), f"{name}.source_receipt")
        if not path.is_relative_to(publication_root):
            raise PublicationError(f"published {name} receipt escaped publication root")
        require_physical_file(
            path,
            f"published {name} receipt",
            require_sha256(
                record.get("source_receipt_sha256"), f"{name}.source_receipt_sha256"
            ),
        )
    if receipt.get("publication_complete") is not True:
        raise PublicationError("publication is incomplete")
    return receipt


def ensure_shared_directory(path: Path, mode: int, group_id: int) -> None:
    path.mkdir(parents=True, exist_ok=True)
    os.chmod(path, mode)
    status = path.stat()
    if stat.S_IMODE(status.st_mode) != mode or status.st_gid != group_id:
        raise PublicationError(f"shared publication directory authority differs: {path}")


def fsync_tree(root: Path) -> None:
    for path in sorted(root.rglob("*"), key=lambda item: len(item.parts), reverse=True):
        if path.is_file() and not path.is_symlink():
            descriptor = os.open(path, os.O_RDONLY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
        elif path.is_dir() and not path.is_symlink():
            descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
    descriptor = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def publish(contract: Mapping[str, Any], source_receipt_path: Path) -> dict[str, Any]:
    plan = publication_plan(contract, source_receipt_path)
    source = plan["source"]
    receipt = plan["receipt"]
    publication_root = Path(receipt["publication_root"])
    receipt_path = Path(receipt["receipt_path"])
    group_id = grp.getgrnam(contract["consumer_group"]).gr_gid
    directory_mode = parse_mode(contract["directory_mode"], "directory_mode")
    receipt_mode = parse_mode(contract["receipt_mode"], "receipt_mode")
    ensure_shared_directory(publication_root.parent, directory_mode, group_id)
    ensure_shared_directory(receipt_path.parent, directory_mode, group_id)
    if publication_root.exists() or receipt_path.exists():
        if not publication_root.is_dir() or not receipt_path.is_file():
            raise PublicationError("publication replay conflicts with an existing object")
        return verify_publication(receipt_path)

    temporary = Path(
        tempfile.mkdtemp(prefix=f".{receipt['publication_id']}.", dir=publication_root.parent)
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
        if tree_receipt(temporary / "postgresql") != source["postgresql_tree"]:
            raise PublicationError("copied PostgreSQL package tree differs")
        if tree_receipt(temporary / "toolchain") != source["toolchain_tree"]:
            raise PublicationError("copied toolchain package tree differs")
        fsync_tree(temporary)
        os.replace(temporary, publication_root)
        parent_descriptor = os.open(publication_root.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(parent_descriptor)
        finally:
            os.close(parent_descriptor)
        temporary_receipt = receipt_path.with_name(f".{receipt_path.name}.{os.getpid()}.tmp")
        descriptor = os.open(
            temporary_receipt, os.O_WRONLY | os.O_CREAT | os.O_EXCL, receipt_mode
        )
        try:
            with os.fdopen(descriptor, "wb", closefd=False) as output:
                output.write(receipt_bytes(receipt))
                output.flush()
                os.fsync(output.fileno())
        finally:
            os.close(descriptor)
        os.replace(temporary_receipt, receipt_path)
        receipt_directory = os.open(receipt_path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(receipt_directory)
        finally:
            os.close(receipt_directory)
    except BaseException:
        if temporary.exists():
            shutil.rmtree(temporary)
        raise
    return verify_publication(receipt_path)


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--contract", default="contracts/postgresql-package-publication.json"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("validate-contract")
    for name in ("plan", "publish"):
        command = subparsers.add_parser(name)
        command.add_argument("--postgresql-receipt", required=True)
    verify = subparsers.add_parser("verify")
    verify.add_argument("--receipt", required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_arguments(argv)
    contract = load_json(Path(arguments.contract))
    validate_contract(contract)
    if arguments.command == "validate-contract":
        return 0
    if arguments.command == "verify":
        result = verify_publication(Path(arguments.receipt))
    elif arguments.command == "plan":
        result = publication_plan(contract, Path(arguments.postgresql_receipt))["receipt"]
    else:
        result = publish(contract, Path(arguments.postgresql_receipt))
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(os.sys.argv[1:]))
    except PublicationError as error:
        print(f"error: {error}", file=os.sys.stderr)
        raise SystemExit(2)
