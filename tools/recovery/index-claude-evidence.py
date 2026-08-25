#!/usr/bin/env python3
"""Build a content-verified Claude event, tool, and artifact-reference index."""

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
import tempfile
from typing import Any, Iterable


SCHEMA = "laplace.recovery.claude-evidence-index/v2"
EVENT_SCHEMA = "laplace.recovery.claude-event/v1"
MESSAGE_SCHEMA = "laplace.recovery.claude-message/v1"
ACTION_SCHEMA = "laplace.recovery.claude-tool-action/v1"
REFERENCE_SCHEMA = "laplace.recovery.artifact-reference/v2"

GENERATED_USER_PREFIXES = (
    "<task-notification>",
    "<local-command",
    "<command-",
    "[Request interrupted",
)

COMMIT_PATTERN = re.compile(r"(?<![0-9a-fA-F])([0-9a-fA-F]{40})(?![0-9a-fA-F])")
DIGEST_PATTERN = re.compile(r"(?<![0-9a-fA-F])([0-9a-fA-F]{64})(?![0-9a-fA-F])")
HEX_TOKEN_PATTERN = re.compile(r"(?<![0-9a-fA-F])([0-9a-fA-F]{7,64})(?![0-9a-fA-F])")
BACKTICK_PATTERN = re.compile(r"`([^`\r\n]+)`")
COMMIT_CONTEXT_PATTERN = re.compile(
    r"(?i)\b(?:commit|revision|rev)\s+`?([0-9a-f]{7,39})`?"
)
GIT_COMMAND_COMMIT_PATTERN = re.compile(
    r"(?i)\bgit\s+(?:show|diff|cat-file|cherry-pick|revert)\b"
    r"[^\r\n;&|]{0,120}?\b([0-9a-f]{7,40})\b"
)
PATH_PREFIXES = (
    "/home/",
    "/vault/",
    "/tmp/",
    "engine/",
    "extension/",
    "app/",
    "docs/",
    "tests/",
    "tools/",
    "scripts/",
    "cmake/",
    ".github/",
)
PATH_KEY_PARTS = ("path", "file", "directory", "repository", "worktree", "cwd")


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


def load_object(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise RuntimeError(f"expected JSON object: {path}")
    return value


def write_json_line(stream: Any, value: dict[str, Any]) -> None:
    stream.write(canonical_json_bytes(value))
    stream.write(b"\n")


def message_blocks(content: Any) -> Iterable[dict[str, Any]]:
    if isinstance(content, str):
        yield {"type": "text", "text": content}
        return
    if isinstance(content, list):
        for block in content:
            if isinstance(block, dict):
                yield block


def direct_human_message(role: str, blocks: list[dict[str, Any]]) -> bool:
    if role != "user" or any(block.get("type") == "tool_result" for block in blocks):
        return False
    texts = [str(block.get("text", "")) for block in blocks if block.get("type") == "text"]
    if not texts:
        return False
    combined = "\n".join(texts)
    return not combined.startswith(GENERATED_USER_PREFIXES)


def looks_like_path(value: str) -> bool:
    candidate = value.strip().strip("'\"")
    if not candidate or any(character in candidate for character in "\r\n\0"):
        return False
    if candidate.startswith(PATH_PREFIXES):
        return True
    return candidate in {
        "CMakeLists.txt",
        "CMakePresets.json",
        "README.md",
        "AGENTS.md",
    }


def structured_path_values(value: Any, parent_key: str = "") -> Iterable[str]:
    if isinstance(value, dict):
        for key, child in value.items():
            lowered = str(key).lower()
            if isinstance(child, str) and any(part in lowered for part in PATH_KEY_PARTS):
                if looks_like_path(child):
                    yield child.strip().strip("'\"")
            yield from structured_path_values(child, lowered)
    elif isinstance(value, list):
        for child in value:
            yield from structured_path_values(child, parent_key)


def text_references(text: str) -> Iterable[tuple[str, str]]:
    emitted: set[tuple[str, str]] = set()

    def emit(kind: str, value: str) -> Iterable[tuple[str, str]]:
        item = (kind, value.lower())
        if item not in emitted:
            emitted.add(item)
            yield item

    for match in COMMIT_PATTERN.finditer(text):
        yield from emit("git-commit", match.group(1))
    for match in DIGEST_PATTERN.finditer(text):
        yield from emit("sha256", match.group(1))
    for pattern in (COMMIT_CONTEXT_PATTERN, GIT_COMMAND_COMMIT_PATTERN):
        for match in pattern.finditer(text):
            value = match.group(1)
            if len(value) == 40:
                yield from emit("git-commit", value)
            else:
                yield from emit("git-commit-prefix", value)
    for match in BACKTICK_PATTERN.finditer(text):
        candidate = match.group(1).strip()
        if HEX_TOKEN_PATTERN.fullmatch(candidate) and 7 <= len(candidate) < 40:
            yield from emit("git-commit-prefix", candidate)
        if looks_like_path(candidate):
            yield from emit("path", candidate)


def known_git_commits(manifest: dict[str, Any]) -> set[str]:
    commits: set[str] = set()
    for entry in manifest.get("commits", []):
        if isinstance(entry, dict) and isinstance(entry.get("commit"), str):
            commits.add(entry["commit"].lower())
    return commits


def repository_git_commits(repository: pathlib.Path) -> set[str]:
    process = subprocess.run(
        ["git", "-C", str(repository), "rev-list", "--all"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if process.returncode != 0:
        raise RuntimeError(f"git rev-list failed: {process.stderr.strip()}")
    commits = {line.strip().lower() for line in process.stdout.splitlines() if line.strip()}
    if not commits or any(not re.fullmatch(r"[0-9a-f]{40}", value) for value in commits):
        raise RuntimeError(f"invalid Git commit set from {repository}")
    return commits


def select_source(group: dict[str, Any]) -> pathlib.Path:
    expected_bytes = int(group["bytes"])
    for value in group.get("paths", []):
        path = pathlib.Path(value)
        if path.is_file() and path.stat().st_size == expected_bytes:
            return path
    raise RuntimeError(
        f"no readable source for content {group.get('content_sha256', '<missing>')}"
    )


def reference_record(
    *,
    event_id: str,
    block_index: int,
    origin: str,
    kind: str,
    value: str,
    git_commits: set[str],
    recovery_commits: set[str],
) -> dict[str, Any]:
    resolved_commits: list[str] = []
    if kind == "git-commit" and value in git_commits:
        resolved_commits = [value]
    elif kind == "git-commit-prefix":
        resolved_commits = sorted(commit for commit in git_commits if commit.startswith(value))
    return {
        "schema": REFERENCE_SCHEMA,
        "event_id": event_id,
        "block_index": block_index,
        "origin": origin,
        "kind": kind,
        "value": value,
        "known_git_commit": len(resolved_commits) == 1,
        "recovered_nonbaseline_commit": (
            len(resolved_commits) == 1 and resolved_commits[0] in recovery_commits
        ),
        "resolved_git_commits": resolved_commits,
    }


def tool_action_id(source_content_sha256: str, tool_use_id: str) -> str:
    return sha256_bytes(f"{source_content_sha256}\0{tool_use_id}".encode("utf-8"))


def classify_tool_records(
    calls: list[dict[str, Any]], results: list[dict[str, Any]]
) -> dict[str, Any]:
    call_variants = {
        (item["message_id"], item["tool_name"], item["input_sha256"])
        for item in calls
    }
    result_variants = {
        (item["message_id"], item["is_error"], item["content_sha256"])
        for item in results
    }
    if not calls:
        status = "unpaired-result"
    elif not results:
        status = "unpaired-call"
    elif len(call_variants) > 1 or len(result_variants) > 1:
        status = "conflicting-records"
    elif len(calls) > 1 or len(results) > 1:
        status = "paired-repeated-records"
    else:
        status = "paired-single-records"
    return {
        "status": status,
        "paired": bool(calls and results),
        "conflicting": status == "conflicting-records",
        "call_record_count": len(calls),
        "call_variant_count": len(call_variants),
        "result_record_count": len(results),
        "result_variant_count": len(result_variants),
    }


def finalize_file(path: pathlib.Path) -> dict[str, Any]:
    line_count = 0
    with path.open("rb") as stream:
        for _ in stream:
            line_count += 1
    return {
        "path": path.name,
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
        "line_count": line_count,
    }


def build_index(arguments: argparse.Namespace) -> dict[str, Any]:
    corpora_path = pathlib.Path(arguments.corpora_manifest).resolve()
    git_path = pathlib.Path(arguments.git_manifest).resolve()
    git_repository = pathlib.Path(arguments.git_repository).resolve()
    destination = pathlib.Path(arguments.output_directory).resolve()
    if destination.exists():
        raise RuntimeError(f"output already exists: {destination}")

    corpora = load_object(corpora_path)
    git = load_object(git_path)
    recovery_commits = known_git_commits(git)
    git_commits = repository_git_commits(git_repository)
    if not recovery_commits <= git_commits:
        missing = sorted(recovery_commits - git_commits)
        raise RuntimeError(f"recovery commits absent from Git repository: {missing[:5]}")
    git_commit_set_sha256 = sha256_bytes(("\n".join(sorted(git_commits)) + "\n").encode("ascii"))
    groups = sorted(
        corpora.get("content_groups", []), key=lambda item: str(item.get("content_sha256", ""))
    )
    if not groups:
        raise RuntimeError("corpora manifest contains no content groups")

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = pathlib.Path(
        tempfile.mkdtemp(prefix=f".{destination.name}.", dir=destination.parent)
    )
    event_counts: collections.Counter[str] = collections.Counter()
    role_counts: collections.Counter[str] = collections.Counter()
    reference_counts: collections.Counter[str] = collections.Counter()
    tool_calls: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    tool_results: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    source_records: list[dict[str, Any]] = []
    human_message_count = 0
    agent_message_count = 0
    malformed_json_count = 0
    non_object_record_count = 0
    generated_user_notice_count = 0
    user_tool_result_message_count = 0
    nontext_user_message_count = 0

    try:
        events_path = temporary / "events.jsonl"
        messages_path = temporary / "messages.jsonl"
        references_path = temporary / "artifact-references.jsonl"
        actions_path = temporary / "tool-actions.jsonl"
        with (
            events_path.open("wb") as events_stream,
            messages_path.open("wb") as messages_stream,
            references_path.open("wb") as references_stream,
        ):
            for group in groups:
                expected_digest = str(group["content_sha256"])
                expected_bytes = int(group["bytes"])
                source_path = select_source(group)
                source_digest = hashlib.sha256()
                source_bytes = 0
                source_lines = 0
                source_malformed_json = 0
                source_non_object = 0
                with source_path.open("rb") as source:
                    for source_line, raw_line in enumerate(source, start=1):
                        source_digest.update(raw_line)
                        source_bytes += len(raw_line)
                        if not raw_line.strip():
                            continue
                        source_lines += 1
                        raw_digest = sha256_bytes(raw_line)
                        valid_json = True
                        valid_object = True
                        try:
                            record = json.loads(raw_line)
                        except (UnicodeDecodeError, json.JSONDecodeError):
                            malformed_json_count += 1
                            source_malformed_json += 1
                            valid_json = False
                            valid_object = False
                            record = {}
                        if not isinstance(record, dict):
                            non_object_record_count += 1
                            source_non_object += 1
                            valid_object = False
                            record = {}

                        event_id = sha256_bytes(
                            f"{expected_digest}\0{source_line}\0{raw_digest}".encode("ascii")
                        )
                        record_type = str(record.get("type", "<invalid>"))
                        event_counts[record_type] += 1
                        message = record.get("message")
                        role = str(message.get("role", "")) if isinstance(message, dict) else ""
                        if role:
                            role_counts[role] += 1
                        blocks = (
                            list(message_blocks(message.get("content")))
                            if isinstance(message, dict)
                            else []
                        )
                        text_descriptors: list[dict[str, Any]] = []
                        tool_action_ids: list[str] = []

                        for block_index, block in enumerate(blocks):
                            block_type = str(block.get("type", ""))
                            if block_type == "text" and isinstance(block.get("text"), str):
                                text = block["text"]
                                encoded = text.encode("utf-8")
                                text_descriptors.append(
                                    {
                                        "block_index": block_index,
                                        "content_bytes": len(encoded),
                                        "content_sha256": sha256_bytes(encoded),
                                    }
                                )
                                for kind, value in text_references(text):
                                    reference_counts[kind] += 1
                                    write_json_line(
                                        references_stream,
                                        reference_record(
                                            event_id=event_id,
                                            block_index=block_index,
                                            origin="message-text",
                                            kind=kind,
                                            value=value,
                                            git_commits=git_commits,
                                            recovery_commits=recovery_commits,
                                        ),
                                    )
                            elif block_type == "tool_use":
                                tool_id = str(block.get("id", ""))
                                action_id = tool_action_id(expected_digest, tool_id)
                                tool_input = block.get("input")
                                encoded = canonical_json_bytes(tool_input)
                                descriptor = {
                                    "event_id": event_id,
                                    "source_content_sha256": expected_digest,
                                    "source_line": source_line,
                                    "block_index": block_index,
                                    "timestamp": str(record.get("timestamp", "")),
                                    "session_id": str(record.get("sessionId", "")),
                                    "message_id": str(record.get("uuid", "")),
                                    "tool_action_id": action_id,
                                    "tool_use_id": tool_id,
                                    "tool_name": str(block.get("name", "")),
                                    "input_bytes": len(encoded),
                                    "input_sha256": sha256_bytes(encoded),
                                    "working_directory": str(record.get("cwd", "")),
                                    "git_branch": str(record.get("gitBranch", "")),
                                }
                                tool_calls[action_id].append(descriptor)
                                tool_action_ids.append(action_id)
                                if isinstance(tool_input, (dict, list)):
                                    for value in structured_path_values(tool_input):
                                        reference_counts["path"] += 1
                                        write_json_line(
                                            references_stream,
                                            reference_record(
                                                event_id=event_id,
                                                block_index=block_index,
                                                origin="tool-input",
                                                kind="path",
                                                value=value,
                                                git_commits=git_commits,
                                                recovery_commits=recovery_commits,
                                            ),
                                        )
                                for kind, value in text_references(
                                    json.dumps(tool_input, ensure_ascii=False)
                                ):
                                    reference_counts[kind] += 1
                                    write_json_line(
                                        references_stream,
                                        reference_record(
                                            event_id=event_id,
                                            block_index=block_index,
                                            origin="tool-input",
                                            kind=kind,
                                            value=value,
                                            git_commits=git_commits,
                                            recovery_commits=recovery_commits,
                                        ),
                                    )
                            elif block_type == "tool_result":
                                tool_id = str(block.get("tool_use_id", ""))
                                action_id = tool_action_id(expected_digest, tool_id)
                                result_content = block.get("content")
                                encoded = canonical_json_bytes(result_content)
                                descriptor = {
                                    "event_id": event_id,
                                    "source_content_sha256": expected_digest,
                                    "source_line": source_line,
                                    "block_index": block_index,
                                    "timestamp": str(record.get("timestamp", "")),
                                    "session_id": str(record.get("sessionId", "")),
                                    "message_id": str(record.get("uuid", "")),
                                    "tool_action_id": action_id,
                                    "tool_use_id": tool_id,
                                    "is_error": bool(block.get("is_error", False)),
                                    "content_bytes": len(encoded),
                                    "content_sha256": sha256_bytes(encoded),
                                }
                                tool_results[action_id].append(descriptor)
                                tool_action_ids.append(action_id)

                        is_human = direct_human_message(role, blocks)
                        is_agent = role == "assistant" and bool(text_descriptors)
                        is_user_tool_result = role == "user" and any(
                            block.get("type") == "tool_result" for block in blocks
                        )
                        text_values = [
                            str(block.get("text", ""))
                            for block in blocks
                            if block.get("type") == "text"
                        ]
                        is_generated_notice = (
                            role == "user"
                            and not is_user_tool_result
                            and bool(text_values)
                            and "\n".join(text_values).startswith(GENERATED_USER_PREFIXES)
                        )
                        is_nontext_user = (
                            role == "user"
                            and not is_user_tool_result
                            and bool(blocks)
                            and not text_values
                        )
                        human_message_count += int(is_human)
                        agent_message_count += int(is_agent)
                        generated_user_notice_count += int(is_generated_notice)
                        user_tool_result_message_count += int(is_user_tool_result)
                        nontext_user_message_count += int(is_nontext_user)
                        event = {
                            "schema": EVENT_SCHEMA,
                            "event_id": event_id,
                            "source_content_sha256": expected_digest,
                            "source_paths": sorted(str(value) for value in group.get("paths", [])),
                            "source_corpora": sorted(str(value) for value in group.get("corpora", [])),
                            "source_line": source_line,
                            "raw_line_bytes": len(raw_line),
                            "raw_line_sha256": raw_digest,
                            "record_type": record_type,
                            "session_id": str(record.get("sessionId", "")),
                            "message_id": str(record.get("uuid", "")),
                            "parent_message_id": str(record.get("parentUuid", "")),
                            "timestamp": str(record.get("timestamp", "")),
                            "role": role,
                            "is_sidechain": bool(record.get("isSidechain", False)),
                            "is_meta": bool(record.get("isMeta", False)),
                            "git_branch": str(record.get("gitBranch", "")),
                            "working_directory": str(record.get("cwd", "")),
                            "text_blocks": text_descriptors,
                            "tool_action_ids": sorted(set(tool_action_ids)),
                            "direct_human_message": is_human,
                            "agent_text_message": is_agent,
                            "valid_json": valid_json,
                            "valid_json_object": valid_object,
                        }
                        write_json_line(events_stream, event)
                        if text_descriptors:
                            write_json_line(
                                messages_stream,
                                {
                                    "schema": MESSAGE_SCHEMA,
                                    "event_id": event_id,
                                    "source_content_sha256": expected_digest,
                                    "source_line": source_line,
                                    "session_id": event["session_id"],
                                    "message_id": event["message_id"],
                                    "parent_message_id": event["parent_message_id"],
                                    "timestamp": event["timestamp"],
                                    "role": role,
                                    "direct_human_message": is_human,
                                    "agent_text_message": is_agent,
                                    "text_blocks": text_descriptors,
                                },
                            )

                actual_digest = source_digest.hexdigest()
                if source_bytes != expected_bytes or actual_digest != expected_digest:
                    raise RuntimeError(
                        "source verification failed for "
                        f"{source_path}: expected {expected_bytes}/{expected_digest}, "
                        f"observed {source_bytes}/{actual_digest}"
                    )
                source_records.append(
                    {
                        "content_sha256": expected_digest,
                        "bytes": source_bytes,
                        "record_count": source_lines,
                        "malformed_json_count": source_malformed_json,
                        "non_object_record_count": source_non_object,
                        "selected_path": str(source_path),
                        "paths": sorted(str(value) for value in group.get("paths", [])),
                        "corpora": sorted(str(value) for value in group.get("corpora", [])),
                    }
                )

        with actions_path.open("wb") as actions_stream:
            action_status_counts: collections.Counter[str] = collections.Counter()
            for action_id in sorted(set(tool_calls) | set(tool_results)):
                calls = sorted(
                    tool_calls.get(action_id, []),
                    key=lambda item: (
                        item["source_content_sha256"], item["source_line"], item["block_index"]
                    ),
                )
                results = sorted(
                    tool_results.get(action_id, []),
                    key=lambda item: (
                        item["source_content_sha256"], item["source_line"], item["block_index"]
                    ),
                )
                classification = classify_tool_records(calls, results)
                action_status_counts[classification["status"]] += 1
                write_json_line(
                    actions_stream,
                    {
                        "schema": ACTION_SCHEMA,
                        "tool_action_id": action_id,
                        "tool_use_id": (
                            calls[0]["tool_use_id"]
                            if calls
                            else results[0]["tool_use_id"]
                        ),
                        "calls": calls,
                        "results": results,
                        **classification,
                    },
                )

        output_files = [
            finalize_file(events_path),
            finalize_file(messages_path),
            finalize_file(actions_path),
            finalize_file(references_path),
        ]
        summary = {
            "content_group_count": len(groups),
            "source_occurrence_count": sum(len(group.get("paths", [])) for group in groups),
            "event_count": sum(event_counts.values()),
            "malformed_json_count": malformed_json_count,
            "non_object_record_count": non_object_record_count,
            "direct_human_message_count": human_message_count,
            "agent_text_message_count": agent_message_count,
            "generated_user_notice_count": generated_user_notice_count,
            "user_tool_result_message_count": user_tool_result_message_count,
            "nontext_user_message_count": nontext_user_message_count,
            "tool_action_count": len(set(tool_calls) | set(tool_results)),
            "tool_action_status_counts": dict(sorted(action_status_counts.items())),
            "event_type_counts": dict(sorted(event_counts.items())),
            "message_role_counts": dict(sorted(role_counts.items())),
            "reference_type_counts": dict(sorted(reference_counts.items())),
        }
        manifest = {
            "schema": SCHEMA,
            "inputs": {
                "corpora_manifest": str(corpora_path),
                "corpora_manifest_sha256": sha256_file(corpora_path),
                "git_manifest": str(git_path),
                "git_manifest_sha256": sha256_file(git_path),
                "git_repository": str(git_repository),
                "git_commit_count": len(git_commits),
                "git_commit_set_sha256": git_commit_set_sha256,
                "recovery_commit_count": len(recovery_commits),
            },
            "summary": summary,
            "sources": source_records,
            "files": output_files,
        }
        manifest_path = temporary / "manifest.json"
        manifest_path.write_bytes(canonical_json_bytes(manifest) + b"\n")
        os.replace(temporary, destination)
        return manifest
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpora-manifest", required=True)
    parser.add_argument("--git-manifest", required=True)
    parser.add_argument("--git-repository", required=True)
    parser.add_argument("--output-directory", required=True)
    arguments = parser.parse_args()
    manifest = build_index(arguments)
    print(json.dumps(manifest["summary"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
