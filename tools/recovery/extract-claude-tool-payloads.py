#!/usr/bin/env python3
"""Extract exact Claude Write/Edit payloads and mutating shell commands."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import pathlib
import re
import sys
import tempfile
from typing import Any, Iterable


SCHEMA = "laplace.claude-tool-payload-recovery/v1"
SHELL_MUTATION_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("patch", re.compile(r"(?:^|[;&|\n]\s*)apply_patch\b|\bgit\s+apply\b", re.I)),
    ("redirected-write", re.compile(r"(?<!\S)(?:>>|>)\s*(?!&|/?dev/null\b)[^\s;]+", re.I)),
    ("stream-write", re.compile(r"\btee\b", re.I)),
    ("in-place-edit", re.compile(r"\b(?:sed\s+-i|perl\s+-p?i)\b", re.I)),
    ("language-file-write", re.compile(r"\b(?:write_text|write_bytes|File\.WriteAll|ofstream)\b", re.I)),
    ("copy-move-remove", re.compile(r"(?:^|[;&|\n]\s*)(?:cp|mv|rm|install)\b", re.I)),
    ("git-tree-change", re.compile(r"\bgit\s+(?:checkout|restore|reset|clean|commit|merge|rebase|cherry-pick)\b", re.I)),
    ("package-install", re.compile(r"\b(?:cmake\s+--install|ninja\s+install|make\s+install)\b", re.I)),
)


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_json_bytes(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def content_bytes(value: Any) -> bytes:
    if isinstance(value, str):
        return value.encode("utf-8")
    return canonical_json_bytes(value)


def store_object(objects_directory: pathlib.Path, content: bytes) -> str:
    digest = sha256_bytes(content)
    target = objects_directory / digest
    if target.exists():
        if target.read_bytes() != content:
            raise RuntimeError(f"content-address collision at {target}")
        return digest
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{digest}.", dir=objects_directory)
    temporary_path = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, target)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()
    return digest


def session_for_log(project_directory: pathlib.Path, log_path: pathlib.Path) -> str:
    first = log_path.relative_to(project_directory).parts[0]
    return first[:-6] if first.endswith(".jsonl") else first


def tool_results(logs: list[pathlib.Path]) -> dict[str, dict[str, Any]]:
    results: dict[str, dict[str, Any]] = {}
    for log_path in logs:
        with log_path.open("r", encoding="utf-8") as stream:
            for line_number, raw_line in enumerate(stream, start=1):
                if not raw_line.strip():
                    continue
                record = json.loads(raw_line)
                if record.get("type") != "user":
                    continue
                message = record.get("message") or {}
                blocks = message.get("content")
                if not isinstance(blocks, list):
                    continue
                for block_index, block in enumerate(blocks):
                    if not isinstance(block, dict) or block.get("type") != "tool_result":
                        continue
                    tool_id = str(block.get("tool_use_id", ""))
                    if not tool_id:
                        continue
                    encoded = content_bytes(block.get("content", ""))
                    results[tool_id] = {
                        "source_log": str(log_path),
                        "source_line": line_number,
                        "block_index": block_index,
                        "is_error": bool(block.get("is_error", False)),
                        "content_bytes": len(encoded),
                        "content_sha256": sha256_bytes(encoded),
                    }
    return results


def shell_classes(command: str) -> list[str]:
    return [name for name, pattern in SHELL_MUTATION_PATTERNS if pattern.search(command)]


def inventory(arguments: argparse.Namespace) -> dict[str, Any]:
    claude_root = pathlib.Path(arguments.claude_root).resolve()
    project_directory = claude_root / "projects" / arguments.project_key
    output_directory = pathlib.Path(arguments.output_directory).resolve()
    objects_directory = output_directory / "objects"
    objects_directory.mkdir(parents=True, exist_ok=True)

    if not project_directory.is_dir():
        raise RuntimeError(f"Claude project directory does not exist: {project_directory}")

    logs = sorted(project_directory.rglob("*.jsonl"))
    results = tool_results(logs)
    sources: list[dict[str, Any]] = []
    actions: list[dict[str, Any]] = []
    tool_counts: collections.Counter[str] = collections.Counter()
    shell_class_counts: collections.Counter[str] = collections.Counter()
    referenced_objects: set[str] = set()
    malformed_records: list[dict[str, Any]] = []

    for log_path in logs:
        relative_log = log_path.relative_to(project_directory).as_posix()
        session_id = session_for_log(project_directory, log_path)
        record_count = 0
        with log_path.open("r", encoding="utf-8") as stream:
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
                            "line_sha256": sha256_bytes(raw_line.encode("utf-8")),
                        }
                    )
                    continue
                if record.get("type") != "assistant":
                    continue
                message = record.get("message") or {}
                blocks = message.get("content")
                if not isinstance(blocks, list):
                    continue
                for block_index, block in enumerate(blocks):
                    if not isinstance(block, dict) or block.get("type") != "tool_use":
                        continue
                    tool_id = str(block.get("id", ""))
                    tool_name = str(block.get("name", ""))
                    tool_input = block.get("input") if isinstance(block.get("input"), dict) else {}
                    result = results.get(tool_id)
                    succeeded = bool(result is not None and not result["is_error"])
                    tool_counts[tool_name] += 1
                    action: dict[str, Any] = {
                        "source_log": relative_log,
                        "source_line": line_number,
                        "session_id": session_id,
                        "message_id": str(record.get("uuid", "")),
                        "parent_message_id": str(record.get("parentUuid", "")),
                        "timestamp": str(record.get("timestamp", "")),
                        "working_directory": str(record.get("cwd", "")),
                        "git_branch": str(record.get("gitBranch", "")),
                        "block_index": block_index,
                        "tool_use_id": tool_id,
                        "tool_name": tool_name,
                        "input_bytes": len(canonical_json_bytes(tool_input)),
                        "input_sha256": sha256_bytes(canonical_json_bytes(tool_input)),
                        "input_keys": sorted(tool_input.keys()),
                        "result": result,
                        "succeeded": succeeded,
                    }

                    if tool_name == "Write":
                        value = tool_input.get("content")
                        if isinstance(value, str):
                            encoded = value.encode("utf-8")
                            digest = store_object(objects_directory, encoded)
                            referenced_objects.add(digest)
                            action["write"] = {
                                "file_path": str(tool_input.get("file_path", "")),
                                "content_bytes": len(encoded),
                                "content_sha256": digest,
                            }
                    elif tool_name == "Edit":
                        old_value = tool_input.get("old_string")
                        new_value = tool_input.get("new_string")
                        if isinstance(old_value, str) and isinstance(new_value, str):
                            old_encoded = old_value.encode("utf-8")
                            new_encoded = new_value.encode("utf-8")
                            old_digest = store_object(objects_directory, old_encoded)
                            new_digest = store_object(objects_directory, new_encoded)
                            referenced_objects.update({old_digest, new_digest})
                            action["edit"] = {
                                "file_path": str(tool_input.get("file_path", "")),
                                "replace_all": bool(tool_input.get("replace_all", False)),
                                "old_bytes": len(old_encoded),
                                "old_sha256": old_digest,
                                "new_bytes": len(new_encoded),
                                "new_sha256": new_digest,
                            }
                    elif tool_name == "Bash":
                        command = tool_input.get("command")
                        if isinstance(command, str):
                            classes = shell_classes(command)
                            if classes:
                                encoded = command.encode("utf-8")
                                digest = store_object(objects_directory, encoded)
                                referenced_objects.add(digest)
                                for class_name in classes:
                                    shell_class_counts[class_name] += 1
                                action["shell_mutation"] = {
                                    "classes": classes,
                                    "command_bytes": len(encoded),
                                    "command_sha256": digest,
                                }
                    actions.append(action)
        sources.append(
            {
                "path": relative_log,
                "session_id": session_id,
                "bytes": log_path.stat().st_size,
                "sha256": sha256_file(log_path),
                "record_count": record_count,
            }
        )

    object_files = sorted(path for path in objects_directory.iterdir() if path.is_file())
    unreferenced_objects = [path.name for path in object_files if path.name not in referenced_objects]
    manifest = {
        "schema": SCHEMA,
        "claude_root": str(claude_root),
        "project_key": arguments.project_key,
        "project_directory": str(project_directory),
        "summary": {
            "source_log_count": len(sources),
            "source_record_count": sum(source["record_count"] for source in sources),
            "malformed_record_count": len(malformed_records),
            "tool_action_count": len(actions),
            "tool_result_count": len(results),
            "successful_tool_action_count": sum(1 for action in actions if action["succeeded"]),
            "failed_or_unpaired_tool_action_count": sum(1 for action in actions if not action["succeeded"]),
            "successful_write_count": sum(
                1 for action in actions if action["succeeded"] and "write" in action
            ),
            "successful_edit_count": sum(
                1 for action in actions if action["succeeded"] and "edit" in action
            ),
            "mutating_shell_action_count": sum(
                1 for action in actions if "shell_mutation" in action
            ),
            "payload_object_count": len(object_files),
            "payload_object_bytes": sum(path.stat().st_size for path in object_files),
            "unreferenced_payload_object_count": len(unreferenced_objects),
        },
        "tool_counts": dict(sorted(tool_counts.items())),
        "shell_mutation_class_counts": dict(sorted(shell_class_counts.items())),
        "sources": sources,
        "malformed_records": malformed_records,
        "actions": actions,
        "unreferenced_payload_objects": unreferenced_objects,
    }
    return manifest


def write_manifest(output_directory: pathlib.Path, manifest: dict[str, Any]) -> pathlib.Path:
    manifest_path = output_directory / "manifest.json"
    encoded = (json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
    descriptor, temporary_name = tempfile.mkstemp(prefix=".manifest.", dir=output_directory)
    temporary_path = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, manifest_path)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()
    return manifest_path


def parse_arguments(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--claude-root", required=True)
    parser.add_argument("--project-key", required=True)
    parser.add_argument("--output-directory", required=True)
    return parser.parse_args(list(argv))


def main(argv: Iterable[str]) -> int:
    try:
        arguments = parse_arguments(argv)
        output_directory = pathlib.Path(arguments.output_directory).resolve()
        output_directory.mkdir(parents=True, exist_ok=True)
        manifest = inventory(arguments)
        manifest_path = write_manifest(output_directory, manifest)
        print(json.dumps(manifest["summary"], sort_keys=True))
        print(f"manifest_sha256={sha256_file(manifest_path)}")
        return 2 if manifest["summary"]["malformed_record_count"] else 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
