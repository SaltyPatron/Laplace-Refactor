#!/usr/bin/env python3
"""Observe and verify the immutable host-side provider used to build PostgreSQL."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import sys
from pathlib import Path
from typing import Any, Sequence


SCHEMA = "laplace.postgresql-host-build-provider/v1"


class ProviderError(RuntimeError):
    pass


def reject_duplicate_object_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    document: dict[str, Any] = {}
    for key, value in pairs:
        if key in document:
            raise ProviderError(f"duplicate JSON object key: {key}")
        document[key] = value
    return document


def read_receipt(path: Path) -> dict[str, Any]:
    value = json.loads(
        path.read_text(encoding="utf-8"),
        object_pairs_hook=reject_duplicate_object_keys,
    )
    if not isinstance(value, dict):
        raise ProviderError("host provider receipt must be a JSON object")
    return value


def canonical_bytes(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def tree_receipt(root: Path) -> dict[str, Any]:
    if not root.is_dir() or root.is_symlink():
        raise ProviderError(f"provider root must be a physical directory: {root}")
    digest = hashlib.sha256()
    file_count = 0
    symlink_count = 0
    directory_count = 0
    total_file_bytes = 0
    for path in sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix()):
        relative = path.relative_to(root).as_posix()
        mode = stat.S_IMODE(path.lstat().st_mode)
        if path.is_symlink():
            kind = "symlink"
            identity = os.readlink(path)
            symlink_count += 1
        elif path.is_file():
            kind = "file"
            identity = sha256_file(path)
            file_count += 1
            total_file_bytes += path.stat().st_size
        elif path.is_dir():
            kind = "directory"
            identity = ""
            directory_count += 1
        else:
            raise ProviderError(f"provider root contains unsupported object: {path}")
        encoded = canonical_bytes([relative, kind, mode, identity])
        digest.update(len(encoded).to_bytes(8, "big"))
        digest.update(encoded)
    return {
        "path": str(root),
        "tree_sha256": digest.hexdigest(),
        "file_count": file_count,
        "symlink_count": symlink_count,
        "directory_count": directory_count,
        "total_file_bytes": total_file_bytes,
    }


def file_receipt(path: Path) -> dict[str, Any]:
    if path.is_symlink():
        target = os.readlink(path)
        resolved = path.resolve()
        if not resolved.is_file():
            raise ProviderError(f"provider symlink target is absent: {path}")
        return {
            "path": str(path),
            "kind": "symlink",
            "target": target,
            "target_path": str(resolved),
            "target_sha256": sha256_file(resolved),
        }
    if not path.is_file():
        raise ProviderError(f"provider file is absent: {path}")
    return {
        "path": str(path),
        "kind": "file",
        "sha256": sha256_file(path),
        "size_bytes": path.stat().st_size,
    }


def observe(roots: Sequence[Path], files: Sequence[Path]) -> dict[str, Any]:
    uname = os.uname()
    payload: dict[str, Any] = {
        "schema": SCHEMA,
        "host": {
            "sysname": uname.sysname,
            "release": uname.release,
            "version": uname.version,
            "machine": uname.machine,
        },
        "roots": [tree_receipt(path.resolve()) for path in roots],
        "files": [file_receipt(path) for path in files],
        "scope": "build-time-provider-only",
        "product_runtime_authority": False,
    }
    payload["provider_id"] = hashlib.sha256(canonical_bytes(payload)).hexdigest()
    return payload


def verify_identity(receipt: dict[str, Any]) -> None:
    if receipt.get("schema") != SCHEMA:
        raise ProviderError("host provider receipt schema differs")
    expected_id = receipt.get("provider_id")
    identity = dict(receipt)
    identity.pop("provider_id", None)
    if expected_id != hashlib.sha256(canonical_bytes(identity)).hexdigest():
        raise ProviderError("host provider receipt identity differs")


def verify_inputs(receipt: dict[str, Any]) -> dict[str, Any]:
    """Replay the exact receipted provider bytes without requalifying the historical host."""
    verify_identity(receipt)
    current = observe(
        [Path(item["path"]) for item in receipt.get("roots", [])],
        [Path(item["path"]) for item in receipt.get("files", [])],
    )
    if (
        current.get("roots") != receipt.get("roots")
        or current.get("files") != receipt.get("files")
    ):
        raise ProviderError("host build provider bytes differ")
    return receipt


def verify(receipt: dict[str, Any]) -> dict[str, Any]:
    verify_identity(receipt)
    current = observe(
        [Path(item["path"]) for item in receipt.get("roots", [])],
        [Path(item["path"]) for item in receipt.get("files", [])],
    )
    if current != receipt:
        raise ProviderError("host build provider bytes or kernel identity differ")
    return current


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    observe_parser = subparsers.add_parser("observe")
    observe_parser.add_argument("--root", action="append", default=[])
    observe_parser.add_argument("--file", action="append", default=[])
    observe_parser.add_argument("--output-root", required=True)
    verify_parser = subparsers.add_parser("verify")
    verify_parser.add_argument("--receipt", required=True)
    verify_inputs_parser = subparsers.add_parser("verify-inputs")
    verify_inputs_parser.add_argument("--receipt", required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.command in {"verify", "verify-inputs"}:
        receipt_path = Path(args.receipt).resolve()
        receipt = read_receipt(receipt_path)
        verified = verify(receipt) if args.command == "verify" else verify_inputs(receipt)
        print(json.dumps(verified, sort_keys=True))
        return 0
    roots = [Path(item).resolve() for item in args.root]
    files = [Path(item) for item in args.file]
    if not roots:
        raise ProviderError("at least one host provider root is required")
    receipt = observe(roots, files)
    output = Path(args.output_root).resolve() / receipt["provider_id"]
    output.mkdir(parents=True, mode=0o700)
    path = output / "host-provider-receipt.json"
    encoded = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if path.exists() and path.read_text(encoding="utf-8") != encoded:
        raise ProviderError("host provider receipt destination conflicts")
    path.write_text(encoded, encoding="utf-8")
    print(path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (ProviderError, OSError, json.JSONDecodeError) as error:
        print(f"postgresql-host-provider: {error}", file=sys.stderr)
        raise SystemExit(1) from error
