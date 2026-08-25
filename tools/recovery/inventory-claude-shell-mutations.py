#!/usr/bin/env python3
"""Inventory successful Claude shell actions and preserve exact embedded bodies."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile
from typing import Any, Iterable


SCHEMA = "laplace.claude-shell-mutation-recovery/v1"
HEREDOC_OPEN = re.compile(
    r"<<(?P<strip_tabs>-?)[ \t]*(?P<quote>['\"]?)(?P<delimiter>[A-Za-z_][A-Za-z0-9_]*)(?P=quote)"
)
ASSIGNMENT = re.compile(
    r"(?:^|(?:&&|\|\||;|\|)[ \t]*)"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)=(?P<value>\"[^\"]*\"|'[^']*'|[^\s;&|]+)"
)
CD_COMMAND = re.compile(
    r"(?:^|(?:&&|\|\||;|\|)[ \t]*)cd[ \t]+(?P<value>\"[^\"]*\"|'[^']*'|[^\s;&|]+)"
)
VARIABLE = re.compile(r"\$(?:\{(?P<braced>[A-Za-z_][A-Za-z0-9_]*)\}|(?P<plain>[A-Za-z_][A-Za-z0-9_]*))")
TOKEN = r'(?:"[^"]*"|\'[^\']*\'|[^\s;&|]+)'

CLASS_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("postgres-client", re.compile(r"(?:^|[;&|\n]\s*)psql\b", re.I)),
    ("patch-application", re.compile(r"(?:^|[;&|\n]\s*)apply_patch\b|\bgit\s+apply\b", re.I)),
    ("in-place-edit", re.compile(r"\b(?:sed\s+-i|perl\s+-p?i)\b", re.I)),
    (
        "language-file-write",
        re.compile(
            r"\b(?:write_text|write_bytes|File\.WriteAll(?:Text|Bytes)|ofstream|fopen)\b"
            r"|\bopen\s*\([^\n]{0,240},\s*['\"][wax+][^'\"]*['\"]",
            re.I,
        ),
    ),
    (
        "copy-move-remove",
        re.compile(r"(?:^|[;&|\n]\s*)(?:cp|mv|rm|install)\b", re.I),
    ),
    (
        "git-tree-change",
        re.compile(
            r"\bgit\s+(?:checkout|restore|reset|clean|commit|merge|rebase|cherry-pick|stash|branch)\b",
            re.I,
        ),
    ),
    (
        "package-install",
        re.compile(r"\b(?:cmake\s+--install|ninja\s+install|make\s+install)\b", re.I),
    ),
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


def strip_shell_quotes(value: str) -> str:
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
        return value[1:-1]
    return value


def expand_value(value: str, variables: dict[str, str]) -> tuple[str, list[str]]:
    raw = strip_shell_quotes(value)
    unresolved: set[str] = set()

    def replace(match: re.Match[str]) -> str:
        name = match.group("braced") or match.group("plain")
        if name not in variables:
            unresolved.add(name)
            return match.group(0)
        return variables[name]

    prior = None
    expanded = raw
    for _ in range(8):
        if expanded == prior:
            break
        prior = expanded
        expanded = VARIABLE.sub(replace, expanded)
    if "$" in expanded and not unresolved:
        unresolved.add("dynamic-expression")
    return expanded, sorted(unresolved)


def update_shell_context(
    header: str,
    working_directory: pathlib.Path,
    variables: dict[str, str],
) -> tuple[pathlib.Path, dict[str, str], list[str]]:
    current = working_directory
    known = dict(variables)
    unresolved: set[str] = set()
    events: list[tuple[int, str, re.Match[str]]] = []
    events.extend((match.start(), "assignment", match) for match in ASSIGNMENT.finditer(header))
    events.extend((match.start(), "cd", match) for match in CD_COMMAND.finditer(header))
    for _, kind, match in sorted(events, key=lambda item: item[0]):
        expanded, missing = expand_value(match.group("value"), known)
        unresolved.update(missing)
        if kind == "assignment":
            if not missing and not any(token in expanded for token in ("`", "$(")):
                known[match.group("name")] = expanded
            continue
        if missing or "$" in expanded or "`" in expanded or "$(" in expanded:
            continue
        candidate = pathlib.Path(os.path.expanduser(expanded))
        current = candidate if candidate.is_absolute() else current / candidate
        current = pathlib.Path(os.path.normpath(current))
    return current, known, sorted(unresolved)


def sink_from_header(header: str) -> dict[str, str] | None:
    cat = re.search(r"(?:^|(?:&&|\|\||;|\|)\s*)cat\b(?P<arguments>.*)", header)
    if cat:
        arguments = cat.group("arguments")
        redirect = re.search(
            rf"(?<![<>])(?P<mode>>>|>)[ \t]*(?P<target>{TOKEN})", arguments
        )
        if redirect:
            target = redirect.group("target")
            if target not in ("&1", "&2") and not target.startswith("&"):
                return {
                    "producer": "cat",
                    "mode": "append" if redirect.group("mode") == ">>" else "replace",
                    "target_token": target,
                }
    tee = re.search(r"(?:^|(?:&&|\|\||;|\|)\s*)tee\b(?P<arguments>.*)", header)
    if tee:
        try:
            arguments = shlex.split(tee.group("arguments"), posix=True)
        except ValueError:
            arguments = []
        append = "-a" in arguments or "--append" in arguments
        targets = [value for value in arguments if not value.startswith("-") and not value.startswith("<<")]
        if targets:
            return {
                "producer": "tee",
                "mode": "append" if append else "replace",
                "target_token": targets[0],
            }
    return None


def exact_heredoc_delivery(body: bytes, quoted: bool, strip_tabs: bool) -> bytes | None:
    if not quoted:
        return None
    if not strip_tabs:
        return body
    return b"".join(line.lstrip(b"\t") for line in body.splitlines(keepends=True))


def extract_heredocs(command: str, initial_working_directory: str) -> tuple[list[dict[str, Any]], str]:
    lines = command.splitlines(keepends=True)
    working_directory = pathlib.Path(initial_working_directory or ".")
    variables: dict[str, str] = {}
    documents: list[dict[str, Any]] = []
    retained_headers: list[str] = []
    line_index = 0
    while line_index < len(lines):
        header = lines[line_index]
        openers = list(HEREDOC_OPEN.finditer(header))
        if not openers:
            retained_headers.append(header)
            working_directory, variables, _ = update_shell_context(
                header, working_directory, variables
            )
            line_index += 1
            continue

        retained_headers.append(header)
        working_directory, variables, context_unresolved = update_shell_context(
            header, working_directory, variables
        )
        sink = sink_from_header(header)
        body_index = line_index + 1
        for opener in openers:
            delimiter = opener.group("delimiter")
            strip_tabs = opener.group("strip_tabs") == "-"
            quoted = bool(opener.group("quote"))
            terminator_index = body_index
            while terminator_index < len(lines):
                candidate = lines[terminator_index].rstrip("\r\n")
                if strip_tabs:
                    candidate = candidate.lstrip("\t")
                if candidate == delimiter:
                    break
                terminator_index += 1
            if terminator_index == len(lines):
                documents.append(
                    {
                        "header_line": line_index + 1,
                        "delimiter": delimiter,
                        "quoted_delimiter": quoted,
                        "strip_leading_tabs": strip_tabs,
                        "status": "unterminated",
                    }
                )
                body_index = terminator_index
                break

            body_text = "".join(lines[body_index:terminator_index])
            body = body_text.encode("utf-8")
            delivered = exact_heredoc_delivery(body, quoted, strip_tabs)
            document: dict[str, Any] = {
                "header_line": line_index + 1,
                "body_start_line": body_index + 1,
                "body_end_line": terminator_index,
                "delimiter": delimiter,
                "quoted_delimiter": quoted,
                "strip_leading_tabs": strip_tabs,
                "status": "complete",
                "source_body": body,
                "execution_body_exact": delivered is not None,
                "delivered_body": delivered,
                "working_directory": str(working_directory),
                "context_unresolved_variables": context_unresolved,
                "sink": sink,
            }
            if sink is not None:
                expanded, missing = expand_value(sink["target_token"], variables)
                target_unresolved = sorted(set(missing) | set(context_unresolved))
                resolved_target = ""
                if not target_unresolved and not any(token in expanded for token in ("$", "`", "$(")):
                    target = pathlib.Path(os.path.expanduser(expanded))
                    target = target if target.is_absolute() else working_directory / target
                    resolved_target = os.path.normpath(str(target))
                document["target_expanded"] = expanded
                document["target_unresolved_variables"] = target_unresolved
                document["resolved_target"] = resolved_target
            documents.append(document)
            body_index = terminator_index + 1
        line_index = body_index
    return documents, "".join(retained_headers)


def classify_target(path: str, repository: pathlib.Path) -> str:
    if not path:
        return "unresolved"
    target = pathlib.Path(path)
    repository_text = str(repository)
    target_text = str(target)
    if target_text == repository_text or target_text.startswith(repository_text + os.sep):
        return "repository-or-worktree"
    if target_text.startswith("/tmp/claude-"):
        return "claude-scratch"
    if target_text == "/vault" or target_text.startswith("/vault/"):
        return "vault"
    if target_text.startswith("/tmp/"):
        return "temporary"
    if target.is_absolute():
        return "external-absolute"
    return "unresolved"


def canonical_repository_path(path: str, repository: pathlib.Path) -> str:
    if not path:
        return ""
    target = pathlib.Path(path)
    try:
        relative = target.relative_to(repository / ".worktrees")
    except ValueError:
        try:
            target.relative_to(repository)
        except ValueError:
            return ""
        return str(target)
    if len(relative.parts) < 2:
        return ""
    return str(repository.joinpath(*relative.parts[1:]))


def file_observation(path: str) -> dict[str, Any]:
    if not path:
        return {"path": "", "state": "unresolved"}
    target = pathlib.Path(path)
    if not target.exists():
        return {"path": path, "state": "missing"}
    if not target.is_file():
        return {"path": path, "state": "not-regular-file"}
    return {
        "path": path,
        "state": "regular-file",
        "bytes": target.stat().st_size,
        "sha256": sha256_file(target),
    }


def git_reachable_blobs(repository: pathlib.Path) -> set[str]:
    process = subprocess.run(
        ["git", "-C", str(repository), "rev-list", "--objects", "--all"],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    object_ids = {line.split(" ", 1)[0] for line in process.stdout.splitlines() if line}
    if not object_ids:
        return set()
    query = subprocess.run(
        ["git", "-C", str(repository), "cat-file", "--batch-check=%(objectname) %(objecttype)"],
        input="\n".join(sorted(object_ids)) + "\n",
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    return {
        line.split()[0]
        for line in query.stdout.splitlines()
        if len(line.split()) == 2 and line.split()[1] == "blob"
    }


def git_blob_observation(
    repository: pathlib.Path, content: bytes, reachable_blobs: set[str]
) -> dict[str, Any]:
    header = f"blob {len(content)}\0".encode("ascii")
    object_id = hashlib.sha1(header + content).hexdigest()
    present = subprocess.run(
        ["git", "-C", str(repository), "cat-file", "-e", f"{object_id}^{{blob}}"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode == 0
    return {
        "object_id": object_id,
        "object_present": present,
        "ref_reachable": object_id in reachable_blobs,
    }


def tool_results(logs: list[pathlib.Path], project_directory: pathlib.Path) -> dict[str, dict[str, Any]]:
    results: dict[str, dict[str, Any]] = {}
    for log_path in logs:
        relative = log_path.relative_to(project_directory).as_posix()
        with log_path.open("r", encoding="utf-8") as stream:
            for line_number, raw_line in enumerate(stream, start=1):
                if not raw_line.strip():
                    continue
                record = json.loads(raw_line)
                if record.get("type") != "user":
                    continue
                blocks = (record.get("message") or {}).get("content")
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
                        "source_log": relative,
                        "source_line": line_number,
                        "block_index": block_index,
                        "is_error": bool(block.get("is_error", False)),
                        "content_bytes": len(encoded),
                        "content_sha256": sha256_bytes(encoded),
                    }
    return results


def command_classes(scrubbed_command: str, documents: list[dict[str, Any]]) -> list[str]:
    inspected = scrubbed_command + "\n" + "\n".join(
        document.get("source_body", b"").decode("utf-8", errors="replace")
        for document in documents
        if isinstance(document.get("source_body"), bytes)
    )
    classes = {
        name for name, pattern in CLASS_PATTERNS if pattern.search(inspected)
    }
    if any(document.get("sink") for document in documents):
        classes.add("heredoc-file-write")
    if any(not document.get("sink") for document in documents):
        classes.add("heredoc-command-input")
    return sorted(classes)


def session_for_log(project_directory: pathlib.Path, log_path: pathlib.Path) -> str:
    first = log_path.relative_to(project_directory).parts[0]
    return first[:-6] if first.endswith(".jsonl") else first


def inventory(arguments: argparse.Namespace) -> dict[str, Any]:
    claude_root = pathlib.Path(arguments.claude_root).resolve()
    project_directory = claude_root / "projects" / arguments.project_key
    repository = pathlib.Path(arguments.repository).resolve()
    output_directory = pathlib.Path(arguments.output_directory).resolve()
    objects_directory = output_directory / "objects"
    objects_directory.mkdir(parents=True, exist_ok=True)
    if not project_directory.is_dir():
        raise RuntimeError(f"Claude project directory does not exist: {project_directory}")
    if not (repository / ".git").exists():
        raise RuntimeError(f"Git repository does not exist: {repository}")

    logs = sorted(project_directory.rglob("*.jsonl"))
    results = tool_results(logs, project_directory)
    reachable_blobs = git_reachable_blobs(repository)
    sources: list[dict[str, Any]] = []
    actions: list[dict[str, Any]] = []
    malformed: list[dict[str, Any]] = []
    class_counts: collections.Counter[str] = collections.Counter()
    target_counts: collections.Counter[str] = collections.Counter()
    referenced_objects: set[str] = set()

    for log_path in logs:
        relative = log_path.relative_to(project_directory).as_posix()
        record_count = 0
        with log_path.open("r", encoding="utf-8") as stream:
            for line_number, raw_line in enumerate(stream, start=1):
                if not raw_line.strip():
                    continue
                record_count += 1
                try:
                    record = json.loads(raw_line)
                except json.JSONDecodeError as error:
                    malformed.append(
                        {
                            "source_log": relative,
                            "source_line": line_number,
                            "error": str(error),
                            "line_sha256": sha256_bytes(raw_line.encode("utf-8")),
                        }
                    )
                    continue
                if record.get("type") != "assistant":
                    continue
                blocks = (record.get("message") or {}).get("content")
                if not isinstance(blocks, list):
                    continue
                for block_index, block in enumerate(blocks):
                    if not isinstance(block, dict) or block.get("type") != "tool_use":
                        continue
                    if str(block.get("name", "")) != "Bash":
                        continue
                    tool_input = block.get("input") if isinstance(block.get("input"), dict) else {}
                    command = tool_input.get("command")
                    if not isinstance(command, str):
                        continue
                    tool_id = str(block.get("id", ""))
                    result = results.get(tool_id)
                    succeeded = result is not None and not result["is_error"]
                    command_bytes = command.encode("utf-8")
                    command_digest = store_object(objects_directory, command_bytes)
                    referenced_objects.add(command_digest)
                    documents, scrubbed = extract_heredocs(
                        command, str(record.get("cwd", ""))
                    )
                    classes = command_classes(scrubbed, documents)
                    for class_name in classes:
                        class_counts[class_name] += 1
                    encoded_documents: list[dict[str, Any]] = []
                    for document in documents:
                        encoded = {key: value for key, value in document.items() if key not in ("source_body", "delivered_body")}
                        source_body = document.get("source_body")
                        if isinstance(source_body, bytes):
                            digest = store_object(objects_directory, source_body)
                            referenced_objects.add(digest)
                            encoded["source_body_bytes"] = len(source_body)
                            encoded["source_body_sha256"] = digest
                        delivered = document.get("delivered_body")
                        if isinstance(delivered, bytes):
                            digest = store_object(objects_directory, delivered)
                            referenced_objects.add(digest)
                            encoded["delivered_body_bytes"] = len(delivered)
                            encoded["delivered_body_sha256"] = digest
                            encoded["git_blob"] = git_blob_observation(
                                repository, delivered, reachable_blobs
                            )
                        target = str(document.get("resolved_target", ""))
                        target_class = classify_target(target, repository)
                        target_counts[target_class] += 1
                        encoded["target_class"] = target_class
                        canonical = canonical_repository_path(target, repository)
                        encoded["canonical_repository_path"] = canonical
                        encoded["target_observation"] = file_observation(target)
                        if canonical and canonical != target:
                            encoded["canonical_observation"] = file_observation(canonical)
                        encoded_documents.append(encoded)
                    actions.append(
                        {
                            "source_log": relative,
                            "source_line": line_number,
                            "session_id": session_for_log(project_directory, log_path),
                            "message_id": str(record.get("uuid", "")),
                            "parent_message_id": str(record.get("parentUuid", "")),
                            "timestamp": str(record.get("timestamp", "")),
                            "working_directory": str(record.get("cwd", "")),
                            "git_branch": str(record.get("gitBranch", "")),
                            "block_index": block_index,
                            "tool_use_id": tool_id,
                            "succeeded": succeeded,
                            "result": result,
                            "command_bytes": len(command_bytes),
                            "command_sha256": command_digest,
                            "classes": classes,
                            "heredocs": encoded_documents,
                        }
                    )
        sources.append(
            {
                "path": relative,
                "session_id": session_for_log(project_directory, log_path),
                "bytes": log_path.stat().st_size,
                "sha256": sha256_file(log_path),
                "record_count": record_count,
            }
        )

    object_files = sorted(path for path in objects_directory.iterdir() if path.is_file())
    invalid_objects = [path.name for path in object_files if sha256_file(path) != path.name]
    unreferenced = [path.name for path in object_files if path.name not in referenced_objects]
    exact_repository_bodies = []
    for action in actions:
        if not action["succeeded"]:
            continue
        for document in action["heredocs"]:
            if (
                document.get("execution_body_exact")
                and (document.get("sink") or {}).get("mode") == "replace"
                and document.get("target_class") == "repository-or-worktree"
            ):
                exact_repository_bodies.append(document)
    return {
        "schema": SCHEMA,
        "claude_root": str(claude_root),
        "project_key": arguments.project_key,
        "project_directory": str(project_directory),
        "repository": str(repository),
        "summary": {
            "source_log_count": len(sources),
            "source_record_count": sum(source["record_count"] for source in sources),
            "malformed_record_count": len(malformed),
            "bash_action_count": len(actions),
            "successful_bash_action_count": sum(1 for action in actions if action["succeeded"]),
            "failed_or_unpaired_bash_action_count": sum(1 for action in actions if not action["succeeded"]),
            "classified_bash_action_count": sum(1 for action in actions if action["classes"]),
            "heredoc_count": sum(len(action["heredocs"]) for action in actions),
            "successful_heredoc_count": sum(
                len(action["heredocs"]) for action in actions if action["succeeded"]
            ),
            "successful_file_heredoc_count": sum(
                1
                for action in actions
                if action["succeeded"]
                for document in action["heredocs"]
                if document.get("sink")
            ),
            "exact_repository_replacement_body_count": len(exact_repository_bodies),
            "exact_repository_replacement_body_absent_git_count": sum(
                1
                for document in exact_repository_bodies
                if not document.get("git_blob", {}).get("object_present", False)
            ),
            "payload_object_count": len(object_files),
            "payload_object_bytes": sum(path.stat().st_size for path in object_files),
            "invalid_payload_object_count": len(invalid_objects),
            "unreferenced_payload_object_count": len(unreferenced),
        },
        "class_counts": dict(sorted(class_counts.items())),
        "target_class_counts": dict(sorted(target_counts.items())),
        "sources": sources,
        "malformed_records": malformed,
        "actions": actions,
        "invalid_payload_objects": invalid_objects,
        "unreferenced_payload_objects": unreferenced,
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
    parser.add_argument("--claude-root", required=True)
    parser.add_argument("--project-key", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--output-directory", required=True)
    return parser.parse_args(list(argv))


def main(argv: Iterable[str]) -> int:
    try:
        arguments = parse_arguments(argv)
        output_directory = pathlib.Path(arguments.output_directory).resolve()
        manifest = inventory(arguments)
        manifest_path = write_manifest(output_directory, manifest)
        print(json.dumps(manifest["summary"], sort_keys=True))
        print(f"manifest_sha256={sha256_file(manifest_path)}")
        failures = (
            manifest["summary"]["malformed_record_count"]
            + manifest["summary"]["invalid_payload_object_count"]
            + manifest["summary"]["unreferenced_payload_object_count"]
        )
        return 2 if failures else 0
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
