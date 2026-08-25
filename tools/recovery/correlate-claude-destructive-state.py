#!/usr/bin/env python3
"""Correlate destructive command candidates with preserved state evidence.

The output keeps five findings separate: executable syntax, tool transport outcome,
corroborated command execution, observed state transition, and unique-state loss.
No later finding is inferred solely from an earlier one.
"""

from __future__ import annotations

import argparse
import bisect
import collections
import datetime as dt
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import types
from typing import Any, Iterable


SCHEMA = "laplace.recovery.claude-destructive-state-correlation/v2"
DISPOSITION_SCHEMA = "laplace.recovery.claude-destructive-invocation-disposition/v2"
REFLOG_SCHEMA = "laplace.recovery.git-reflog-event/v1"
REFLOG_LINE = re.compile(
    r"^([0-9a-f]{40,64}) ([0-9a-f]{40,64}) (.*) (\d+) ([+-]\d{4})\t(.*)$"
)


def load_sibling(name: str) -> types.ModuleType:
    path = pathlib.Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load sibling recovery tool: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


SHELL_MUTATIONS = load_sibling("inventory-claude-shell-mutations.py")
DESTRUCTIVE_SHELL = load_sibling("index-claude-destructive-shell-actions.py")

NULL_SEMANTIC_MARKERS: tuple[tuple[str, bytes, str], ...] = (
    (
        "snapshot-missing-path",
        b'tengu_file_history_backup_deleted_file",',
        "a missing tracked path is serialized with a null backup reference",
    ),
    (
        "snapshot-missing-log",
        b"FileHistory: Missing tracked file:",
        "the snapshot path labels the null state as a missing tracked file",
    ),
    (
        "rewind-null-deletes",
        b"FileHistory: [Rewind] Deleted",
        "rewinding to a null backup state deletes the tracked path",
    ),
    (
        "initial-missing-path",
        b"return{backupFileName:null,version:",
        "initial tracking also emits null when the path stat reports absence",
    ),
)

DIRECT_FILE_MUTATORS = {
    "Edit",
    "MultiEdit",
    "NotebookEdit",
    "Write",
}
DIRECT_FILE_READERS = {
    "Glob",
    "Grep",
    "Read",
}
SHELL_MUTATION_CLASSES = {
    "copy-move-remove",
    "git-tree-change",
    "heredoc-file-write",
    "in-place-edit",
    "language-file-write",
    "package-install",
    "patch-application",
    "postgres-client",
}


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


def verify_null_semantics(executable: pathlib.Path) -> dict[str, Any]:
    if not executable.is_file():
        raise RuntimeError(f"Claude executable is missing: {executable}")
    content = executable.read_bytes()
    matches = []
    for marker_id, marker, interpretation in NULL_SEMANTIC_MARKERS:
        offsets = []
        start = 0
        while True:
            offset = content.find(marker, start)
            if offset < 0:
                break
            offsets.append(offset)
            start = offset + len(marker)
        if not offsets:
            raise RuntimeError(
                f"Claude executable does not contain required null-state marker: {marker_id}"
            )
        window_start = max(0, offsets[0] - 360)
        window_end = min(len(content), offsets[0] + len(marker) + 520)
        window = content[window_start:window_end]
        matches.append(
            {
                "marker_id": marker_id,
                "marker_sha256": sha256_bytes(marker),
                "marker_bytes": len(marker),
                "offsets": offsets,
                "interpretation": interpretation,
                "evidence_window_start": window_start,
                "evidence_window_bytes": len(window),
                "evidence_window_sha256": sha256_bytes(window),
                "evidence_window_utf8": window.decode("utf-8", errors="replace"),
            }
        )
    return {
        "executable_path": str(executable.resolve()),
        "executable_name": executable.name,
        "executable_bytes": len(content),
        "executable_sha256": sha256_bytes(content),
        "matched_markers": matches,
        "scope": (
            "direct source evidence for the installed executable; archived session "
            "records are evaluated separately as historical observations"
        ),
    }


def verify_null_semantics_spec(value: str) -> dict[str, Any]:
    parts = value.split(",", 3)
    if len(parts) != 4 or not all(parts):
        raise RuntimeError(
            "Claude executable specification must be VERSION,PLATFORM,SOURCE_URL,PATH"
        )
    version, platform, source_url, path_text = parts
    receipt = verify_null_semantics(pathlib.Path(path_text).resolve())
    return {
        **receipt,
        "client_version": version,
        "platform": platform,
        "source_url": source_url,
    }


def load_json_object(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise RuntimeError(f"expected JSON object: {path}")
    return value


def verify_manifest_file(directory: pathlib.Path, manifest: dict[str, Any], name: str) -> pathlib.Path:
    matches = [item for item in manifest.get("files", []) if item.get("path") == name]
    if len(matches) != 1:
        raise RuntimeError(f"manifest does not identify exactly one {name}")
    path = directory / name
    expected = matches[0]
    if not path.is_file():
        raise RuntimeError(f"manifest file is missing: {path}")
    observed_bytes = path.stat().st_size
    observed_digest = sha256_file(path)
    if observed_bytes != int(expected["bytes"]) or observed_digest != expected["sha256"]:
        raise RuntimeError(
            f"input verification failed for {path}: expected "
            f"{expected['bytes']}/{expected['sha256']}, observed "
            f"{observed_bytes}/{observed_digest}"
        )
    return path


def parse_time(value: str) -> dt.datetime | None:
    if not value:
        return None
    try:
        parsed = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=dt.timezone.utc)
    return parsed.astimezone(dt.timezone.utc)


def epoch_seconds(value: str) -> float | None:
    parsed = parse_time(value)
    return parsed.timestamp() if parsed is not None else None


def state_time(state: dict[str, Any]) -> float | None:
    values = []
    for descriptor in state.get("descriptor_variants", []):
        if isinstance(descriptor, dict):
            parsed = epoch_seconds(str(descriptor.get("backupTime", "")))
            if parsed is not None:
                values.append(parsed)
    return max(values) if values else None


def state_observation_points(state: dict[str, Any]) -> list[dict[str, Any]]:
    points = []
    for observation in state.get("observations", []):
        if not isinstance(observation, dict):
            continue
        observed = epoch_seconds(str(observation.get("snapshot_timestamp", "")))
        if observed is None:
            observed = epoch_seconds(str(observation.get("record_timestamp", "")))
        if observed is None:
            continue
        point = dict(state)
        point["_time"] = observed
        point["_observation"] = {
            "kind": observation.get("kind", ""),
            "message_id": observation.get("message_id", ""),
            "snapshot_message_id": observation.get("snapshot_message_id", ""),
            "source_content_sha256": observation.get("source_content_sha256", ""),
            "source_line": observation.get("source_line", 0),
            "snapshot_timestamp": observation.get("snapshot_timestamp", ""),
            "record_timestamp": observation.get("record_timestamp", ""),
        }
        points.append(point)
    if points:
        return points
    point = dict(state)
    point["_time"] = state_time(state)
    point["_observation"] = None
    return [point] if point["_time"] is not None else []


def strip_worktree_prefix(value: str) -> str:
    parts = pathlib.PurePosixPath(value).parts
    if len(parts) >= 3 and parts[0] == ".worktrees":
        parts = parts[2:]
    return pathlib.PurePosixPath(*parts).as_posix() if parts else ""


def canonical_repository_path(
    value: str,
    repository: pathlib.Path,
    windows_repository_roots: list[str],
) -> str:
    normalized = value.replace("\\", "/").strip()
    repository_text = repository.resolve().as_posix().rstrip("/")
    if normalized.startswith(repository_text + "/"):
        return strip_worktree_prefix(normalized[len(repository_text) + 1 :])
    lowered = normalized.lower()
    for root in windows_repository_roots:
        root_normalized = root.replace("\\", "/").rstrip("/")
        if lowered.startswith(root_normalized.lower() + "/"):
            return strip_worktree_prefix(normalized[len(root_normalized) + 1 :])
    return ""


def expected_head_reflog(working_directory: str, repository: pathlib.Path) -> str:
    normalized = working_directory.replace("\\", "/")
    root = repository.resolve().as_posix().rstrip("/")
    marker = root + "/.worktrees/"
    if normalized.startswith(marker):
        remainder = normalized[len(marker) :]
        name = remainder.split("/", 1)[0]
        if name:
            return f"worktrees/{name}/HEAD"
    return "HEAD"


def git_common_directory(repository: pathlib.Path) -> pathlib.Path:
    result = subprocess.run(
        ["git", "-C", str(repository), "rev-parse", "--git-common-dir"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise RuntimeError(f"git common-dir lookup failed: {result.stderr.strip()}")
    value = pathlib.Path(result.stdout.strip())
    return value.resolve() if value.is_absolute() else (repository / value).resolve()


def reflog_paths(common: pathlib.Path) -> list[tuple[pathlib.Path, str]]:
    paths: list[tuple[pathlib.Path, str]] = []
    main_logs = common / "logs"
    if main_logs.is_dir():
        for path in sorted(item for item in main_logs.rglob("*") if item.is_file()):
            paths.append((path, path.relative_to(main_logs).as_posix()))
    worktrees = common / "worktrees"
    if worktrees.is_dir():
        for worktree in sorted(item for item in worktrees.iterdir() if item.is_dir()):
            logs = worktree / "logs"
            if not logs.is_dir():
                continue
            for path in sorted(item for item in logs.rglob("*") if item.is_file()):
                relative = path.relative_to(logs).as_posix()
                paths.append((path, f"worktrees/{worktree.name}/{relative}"))
    return paths


def read_reflogs(repository: pathlib.Path) -> list[dict[str, Any]]:
    common = git_common_directory(repository)
    records: list[dict[str, Any]] = []
    for path, ref_name in reflog_paths(common):
        with path.open("rb") as stream:
            for line_number, raw in enumerate(stream, start=1):
                text = raw.rstrip(b"\n").decode("utf-8", errors="replace")
                match = REFLOG_LINE.match(text)
                if not match:
                    records.append(
                        {
                            "schema": REFLOG_SCHEMA,
                            "event_id": sha256_bytes(
                                f"{ref_name}\0{line_number}\0".encode("utf-8") + raw
                            ),
                            "ref_name": ref_name,
                            "source_path": str(path),
                            "source_line": line_number,
                            "raw_line_sha256": sha256_bytes(raw),
                            "valid": False,
                        }
                    )
                    continue
                old_id, new_id, actor, timestamp, timezone, message = match.groups()
                records.append(
                    {
                        "schema": REFLOG_SCHEMA,
                        "event_id": sha256_bytes(
                            f"{ref_name}\0{line_number}\0".encode("utf-8") + raw
                        ),
                        "ref_name": ref_name,
                        "source_path": str(path),
                        "source_line": line_number,
                        "raw_line_sha256": sha256_bytes(raw),
                        "valid": True,
                        "old_object_id": old_id,
                        "new_object_id": new_id,
                        "object_id_changed": old_id != new_id,
                        "actor": actor,
                        "timestamp": int(timestamp),
                        "timezone": timezone,
                        "message": message,
                    }
                )
    return records


def operation_reflog_prefix(invocation: dict[str, Any]) -> str:
    category = str(invocation.get("category", ""))
    operation = str(invocation.get("operation", ""))
    if category == "git-worktree-reset" or operation == "reset":
        return "reset:"
    if category == "git-ref-force-update" and operation == "branch":
        return "branch:"
    return ""


def matching_reflog_events(
    invocation: dict[str, Any],
    call: dict[str, Any],
    result_records: list[dict[str, Any]],
    reflogs: list[dict[str, Any]],
    repository: pathlib.Path,
) -> list[dict[str, Any]]:
    prefix = operation_reflog_prefix(invocation)
    start = epoch_seconds(str(call.get("timestamp", "")))
    result_times = [
        value
        for value in (epoch_seconds(str(item.get("timestamp", ""))) for item in result_records)
        if value is not None
    ]
    if not prefix or start is None:
        return []
    end = max(result_times) if result_times else start
    expected_ref = expected_head_reflog(str(invocation.get("working_directory", "")), repository)
    if prefix == "branch:":
        subjects = invocation.get("subjects", [])
        branch = str(subjects[0].get("expanded", "")) if subjects else ""
        if branch:
            expected_ref = f"refs/heads/{branch}"
    matches = []
    for event in reflogs:
        if not event.get("valid"):
            continue
        if event.get("ref_name") != expected_ref:
            continue
        timestamp = float(event["timestamp"])
        if timestamp < start - 2.0 or timestamp > end + 2.0:
            continue
        if not str(event.get("message", "")).startswith(prefix):
            continue
        matches.append(event)
    return sorted(matches, key=lambda item: (item["timestamp"], item["source_line"]))


def state_summary(state: dict[str, Any], contents: dict[str, dict[str, Any]]) -> dict[str, Any]:
    content_records = [
        contents[digest]
        for digest in state.get("content_sha256_variants", [])
        if digest in contents
    ]
    return {
        "state_version_id": state["state_version_id"],
        "observation_time_epoch": state.get("_time"),
        "observation": state.get("_observation"),
        "version": state.get("version"),
        "backup_file_name": state.get("backup_file_name"),
        "resolution": state.get("resolution"),
        "content_sha256_variants": state.get("content_sha256_variants", []),
        "body_absent_from_git_object_database": any(
            not item.get("git_object_present", False) for item in content_records
        ),
        "body_absent_from_all_refs": any(
            not item.get("reachable_from_any_ref", False) for item in content_records
        ),
        "body_absent_from_baseline": any(
            not item.get("reachable_from_baseline", False) for item in content_records
        ),
    }


def history_neighbors(
    states: list[dict[str, Any]],
    call_time: float | None,
    result_time: float | None,
    contents: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    timed = [state for state in states if state.get("_time") is not None]
    before = [] if call_time is None else [state for state in timed if state["_time"] <= call_time]
    after = [] if result_time is None else [state for state in timed if state["_time"] >= result_time]
    during = []
    if call_time is not None and result_time is not None:
        during = [
            state for state in timed if call_time - 2.0 <= state["_time"] <= result_time + 2.0
        ]
    prior = max(before, key=lambda item: item["_time"]) if before else None
    following = min(after, key=lambda item: item["_time"]) if after else None
    return {
        "state_count": len({state["state_version_id"] for state in states}),
        "observation_count": len(states),
        "nearest_prior": state_summary(prior, contents) if prior else None,
        "nearest_following": state_summary(following, contents) if following else None,
        "interval_states": [state_summary(state, contents) for state in during],
        "prior_seconds": call_time - prior["_time"] if prior and call_time is not None else None,
        "following_seconds": following["_time"] - result_time
        if following and result_time is not None
        else None,
    }


def canonical_input_path(
    value: str,
    working_directory: str,
    repository: pathlib.Path,
    windows_repository_roots: list[str],
) -> str:
    direct = canonical_repository_path(value, repository, windows_repository_roots)
    if direct:
        return direct
    normalized = value.replace("\\", "/").strip()
    if not normalized or normalized.startswith(("$", "~")):
        return ""
    working = working_directory.replace("\\", "/").rstrip("/")
    if normalized.startswith("/") or re.match(r"^[A-Za-z]:/", normalized):
        return ""
    combined = str(pathlib.PurePosixPath(working) / pathlib.PurePosixPath(normalized))
    return canonical_repository_path(combined, repository, windows_repository_roots)


def explicit_input_paths(value: Any) -> list[str]:
    selected_keys = {
        "destination",
        "destination_path",
        "file",
        "file_path",
        "notebook_path",
        "output_file",
        "path",
        "source",
        "source_path",
        "target",
        "target_path",
    }
    paths: list[str] = []

    def visit(item: Any, key: str = "") -> None:
        if isinstance(item, dict):
            for nested_key, nested in item.items():
                visit(nested, str(nested_key).lower())
        elif isinstance(item, list):
            for nested in item:
                visit(nested, key)
        elif isinstance(item, str) and key in selected_keys:
            paths.append(item)

    visit(value)
    return sorted(set(paths))


def shell_profile(
    tool_name: str,
    tool_input: dict[str, Any],
    working_directory: str,
    repository: pathlib.Path,
    windows_repository_roots: list[str],
) -> dict[str, Any]:
    command = str(tool_input.get("command", ""))
    classes: set[str] = set()
    parse_error = ""
    repository_paths: set[str] = set()
    if tool_name == "Bash":
        documents, scrubbed = SHELL_MUTATIONS.extract_heredocs(
            command, working_directory or str(repository)
        )
        classes.update(SHELL_MUTATIONS.command_classes(scrubbed, documents))
        tokens, parse_error = DESTRUCTIVE_SHELL.shell_tokens(command)
        if any(token in {">", ">>", ">|", "<>"} for token in tokens):
            classes.add("shell-redirection-write")
        invocations, destructive_error = DESTRUCTIVE_SHELL.invocation_records(
            command, working_directory, repository
        )
        parse_error = parse_error or destructive_error
        for invocation in invocations:
            for target in invocation.get("targets", []):
                path = canonical_repository_path(
                    str(target.get("resolved", "")), repository, windows_repository_roots
                )
                if path:
                    repository_paths.add(path)
        for document in documents:
            path = canonical_repository_path(
                str(document.get("resolved_target", "")),
                repository,
                windows_repository_roots,
            )
            if path:
                repository_paths.add(path)
    else:
        invocations, parse_error = DESTRUCTIVE_SHELL.powershell_invocation_records(
            command, working_directory, repository
        )
        for invocation in invocations:
            classes.add(str(invocation.get("category", "powershell-mutation")))
            for target in invocation.get("targets", []):
                path = canonical_repository_path(
                    str(target.get("resolved", "")), repository, windows_repository_roots
                )
                if path:
                    repository_paths.add(path)
        if re.search(
            r"\b(?:Add-Content|Clear-Content|Copy-Item|Move-Item|New-Item|"
            r"Out-File|Remove-Item|Rename-Item|Set-Content)\b",
            command,
            flags=re.IGNORECASE,
        ):
            classes.add("powershell-content-mutation")
    mutating = bool(classes & SHELL_MUTATION_CLASSES) or any(
        value.startswith(("git-", "powershell-"))
        for value in classes
    ) or "shell-redirection-write" in classes
    return {
        "mutation_kind": "shell" if mutating else "none",
        "mutation_classes": sorted(classes),
        "repository_paths": sorted(repository_paths),
        "unresolved_mutation_scope": bool(parse_error) or mutating,
        "parse_error": parse_error,
    }


def summarize_tool_call(
    descriptor: dict[str, Any],
    record: dict[str, Any],
    block: dict[str, Any],
    repository: pathlib.Path,
    windows_repository_roots: list[str],
) -> dict[str, Any]:
    tool_name = str(descriptor.get("tool_name", block.get("name", "")))
    tool_input = block.get("input") if isinstance(block.get("input"), dict) else {}
    working_directory = str(descriptor.get("working_directory", ""))
    repository_paths = {
        path
        for value in explicit_input_paths(tool_input)
        if (
            path := canonical_input_path(
                value, working_directory, repository, windows_repository_roots
            )
        )
    }
    if tool_name in {"Bash", "PowerShell"}:
        profile = shell_profile(
            tool_name,
            tool_input,
            working_directory,
            repository,
            windows_repository_roots,
        )
        repository_paths.update(profile["repository_paths"])
    elif tool_name in DIRECT_FILE_MUTATORS:
        profile = {
            "mutation_kind": "direct-file",
            "mutation_classes": [tool_name.lower()],
            "unresolved_mutation_scope": not bool(repository_paths),
            "parse_error": "",
        }
    elif tool_name in DIRECT_FILE_READERS:
        profile = {
            "mutation_kind": "none",
            "mutation_classes": [],
            "unresolved_mutation_scope": False,
            "parse_error": "",
        }
    else:
        profile = {
            "mutation_kind": "unclassified-tool",
            "mutation_classes": [],
            "unresolved_mutation_scope": True,
            "parse_error": "",
        }
    encoded_input = canonical_json_bytes(tool_input)
    return {
        "tool_action_id": descriptor.get("tool_action_id", ""),
        "tool_use_id": descriptor.get("tool_use_id", ""),
        "event_id": descriptor.get("event_id", ""),
        "session_id": descriptor.get("session_id", ""),
        "timestamp": descriptor.get("timestamp", ""),
        "time_epoch": epoch_seconds(str(descriptor.get("timestamp", ""))),
        "tool_name": tool_name,
        "working_directory": working_directory,
        "source_content_sha256": descriptor.get("source_content_sha256", ""),
        "source_line": descriptor.get("source_line", 0),
        "block_index": descriptor.get("block_index", 0),
        "client_version": record.get("version", ""),
        "input_sha256": sha256_bytes(encoded_input),
        "input_bytes": len(encoded_input),
        "repository_paths": sorted(repository_paths),
        **profile,
    }


def verified_corpus_source(group: dict[str, Any]) -> pathlib.Path:
    digest = str(group.get("content_sha256", ""))
    for value in group.get("paths", []):
        path = pathlib.Path(str(value))
        if path.is_file() and sha256_file(path) == digest:
            return path
    raise RuntimeError(f"no verified corpus source for content {digest}")


def load_global_tool_calls(
    evidence_directory: pathlib.Path,
    evidence_manifest: dict[str, Any],
    repository: pathlib.Path,
    windows_repository_roots: list[str],
) -> list[dict[str, Any]]:
    tool_actions_path = verify_manifest_file(
        evidence_directory, evidence_manifest, "tool-actions.jsonl"
    )
    corpora_path = pathlib.Path(str(evidence_manifest["inputs"]["corpora_manifest"]))
    if not corpora_path.is_file() or sha256_file(corpora_path) != evidence_manifest["inputs"].get(
        "corpora_manifest_sha256"
    ):
        raise RuntimeError("evidence-index corpus manifest verification failed")
    corpora = load_json_object(corpora_path)
    groups = {
        str(group.get("content_sha256", "")): group
        for group in corpora.get("content_groups", [])
    }
    requests: dict[str, dict[int, list[dict[str, Any]]]] = collections.defaultdict(
        lambda: collections.defaultdict(list)
    )
    with tool_actions_path.open("r", encoding="utf-8") as stream:
        for line in stream:
            action = json.loads(line)
            for call in action.get("calls", []):
                descriptor = dict(call)
                descriptor["tool_action_id"] = action.get("tool_action_id", "")
                requests[str(call.get("source_content_sha256", ""))][
                    int(call.get("source_line", 0))
                ].append(descriptor)
    calls = []
    for digest in sorted(requests):
        if digest not in groups:
            raise RuntimeError(f"tool call source is absent from corpus manifest: {digest}")
        path = verified_corpus_source(groups[digest])
        remaining = set(requests[digest])
        with path.open("rb") as stream:
            for line_number, raw in enumerate(stream, start=1):
                if line_number not in remaining:
                    continue
                record = json.loads(raw)
                message = record.get("message") if isinstance(record, dict) else None
                blocks = message.get("content") if isinstance(message, dict) else None
                if not isinstance(blocks, list):
                    raise RuntimeError(f"tool call has no block list at {path}:{line_number}")
                for descriptor in requests[digest][line_number]:
                    index = int(descriptor.get("block_index", -1))
                    if index < 0 or index >= len(blocks):
                        raise RuntimeError(f"tool block index is invalid at {path}:{line_number}")
                    block = blocks[index]
                    if not isinstance(block, dict) or block.get("type") != "tool_use":
                        raise RuntimeError(f"tool locator is invalid at {path}:{line_number}")
                    if str(block.get("id", "")) != str(descriptor.get("tool_use_id", "")):
                        raise RuntimeError(f"tool identity mismatch at {path}:{line_number}")
                    calls.append(
                        summarize_tool_call(
                            descriptor,
                            record,
                            block,
                            repository,
                            windows_repository_roots,
                        )
                    )
                remaining.remove(line_number)
        if remaining:
            raise RuntimeError(f"tool source lines were not found in {path}: {sorted(remaining)}")
    return sorted(
        (call for call in calls if call.get("time_epoch") is not None),
        key=lambda item: (item["time_epoch"], item["tool_action_id"], item["source_line"]),
    )


def calls_between(
    calls: list[dict[str, Any]],
    call_times: list[float],
    start: float,
    end: float,
    excluded_action_id: str,
    target_path: str,
) -> dict[str, Any]:
    begin = bisect.bisect_right(call_times, start)
    finish = bisect.bisect_left(call_times, end)
    selected = [
        call
        for call in calls[begin:finish]
        if call.get("tool_action_id") != excluded_action_id
    ]
    same_target = [
        call
        for call in selected
        if call.get("mutation_kind") != "none"
        and target_path in call.get("repository_paths", [])
    ]
    unresolved = [
        call
        for call in selected
        if call.get("mutation_kind") != "none"
        and call.get("unresolved_mutation_scope")
        and target_path not in call.get("repository_paths", [])
    ]
    def locator(call: dict[str, Any]) -> dict[str, Any]:
        return {
            "tool_action_id": call.get("tool_action_id", ""),
            "event_id": call.get("event_id", ""),
            "session_id": call.get("session_id", ""),
            "timestamp": call.get("timestamp", ""),
            "tool_name": call.get("tool_name", ""),
            "source_content_sha256": call.get("source_content_sha256", ""),
            "source_line": call.get("source_line", 0),
            "input_sha256": call.get("input_sha256", ""),
            "mutation_kind": call.get("mutation_kind", ""),
            "mutation_classes": call.get("mutation_classes", []),
            "repository_paths": call.get("repository_paths", []),
            "unresolved_mutation_scope": call.get("unresolved_mutation_scope", False),
        }
    return {
        "interval_tool_call_count": len(selected),
        "same_target_mutator_count": len(same_target),
        "unresolved_mutator_count": len(unresolved),
        "same_target_mutators": [locator(call) for call in same_target],
        "unresolved_mutators": [locator(call) for call in unresolved],
    }


def target_transition(
    category: str,
    action_outcome: str,
    action_id: str,
    target_path: str,
    neighbors: dict[str, Any],
    global_calls: list[dict[str, Any]],
    global_call_times: list[float],
    null_semantics_bound: bool,
) -> dict[str, Any]:
    prior = neighbors.get("nearest_prior")
    following = neighbors.get("nearest_following")
    unresolved = {
        "status": "unresolved",
        "basis": "no qualifying exact before/after state transition",
    }
    result = {
        "intervening_operations": None,
        "actual_state_mutation": dict(unresolved),
        "discarded_worktree_state": dict(unresolved),
        "recoverability": {"status": "unresolved"},
        "unrecoverable_unique_state_loss": {"status": "unresolved"},
        "historical_null_semantics_bound": null_semantics_bound,
    }
    if not prior or not following or not target_path:
        return result
    start = prior.get("observation_time_epoch")
    end = following.get("observation_time_epoch")
    if start is None or end is None or end <= start:
        return result
    intervening = calls_between(
        global_calls, global_call_times, float(start), float(end), action_id, target_path
    )
    result["intervening_operations"] = intervening
    delete_categories = {
        "filesystem-delete",
        "git-untracked-delete",
        "powershell-filesystem-delete",
    }
    prior_content = prior.get("content_sha256_variants", [])
    following_absent = following.get("backup_file_name") is None and not following.get(
        "content_sha256_variants", []
    )
    successful_transport = action_outcome not in {
        "conflicting",
        "reported-nonzero-exit",
        "tool-reported-error",
        "unpaired",
    }
    if (
        category in delete_categories
        and successful_transport
        and prior_content
        and following_absent
        and null_semantics_bound
        and intervening["same_target_mutator_count"] == 0
        and intervening["unresolved_mutator_count"] == 0
    ):
        result["actual_state_mutation"] = {
            "status": "observed",
            "kind": "present-to-absent",
            "basis": (
                "exact target deletion, non-error tool result, observed body before, "
                "observed missing-path state after, and no competing corpus-visible mutator"
            ),
        }
        if prior.get("body_absent_from_git_object_database"):
            result["discarded_worktree_state"] = {
                "status": "observed",
                "kind": "non-git-body-removed-from-worktree",
                "content_sha256": prior_content,
            }
            result["recoverability"] = {
                "status": "recovered",
                "source": "Claude file-history body object",
                "content_sha256": prior_content,
            }
            result["unrecoverable_unique_state_loss"] = {
                "status": "not-observed",
                "basis": "the exact pre-action body is present in the preservation package",
            }
    return result


def result_requests(actions: list[dict[str, Any]]) -> dict[str, dict[int, list[dict[str, Any]]]]:
    requests: dict[str, dict[int, list[dict[str, Any]]]] = collections.defaultdict(
        lambda: collections.defaultdict(list)
    )
    for action in actions:
        for result in action.get("results", []):
            digest = str(result.get("source_content_sha256", ""))
            line = int(result.get("source_line", 0))
            if digest and line:
                requests[digest][line].append(result)
    return requests


def verified_source_for_action(action: dict[str, Any], digest: str) -> pathlib.Path:
    paths = []
    for call in action.get("calls", []):
        if call.get("source_content_sha256") == digest:
            paths.extend(str(value) for value in call.get("source_paths", []))
    for value in sorted(set(paths)):
        path = pathlib.Path(value)
        if path.is_file() and sha256_file(path) == digest:
            return path
    raise RuntimeError(f"no verified source transcript for result content {digest}")


def capture_results(
    actions: list[dict[str, Any]], objects: pathlib.Path
) -> dict[tuple[str, int, int, str], dict[str, Any]]:
    action_by_digest: dict[str, dict[str, Any]] = {}
    for action in actions:
        for call in action.get("calls", []):
            digest = str(call.get("source_content_sha256", ""))
            if digest:
                action_by_digest.setdefault(digest, action)
    requests = result_requests(actions)
    captures: dict[tuple[str, int, int, str], dict[str, Any]] = {}
    for digest in sorted(requests):
        path = verified_source_for_action(action_by_digest[digest], digest)
        requested_lines = requests[digest]
        with path.open("rb") as stream:
            for line_number, raw in enumerate(stream, start=1):
                if line_number not in requested_lines:
                    continue
                record = json.loads(raw)
                message = record.get("message") if isinstance(record, dict) else None
                blocks = message.get("content") if isinstance(message, dict) else None
                if not isinstance(blocks, list):
                    raise RuntimeError(f"result locator has no block list at {path}:{line_number}")
                for descriptor in requested_lines[line_number]:
                    block_index = int(descriptor.get("block_index", -1))
                    if block_index < 0 or block_index >= len(blocks):
                        raise RuntimeError(f"result block index is invalid at {path}:{line_number}")
                    block = blocks[block_index]
                    if not isinstance(block, dict) or block.get("type") != "tool_result":
                        raise RuntimeError(f"result locator does not identify tool_result at {path}:{line_number}")
                    tool_id = str(descriptor.get("tool_use_id", ""))
                    if str(block.get("tool_use_id", "")) != tool_id:
                        raise RuntimeError(f"result tool identity mismatch at {path}:{line_number}")
                    content = canonical_json_bytes(block.get("content"))
                    content_digest = sha256_bytes(content)
                    if content_digest != descriptor.get("content_sha256"):
                        raise RuntimeError(f"result content verification failed at {path}:{line_number}")
                    object_path = objects / content_digest
                    if object_path.exists():
                        if object_path.read_bytes() != content:
                            raise RuntimeError(f"result object collision at {object_path}")
                    else:
                        object_path.write_bytes(content)
                    key = (digest, line_number, block_index, tool_id)
                    captures[key] = {
                        "source_content_sha256": digest,
                        "source_line": line_number,
                        "block_index": block_index,
                        "tool_use_id": tool_id,
                        "content_sha256": content_digest,
                        "content_bytes": len(content),
                    }
    return captures


def result_capture(descriptor: dict[str, Any], captures: dict[tuple[str, int, int, str], dict[str, Any]]) -> dict[str, Any]:
    key = (
        str(descriptor.get("source_content_sha256", "")),
        int(descriptor.get("source_line", 0)),
        int(descriptor.get("block_index", -1)),
        str(descriptor.get("tool_use_id", "")),
    )
    if key not in captures:
        raise RuntimeError(f"missing captured result {key}")
    return captures[key]


def build_index(arguments: argparse.Namespace) -> dict[str, Any]:
    destructive_directory = pathlib.Path(arguments.destructive_index).resolve()
    history_directory = pathlib.Path(arguments.file_history_index).resolve()
    evidence_directory = pathlib.Path(arguments.evidence_index).resolve()
    repository = pathlib.Path(arguments.repository).resolve()
    destination = pathlib.Path(arguments.output_directory).resolve()
    windows_roots = list(arguments.windows_repository_root)
    if destination.exists():
        raise RuntimeError(f"output already exists: {destination}")

    destructive_manifest_path = destructive_directory / "manifest.json"
    history_manifest_path = history_directory / "manifest.json"
    evidence_manifest_path = evidence_directory / "manifest.json"
    destructive_manifest = load_json_object(destructive_manifest_path)
    history_manifest = load_json_object(history_manifest_path)
    evidence_manifest = load_json_object(evidence_manifest_path)
    actions_path = verify_manifest_file(
        destructive_directory, destructive_manifest, "destructive-actions.jsonl"
    )
    states_path = verify_manifest_file(history_directory, history_manifest, "state-versions.jsonl")
    contents_path = verify_manifest_file(history_directory, history_manifest, "content-index.jsonl")

    actions = [json.loads(line) for line in actions_path.read_text(encoding="utf-8").splitlines()]
    contents = {
        item["content_sha256"]: item
        for item in (
            json.loads(line) for line in contents_path.read_text(encoding="utf-8").splitlines()
        )
    }
    null_semantics = [
        verify_null_semantics_spec(value) for value in arguments.claude_executable
    ]
    null_semantics_versions = {
        str(item["client_version"]) for item in null_semantics
    }
    global_calls = load_global_tool_calls(
        evidence_directory, evidence_manifest, repository, windows_roots
    )
    global_call_times = [float(call["time_epoch"]) for call in global_calls]
    global_calls_by_action: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    for global_call in global_calls:
        global_calls_by_action[str(global_call.get("tool_action_id", ""))].append(global_call)
    states_by_session_path: dict[tuple[str, str], list[dict[str, Any]]] = collections.defaultdict(list)
    states_by_path: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    with states_path.open("r", encoding="utf-8") as stream:
        for line in stream:
            state = json.loads(line)
            key = (str(state.get("session_id", "")), str(state.get("repository_relative_path", "")))
            for point in state_observation_points(state):
                states_by_session_path[key].append(point)
                states_by_path[key[1]].append(point)
    for states in states_by_session_path.values():
        states.sort(key=lambda item: (item.get("_time") is None, item.get("_time") or 0.0))

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = pathlib.Path(tempfile.mkdtemp(prefix=f".{destination.name}.", dir=destination.parent))
    result_objects = temporary / "result-objects"
    result_objects.mkdir()
    try:
        captures = capture_results(actions, result_objects)
        reflogs = read_reflogs(repository)
        reflog_path = temporary / "git-reflog-events.jsonl"
        with reflog_path.open("wb") as stream:
            for event in sorted(reflogs, key=lambda item: item["event_id"]):
                stream.write(canonical_json_bytes(event) + b"\n")

        dispositions: list[dict[str, Any]] = []
        category_counts: collections.Counter[str] = collections.Counter()
        execution_counts: collections.Counter[str] = collections.Counter()
        history_match_count = 0
        non_git_prior_count = 0
        for action in actions:
            call = action["calls"][0]
            historical_client_versions = sorted(
                {
                    str(item.get("client_version", ""))
                    for item in global_calls_by_action.get(action["tool_action_id"], [])
                    if item.get("client_version")
                }
            )
            null_semantics_bound = bool(historical_client_versions) and all(
                version in null_semantics_versions
                for version in historical_client_versions
            )
            result_records = action.get("results", [])
            result_times = [
                value
                for value in (
                    epoch_seconds(str(item.get("timestamp", ""))) for item in result_records
                )
                if value is not None
            ]
            call_time = epoch_seconds(str(call.get("timestamp", "")))
            result_time = max(result_times) if result_times else call_time
            result_evidence = [result_capture(item, captures) for item in result_records]
            for invocation_index, invocation in enumerate(call.get("invocations", [])):
                invocation_id = sha256_bytes(
                    f"{action['tool_action_id']}\0{invocation_index}".encode("utf-8")
                )
                category = str(invocation.get("category", ""))
                category_counts[category] += 1
                reflog_matches = matching_reflog_events(
                    invocation, call, result_records, reflogs, repository
                )
                if reflog_matches:
                    execution_disposition = "corroborated-by-reflog"
                elif action["outcome"] in {
                    "reported-nonzero-exit", "tool-reported-error", "unpaired", "conflicting"
                }:
                    execution_disposition = "not-corroborated-tool-failure-or-unknown"
                else:
                    execution_disposition = "tool-success-not-independently-corroborated"
                targets = []
                for target in invocation.get("targets", []):
                    repo_path = canonical_repository_path(
                        str(target.get("resolved", "")), repository, windows_roots
                    )
                    session_states = states_by_session_path.get(
                        (str(call.get("session_id", "")), repo_path), []
                    ) if repo_path else []
                    neighbors = history_neighbors(
                        session_states, call_time, result_time, contents
                    )
                    if session_states:
                        history_match_count += 1
                    prior = neighbors["nearest_prior"]
                    if prior and prior["body_absent_from_git_object_database"]:
                        non_git_prior_count += 1
                    transition = target_transition(
                        category,
                        action["outcome"],
                        action["tool_action_id"],
                        repo_path,
                        neighbors,
                        global_calls,
                        global_call_times,
                        null_semantics_bound,
                    )
                    targets.append(
                        {
                            **target,
                            "repository_relative_path": repo_path,
                            "same_session_history": neighbors,
                            "cross_session_observation_count": len(states_by_path.get(repo_path, []))
                            if repo_path else 0,
                            "state_findings": transition,
                        }
                    )
                observed_targets = [
                    target
                    for target in targets
                    if target["state_findings"]["actual_state_mutation"]["status"]
                    == "observed"
                ]
                discarded_targets = [
                    target
                    for target in targets
                    if target["state_findings"]["discarded_worktree_state"]["status"]
                    == "observed"
                ]
                recovered_targets = [
                    target
                    for target in targets
                    if target["state_findings"]["recoverability"]["status"] == "recovered"
                ]
                if observed_targets and not reflog_matches:
                    execution_disposition = "corroborated-by-observed-state-transition"
                execution_counts[execution_disposition] += 1
                if observed_targets:
                    actual_state_mutation: dict[str, Any] = {
                        "status": "observed",
                        "target_count": len(observed_targets),
                        "target_paths": [
                            target["repository_relative_path"] for target in observed_targets
                        ],
                    }
                elif reflog_matches:
                    actual_state_mutation = {
                        "status": "observed",
                        "kind": "git-reference-transition",
                        "event_ids": [item["event_id"] for item in reflog_matches],
                        "object_id_changed": any(
                            item.get("object_id_changed", False) for item in reflog_matches
                        ),
                    }
                else:
                    actual_state_mutation = {"status": "unresolved"}
                discarded_worktree_state = (
                    {
                        "status": "observed",
                        "target_count": len(discarded_targets),
                        "target_paths": [
                            target["repository_relative_path"] for target in discarded_targets
                        ],
                    }
                    if discarded_targets
                    else {"status": "unresolved"}
                )
                recoverability = (
                    {
                        "status": "recovered",
                        "target_count": len(recovered_targets),
                        "target_paths": [
                            target["repository_relative_path"] for target in recovered_targets
                        ],
                    }
                    if recovered_targets
                    else {"status": "unresolved"}
                )
                dispositions.append(
                    {
                        "schema": DISPOSITION_SCHEMA,
                        "invocation_id": invocation_id,
                        "tool_action_id": action["tool_action_id"],
                        "invocation_index": invocation_index,
                        "session_id": call.get("session_id", ""),
                        "call_timestamp": call.get("timestamp", ""),
                        "historical_client_versions": historical_client_versions,
                        "historical_null_semantics_bound": null_semantics_bound,
                        "result_timestamps": [item.get("timestamp", "") for item in result_records],
                        "category": category,
                        "operation": invocation.get("operation", ""),
                        "working_directory": invocation.get("working_directory", ""),
                        "tool_transport_outcome": action["outcome"],
                        "execution_disposition": execution_disposition,
                        "reflog_event_ids": [item["event_id"] for item in reflog_matches],
                        "reflog_object_change_observed": any(
                            item.get("object_id_changed", False) for item in reflog_matches
                        ),
                        "result_evidence": result_evidence,
                        "targets": targets,
                        "subjects": invocation.get("subjects", []),
                        "actual_state_mutation": actual_state_mutation,
                        "discarded_worktree_state": discarded_worktree_state,
                        "recoverability": recoverability,
                        "unrecoverable_unique_state_loss": (
                            {
                                "status": "not-observed-for-recovered-targets",
                                "target_count": len(recovered_targets),
                            }
                            if recovered_targets
                            else {"status": "unresolved"}
                        ),
                    }
                )

        disposition_path = temporary / "invocation-dispositions.jsonl"
        with disposition_path.open("wb") as stream:
            for item in sorted(dispositions, key=lambda value: value["invocation_id"]):
                stream.write(canonical_json_bytes(item) + b"\n")
        null_semantics_path = temporary / "claude-file-history-null-semantics.json"
        null_semantics_path.write_bytes(canonical_json_bytes(null_semantics) + b"\n")
        files = []
        for path in (disposition_path, reflog_path, null_semantics_path):
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
        object_files = sorted(item for item in result_objects.iterdir() if item.is_file())
        result_object_set = "".join(
            f"{item.name}\t{item.stat().st_size}\n" for item in object_files
        ).encode("utf-8")
        target_findings = [
            target["state_findings"]
            for disposition in dispositions
            for target in disposition["targets"]
        ]
        summary = {
            "tool_action_count": len(actions),
            "invocation_count": len(dispositions),
            "category_counts": dict(sorted(category_counts.items())),
            "execution_disposition_counts": dict(sorted(execution_counts.items())),
            "reflog_event_count": len(reflogs),
            "valid_reflog_event_count": sum(1 for item in reflogs if item.get("valid")),
            "reflog_corroborated_invocation_count": sum(
                1 for item in dispositions if item["reflog_event_ids"]
            ),
            "reflog_object_change_invocation_count": sum(
                1 for item in dispositions if item["reflog_object_change_observed"]
            ),
            "target_with_same_session_history_count": history_match_count,
            "target_with_non_git_prior_body_count": non_git_prior_count,
            "global_tool_call_count": len(global_calls),
            "global_tool_action_count": evidence_manifest.get("summary", {}).get(
                "tool_action_count", 0
            ),
            "result_object_count": len(object_files),
            "result_object_bytes": sum(item.stat().st_size for item in object_files),
            "result_object_filename_set_sha256": sha256_bytes(result_object_set),
            "actual_state_mutation_observed_target_count": sum(
                1
                for item in target_findings
                if item["actual_state_mutation"]["status"] == "observed"
            ),
            "discarded_non_git_worktree_state_observed_target_count": sum(
                1
                for item in target_findings
                if item["discarded_worktree_state"]["status"] == "observed"
            ),
            "file_history_recovered_target_count": sum(
                1
                for item in target_findings
                if item["recoverability"]["status"] == "recovered"
            ),
            "unrecoverable_unique_state_loss_observed_target_count": sum(
                1
                for item in target_findings
                if item["unrecoverable_unique_state_loss"]["status"] == "observed"
            ),
            "invocation_actual_state_mutation_observed_count": sum(
                1
                for item in dispositions
                if item["actual_state_mutation"]["status"] == "observed"
            ),
        }
        manifest = {
            "schema": SCHEMA,
            "inputs": {
                "destructive_index": str(destructive_directory),
                "destructive_manifest_sha256": sha256_file(destructive_manifest_path),
                "file_history_index": str(history_directory),
                "file_history_manifest_sha256": sha256_file(history_manifest_path),
                "evidence_index": str(evidence_directory),
                "evidence_manifest_sha256": sha256_file(evidence_manifest_path),
                "claude_executables": [
                    {
                        "client_version": item["client_version"],
                        "platform": item["platform"],
                        "source_url": item["source_url"],
                        "path": item["executable_path"],
                        "sha256": item["executable_sha256"],
                        "bytes": item["executable_bytes"],
                    }
                    for item in null_semantics
                ],
                "repository": str(repository),
                "git_common_directory": str(git_common_directory(repository)),
                "windows_repository_roots": windows_roots,
            },
            "summary": summary,
            "files": files,
        }
        (temporary / "manifest.json").write_bytes(canonical_json_bytes(manifest) + b"\n")
        os.replace(temporary, destination)
        return manifest
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def parse_arguments(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--destructive-index", required=True)
    parser.add_argument("--file-history-index", required=True)
    parser.add_argument("--evidence-index", required=True)
    parser.add_argument("--claude-executable", action="append", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--windows-repository-root", action="append", default=[])
    parser.add_argument("--output-directory", required=True)
    return parser.parse_args(list(argv) if argv is not None else None)


def main(argv: Iterable[str] | None = None) -> int:
    try:
        manifest = build_index(parse_arguments(argv))
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(manifest["summary"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
