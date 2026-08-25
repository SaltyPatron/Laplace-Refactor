#!/usr/bin/env python3
"""Reconstruct missing Claude history bodies from captured successful file tools."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
from typing import Any, Iterable


SCHEMA = "laplace.claude-file-history-reconstruction/v1"


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise RuntimeError(f"expected JSON object: {path}")
    return value


def delta_message_ids(version: dict[str, Any]) -> set[str]:
    return {
        str(observation.get("message_id", ""))
        for observation in version.get("observations", [])
        if observation.get("kind") == "delta"
    }


def action_file_path(action: dict[str, Any]) -> str:
    if isinstance(action.get("write"), dict):
        return str(action["write"].get("file_path", ""))
    if isinstance(action.get("edit"), dict):
        return str(action["edit"].get("file_path", ""))
    return ""


def direct_actions(payload_manifest: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    grouped: dict[str, list[dict[str, Any]]] = {}
    for action in payload_manifest.get("actions", []):
        if not action.get("succeeded") or not ("write" in action or "edit" in action):
            continue
        file_path = action_file_path(action)
        if file_path:
            grouped.setdefault(file_path, []).append(action)
    for actions in grouped.values():
        actions.sort(key=lambda item: (item["timestamp"], item["source_log"], item["source_line"]))
    return grouped


def shell_actions(payload_manifest: dict[str, Any]) -> list[dict[str, Any]]:
    actions = [
        action
        for action in payload_manifest.get("actions", [])
        if action.get("succeeded") and isinstance(action.get("shell_mutation"), dict)
    ]
    actions.sort(key=lambda item: (item["timestamp"], item["source_log"], item["source_line"]))
    return actions


def actions_between(
    actions: list[dict[str, Any]],
    start: dict[str, Any],
    end: dict[str, Any],
) -> list[dict[str, Any]]:
    start_ids = delta_message_ids(start)
    end_ids = delta_message_ids(end)
    selected = []
    for action in actions:
        timestamp = str(action.get("timestamp", ""))
        message_id = str(action.get("message_id", ""))
        after_start = timestamp > start["backup_time"] or message_id in start_ids
        before_end = timestamp < end["backup_time"] and message_id not in end_ids
        if after_start and before_end:
            selected.append(action)
    return selected


def apply_actions(
    initial: bytes,
    actions: list[dict[str, Any]],
    objects_directory: pathlib.Path,
) -> tuple[bytes, list[dict[str, Any]], list[str]]:
    state = initial
    applied: list[dict[str, Any]] = []
    errors: list[str] = []
    for action in actions:
        if "write" in action:
            digest = action["write"]["content_sha256"]
            state = (objects_directory / digest).read_bytes()
            applied.append({"tool_use_id": action["tool_use_id"], "kind": "write"})
            continue
        edit = action["edit"]
        old = (objects_directory / edit["old_sha256"]).read_bytes()
        new = (objects_directory / edit["new_sha256"]).read_bytes()
        count = state.count(old)
        if count == 0:
            errors.append(f"{action['tool_use_id']}: old content not found")
            break
        if not edit["replace_all"] and count != 1:
            errors.append(f"{action['tool_use_id']}: old content matched {count} times")
            break
        state = state.replace(old, new) if edit["replace_all"] else state.replace(old, new, 1)
        applied.append({"tool_use_id": action["tool_use_id"], "kind": "edit"})
    return state, applied, errors


def command_mentions_path(
    action: dict[str, Any],
    command: str,
    version: dict[str, Any],
) -> bool:
    original = str(version.get("original_path", ""))
    tracking = str(version.get("tracking_path", ""))
    repo_relative = str(version.get("repository_relative_path", ""))
    tokens = [value for value in (original, tracking, repo_relative) if value]
    if any(token in command for token in tokens):
        return True
    original_path = pathlib.Path(original)
    return (
        str(action.get("working_directory", "")) == str(original_path.parent)
        and original_path.name in command
    )


def shell_mentions_between(
    shell: list[dict[str, Any]],
    start_time: str,
    end: dict[str, Any],
    version: dict[str, Any],
    objects_directory: pathlib.Path,
) -> list[dict[str, Any]]:
    end_ids = delta_message_ids(end)
    matches = []
    for action in shell:
        timestamp = str(action.get("timestamp", ""))
        message_id = str(action.get("message_id", ""))
        if not (timestamp > start_time and timestamp < end["backup_time"] and message_id not in end_ids):
            continue
        digest = action["shell_mutation"]["command_sha256"]
        command = (objects_directory / digest).read_text(encoding="utf-8")
        if command_mentions_path(action, command, version):
            matches.append(
                {
                    "tool_use_id": action["tool_use_id"],
                    "timestamp": timestamp,
                    "command_sha256": digest,
                    "classes": action["shell_mutation"]["classes"],
                }
            )
    return matches


def write_body(path: pathlib.Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if path.read_bytes() != content:
            raise RuntimeError(f"refusing to replace different body: {path}")
        return
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary_path = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, path)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


def git_blob_status(repository: pathlib.Path, content: bytes) -> tuple[str, bool]:
    header = f"blob {len(content)}\0".encode("ascii")
    object_id = hashlib.sha1(header + content).hexdigest()
    result = subprocess.run(
        ["git", "-C", str(repository), "cat-file", "-e", f"{object_id}^{{blob}}"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return object_id, result.returncode == 0


def reconstruct(arguments: argparse.Namespace) -> dict[str, Any]:
    history_path = pathlib.Path(arguments.history_manifest).resolve()
    payload_path = pathlib.Path(arguments.payload_manifest).resolve()
    repository = pathlib.Path(arguments.repository).resolve()
    output_directory = pathlib.Path(arguments.output_directory).resolve()
    history = load_json(history_path)
    payload = load_json(payload_path)
    objects_directory = payload_path.parent / "objects"
    direct = direct_actions(payload)
    shell = shell_actions(payload)

    groups: dict[str, list[dict[str, Any]]] = {}
    for version in history.get("versions", []):
        groups.setdefault(str(version["original_path"]), []).append(version)
    for versions in groups.values():
        versions.sort(key=lambda item: (item["backup_time"], item["version"]))

    results: list[dict[str, Any]] = []
    for original_path, versions in sorted(groups.items()):
        file_actions = direct.get(original_path, [])
        for target_index, target in enumerate(versions):
            if target["backup_exists"]:
                continue
            prior = next(
                (version for version in reversed(versions[:target_index]) if version["backup_exists"]),
                None,
            )
            candidate: bytes | None = None
            applied: list[dict[str, Any]] = []
            errors: list[str] = []
            source_kind = ""
            start_time = ""

            if prior is not None:
                candidate = pathlib.Path(prior["backup_path"]).read_bytes()
                selected = actions_between(file_actions, prior, target)
                candidate, applied, errors = apply_actions(candidate, selected, objects_directory)
                source_kind = "history-body-and-tools"
                start_time = prior["backup_time"]
            else:
                end_ids = delta_message_ids(target)
                writes = [
                    action
                    for action in file_actions
                    if "write" in action
                    and action["timestamp"] < target["backup_time"]
                    and action["message_id"] not in end_ids
                ]
                if writes:
                    write_action = writes[-1]
                    digest = write_action["write"]["content_sha256"]
                    candidate = (objects_directory / digest).read_bytes()
                    start_time = write_action["timestamp"]
                    later = [
                        action
                        for action in file_actions
                        if action["timestamp"] > start_time
                        and action["timestamp"] < target["backup_time"]
                        and action["message_id"] not in end_ids
                    ]
                    candidate, applied, errors = apply_actions(candidate, later, objects_directory)
                    applied.insert(0, {"tool_use_id": write_action["tool_use_id"], "kind": "write"})
                    source_kind = "captured-write-and-tools"

            result: dict[str, Any] = {
                "session_id": target["session_id"],
                "tracking_path": target["tracking_path"],
                "original_path": original_path,
                "backup_file_name": target["backup_file_name"],
                "version": target["version"],
                "backup_time": target["backup_time"],
                "source_kind": source_kind,
                "applied_tools": applied,
                "replay_errors": errors,
                "status": "unresolved",
            }
            if candidate is None or errors:
                results.append(result)
                continue

            shell_matches = shell_mentions_between(
                shell, start_time, target, target, objects_directory
            )
            next_known = next(
                (version for version in versions[target_index + 1 :] if version["backup_exists"]),
                None,
            )
            next_anchor_match = False
            next_anchor_checked = False
            next_anchor_errors: list[str] = []
            if next_known is not None:
                next_anchor_checked = True
                selected = actions_between(file_actions, target, next_known)
                replayed, _, next_anchor_errors = apply_actions(candidate, selected, objects_directory)
                if not next_anchor_errors:
                    next_anchor_match = replayed == pathlib.Path(next_known["backup_path"]).read_bytes()

            if next_anchor_match:
                status = "validated-against-later-history-body"
                bucket = "validated"
            elif next_anchor_checked and not next_anchor_match:
                status = "candidate-later-transition-unexplained"
                bucket = "candidates"
            elif shell_matches:
                status = "candidate-with-unmodeled-shell-activity"
                bucket = "candidates"
            else:
                status = "deterministic-captured-tool-chain"
                bucket = "validated"

            body_path = (
                output_directory
                / bucket
                / target["session_id"]
                / target["backup_file_name"]
            )
            write_body(body_path, candidate)
            blob_id, git_present = git_blob_status(repository, candidate)
            result.update(
                {
                    "status": status,
                    "content_bytes": len(candidate),
                    "content_sha256": sha256_bytes(candidate),
                    "git_blob_id": blob_id,
                    "git_object_present": git_present,
                    "materialized_path": str(body_path),
                    "shell_mentions": shell_matches,
                    "next_anchor_checked": next_anchor_checked,
                    "next_anchor_match": next_anchor_match,
                    "next_anchor_errors": next_anchor_errors,
                }
            )
            results.append(result)

    return {
        "schema": SCHEMA,
        "history_manifest": str(history_path),
        "history_manifest_sha256": sha256_file(history_path),
        "payload_manifest": str(payload_path),
        "payload_manifest_sha256": sha256_file(payload_path),
        "repository": str(repository),
        "summary": {
            "missing_body_count": len(results),
            "validated_body_count": sum(1 for result in results if result["status"].startswith("validated") or result["status"].startswith("deterministic")),
            "candidate_body_count": sum(1 for result in results if result["status"].startswith("candidate")),
            "unresolved_body_count": sum(1 for result in results if result["status"] == "unresolved"),
            "materialized_body_count": sum(1 for result in results if "materialized_path" in result),
            "materialized_body_absent_from_git_count": sum(
                1 for result in results if "materialized_path" in result and not result["git_object_present"]
            ),
        },
        "results": results,
    }


def write_manifest(output_directory: pathlib.Path, manifest: dict[str, Any]) -> pathlib.Path:
    output_directory.mkdir(parents=True, exist_ok=True)
    path = output_directory / "manifest.json"
    encoded = (json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
    descriptor, temporary_name = tempfile.mkstemp(prefix=".manifest.", dir=output_directory)
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
    return path


def parse_arguments(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--history-manifest", required=True)
    parser.add_argument("--payload-manifest", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--output-directory", required=True)
    return parser.parse_args(list(argv))


def main(argv: Iterable[str]) -> int:
    try:
        arguments = parse_arguments(argv)
        manifest = reconstruct(arguments)
        manifest_path = write_manifest(pathlib.Path(arguments.output_directory).resolve(), manifest)
        print(json.dumps(manifest["summary"], sort_keys=True))
        print(f"manifest_sha256={sha256_file(manifest_path)}")
        return 2 if manifest["summary"]["unresolved_body_count"] else 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
