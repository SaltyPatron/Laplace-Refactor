#!/usr/bin/env python3
"""Verify and import pinned upstream source archives."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import posixpath
import shutil
import stat
import struct
import sys
import tarfile
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, BinaryIO, Sequence


SCHEMA = "laplace.release-lock/v1"
RECEIPT_SCHEMA = "laplace.release-verification/v1"
CHUNK_SIZE = 1024 * 1024


class ReleaseError(RuntimeError):
    pass


def reject_duplicate_object_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    document: dict[str, Any] = {}
    for key, value in pairs:
        if key in document:
            raise ReleaseError(f"duplicate JSON object key: {key}")
        document[key] = value
    return document


def sha256_stream(stream: BinaryIO) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    while True:
        block = stream.read(CHUNK_SIZE)
        if not block:
            break
        size += len(block)
        digest.update(block)
    return digest.hexdigest(), size


def sha256_file(path: Path) -> tuple[str, int]:
    with path.open("rb") as stream:
        return sha256_stream(stream)


def hash_field(digest: Any, value: bytes) -> None:
    digest.update(struct.pack(">Q", len(value)))
    digest.update(value)


def normalized_member_name(name: str, top_directory: str) -> str:
    if not name or "\\" in name or name.startswith("/"):
        raise ReleaseError(f"unsafe archive member path: {name!r}")
    normalized = posixpath.normpath(name)
    parts = PurePosixPath(normalized).parts
    if normalized in ("", ".") or ".." in parts:
        raise ReleaseError(f"unsafe archive member path: {name!r}")
    if normalized != top_directory and not normalized.startswith(f"{top_directory}/"):
        raise ReleaseError(f"archive member escapes {top_directory!r}: {name!r}")
    return normalized


def normalized_link_target(member_name: str, link_name: str, top_directory: str) -> str:
    if not link_name or "\\" in link_name or link_name.startswith("/"):
        raise ReleaseError(f"unsafe archive link target: {member_name!r} -> {link_name!r}")
    resolved = posixpath.normpath(posixpath.join(posixpath.dirname(member_name), link_name))
    if resolved != top_directory and not resolved.startswith(f"{top_directory}/"):
        raise ReleaseError(f"archive link escapes {top_directory!r}: {member_name!r} -> {link_name!r}")
    return resolved


def normalized_hardlink_target(member_name: str, link_name: str, top_directory: str) -> str:
    if not link_name or "\\" in link_name or link_name.startswith("/"):
        raise ReleaseError(f"unsafe archive hardlink target: {member_name!r} -> {link_name!r}")
    root_relative = posixpath.normpath(link_name)
    member_relative = posixpath.normpath(posixpath.join(posixpath.dirname(member_name), link_name))
    for resolved in (root_relative, member_relative):
        if resolved == top_directory or resolved.startswith(f"{top_directory}/"):
            return resolved
    raise ReleaseError(
        f"archive hardlink escapes {top_directory!r}: {member_name!r} -> {link_name!r}"
    )


def member_kind(member: tarfile.TarInfo) -> str:
    if member.isfile():
        return "file"
    if member.isdir():
        return "directory"
    if member.issym():
        return "symlink"
    if member.islnk():
        return "hardlink"
    raise ReleaseError(f"archive member has prohibited type: {member.name!r}")


def inspect_archive(path: Path, top_directory: str) -> dict[str, Any]:
    archive_sha, archive_size = sha256_file(path)
    tree_digest = hashlib.sha256()
    seen_names: set[str] = set()
    records: list[dict[str, Any]] = []
    file_digests: dict[str, str] = {}
    uncompressed_bytes = 0
    regular_file_count = 0

    try:
        archive = tarfile.open(path, mode="r:*")
    except (tarfile.TarError, OSError) as error:
        raise ReleaseError(f"cannot read source archive {path}: {error}") from error
    with archive:
        for member in archive:
            name = normalized_member_name(member.name, top_directory)
            if name in seen_names:
                raise ReleaseError(f"duplicate archive member: {name}")
            seen_names.add(name)
            if member.mode & (stat.S_ISUID | stat.S_ISGID):
                raise ReleaseError(f"archive member has privileged mode bits: {name}")
            kind = member_kind(member)
            link_target = ""
            content_sha = ""
            content_size = 0
            if kind == "file":
                source = archive.extractfile(member)
                if source is None:
                    raise ReleaseError(f"cannot read archive member: {name}")
                with source:
                    content_sha, content_size = sha256_stream(source)
                if content_size != member.size:
                    raise ReleaseError(f"archive member size changed while reading: {name}")
                regular_file_count += 1
                uncompressed_bytes += content_size
                file_digests[name] = content_sha
            elif kind == "symlink":
                link_target = normalized_link_target(name, member.linkname, top_directory)
            elif kind == "hardlink":
                link_target = normalized_hardlink_target(name, member.linkname, top_directory)
            records.append(
                {
                    "name": name,
                    "kind": kind,
                    "mode": member.mode & 0o7777,
                    "size": content_size,
                    "sha256": content_sha,
                    "link_target": link_target,
                }
            )

    for record in sorted(records, key=lambda item: item["name"]):
        for field in (
            record["name"].encode("utf-8"),
            record["kind"].encode("ascii"),
            str(record["mode"]).encode("ascii"),
            str(record["size"]).encode("ascii"),
            record["sha256"].encode("ascii"),
            record["link_target"].encode("utf-8"),
        ):
            hash_field(tree_digest, field)
    return {
        "archive_sha256": archive_sha,
        "archive_size_bytes": archive_size,
        "member_count": len(records),
        "regular_file_count": regular_file_count,
        "uncompressed_bytes": uncompressed_bytes,
        "tree_sha256": tree_digest.hexdigest(),
        "file_sha256": file_digests,
    }


def load_lock(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_object_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ReleaseError(f"cannot read release lock {path}: {error}") from error
    if document.get("schema") != SCHEMA:
        raise ReleaseError(f"release lock schema must be {SCHEMA}")
    archives = document.get("archives")
    if not isinstance(archives, dict) or not archives:
        raise ReleaseError("release lock must contain a non-empty archives object")
    return document


def verify_entry(name: str, entry: dict[str, Any], archive_root: Path) -> dict[str, Any]:
    required_strings = ("version", "filename", "url", "sha256", "top_directory", "tree_sha256")
    for field in required_strings:
        if not isinstance(entry.get(field), str) or not entry[field]:
            raise ReleaseError(f"{name}.{field} must be a non-empty string")
    required_integers = (
        "size_bytes",
        "member_count",
        "regular_file_count",
        "uncompressed_bytes",
    )
    for field in required_integers:
        if not isinstance(entry.get(field), int) or entry[field] < 0:
            raise ReleaseError(f"{name}.{field} must be a non-negative integer")
    filename = entry["filename"]
    if Path(filename).name != filename:
        raise ReleaseError(f"{name}.filename must not contain a directory")
    archive_path = archive_root / filename
    if not archive_path.is_file():
        raise ReleaseError(f"release archive is missing: {archive_path}")
    observed = inspect_archive(archive_path, entry["top_directory"])
    expected = {
        "archive_sha256": entry["sha256"],
        "archive_size_bytes": entry["size_bytes"],
        "member_count": entry["member_count"],
        "regular_file_count": entry["regular_file_count"],
        "uncompressed_bytes": entry["uncompressed_bytes"],
        "tree_sha256": entry["tree_sha256"],
    }
    for field, expected_value in expected.items():
        if observed[field] != expected_value:
            raise ReleaseError(
                f"{name} {field} mismatch: expected {expected_value}, observed {observed[field]}"
            )
    licenses = entry.get("licenses")
    if not isinstance(licenses, list) or not licenses:
        raise ReleaseError(f"{name}.licenses must contain at least one file")
    for license_entry in licenses:
        member_path = license_entry.get("path", "")
        expected_sha = license_entry.get("sha256", "")
        if not member_path.startswith(f"{entry['top_directory']}/"):
            raise ReleaseError(f"{name} license path is outside the top directory")
        observed_sha = observed["file_sha256"].get(member_path)
        if observed_sha != expected_sha:
            raise ReleaseError(
                f"{name} license digest mismatch for {member_path}: expected {expected_sha}, observed {observed_sha}"
            )
    return {
        "name": name,
        "version": entry["version"],
        "filename": filename,
        "url": entry["url"],
        **{field: observed[field] for field in expected},
    }


def verify_lock(lock_path: Path, archive_root: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    document = load_lock(lock_path)
    receipts = [
        verify_entry(name, entry, archive_root)
        for name, entry in sorted(document["archives"].items())
    ]
    return document, receipts


def extracted_path(root: Path, top_directory: str, member_name: str) -> Path:
    relative = PurePosixPath(member_name).relative_to(PurePosixPath(top_directory))
    return root.joinpath(*relative.parts)


def import_entry(name: str, entry: dict[str, Any], archive_root: Path, stage: Path) -> None:
    target_root = stage / name
    target_root.mkdir(mode=0o755)
    directories: list[tuple[Path, int]] = []
    symlinks: list[tuple[Path, str]] = []
    hardlinks: list[tuple[Path, Path]] = []
    archive_path = archive_root / entry["filename"]
    with tarfile.open(archive_path, mode="r:*") as archive:
        for member in archive:
            normalized = normalized_member_name(member.name, entry["top_directory"])
            if normalized == entry["top_directory"]:
                directories.append((target_root, member.mode & 0o7777))
                continue
            destination = extracted_path(target_root, entry["top_directory"], normalized)
            kind = member_kind(member)
            if kind == "directory":
                destination.mkdir(parents=True, exist_ok=False)
                directories.append((destination, member.mode & 0o7777))
            elif kind == "file":
                destination.parent.mkdir(parents=True, exist_ok=True)
                source = archive.extractfile(member)
                if source is None:
                    raise ReleaseError(f"cannot read archive member: {normalized}")
                with source, destination.open("xb") as output:
                    shutil.copyfileobj(source, output, CHUNK_SIZE)
                destination.chmod(member.mode & 0o7777)
            elif kind == "symlink":
                normalized_link_target(normalized, member.linkname, entry["top_directory"])
                destination.parent.mkdir(parents=True, exist_ok=True)
                symlinks.append((destination, member.linkname))
            elif kind == "hardlink":
                target_member = normalized_hardlink_target(
                    normalized, member.linkname, entry["top_directory"]
                )
                destination.parent.mkdir(parents=True, exist_ok=True)
                hardlinks.append(
                    (
                        destination,
                        extracted_path(target_root, entry["top_directory"], target_member),
                    )
                )
    for destination, target in hardlinks:
        if not target.is_file():
            raise ReleaseError(f"hardlink target was not imported: {target}")
        os.link(target, destination)
    for destination, link_name in symlinks:
        os.symlink(link_name, destination)
    for directory, mode in sorted(directories, key=lambda item: len(item[0].parts), reverse=True):
        directory.chmod(mode)


def verify_imported_entry(
    name: str,
    entry: dict[str, Any],
    archive_root: Path,
    destination: Path,
) -> None:
    target_root = destination / name
    if not target_root.is_dir() or target_root.is_symlink():
        raise ReleaseError(f"imported source root is missing: {target_root}")
    expected_paths: set[Path] = {target_root}
    archive_path = archive_root / entry["filename"]
    with tarfile.open(archive_path, mode="r:*") as archive:
        for member in archive:
            normalized = normalized_member_name(member.name, entry["top_directory"])
            target = extracted_path(target_root, entry["top_directory"], normalized)
            current = target
            while current != target_root:
                expected_paths.add(current)
                current = current.parent
            expected_paths.add(target_root)
            kind = member_kind(member)
            if normalized == entry["top_directory"]:
                target = target_root
            if kind == "directory":
                if not target.is_dir() or target.is_symlink():
                    raise ReleaseError(f"imported directory mismatch: {target}")
                observed_mode = stat.S_IMODE(os.stat(target).st_mode)
                if observed_mode != (member.mode & 0o7777):
                    raise ReleaseError(f"imported directory mode mismatch: {target}")
            elif kind == "file":
                if not target.is_file() or target.is_symlink():
                    raise ReleaseError(f"imported file mismatch: {target}")
                observed_sha, observed_size = sha256_file(target)
                source = archive.extractfile(member)
                if source is None:
                    raise ReleaseError(f"cannot read archive member: {normalized}")
                with source:
                    expected_sha, expected_size = sha256_stream(source)
                observed_mode = stat.S_IMODE(os.stat(target).st_mode)
                if (observed_sha, observed_size, observed_mode) != (
                    expected_sha,
                    expected_size,
                    member.mode & 0o7777,
                ):
                    raise ReleaseError(f"imported file content or mode mismatch: {target}")
            elif kind == "symlink":
                if not target.is_symlink() or os.readlink(target) != member.linkname:
                    raise ReleaseError(f"imported symlink mismatch: {target}")
            elif kind == "hardlink":
                linked_member = normalized_hardlink_target(
                    normalized, member.linkname, entry["top_directory"]
                )
                linked_target = extracted_path(
                    target_root, entry["top_directory"], linked_member
                )
                target_stat = os.stat(target)
                linked_stat = os.stat(linked_target)
                if (target_stat.st_dev, target_stat.st_ino) != (
                    linked_stat.st_dev,
                    linked_stat.st_ino,
                ):
                    raise ReleaseError(f"imported hardlink mismatch: {target}")

    observed_paths: set[Path] = {target_root}
    for directory, directory_names, file_names in os.walk(target_root, followlinks=False):
        directory_path = Path(directory)
        for item in (*directory_names, *file_names):
            observed_paths.add(directory_path / item)
    extras = sorted(str(path) for path in observed_paths - expected_paths)
    if extras:
        raise ReleaseError(f"imported source contains undeclared paths: {extras[0]}")


def verify_imported_lock(
    lock_path: Path,
    archive_root: Path,
    destination: Path,
) -> list[dict[str, Any]]:
    document, receipts = verify_lock(lock_path, archive_root)
    for name, entry in sorted(document["archives"].items()):
        verify_imported_entry(name, entry, archive_root, destination)
    return receipts


def import_lock(
    lock_path: Path,
    archive_root: Path,
    destination: Path,
) -> list[dict[str, Any]]:
    document, receipts = verify_lock(lock_path, archive_root)
    if destination.exists() or destination.is_symlink():
        raise ReleaseError(f"release source destination already exists: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".release-import.", dir=destination.parent))
    try:
        for name, entry in sorted(document["archives"].items()):
            import_entry(name, entry, archive_root, stage)
            verify_imported_entry(name, entry, archive_root, stage)
        os.replace(stage, destination)
    except BaseException:
        shutil.rmtree(stage, ignore_errors=True)
        raise
    return receipts


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify and import pinned release archives.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    inspect_parser = subparsers.add_parser("inspect")
    inspect_parser.add_argument("--archive", required=True)
    inspect_parser.add_argument("--top-directory", required=True)

    verify_parser = subparsers.add_parser("verify")
    verify_parser.add_argument("--lock", required=True)
    verify_parser.add_argument("--archive-root", required=True)

    import_parser = subparsers.add_parser("import")
    import_parser.add_argument("--lock", required=True)
    import_parser.add_argument("--archive-root", required=True)
    import_parser.add_argument("--destination", required=True)

    verify_import_parser = subparsers.add_parser("verify-import")
    verify_import_parser.add_argument("--lock", required=True)
    verify_import_parser.add_argument("--archive-root", required=True)
    verify_import_parser.add_argument("--destination", required=True)
    return parser.parse_args(argv)


def emit_receipt(command: str, receipts: list[dict[str, Any]]) -> None:
    print(
        json.dumps(
            {"schema": RECEIPT_SCHEMA, "command": command, "archives": receipts},
            indent=2,
            sort_keys=True,
        )
    )


def main(argv: Sequence[str]) -> int:
    arguments = parse_arguments(argv)
    if arguments.command == "inspect":
        result = inspect_archive(Path(arguments.archive), arguments.top_directory)
        result.pop("file_sha256")
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    if arguments.command == "verify":
        _, receipts = verify_lock(Path(arguments.lock), Path(arguments.archive_root))
        emit_receipt("verify", receipts)
        return 0
    if arguments.command == "import":
        receipts = import_lock(
            Path(arguments.lock),
            Path(arguments.archive_root),
            Path(arguments.destination),
        )
        emit_receipt("import", receipts)
        return 0
    if arguments.command == "verify-import":
        receipts = verify_imported_lock(
            Path(arguments.lock),
            Path(arguments.archive_root),
            Path(arguments.destination),
        )
        emit_receipt("verify-import", receipts)
        return 0
    raise ReleaseError(f"unknown command: {arguments.command}")


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except ReleaseError as error:
        print(f"release-assets: {error}", file=sys.stderr)
        raise SystemExit(1) from error
