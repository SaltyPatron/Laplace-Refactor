#!/usr/bin/env python3
"""Inventory Claude file-history bodies and bind them to session evidence."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
from typing import Any, Iterable


SCHEMA = "laplace.claude-file-history-recovery/v1"


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_json_hash(value: Any) -> str:
    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def run_git(repository: pathlib.Path, arguments: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def git_object_set(repository: pathlib.Path, revision: str) -> set[str]:
    process = subprocess.Popen(
        ["git", "-C", str(repository), "rev-list", "--objects", revision],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert process.stdout is not None
    objects: set[str] = set()
    for line in process.stdout:
        object_id = line.split(" ", 1)[0].strip()
        if object_id:
            objects.add(object_id)
    stderr = process.stderr.read() if process.stderr is not None else ""
    return_code = process.wait()
    if return_code != 0:
        raise RuntimeError(f"git rev-list {revision} failed: {stderr.strip()}")
    return objects


def session_for_log(project_directory: pathlib.Path, log_path: pathlib.Path) -> str:
    relative = log_path.relative_to(project_directory)
    first = relative.parts[0]
    return first[:-6] if first.endswith(".jsonl") else first


def normalize_repo_path(repository: pathlib.Path, original: pathlib.Path) -> str:
    try:
        return original.resolve(strict=False).relative_to(repository.resolve()).as_posix()
    except ValueError:
        return ""


def canonical_worktree_path(repository: pathlib.Path, repo_relative: str) -> pathlib.Path:
    parts = pathlib.PurePosixPath(repo_relative).parts
    if len(parts) >= 3 and parts[0] == ".worktrees":
        return repository.joinpath(*parts[2:])
    return repository.joinpath(*parts)


def classify_domain(repo_relative: str) -> str:
    parts = pathlib.PurePosixPath(repo_relative).parts
    if len(parts) >= 3 and parts[0] == ".worktrees":
        parts = parts[2:]
    lowered = "/".join(parts).lower()
    suffix = pathlib.PurePosixPath(lowered).suffix
    if suffix == ".sql" or any(part in {"sql", "schema", "database", "db"} for part in parts):
        return "sql"
    if suffix in {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp"} or (
        parts and parts[0] == "engine"
    ):
        return "native"
    if suffix in {".cs", ".csproj", ".fs", ".fsproj"} or (parts and parts[0] == "app"):
        return "managed"
    if "test" in lowered:
        return "tests"
    if suffix in {".md", ".rst", ".adoc"} or (parts and parts[0] == "docs"):
        return "documentation"
    if parts and parts[0] in {"scripts", "tools", "cmake", ".github"}:
        return "delivery"
    return "other"


def file_state(path: pathlib.Path) -> dict[str, Any]:
    if not path.is_file():
        return {"exists": False, "bytes": 0, "sha256": ""}
    return {
        "exists": True,
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def add_reference(
    references: dict[tuple[str, str, str, int], dict[str, Any]],
    session_id: str,
    source_log: str,
    source_line: int,
    observation_kind: str,
    message_id: str,
    snapshot_message_id: str,
    tracking_path: str,
    descriptor: dict[str, Any],
) -> None:
    backup_value = descriptor.get("backupFileName")
    backup_name = backup_value if isinstance(backup_value, str) else ""
    version_value = descriptor.get("version", 0)
    version = int(version_value) if isinstance(version_value, int) else 0
    if not backup_name or not tracking_path:
        return
    key = (session_id, tracking_path, backup_name, version)
    entry = references.setdefault(
        key,
        {
            "session_id": session_id,
            "tracking_path": tracking_path,
            "backup_file_name": backup_name,
            "version": version,
            "backup_time": str(descriptor.get("backupTime", "")),
            "real_parent_directory": str(descriptor.get("realParentDir", "")),
            "observations": [],
        },
    )
    observation = {
        "kind": observation_kind,
        "message_id": message_id,
        "snapshot_message_id": snapshot_message_id,
        "source_log": source_log,
        "source_line": source_line,
    }
    if observation not in entry["observations"]:
        entry["observations"].append(observation)


def inventory(arguments: argparse.Namespace) -> tuple[dict[str, Any], int]:
    claude_root = pathlib.Path(arguments.claude_root).resolve()
    repository = pathlib.Path(arguments.repository).resolve()
    project_directory = claude_root / "projects" / arguments.project_key
    file_history_root = claude_root / "file-history"

    if not project_directory.is_dir():
        raise RuntimeError(f"Claude project directory does not exist: {project_directory}")
    if run_git(repository, ["rev-parse", "--is-inside-work-tree"]).stdout.strip() != "true":
        raise RuntimeError(f"not a Git working tree: {repository}")

    logs = sorted(project_directory.rglob("*.jsonl"))
    references: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    source_records: list[dict[str, Any]] = []
    record_type_counts: collections.Counter[str] = collections.Counter()
    malformed_records: list[dict[str, Any]] = []
    snapshot_count = 0
    delta_count = 0

    for log_path in logs:
        session_id = session_for_log(project_directory, log_path)
        type_counts: collections.Counter[str] = collections.Counter()
        record_count = 0
        relative_log = log_path.relative_to(project_directory).as_posix()
        with log_path.open("r", encoding="utf-8", errors="strict") as stream:
            for line_number, raw_line in enumerate(stream, start=1):
                if not raw_line.strip():
                    continue
                record_count += 1
                try:
                    record = json.loads(raw_line)
                except json.JSONDecodeError as error:
                    malformed_records.append(
                        {
                            "source_log": relative_log,
                            "source_line": line_number,
                            "error": str(error),
                            "line_sha256": hashlib.sha256(raw_line.encode("utf-8")).hexdigest(),
                        }
                    )
                    continue
                record_type = str(record.get("type", ""))
                type_counts[record_type] += 1
                record_type_counts[record_type] += 1
                if record_type == "file-history-snapshot":
                    snapshot_count += 1
                    snapshot = record.get("snapshot") or {}
                    tracked = snapshot.get("trackedFileBackups") or {}
                    for tracking_path, descriptor in sorted(tracked.items()):
                        if isinstance(descriptor, dict):
                            add_reference(
                                references,
                                session_id,
                                relative_log,
                                line_number,
                                "snapshot",
                                str(record.get("messageId", "")),
                                str(snapshot.get("messageId", "")),
                                str(tracking_path),
                                descriptor,
                            )
                elif record_type == "file-history-delta":
                    delta_count += 1
                    descriptor = record.get("backup") or {}
                    if isinstance(descriptor, dict):
                        add_reference(
                            references,
                            session_id,
                            relative_log,
                            line_number,
                            "delta",
                            str(record.get("messageId", "")),
                            str(record.get("snapshotMessageId", "")),
                            str(record.get("trackingPath", "")),
                            descriptor,
                        )
        source_records.append(
            {
                "path": relative_log,
                "session_id": session_id,
                "bytes": log_path.stat().st_size,
                "sha256": sha256_file(log_path),
                "record_count": record_count,
                "record_type_counts": dict(sorted(type_counts.items())),
            }
        )

    all_ref_objects = git_object_set(repository, "--all")
    baseline_objects = git_object_set(repository, arguments.baseline)
    versions: list[dict[str, Any]] = []
    referenced_backups: set[pathlib.Path] = set()

    for key in sorted(references):
        entry = references[key]
        backup_path = file_history_root / entry["session_id"] / entry["backup_file_name"]
        referenced_backups.add(backup_path)
        tracking = pathlib.PurePosixPath(entry["tracking_path"])
        parent_text = entry["real_parent_directory"]
        original_path = (
            pathlib.Path(parent_text) / tracking.name
            if parent_text
            else repository.joinpath(*tracking.parts)
        )
        repo_relative = normalize_repo_path(repository, original_path)
        canonical_path = canonical_worktree_path(repository, repo_relative) if repo_relative else original_path
        backup_state = file_state(backup_path)
        blob_id = ""
        if backup_state["exists"]:
            hash_result = run_git(repository, ["hash-object", "--no-filters", str(backup_path)])
            if hash_result.returncode != 0:
                raise RuntimeError(hash_result.stderr.strip())
            blob_id = hash_result.stdout.strip()
        original_state = file_state(original_path)
        canonical_state = file_state(canonical_path)
        versions.append(
            {
                **entry,
                "observations": sorted(
                    entry["observations"],
                    key=lambda item: (
                        item["source_log"], item["source_line"], item["kind"], item["message_id"]
                    ),
                ),
                "backup_path": str(backup_path),
                "backup_exists": backup_state["exists"],
                "content_bytes": backup_state["bytes"],
                "content_sha256": backup_state["sha256"],
                "git_blob_id": blob_id,
                "git_object_present": bool(blob_id and run_git(repository, ["cat-file", "-e", f"{blob_id}^{{blob}}" ]).returncode == 0),
                "reachable_from_any_ref": blob_id in all_ref_objects,
                "reachable_from_baseline": blob_id in baseline_objects,
                "original_path": str(original_path),
                "repository_relative_path": repo_relative,
                "canonical_repository_path": str(canonical_path),
                "domain": classify_domain(repo_relative),
                "original_file_state": original_state,
                "canonical_file_state": canonical_state,
                "matches_original_file": bool(
                    backup_state["exists"]
                    and original_state["exists"]
                    and backup_state["sha256"] == original_state["sha256"]
                ),
                "matches_canonical_file": bool(
                    backup_state["exists"]
                    and canonical_state["exists"]
                    and backup_state["sha256"] == canonical_state["sha256"]
                ),
            }
        )

    all_backup_files = sorted(path for path in file_history_root.rglob("*") if path.is_file())
    orphan_backups = []
    for backup_path in all_backup_files:
        if backup_path not in referenced_backups:
            orphan_backups.append(
                {
                    "path": str(backup_path),
                    "bytes": backup_path.stat().st_size,
                    "sha256": sha256_file(backup_path),
                }
            )

    missing_versions = [version for version in versions if not version["backup_exists"]]
    unique_content = {
        version["content_sha256"]
        for version in versions
        if version["backup_exists"] and version["content_sha256"]
    }
    unique_not_git = {
        version["content_sha256"]
        for version in versions
        if version["backup_exists"] and not version["git_object_present"]
    }
    domains = collections.Counter(version["domain"] for version in versions)
    summary = {
        "source_log_count": len(source_records),
        "source_record_count": sum(source["record_count"] for source in source_records),
        "malformed_record_count": len(malformed_records),
        "snapshot_record_count": snapshot_count,
        "delta_record_count": delta_count,
        "referenced_version_count": len(versions),
        "distinct_tracking_path_count": len({version["tracking_path"] for version in versions}),
        "backup_file_count": len(all_backup_files),
        "referenced_backup_present_count": len(versions) - len(missing_versions),
        "referenced_backup_missing_count": len(missing_versions),
        "orphan_backup_count": len(orphan_backups),
        "unique_content_count": len(unique_content),
        "unique_content_absent_from_git_object_database_count": len(unique_not_git),
        "version_absent_from_all_refs_count": sum(
            1 for version in versions if version["backup_exists"] and not version["reachable_from_any_ref"]
        ),
        "version_absent_from_baseline_count": sum(
            1 for version in versions if version["backup_exists"] and not version["reachable_from_baseline"]
        ),
        "version_matching_original_file_count": sum(1 for version in versions if version["matches_original_file"]),
        "version_matching_canonical_file_count": sum(1 for version in versions if version["matches_canonical_file"]),
        "original_path_missing_count": sum(
            1 for version in versions if not version["original_file_state"]["exists"]
        ),
        "domain_version_counts": dict(sorted(domains.items())),
    }

    manifest = {
        "schema": SCHEMA,
        "claude_root": str(claude_root),
        "project_key": arguments.project_key,
        "project_directory": str(project_directory),
        "repository": str(repository),
        "baseline": arguments.baseline,
        "input_fingerprint": canonical_json_hash(
            [{"path": source["path"], "sha256": source["sha256"]} for source in source_records]
        ),
        "summary": summary,
        "record_type_counts": dict(sorted(record_type_counts.items())),
        "sources": source_records,
        "malformed_records": malformed_records,
        "versions": versions,
        "orphan_backups": orphan_backups,
    }
    return manifest, 2 if missing_versions or malformed_records else 0


def write_manifest(path: pathlib.Path, manifest: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = (json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
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
    parser.add_argument("--claude-root", required=True)
    parser.add_argument("--project-key", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args(list(argv))


def main(argv: Iterable[str]) -> int:
    try:
        arguments = parse_arguments(argv)
        manifest, return_code = inventory(arguments)
        write_manifest(pathlib.Path(arguments.output).resolve(), manifest)
        print(json.dumps(manifest["summary"], sort_keys=True))
        return return_code
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
