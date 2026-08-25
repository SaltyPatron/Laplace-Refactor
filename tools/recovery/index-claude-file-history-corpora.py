#!/usr/bin/env python3
"""Recover session-scoped Claude file-history state from verified corpora.

The index treats a transcript record, a file-history reference, an archived body,
and a Git object as separate evidence.  Backup names are never resolved without
their session identifier.  Repeated archive copies remain source occurrences;
conflicting bodies remain explicit content variants.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from typing import Any, Iterable


SCHEMA = "laplace.recovery.claude-file-history-corpora/v1"
REFERENCE_SCHEMA = "laplace.recovery.claude-file-history-reference/v1"
SESSION_PATTERN = re.compile(r"^[0-9a-fA-F-]{16,}$")
FILE_HISTORY_SUFFIX = re.compile(r"(?:^|/)file-history/([^/]+)/([^/]+)$")


@dataclass(frozen=True)
class ArchiveBinding:
    manifest_path: pathlib.Path
    live_root: pathlib.Path
    label: str


def canonical_json_bytes(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json_object(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise RuntimeError(f"expected JSON object: {path}")
    return value


def parse_archive_binding(value: str) -> ArchiveBinding:
    parts = value.split("::")
    if len(parts) not in {2, 3}:
        raise argparse.ArgumentTypeError(
            "archive binding must be MANIFEST::LIVE_ROOT or LABEL::MANIFEST::LIVE_ROOT"
        )
    if len(parts) == 2:
        manifest_text, live_root_text = parts
        label = pathlib.Path(manifest_text).stem
    else:
        label, manifest_text, live_root_text = parts
    if not label or not manifest_text or not live_root_text:
        raise argparse.ArgumentTypeError("archive binding components cannot be empty")
    return ArchiveBinding(
        manifest_path=pathlib.Path(manifest_text).resolve(),
        live_root=pathlib.Path(live_root_text).resolve(),
        label=label,
    )


def select_source(group: dict[str, Any]) -> pathlib.Path:
    expected_bytes = int(group["bytes"])
    for value in group.get("paths", []):
        path = pathlib.Path(str(value))
        if path.is_file() and path.stat().st_size == expected_bytes:
            return path
    raise RuntimeError(
        f"no readable source for content {group.get('content_sha256', '<missing>')}"
    )


def session_from_source_path(value: str) -> str:
    parts = [part for part in value.replace("\\", "/").split("/") if part]
    project_positions = [index for index, part in enumerate(parts) if part == "projects"]
    for index in reversed(project_positions):
        if index + 2 >= len(parts):
            continue
        candidate = parts[index + 2]
        if candidate.endswith(".jsonl"):
            candidate = candidate[:-6]
        if candidate and SESSION_PATTERN.fullmatch(candidate):
            return candidate
    return ""


def source_occurrences(group: dict[str, Any]) -> list[dict[str, str]]:
    return [
        {"path": str(value), "session_id": session_from_source_path(str(value))}
        for value in sorted(str(item) for item in group.get("paths", []))
    ]


def normalize_observation(value: dict[str, Any]) -> dict[str, Any]:
    return {key: value[key] for key in sorted(value)}


def descriptor_version(descriptor: dict[str, Any]) -> int | None:
    value = descriptor.get("version")
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def add_reference(
    references: dict[tuple[str, str, str, str, int | None], dict[str, Any]],
    *,
    source_content_sha256: str,
    occurrences: list[dict[str, str]],
    session_id: str,
    source_line: int,
    record: dict[str, Any],
    kind: str,
    tracking_path: str,
    descriptor: dict[str, Any],
    record_timestamp: str,
    snapshot_message_id: str,
    snapshot_timestamp: str,
) -> None:
    backup_value = descriptor.get("backupFileName")
    backup_name = backup_value if isinstance(backup_value, str) and backup_value else None
    version = descriptor_version(descriptor)
    key = (
        source_content_sha256,
        session_id,
        tracking_path,
        backup_name or "<null>",
        version,
    )
    reference = references.setdefault(
        key,
        {
            "source_content_sha256": source_content_sha256,
            "source_occurrences": [
                occurrence for occurrence in occurrences if occurrence["session_id"] == session_id
            ],
            "session_id": session_id,
            "tracking_path": tracking_path,
            "backup_file_name": backup_name,
            "version": version,
            "backup_time": str(descriptor.get("backupTime", "")),
            "real_parent_directory": str(descriptor.get("realParentDir", "")),
            "descriptor_variants": [],
            "observations": [],
        },
    )
    descriptor_copy = json.loads(json.dumps(descriptor, ensure_ascii=False, sort_keys=True))
    if descriptor_copy not in reference["descriptor_variants"]:
        reference["descriptor_variants"].append(descriptor_copy)
    observation = normalize_observation(
        {
            "kind": kind,
            "source_line": source_line,
            "message_id": str(record.get("messageId", record.get("uuid", ""))),
            "snapshot_message_id": snapshot_message_id,
            "record_timestamp": record_timestamp,
            "snapshot_timestamp": snapshot_timestamp,
        }
    )
    if observation not in reference["observations"]:
        reference["observations"].append(observation)


def reference_id(reference: dict[str, Any]) -> str:
    identity = {
        "source_content_sha256": reference["source_content_sha256"],
        "session_id": reference["session_id"],
        "tracking_path": reference["tracking_path"],
        "backup_file_name": reference["backup_file_name"],
        "version": reference["version"],
    }
    return sha256_bytes(canonical_json_bytes(identity))


def store_object(directory: pathlib.Path, content: bytes) -> str:
    digest = sha256_bytes(content)
    path = directory / digest
    if path.exists():
        if path.read_bytes() != content:
            raise RuntimeError(f"content-address collision at {path}")
        return digest
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{digest}.", dir=directory)
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()
    return digest


def safe_archive_entry_path(value: str) -> pathlib.PurePosixPath:
    path = pathlib.PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise RuntimeError(f"unsafe archive entry path: {value}")
    return path


def archive_entries(
    bindings: list[ArchiveBinding],
) -> tuple[dict[tuple[str, str], list[dict[str, Any]]], list[dict[str, Any]]]:
    index: dict[tuple[str, str], list[dict[str, Any]]] = collections.defaultdict(list)
    inputs: list[dict[str, Any]] = []
    for binding in sorted(bindings, key=lambda item: (item.label, str(item.manifest_path))):
        manifest = load_json_object(binding.manifest_path)
        manifest_digest = sha256_file(binding.manifest_path)
        archive_value = str(manifest.get("archive", ""))
        archive_path = pathlib.Path(archive_value).resolve() if archive_value else None
        archive_schema = str(manifest.get("schema", ""))
        accepted = 0
        for entry in manifest.get("entries", []):
            if not isinstance(entry, dict) or entry.get("kind") != "file":
                continue
            entry_path = str(entry.get("path", ""))
            match = FILE_HISTORY_SUFFIX.search(entry_path.replace("\\", "/"))
            if not match:
                continue
            safe_archive_entry_path(entry_path.replace("\\", "/"))
            session_id, backup_name = match.groups()
            index[(session_id, backup_name)].append(
                {
                    "binding": binding,
                    "manifest_sha256": manifest_digest,
                    "archive_path": archive_path,
                    "archive_schema": archive_schema,
                    "entry_path": entry_path,
                    "expected_bytes": int(entry.get("size", 0)),
                    "expected_sha256": str(entry.get("sha256", "")),
                }
            )
            accepted += 1
        inputs.append(
            {
                "label": binding.label,
                "manifest_path": str(binding.manifest_path),
                "manifest_sha256": manifest_digest,
                "live_root": str(binding.live_root),
                "archive_path": str(archive_path) if archive_path else "",
                "archive_sha256": str(manifest.get("archive_sha256", "")),
                "archive_schema": archive_schema,
                "file_history_entry_count": accepted,
            }
        )
    for candidates in index.values():
        candidates.sort(key=lambda item: (item["binding"].label, item["entry_path"]))
    return index, inputs


def extract_archive_entry(candidate: dict[str, Any]) -> bytes:
    archive_path = candidate["archive_path"]
    if archive_path is None or not archive_path.is_file():
        raise RuntimeError("preserved archive is unavailable")
    command = ["tar"]
    if candidate["archive_schema"] == "laplace.zstd-tar-content-inventory/v1":
        command.append("--zstd")
    command.extend(["-xOf", str(archive_path), candidate["entry_path"]])
    result = subprocess.run(command, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.decode("utf-8", errors="replace").strip())
    return result.stdout


def read_candidate(candidate: dict[str, Any]) -> tuple[bytes | None, dict[str, Any]]:
    binding: ArchiveBinding = candidate["binding"]
    relative = safe_archive_entry_path(candidate["entry_path"].replace("\\", "/"))
    live_path = binding.live_root.joinpath(*relative.parts)
    expected_bytes = candidate["expected_bytes"]
    expected_digest = candidate["expected_sha256"]
    record = {
        "archive_label": binding.label,
        "archive_manifest_sha256": candidate["manifest_sha256"],
        "archive_entry_path": candidate["entry_path"],
        "live_path": str(live_path),
        "expected_bytes": expected_bytes,
        "expected_sha256": expected_digest,
        "source": "",
        "status": "",
    }
    if live_path.is_file():
        content = live_path.read_bytes()
        if len(content) == expected_bytes and sha256_bytes(content) == expected_digest:
            record.update({"source": "live-archive-tree", "status": "verified"})
            return content, record
        record["live_source_status"] = "content-mismatch"
    try:
        content = extract_archive_entry(candidate)
    except RuntimeError as error:
        record.update({"source": "", "status": "unavailable", "error": str(error)})
        return None, record
    observed_digest = sha256_bytes(content)
    if len(content) != expected_bytes or observed_digest != expected_digest:
        raise RuntimeError(
            "archive entry verification failed for "
            f"{candidate['entry_path']}: expected {expected_bytes}/{expected_digest}, "
            f"observed {len(content)}/{observed_digest}"
        )
    record.update({"source": "preserved-archive", "status": "verified"})
    return content, record


def git_output(repository: pathlib.Path, arguments: list[str]) -> str:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(arguments)} failed: {result.stderr.strip()}")
    return result.stdout


def git_object_set(repository: pathlib.Path, revision: str) -> set[str]:
    output = git_output(repository, ["rev-list", "--objects", revision])
    return {line.split(" ", 1)[0] for line in output.splitlines() if line.strip()}


def git_blob_id(content: bytes, object_format: str) -> str:
    if object_format not in {"sha1", "sha256"}:
        raise RuntimeError(f"unsupported Git object format: {object_format}")
    constructor = hashlib.sha1 if object_format == "sha1" else hashlib.sha256
    digest = constructor()
    digest.update(f"blob {len(content)}\0".encode("ascii"))
    digest.update(content)
    return digest.hexdigest()


def git_object_presence(repository: pathlib.Path, object_ids: set[str]) -> set[str]:
    if not object_ids:
        return set()
    result = subprocess.run(
        ["git", "-C", str(repository), "cat-file", "--batch-check"],
        input="".join(f"{object_id}\n" for object_id in sorted(object_ids)),
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise RuntimeError(f"git cat-file failed: {result.stderr.strip()}")
    present: set[str] = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[1] != "missing":
            present.add(fields[0])
    return present


def posix_text(value: str) -> str:
    return value.replace("\\", "/")


def strip_worktree_prefix(value: str) -> str:
    parts = pathlib.PurePosixPath(value).parts
    if len(parts) >= 3 and parts[0] == ".worktrees":
        parts = parts[2:]
    return pathlib.PurePosixPath(*parts).as_posix() if parts else ""


def repository_relative_path(
    tracking_path: str,
    repository: pathlib.Path,
    windows_repository_roots: list[str],
) -> str:
    normalized = posix_text(tracking_path).strip()
    if not normalized:
        return ""
    repository_text = repository.resolve().as_posix().rstrip("/")
    if normalized == repository_text:
        return ""
    if normalized.startswith(repository_text + "/"):
        return strip_worktree_prefix(normalized[len(repository_text) + 1 :])
    lowered = normalized.lower().rstrip("/")
    for root in windows_repository_roots:
        root_normalized = posix_text(root).rstrip("/")
        root_lowered = root_normalized.lower()
        if lowered.startswith(root_lowered + "/"):
            return strip_worktree_prefix(normalized[len(root_normalized) + 1 :])
    if re.match(r"^[A-Za-z]:/", normalized) or normalized.startswith("/"):
        return ""
    while normalized.startswith("./"):
        normalized = normalized[2:]
    if ".." in pathlib.PurePosixPath(normalized).parts:
        return ""
    return strip_worktree_prefix(normalized)


def classify_domain(repo_relative: str) -> str:
    parts = pathlib.PurePosixPath(repo_relative).parts
    lowered = repo_relative.lower()
    suffix = pathlib.PurePosixPath(lowered).suffix
    if suffix == ".sql" or any(part.lower() in {"sql", "schema", "database", "db"} for part in parts):
        return "sql"
    if suffix in {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp"} or (
        parts and parts[0].lower() == "engine"
    ):
        return "native"
    if suffix in {".cs", ".csproj", ".fs", ".fsproj"} or (
        parts and parts[0].lower() == "app"
    ):
        return "managed"
    if "test" in lowered:
        return "tests"
    if suffix in {".md", ".rst", ".adoc"} or (parts and parts[0].lower() == "docs"):
        return "documentation"
    if parts and parts[0].lower() in {"scripts", "tools", "cmake", ".github"}:
        return "delivery"
    return "other"


def build_index(arguments: argparse.Namespace) -> dict[str, Any]:
    corpora_path = pathlib.Path(arguments.corpora_manifest).resolve()
    repository = pathlib.Path(arguments.repository).resolve()
    destination = pathlib.Path(arguments.output_directory).resolve()
    baseline = str(arguments.baseline)
    bindings = list(arguments.archive_binding)
    windows_roots = list(arguments.windows_repository_root)
    if destination.exists():
        raise RuntimeError(f"output already exists: {destination}")
    if git_output(repository, ["rev-parse", "--is-inside-work-tree"]).strip() != "true":
        raise RuntimeError(f"not a Git working tree: {repository}")
    corpora = load_json_object(corpora_path)
    groups = sorted(
        corpora.get("content_groups", []), key=lambda item: str(item.get("content_sha256", ""))
    )
    if not groups:
        raise RuntimeError("corpora manifest contains no content groups")

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = pathlib.Path(tempfile.mkdtemp(prefix=f".{destination.name}.", dir=destination.parent))
    objects = temporary / "objects"
    objects.mkdir()
    references: dict[tuple[str, str, str, str, int | None], dict[str, Any]] = {}
    sources: list[dict[str, Any]] = []
    malformed_records: list[dict[str, Any]] = []
    invalid_references: list[dict[str, Any]] = []
    record_type_counts: collections.Counter[str] = collections.Counter()
    try:
        candidate_index, archive_inputs = archive_entries(bindings)
        for group in groups:
            expected_digest = str(group["content_sha256"])
            expected_bytes = int(group["bytes"])
            path = select_source(group)
            occurrences = source_occurrences(group)
            sessions = sorted({item["session_id"] for item in occurrences if item["session_id"]})
            if not sessions:
                raise RuntimeError(f"cannot derive a session identifier for {expected_digest}")
            digest = hashlib.sha256()
            observed_bytes = 0
            record_count = 0
            with path.open("rb") as stream:
                for source_line, raw in enumerate(stream, start=1):
                    digest.update(raw)
                    observed_bytes += len(raw)
                    if not raw.strip():
                        continue
                    record_count += 1
                    try:
                        record = json.loads(raw)
                    except (UnicodeDecodeError, json.JSONDecodeError) as error:
                        malformed_records.append(
                            {
                                "source_content_sha256": expected_digest,
                                "source_line": source_line,
                                "line_sha256": sha256_bytes(raw),
                                "error": str(error),
                            }
                        )
                        continue
                    if not isinstance(record, dict):
                        continue
                    kind = str(record.get("type", ""))
                    record_type_counts[kind] += 1
                    descriptors: list[tuple[str, str, dict[str, Any], str, str]] = []
                    if kind == "file-history-snapshot":
                        snapshot = record.get("snapshot")
                        if not isinstance(snapshot, dict):
                            continue
                        tracked = snapshot.get("trackedFileBackups")
                        if not isinstance(tracked, dict):
                            continue
                        snapshot_message = str(snapshot.get("messageId", ""))
                        snapshot_time = str(snapshot.get("timestamp", ""))
                        for tracking_path, descriptor in sorted(tracked.items()):
                            if isinstance(descriptor, dict):
                                descriptors.append(
                                    ("snapshot", str(tracking_path), descriptor, snapshot_message, snapshot_time)
                                )
                    elif kind == "file-history-delta":
                        descriptor = record.get("backup")
                        if isinstance(descriptor, dict):
                            descriptors.append(
                                (
                                    "delta",
                                    str(record.get("trackingPath", "")),
                                    descriptor,
                                    str(record.get("snapshotMessageId", "")),
                                    "",
                                )
                            )
                    for observation_kind, tracking_path, descriptor, snapshot_message, snapshot_time in descriptors:
                        if not tracking_path:
                            invalid_references.append(
                                {
                                    "source_content_sha256": expected_digest,
                                    "source_line": source_line,
                                    "kind": observation_kind,
                                    "reason": "missing-tracking-path",
                                }
                            )
                            continue
                        for session_id in sessions:
                            add_reference(
                                references,
                                source_content_sha256=expected_digest,
                                occurrences=occurrences,
                                session_id=session_id,
                                source_line=source_line,
                                record=record,
                                kind=observation_kind,
                                tracking_path=tracking_path,
                                descriptor=descriptor,
                                record_timestamp=str(record.get("timestamp", "")),
                                snapshot_message_id=snapshot_message,
                                snapshot_timestamp=snapshot_time,
                            )
            actual_digest = digest.hexdigest()
            if observed_bytes != expected_bytes or actual_digest != expected_digest:
                raise RuntimeError(
                    "source verification failed for "
                    f"{path}: expected {expected_bytes}/{expected_digest}, "
                    f"observed {observed_bytes}/{actual_digest}"
                )
            sources.append(
                {
                    "content_sha256": expected_digest,
                    "bytes": observed_bytes,
                    "record_count": record_count,
                    "selected_path": str(path),
                    "source_occurrences": occurrences,
                    "corpora": sorted(str(value) for value in group.get("corpora", [])),
                }
            )

        reference_records: list[dict[str, Any]] = []
        unique_contents: dict[str, bytes] = {}
        resolution_counts: collections.Counter[str] = collections.Counter()
        candidate_source_counts: collections.Counter[str] = collections.Counter()
        resolution_cache: dict[tuple[str, str], dict[str, Any]] = {}
        for key in sorted(references, key=lambda item: tuple(str(part) for part in item)):
            reference = references[key]
            reference["observations"] = sorted(
                reference["observations"],
                key=lambda item: (
                    item["source_line"], item["kind"], item["message_id"], item["snapshot_message_id"]
                ),
            )
            repo_relative = repository_relative_path(
                reference["tracking_path"], repository, windows_roots
            )
            candidates: list[dict[str, Any]] = []
            variants: set[str] = set()
            backup_name = reference["backup_file_name"]
            if backup_name is None:
                resolution = "no-backup-reference"
            else:
                resolution_key = (reference["session_id"], backup_name)
                cached = resolution_cache.get(resolution_key)
                if cached is not None:
                    resolution = cached["resolution"]
                    candidates = cached["archive_candidates"]
                    variants = set(cached["content_sha256_variants"])
                else:
                    archive_candidates = candidate_index.get(resolution_key, [])
                    if not archive_candidates:
                        resolution = "archive-reference-not-found"
                    else:
                        for candidate in archive_candidates:
                            content, candidate_record = read_candidate(candidate)
                            candidates.append(candidate_record)
                            candidate_source_counts[candidate_record["source"] or "unavailable"] += 1
                            if content is not None:
                                digest = store_object(objects, content)
                                if digest != candidate_record["expected_sha256"]:
                                    raise RuntimeError("stored file-history body differs from archive inventory")
                                unique_contents[digest] = content
                                variants.add(digest)
                        if not variants:
                            resolution = "body-unavailable"
                        elif len(variants) > 1:
                            resolution = "conflicting-content"
                        elif len(candidates) > 1:
                            resolution = "repeated-identical-content"
                        else:
                            resolution = "single-content"
                    resolution_cache[resolution_key] = {
                        "resolution": resolution,
                        "archive_candidates": candidates,
                        "content_sha256_variants": sorted(variants),
                    }
            resolution_counts[resolution] += 1
            reference_records.append(
                {
                    "schema": REFERENCE_SCHEMA,
                    "reference_id": reference_id(reference),
                    **reference,
                    "repository_relative_path": repo_relative,
                    "domain": classify_domain(repo_relative),
                    "resolution": resolution,
                    "content_sha256_variants": sorted(variants),
                    "archive_candidates": candidates,
                }
            )

        state_groups: dict[tuple[str, str, str, int | None], list[dict[str, Any]]] = collections.defaultdict(list)
        for reference in reference_records:
            state_groups[
                (
                    reference["session_id"],
                    reference["tracking_path"],
                    reference["backup_file_name"] or "<null>",
                    reference["version"],
                )
            ].append(reference)
        state_records: list[dict[str, Any]] = []
        state_resolution_counts: collections.Counter[str] = collections.Counter()
        for state_key in sorted(state_groups, key=lambda item: tuple(str(part) for part in item)):
            members = state_groups[state_key]
            resolutions = sorted({member["resolution"] for member in members})
            resolution = resolutions[0] if len(resolutions) == 1 else "conflicting-reference-resolution"
            state_resolution_counts[resolution] += 1
            descriptor_variants: dict[bytes, dict[str, Any]] = {}
            occurrence_variants: dict[bytes, dict[str, Any]] = {}
            observation_variants: dict[bytes, dict[str, Any]] = {}
            archive_candidate_variants: dict[bytes, dict[str, Any]] = {}
            content_variants: set[str] = set()
            for member in members:
                for descriptor in member["descriptor_variants"]:
                    descriptor_variants[canonical_json_bytes(descriptor)] = descriptor
                for occurrence in member["source_occurrences"]:
                    occurrence_variants[canonical_json_bytes(occurrence)] = occurrence
                for observation in member["observations"]:
                    rooted = {
                        **observation,
                        "source_content_sha256": member["source_content_sha256"],
                        "reference_id": member["reference_id"],
                    }
                    observation_variants[canonical_json_bytes(rooted)] = rooted
                for candidate in member["archive_candidates"]:
                    archive_candidate_variants[canonical_json_bytes(candidate)] = candidate
                content_variants.update(member["content_sha256_variants"])
            state_identity = {
                "session_id": state_key[0],
                "tracking_path": state_key[1],
                "backup_file_name": None if state_key[2] == "<null>" else state_key[2],
                "version": state_key[3],
            }
            state_records.append(
                {
                    "schema": "laplace.recovery.claude-file-history-state-version/v1",
                    "state_version_id": sha256_bytes(canonical_json_bytes(state_identity)),
                    **state_identity,
                    "reference_ids": sorted(member["reference_id"] for member in members),
                    "source_content_sha256s": sorted(
                        {member["source_content_sha256"] for member in members}
                    ),
                    "source_occurrences": [
                        occurrence_variants[key] for key in sorted(occurrence_variants)
                    ],
                    "observations": [
                        observation_variants[key] for key in sorted(observation_variants)
                    ],
                    "descriptor_variants": [
                        descriptor_variants[key] for key in sorted(descriptor_variants)
                    ],
                    "repository_relative_path": members[0]["repository_relative_path"],
                    "domain": members[0]["domain"],
                    "resolution": resolution,
                    "content_sha256_variants": sorted(content_variants),
                    "archive_candidates": [
                        archive_candidate_variants[key]
                        for key in sorted(archive_candidate_variants)
                    ],
                }
            )

        object_format = git_output(repository, ["rev-parse", "--show-object-format"]).strip()
        all_ref_objects = git_object_set(repository, "--all")
        baseline_objects = git_object_set(repository, baseline)
        content_records: list[dict[str, Any]] = []
        blob_ids = {
            digest: git_blob_id(content, object_format)
            for digest, content in unique_contents.items()
        }
        present_objects = git_object_presence(repository, set(blob_ids.values()))
        for digest in sorted(unique_contents):
            blob_id = blob_ids[digest]
            content_records.append(
                {
                    "content_sha256": digest,
                    "bytes": len(unique_contents[digest]),
                    "git_blob_id": blob_id,
                    "git_object_present": blob_id in present_objects,
                    "reachable_from_any_ref": blob_id in all_ref_objects,
                    "reachable_from_baseline": blob_id in baseline_objects,
                }
            )

        references_path = temporary / "file-history-references.jsonl"
        with references_path.open("wb") as stream:
            for record in sorted(reference_records, key=lambda item: item["reference_id"]):
                stream.write(canonical_json_bytes(record) + b"\n")
        states_path = temporary / "state-versions.jsonl"
        with states_path.open("wb") as stream:
            for record in sorted(state_records, key=lambda item: item["state_version_id"]):
                stream.write(canonical_json_bytes(record) + b"\n")
        contents_path = temporary / "content-index.jsonl"
        with contents_path.open("wb") as stream:
            for record in content_records:
                stream.write(canonical_json_bytes(record) + b"\n")

        backup_sessions: dict[str, set[str]] = collections.defaultdict(set)
        for reference in reference_records:
            if reference["backup_file_name"]:
                backup_sessions[reference["backup_file_name"]].add(reference["session_id"])
        summary = {
            "content_group_count": len(groups),
            "source_occurrence_count": sum(len(item["source_occurrences"]) for item in sources),
            "source_record_count": sum(item["record_count"] for item in sources),
            "malformed_record_count": len(malformed_records),
            "snapshot_record_count": record_type_counts["file-history-snapshot"],
            "delta_record_count": record_type_counts["file-history-delta"],
            "reference_count": len(reference_records),
            "state_version_count": len(state_records),
            "distinct_tracking_path_count": len(
                {item["tracking_path"] for item in reference_records}
            ),
            "distinct_session_count": len({item["session_id"] for item in reference_records}),
            "invalid_reference_count": len(invalid_references),
            "null_backup_reference_count": resolution_counts["no-backup-reference"],
            "null_backup_state_version_count": state_resolution_counts["no-backup-reference"],
            "session_scoped_backup_name_collision_count": sum(
                1 for sessions in backup_sessions.values() if len(sessions) > 1
            ),
            "resolution_counts": dict(sorted(resolution_counts.items())),
            "state_resolution_counts": dict(sorted(state_resolution_counts.items())),
            "archive_candidate_source_counts": dict(sorted(candidate_source_counts.items())),
            "unique_body_content_count": len(content_records),
            "body_absent_from_git_object_database_count": sum(
                1 for item in content_records if not item["git_object_present"]
            ),
            "body_absent_from_all_refs_count": sum(
                1 for item in content_records if not item["reachable_from_any_ref"]
            ),
            "body_absent_from_baseline_count": sum(
                1 for item in content_records if not item["reachable_from_baseline"]
            ),
        }
        files = []
        for path in (references_path, states_path, contents_path):
            with path.open("rb") as stream:
                line_count = sum(1 for _ in stream)
            files.append(
                {
                    "path": path.name,
                    "bytes": path.stat().st_size,
                    "sha256": sha256_file(path),
                    "line_count": line_count,
                }
            )
        manifest = {
            "schema": SCHEMA,
            "inputs": {
                "corpora_manifest": str(corpora_path),
                "corpora_manifest_sha256": sha256_file(corpora_path),
                "repository": str(repository),
                "git_object_format": object_format,
                "baseline": baseline,
                "windows_repository_roots": windows_roots,
                "archive_bindings": archive_inputs,
            },
            "summary": summary,
            "record_type_counts": dict(sorted(record_type_counts.items())),
            "sources": sources,
            "malformed_records": malformed_records,
            "invalid_references": invalid_references,
            "files": files,
            "content_objects": content_records,
        }
        (temporary / "manifest.json").write_bytes(canonical_json_bytes(manifest) + b"\n")
        os.replace(temporary, destination)
        return manifest
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def parse_arguments(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpora-manifest", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--archive-binding", action="append", required=True, type=parse_archive_binding)
    parser.add_argument("--windows-repository-root", action="append", default=[])
    parser.add_argument("--output-directory", required=True)
    return parser.parse_args(list(argv) if argv is not None else None)


def main(argv: Iterable[str] | None = None) -> int:
    try:
        arguments = parse_arguments(argv)
        manifest = build_index(arguments)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(manifest["summary"], sort_keys=True))
    unresolved = manifest["summary"]["resolution_counts"].get("body-unavailable", 0)
    unresolved += manifest["summary"]["resolution_counts"].get("conflicting-content", 0)
    unresolved += manifest["summary"]["invalid_reference_count"]
    unresolved += manifest["summary"]["malformed_record_count"]
    return 2 if unresolved else 0


if __name__ == "__main__":
    raise SystemExit(main())
