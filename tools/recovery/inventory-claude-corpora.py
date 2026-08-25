#!/usr/bin/env python3
"""Inventory and deduplicate Claude project-event JSONL corpora.

The output deliberately contains metadata and counts, not message or tool payloads.
Every accepted log is hashed from the live source and, when an archive inventory is
provided, compared with the hash captured inside that archive.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


CLAUDE_EVENT_TYPES = {
    "assistant",
    "file-history-snapshot",
    "progress",
    "queue-operation",
    "system",
    "user",
}
SESSION_FILE_RE = re.compile(r"(?:agent-[0-9a-z]+|[0-9a-f]{8}-[0-9a-f-]{27})\.jsonl$")


@dataclass(frozen=True)
class Corpus:
    label: str
    root: Path
    archive_manifest: Path | None


def parse_binding(value: str) -> tuple[str, str]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("expected LABEL=PATH")
    label, path = value.split("=", 1)
    if not label or not path:
        raise argparse.ArgumentTypeError("expected non-empty LABEL=PATH")
    return label, path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def archive_index(path: Path | None) -> tuple[dict[str, dict[str, Any]], dict[str, Any] | None]:
    if path is None:
        return {}, None
    raw = path.read_bytes()
    manifest = json.loads(raw)
    entries = {
        str(entry["path"]): entry
        for entry in manifest.get("entries", [])
        if entry.get("kind") in {"file", "regular"}
    }
    binding = {
        "manifest": str(path),
        "manifest_sha256": hashlib.sha256(raw).hexdigest(),
        "archive": manifest.get("archive"),
        "archive_sha256": manifest.get("archive_sha256"),
    }
    return entries, binding


def find_archive_entry(
    entries: dict[str, dict[str, Any]], root: Path, source: Path
) -> dict[str, Any] | None:
    relative = source.relative_to(root).as_posix()
    direct = entries.get(relative)
    if direct is not None:
        return direct
    matches = [entry for name, entry in entries.items() if name.endswith("/" + relative)]
    return matches[0] if len(matches) == 1 else None


def first_json_record(path: Path) -> dict[str, Any] | None:
    with path.open("rb") as stream:
        for raw_line in stream:
            if not raw_line.strip():
                continue
            try:
                value = json.loads(raw_line)
            except (UnicodeDecodeError, json.JSONDecodeError):
                return None
            return value if isinstance(value, dict) else None
    return None


def looks_like_claude_log(path: Path, first: dict[str, Any] | None) -> bool:
    if first is None:
        return False
    event_type = first.get("type")
    event_keys = {"sessionId", "uuid", "parentUuid", "message", "cwd"}
    if event_type in CLAUDE_EVENT_TYPES and event_keys.intersection(first):
        return True
    # Tiny resumed-session records can contain only queue metadata initially.
    return bool(SESSION_FILE_RE.search(path.name) and "projects" in path.parts and event_type)


def text_blocks(content: Any) -> Iterable[dict[str, Any]]:
    if isinstance(content, list):
        for item in content:
            if isinstance(item, dict):
                yield item


def analyze_log(path: Path) -> dict[str, Any]:
    event_types: collections.Counter[str] = collections.Counter()
    roles: collections.Counter[str] = collections.Counter()
    tool_calls: collections.Counter[str] = collections.Counter()
    session_ids: set[str] = set()
    timestamps: list[str] = []
    record_count = 0
    invalid_line_count = 0
    human_message_count = 0
    tool_result_count = 0
    psql_bash_call_count = 0

    with path.open("rb") as stream:
        for raw_line in stream:
            if not raw_line.strip():
                continue
            try:
                event = json.loads(raw_line)
            except (UnicodeDecodeError, json.JSONDecodeError):
                invalid_line_count += 1
                continue
            if not isinstance(event, dict):
                invalid_line_count += 1
                continue
            record_count += 1
            event_types[str(event.get("type", "<missing>"))] += 1
            if isinstance(event.get("sessionId"), str):
                session_ids.add(event["sessionId"])
            if isinstance(event.get("timestamp"), str):
                timestamps.append(event["timestamp"])

            message = event.get("message")
            if not isinstance(message, dict):
                continue
            role = message.get("role")
            if isinstance(role, str):
                roles[role] += 1
            content = message.get("content")
            blocks = list(text_blocks(content))
            if role == "user":
                has_tool_result = any(block.get("type") == "tool_result" for block in blocks)
                tool_result_count += sum(block.get("type") == "tool_result" for block in blocks)
                if not has_tool_result and (isinstance(content, str) or blocks):
                    human_message_count += 1
            for block in blocks:
                if block.get("type") != "tool_use":
                    continue
                name = str(block.get("name", "<missing>"))
                tool_calls[name] += 1
                tool_input = block.get("input")
                if name == "Bash" and isinstance(tool_input, dict):
                    command = tool_input.get("command")
                    if isinstance(command, str) and re.search(r"(?:^|[\s|;&])psql(?:\s|$)", command):
                        psql_bash_call_count += 1

    return {
        "event_type_counts": dict(sorted(event_types.items())),
        "first_timestamp": min(timestamps) if timestamps else None,
        "human_message_count": human_message_count,
        "invalid_line_count": invalid_line_count,
        "last_timestamp": max(timestamps) if timestamps else None,
        "message_role_counts": dict(sorted(roles.items())),
        "psql_bash_call_count": psql_bash_call_count,
        "record_count": record_count,
        "session_ids": sorted(session_ids),
        "tool_call_counts": dict(sorted(tool_calls.items())),
        "tool_result_count": tool_result_count,
    }


def add_counts(target: collections.Counter[str], values: dict[str, int]) -> None:
    target.update(values)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", action="append", required=True, type=parse_binding)
    parser.add_argument("--archive-manifest", action="append", default=[], type=parse_binding)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    archive_paths = dict(args.archive_manifest)
    corpora = [
        Corpus(label, Path(root).resolve(), Path(archive_paths[label]).resolve() if label in archive_paths else None)
        for label, root in args.corpus
    ]
    labels = [corpus.label for corpus in corpora]
    if len(labels) != len(set(labels)):
        parser.error("corpus labels must be unique")

    logs: list[dict[str, Any]] = []
    rejected: list[dict[str, Any]] = []
    archive_bindings: dict[str, Any] = {}
    archive_mismatches: list[dict[str, Any]] = []

    for corpus in corpora:
        if not corpus.root.is_dir():
            parser.error(f"corpus root is not a directory: {corpus.root}")
        entries, binding = archive_index(corpus.archive_manifest)
        archive_bindings[corpus.label] = binding
        candidates = sorted(corpus.root.rglob("*.jsonl"))
        for path in candidates:
            first = first_json_record(path)
            in_laplace_tree = "laplace" in str(path).lower()
            if not (in_laplace_tree and looks_like_claude_log(path, first)):
                rejected.append(
                    {
                        "bytes": path.stat().st_size,
                        "corpus": corpus.label,
                        "path": str(path),
                        "reason": "not-claude-laplace-event-log",
                    }
                )
                continue
            content_sha = sha256_file(path)
            archive_entry = find_archive_entry(entries, corpus.root, path) if entries else None
            expected_sha = archive_entry.get("sha256") if archive_entry else None
            archive_status = (
                "not-bound"
                if not entries
                else "not-found"
                if archive_entry is None
                else "verified"
                if expected_sha == content_sha
                else "mismatch"
            )
            if archive_status in {"not-found", "mismatch"}:
                archive_mismatches.append(
                    {
                        "actual_sha256": content_sha,
                        "corpus": corpus.label,
                        "expected_sha256": expected_sha,
                        "path": str(path),
                        "status": archive_status,
                    }
                )
            analyzed = analyze_log(path)
            analyzed.update(
                {
                    "archive_status": archive_status,
                    "bytes": path.stat().st_size,
                    "content_sha256": content_sha,
                    "corpus": corpus.label,
                    "is_subagent": "subagents" in path.parts,
                    "path": str(path),
                }
            )
            logs.append(analyzed)

    by_content: dict[str, list[int]] = collections.defaultdict(list)
    by_session: dict[str, list[int]] = collections.defaultdict(list)
    for index, log in enumerate(logs):
        by_content[log["content_sha256"]].append(index)
        for session_id in log["session_ids"]:
            by_session[session_id].append(index)

    content_groups: list[dict[str, Any]] = []
    for digest, indexes in sorted(by_content.items()):
        group_labels = sorted({logs[index]["corpus"] for index in indexes})
        content_groups.append(
            {
                "bytes": logs[indexes[0]]["bytes"],
                "content_sha256": digest,
                "corpora": group_labels,
                "occurrence_count": len(indexes),
                "paths": [logs[index]["path"] for index in indexes],
            }
        )

    session_groups: list[dict[str, Any]] = []
    for session_id, indexes in sorted(by_session.items()):
        session_groups.append(
            {
                "content_variant_count": len({logs[index]["content_sha256"] for index in indexes}),
                "corpora": sorted({logs[index]["corpus"] for index in indexes}),
                "file_count": len(indexes),
                "paths": [logs[index]["path"] for index in indexes],
                "session_id": session_id,
            }
        )

    corpus_summaries: dict[str, Any] = {}
    for corpus in corpora:
        selected = [log for log in logs if log["corpus"] == corpus.label]
        event_counts: collections.Counter[str] = collections.Counter()
        tool_counts: collections.Counter[str] = collections.Counter()
        for log in selected:
            add_counts(event_counts, log["event_type_counts"])
            add_counts(tool_counts, log["tool_call_counts"])
        corpus_summaries[corpus.label] = {
            "archive_status_counts": dict(sorted(collections.Counter(log["archive_status"] for log in selected).items())),
            "bytes": sum(log["bytes"] for log in selected),
            "event_type_counts": dict(sorted(event_counts.items())),
            "human_message_count": sum(log["human_message_count"] for log in selected),
            "invalid_line_count": sum(log["invalid_line_count"] for log in selected),
            "log_count": len(selected),
            "psql_bash_call_count": sum(log["psql_bash_call_count"] for log in selected),
            "record_count": sum(log["record_count"] for log in selected),
            "tool_call_counts": dict(sorted(tool_counts.items())),
            "unique_content_count": len({log["content_sha256"] for log in selected}),
        }

    unique_by_corpus = collections.Counter()
    for group in content_groups:
        if len(group["corpora"]) == 1:
            unique_by_corpus[group["corpora"][0]] += 1

    output = {
        "archive_bindings": archive_bindings,
        "archive_mismatches": archive_mismatches,
        "content_groups": content_groups,
        "corpora": corpus_summaries,
        "logs": logs,
        "rejected_jsonl": rejected,
        "schema": "laplace.recovery.claude-corpora.v1",
        "session_groups": session_groups,
        "summary": {
            "archive_mismatch_count": len(archive_mismatches),
            "content_group_count": len(content_groups),
            "cross_corpus_content_group_count": sum(len(group["corpora"]) > 1 for group in content_groups),
            "duplicate_content_group_count": sum(group["occurrence_count"] > 1 for group in content_groups),
            "log_bytes": sum(log["bytes"] for log in logs),
            "log_count": len(logs),
            "rejected_jsonl_count": len(rejected),
            "session_count": len(session_groups),
            "unique_content_by_corpus": dict(sorted(unique_by_corpus.items())),
        },
    }

    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    serialized = (json.dumps(output, indent=2, sort_keys=True) + "\n").encode()
    args.manifest.write_bytes(serialized)
    print(json.dumps(output["summary"], sort_keys=True))
    print(f"manifest_sha256={hashlib.sha256(serialized).hexdigest()}")
    return 1 if archive_mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
