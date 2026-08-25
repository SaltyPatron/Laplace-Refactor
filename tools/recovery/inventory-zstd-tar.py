#!/usr/bin/env python3
"""Create a content inventory for a zstd- or gzip-compressed tar artifact."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tarfile
import tempfile
from typing import Any, BinaryIO, Iterable


ZSTD_SCHEMA = "laplace.zstd-tar-content-inventory/v1"
GZIP_SCHEMA = "laplace.gzip-tar-content-inventory/v1"


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_stream(stream: BinaryIO) -> str:
    digest = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(chunk)
    return digest.hexdigest()


def member_kind(member: tarfile.TarInfo) -> str:
    if member.isfile():
        return "file"
    if member.isdir():
        return "directory"
    if member.issym():
        return "symbolic-link"
    if member.islnk():
        return "hard-link"
    if member.ischr():
        return "character-device"
    if member.isblk():
        return "block-device"
    if member.isfifo():
        return "fifo"
    return "other"


def inventory(archive: pathlib.Path) -> dict[str, Any]:
    if archive.name.endswith((".tar.zst", ".tzst", ".zst")):
        decoder = ["zstd", "-q", "-d", "-c", str(archive)]
        decoder_name = "zstd"
        schema = ZSTD_SCHEMA
    elif archive.name.endswith((".tar.gz", ".tgz", ".gz")):
        decoder = ["gzip", "-d", "-c", str(archive)]
        decoder_name = "gzip"
        schema = GZIP_SCHEMA
    else:
        raise ValueError(f"unsupported compressed tar suffix: {archive}")
    process = subprocess.Popen(
        decoder, stdout=subprocess.PIPE
    )
    if process.stdout is None:
        raise RuntimeError("zstd stdout was not created")
    entries: list[dict[str, Any]] = []
    kinds: collections.Counter[str] = collections.Counter()
    extensions: collections.Counter[str] = collections.Counter()
    content_paths: dict[str, list[str]] = collections.defaultdict(list)
    read_errors: list[dict[str, str]] = []
    try:
        with tarfile.open(fileobj=process.stdout, mode="r|") as archive_stream:
            for member in archive_stream:
                kind = member_kind(member)
                kinds[kind] += 1
                record: dict[str, Any] = {
                    "path": member.name,
                    "kind": kind,
                    "mode": member.mode,
                    "uid": member.uid,
                    "gid": member.gid,
                    "mtime": member.mtime,
                    "size": member.size,
                }
                if member.linkname:
                    record["link_target"] = member.linkname
                if member.isfile():
                    suffix = pathlib.PurePosixPath(member.name).suffix.lower() or "[no-extension]"
                    extensions[suffix] += 1
                    stream = archive_stream.extractfile(member)
                    if stream is None:
                        read_errors.append(
                            {"path": member.name, "error": "regular member has no stream"}
                        )
                    else:
                        digest = sha256_stream(stream)
                        record["sha256"] = digest
                        content_paths[digest].append(member.name)
                entries.append(record)
    except (tarfile.TarError, OSError) as error:
        read_errors.append({"path": "[archive]", "error": str(error)})
    finally:
        process.stdout.close()
    return_code = process.wait()
    if return_code != 0:
        read_errors.append(
            {"path": f"[{decoder_name}]", "error": f"decoder exited with {return_code}"}
        )
    duplicate_groups = [
        {"sha256": digest, "count": len(paths), "paths": paths}
        for digest, paths in content_paths.items()
        if len(paths) > 1
    ]
    duplicate_groups.sort(key=lambda item: (-item["count"], item["sha256"]))
    return {
        "schema": schema,
        "archive": str(archive),
        "archive_bytes": archive.stat().st_size,
        "archive_sha256": sha256_file(archive),
        "summary": {
            "entry_count": len(entries),
            "regular_file_count": kinds["file"],
            "regular_file_bytes": sum(
                entry["size"] for entry in entries if entry["kind"] == "file"
            ),
            "unique_regular_content_count": len(content_paths),
            "duplicate_content_group_count": len(duplicate_groups),
            "duplicate_regular_file_count": sum(item["count"] for item in duplicate_groups),
            "jsonl_file_count": extensions[".jsonl"],
            "json_file_count": extensions[".json"],
            "sql_file_count": extensions[".sql"],
            "read_error_count": len(read_errors),
        },
        "kind_counts": dict(sorted(kinds.items())),
        "extension_counts": dict(
            sorted(extensions.items(), key=lambda item: (-item[1], item[0]))
        ),
        "read_errors": read_errors,
        "duplicate_content_groups": duplicate_groups,
        "entries": entries,
    }


def write_manifest(path: pathlib.Path, manifest: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = (json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode(
        "utf-8"
    )
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary_path = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, path)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


def parse_arguments(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", required=True)
    parser.add_argument("--manifest", required=True)
    return parser.parse_args(list(argv))


def main(argv: Iterable[str]) -> int:
    try:
        arguments = parse_arguments(argv)
        archive = pathlib.Path(arguments.archive).resolve()
        manifest_path = pathlib.Path(arguments.manifest).resolve()
        manifest = inventory(archive)
        write_manifest(manifest_path, manifest)
        print(json.dumps(manifest["summary"], sort_keys=True))
        print(f"manifest_sha256={sha256_file(manifest_path)}")
        return 2 if manifest["summary"]["read_error_count"] else 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
