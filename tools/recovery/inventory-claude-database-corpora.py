#!/usr/bin/env python3
"""Reconstruct database client activity across deduplicated Claude corpora."""

from __future__ import annotations

import argparse
import collections
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import sys
import tempfile
import types
from typing import Any, Iterable


SCHEMA = "laplace.recovery.claude-database-corpora.v1"
RAW_SQL_TOOL = re.compile(r"(?:^|__)(?:sql|execute_sql)$", re.I)
SQL_INPUT_KEYS = {"sql", "query", "statement"}


def load_sibling(name: str) -> types.ModuleType:
    path = pathlib.Path(__file__).with_name(name)
    module_name = "laplace_recovery_" + name.replace("-", "_").removesuffix(".py")
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load helper: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


SHELL = load_sibling("inventory-claude-shell-mutations.py")
SQL = load_sibling("inventory-claude-sql-activity.py")


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_object(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_bytes())
    if not isinstance(value, dict):
        raise RuntimeError(f"expected JSON object: {path}")
    return value


def store_object(objects: pathlib.Path, content: bytes) -> str:
    digest = sha256_bytes(content)
    target = objects / digest
    if target.exists():
        if target.read_bytes() != content:
            raise RuntimeError(f"content-address collision at {target}")
        return digest
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{digest}.", dir=objects)
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, target)
    finally:
        if temporary.exists():
            temporary.unlink()
    return digest


def result_content_text(content: Any) -> str:
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        parts: list[str] = []
        for item in content:
            if isinstance(item, dict):
                value = item.get("text", item.get("content", ""))
                parts.append(value if isinstance(value, str) else json.dumps(value, sort_keys=True))
            else:
                parts.append(str(item))
        return "\n".join(parts)
    return json.dumps(content, ensure_ascii=False, sort_keys=True)


def iter_records(path: pathlib.Path) -> Iterable[tuple[int, dict[str, Any]]]:
    with path.open("r", encoding="utf-8") as stream:
        for line_number, raw_line in enumerate(stream, start=1):
            if not raw_line.strip():
                continue
            try:
                value = json.loads(raw_line)
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict):
                yield line_number, value


def tool_blocks(record: dict[str, Any], block_type: str) -> Iterable[tuple[int, dict[str, Any]]]:
    message = record.get("message")
    if not isinstance(message, dict):
        return
    content = message.get("content")
    if not isinstance(content, list):
        return
    for index, block in enumerate(content):
        if isinstance(block, dict) and block.get("type") == block_type:
            yield index, block


def selected_logs(corpora: dict[str, Any]) -> list[dict[str, Any]]:
    by_path = {log["path"]: log for log in corpora.get("logs", [])}
    selected: list[dict[str, Any]] = []
    for group in corpora.get("content_groups", []):
        paths = group.get("paths", [])
        if not paths:
            continue
        path = paths[0]
        log = by_path.get(path)
        if log is None:
            raise RuntimeError(f"content group path is absent from log inventory: {path}")
        source = pathlib.Path(path)
        if not source.is_file():
            raise RuntimeError(f"source log is unavailable: {source}")
        actual = sha256_file(source)
        if actual != group["content_sha256"]:
            raise RuntimeError(f"source log changed after corpus inventory: {source}")
        selected.append(log)
    return selected


def index_results(
    logs: list[dict[str, Any]],
) -> tuple[dict[tuple[str, str], dict[str, Any]], list[dict[str, Any]]]:
    results: dict[tuple[str, str], dict[str, Any]] = {}
    conflicts: list[dict[str, Any]] = []
    for log in logs:
        path = pathlib.Path(log["path"])
        for line_number, record in iter_records(path):
            if record.get("type") != "user":
                continue
            for block_index, block in tool_blocks(record, "tool_result"):
                tool_id = str(block.get("tool_use_id", ""))
                if not tool_id:
                    continue
                text = result_content_text(block.get("content", ""))
                candidate = {
                    "block_index": block_index,
                    "content_sha256": sha256_bytes(text.encode()),
                    "is_error": bool(block.get("is_error", False)),
                    "source_line": line_number,
                    "source_log": str(path),
                    "text": text,
                }
                result_key = (str(path), tool_id)
                existing = results.get(result_key)
                if existing is not None and (
                    existing["content_sha256"] != candidate["content_sha256"]
                    or existing["is_error"] != candidate["is_error"]
                ):
                    conflicts.append(
                        {
                            "tool_use_id": tool_id,
                            "first_source": existing["source_log"],
                            "second_source": str(path),
                        }
                    )
                    continue
                results[result_key] = candidate
    return results, conflicts


def encode_heredocs(
    command: str, working_directory: str, objects: pathlib.Path
) -> tuple[list[dict[str, Any]], str]:
    documents, scrubbed = SHELL.extract_heredocs(command, working_directory)
    encoded_documents: list[dict[str, Any]] = []
    for document in documents:
        encoded = {
            key: value
            for key, value in document.items()
            if key not in {"source_body", "delivered_body"}
        }
        source = document.get("source_body")
        if isinstance(source, bytes):
            encoded["source_body_bytes"] = len(source)
            encoded["source_body_sha256"] = store_object(objects, source)
        delivered = document.get("delivered_body")
        if isinstance(delivered, bytes):
            encoded["delivered_body_bytes"] = len(delivered)
            encoded["delivered_body_sha256"] = store_object(objects, delivered)
        encoded_documents.append(encoded)
    return encoded_documents, scrubbed


def pipeline_file_sources(tokens: list[str], psql_index: int) -> list[str]:
    pipe_index = next(
        (index for index in range(psql_index - 1, -1, -1) if tokens[index] == "|"),
        -1,
    )
    if pipe_index < 0:
        return []
    start = 0
    for index in range(pipe_index - 1, -1, -1):
        if tokens[index] in {";", "&&", "||"}:
            start = index + 1
            break
    producer = tokens[start:pipe_index]
    if not producer:
        return []
    executable = os.path.basename(producer[0])
    if executable == "cat":
        return [token for token in producer[1:] if token and not token.startswith("-")]
    if executable == "sed":
        values: list[str] = []
        expression_seen = False
        arguments = producer[1:]
        index = 0
        while index < len(arguments):
            token = arguments[index]
            if token in {"-e", "--expression", "-f", "--file"}:
                expression_seen = True
                index += 2
                continue
            if token.startswith("-"):
                index += 1
                continue
            if not expression_seen:
                expression_seen = True
                index += 1
                continue
            values.append(token)
            index += 1
        return values
    return []


def has_pipeline_input(tokens: list[str], psql_index: int) -> bool:
    for index in range(psql_index - 1, -1, -1):
        if tokens[index] == "|":
            return True
        if tokens[index] in {";", "&&", "||"}:
            return False
    return False


def input_redirection_sources(tokens: list[str]) -> list[str]:
    return [
        tokens[index + 1]
        for index, token in enumerate(tokens[:-1])
        if token in {"<", "<<", "<<<"}
    ]


def is_client_version_or_help(tokens: list[str]) -> bool:
    return any(token in {"--version", "-V", "--help", "-?"} for token in tokens)


def is_client_database_list(tokens: list[str]) -> bool:
    for token in tokens:
        if token == "--list":
            return True
        if re.fullmatch(r"-[A-Za-z]+", token) and "l" in token[1:]:
            return True
    return False


def sql_values(value: Any, parent_key: str = "") -> list[str]:
    found: list[str] = []
    if isinstance(value, dict):
        for key, child in value.items():
            lowered = str(key).lower()
            if lowered in SQL_INPUT_KEYS and isinstance(child, str):
                found.append(child)
            else:
                found.extend(sql_values(child, lowered))
    elif isinstance(value, list):
        for child in value:
            found.extend(sql_values(child, parent_key))
    return found


def statement_record(
    sql: str,
    action: dict[str, Any],
    invocation_key: str,
    database: str,
    source_ordinal: int,
    objects: pathlib.Path,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    raw = sql.encode()
    source: dict[str, Any] = {
        "kind": "inline-sql",
        "sql_bytes": len(raw),
        "sql_sha256": store_object(objects, raw),
        "execution_body_exact": True,
        "server_timeout_seconds": SQL.server_timeout_seconds(sql),
    }
    statements: list[dict[str, Any]] = []
    for ordinal, statement in enumerate(SQL.split_statements(sql)):
        normalized = SQL.normalize_sql(statement, False)
        shape = SQL.normalize_sql(statement, True)
        if not normalized:
            continue
        facts = SQL.statement_facts(normalized)
        statements.append(
            {
                "confirmed_execution": action["confirmed_execution"],
                "database": database,
                "invocation_key": invocation_key,
                "normalized_sha256": store_object(objects, normalized.encode()),
                "shape_sha256": store_object(objects, shape.encode()),
                "source_ordinal": source_ordinal,
                "statement_ordinal": ordinal,
                "timestamp": action["timestamp"],
                "tool_name": action["tool_name"],
                "tool_use_id": action["tool_use_id"],
                **facts,
            }
        )
    return source, statements


def repeated_groups(
    statements: list[dict[str, Any]], invocations: dict[str, dict[str, Any]]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    exact: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    shapes: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    for statement in statements:
        if not statement["confirmed_execution"] or statement["operation"] == "set":
            continue
        exact[statement["normalized_sha256"]].append(statement)
        shapes[statement["shape_sha256"]].append(statement)

    exact_output: list[dict[str, Any]] = []
    for digest, members in exact.items():
        if len(members) < 2:
            continue
        exact_output.append(
            {
                "count": len(members),
                "first_timestamp": min(member["timestamp"] for member in members),
                "last_timestamp": max(member["timestamp"] for member in members),
                "normalized_sha256": digest,
                "operation": members[0]["operation"],
                "relations": members[0]["relations"],
                "functions": members[0]["functions"],
                "invocation_keys": [member["invocation_key"] for member in members],
            }
        )
    exact_output.sort(key=lambda item: (-item["count"], item["normalized_sha256"]))

    shape_output: list[dict[str, Any]] = []
    for digest, members in shapes.items():
        if len(members) < 2:
            continue
        normalized = {member["normalized_sha256"] for member in members}
        invocation_keys = [member["invocation_key"] for member in members]
        limits = [
            invocations[key]["client_timeout_seconds"]
            for key in invocation_keys
            if key in invocations and invocations[key]["client_timeout_seconds"] is not None
        ]
        timed_out = sum(
            bool(invocations[key]["result"]["timed_out"])
            for key in invocation_keys
            if key in invocations
        )
        facts = {
            key: members[0][key]
            for key in (
                "operation",
                "relations",
                "functions",
                "casts",
                "has_explain_analyze",
                "has_grouping",
                "has_system_catalog",
                "has_4d_or_postgis",
            )
        }
        shape_output.append(
            {
                "client_timeout_seconds": limits,
                "count": len(members),
                "distinct_normalized_count": len(normalized),
                "facts": facts,
                "first_timestamp": min(member["timestamp"] for member in members),
                "last_timestamp": max(member["timestamp"] for member in members),
                "shape_sha256": digest,
                "surface_candidate": SQL.surface_kind(facts, len(normalized) > 1),
                "timed_out_result_count": timed_out,
            }
        )
    shape_output.sort(key=lambda item: (-item["count"], item["shape_sha256"]))
    return exact_output, shape_output


def inventory(corpora_path: pathlib.Path, output: pathlib.Path) -> dict[str, Any]:
    corpora = load_object(corpora_path)
    logs = selected_logs(corpora)
    results, result_conflicts = index_results(logs)
    objects = output / "objects"
    objects.mkdir(parents=True, exist_ok=True)

    actions: list[dict[str, Any]] = []
    invocations: list[dict[str, Any]] = []
    statements: list[dict[str, Any]] = []
    raw_surface_calls: collections.Counter[str] = collections.Counter()
    malformed_record_count = sum(log.get("invalid_line_count", 0) for log in logs)

    for log in logs:
        path = pathlib.Path(log["path"])
        for line_number, record in iter_records(path):
            if record.get("type") != "assistant":
                continue
            for block_index, block in tool_blocks(record, "tool_use"):
                tool_name = str(block.get("name", ""))
                if tool_name.startswith("mcp__laplace"):
                    raw_surface_calls[tool_name] += 1
                tool_id = str(block.get("id", ""))
                tool_input = block.get("input") if isinstance(block.get("input"), dict) else {}
                result = results.get((str(path), tool_id))
                confirmed = result is not None and not result["is_error"]
                signal = SQL.result_signals(result["text"] if result else "")
                action = {
                    "block_index": block_index,
                    "confirmed_execution": confirmed,
                    "result": signal,
                    "source_line": line_number,
                    "source_log": str(path),
                    "timestamp": str(record.get("timestamp", "")),
                    "tool_name": tool_name,
                    "tool_use_id": tool_id,
                    "working_directory": str(record.get("cwd", "")),
                }

                if tool_name in {"Bash", "PowerShell"}:
                    command = tool_input.get("command")
                    if not isinstance(command, str):
                        continue
                    documents, _ = encode_heredocs(command, action["working_directory"], objects)
                    action["command_sha256"] = store_object(objects, command.encode())
                    action["heredocs"] = documents
                    tokens = SQL.shell_tokens(
                        SQL.command_without_heredoc_bodies(action, command),
                        powershell=tool_name == "PowerShell",
                    )
                    indices = SQL.psql_indices(tokens)
                    if not indices:
                        continue
                    actions.append(action)
                    heredoc_sources = SQL.heredoc_sql_sources(action, command, objects)
                    for ordinal, psql_index in enumerate(indices):
                        end = SQL.invocation_end(tokens, psql_index)
                        invocation_tokens = tokens[psql_index + 1 : end]
                        if is_client_version_or_help(invocation_tokens):
                            continue
                        database = SQL.option_value(invocation_tokens, "-d", "--dbname") or "unspecified"
                        invocation_key = f"{tool_id}:{ordinal}"
                        record_sources: list[dict[str, Any]] = []
                        sql_sources: list[str] = SQL.command_values(invocation_tokens)
                        sql_sources.extend(
                            source["sql"]
                            for source in heredoc_sources
                            if source["invocation_ordinal"] == ordinal
                        )
                        for source_ordinal, source_sql in enumerate(sql_sources):
                            encoded_source, parsed = statement_record(
                                source_sql,
                                action,
                                invocation_key,
                                database,
                                source_ordinal,
                                objects,
                            )
                            encoded_source["kind"] = (
                                "command-option"
                                if source_ordinal < len(SQL.command_values(invocation_tokens))
                                else "heredoc-stdin"
                            )
                            record_sources.append(encoded_source)
                            statements.extend(parsed)
                        file_references = SQL.file_values(invocation_tokens)
                        pipeline_references = pipeline_file_sources(tokens, psql_index)
                        redirected_references = input_redirection_sources(invocation_tokens)
                        record_sources.extend(
                            {"kind": "file-reference", "path": value, "execution_body_exact": False}
                            for value in file_references
                        )
                        record_sources.extend(
                            {"kind": "pipeline-file-reference", "path": value, "execution_body_exact": False}
                            for value in pipeline_references
                        )
                        record_sources.extend(
                            {"kind": "input-file-reference", "path": value, "execution_body_exact": False}
                            for value in redirected_references
                            if value not in {document.get("delimiter") for document in documents}
                        )
                        if (
                            not sql_sources
                            and not file_references
                            and not pipeline_references
                            and not redirected_references
                            and has_pipeline_input(tokens, psql_index)
                        ):
                            record_sources.append(
                                {
                                    "kind": "pipeline-generated-input",
                                    "execution_body_exact": False,
                                }
                            )
                        if not record_sources and is_client_database_list(invocation_tokens):
                            record_sources.append(
                                {
                                    "kind": "client-database-list",
                                    "execution_body_exact": True,
                                }
                            )
                        if not record_sources:
                            record_sources.append(
                                {"kind": "unresolved-stdin", "execution_body_exact": False}
                            )
                        invocations.append(
                            {
                                "client_timeout_seconds": SQL.client_timeout(tokens, psql_index),
                                "confirmed_execution": confirmed,
                                "database": database,
                                "invocation_key": invocation_key,
                                "invocation_ordinal": ordinal,
                                "result": signal,
                                "source_line": action["source_line"],
                                "source_log": action["source_log"],
                                "sources": record_sources,
                                "timestamp": action["timestamp"],
                                "tool_name": tool_name,
                                "tool_use_id": tool_id,
                            }
                        )
                    continue

                if not RAW_SQL_TOOL.search(tool_name):
                    continue
                values = sql_values(tool_input)
                if not values:
                    continue
                actions.append(action)
                database = str(tool_input.get("database", tool_input.get("dbname", "unspecified")))
                invocation_key = f"{tool_id}:0"
                record_sources = []
                for source_ordinal, source_sql in enumerate(values):
                    encoded_source, parsed = statement_record(
                        source_sql,
                        action,
                        invocation_key,
                        database,
                        source_ordinal,
                        objects,
                    )
                    encoded_source["kind"] = "mcp-tool-input"
                    record_sources.append(encoded_source)
                    statements.extend(parsed)
                invocations.append(
                    {
                        "client_timeout_seconds": None,
                        "confirmed_execution": confirmed,
                        "database": database,
                        "invocation_key": invocation_key,
                        "invocation_ordinal": 0,
                        "result": signal,
                        "source_line": action["source_line"],
                        "source_log": action["source_log"],
                        "sources": record_sources,
                        "timestamp": action["timestamp"],
                        "tool_name": tool_name,
                        "tool_use_id": tool_id,
                    }
                )

    by_key = {invocation["invocation_key"]: invocation for invocation in invocations}
    repeated_exact, repeated_shapes = repeated_groups(statements, by_key)
    object_files = [path for path in objects.iterdir() if path.is_file()]
    invalid_objects = [path.name for path in object_files if sha256_file(path) != path.name]
    source_kinds = collections.Counter(
        source["kind"] for invocation in invocations for source in invocation["sources"]
    )
    database_counts = collections.Counter(
        invocation["database"] for invocation in invocations if invocation["confirmed_execution"]
    )
    limits = [
        invocation["client_timeout_seconds"]
        for invocation in invocations
        if invocation["confirmed_execution"] and invocation["client_timeout_seconds"] is not None
    ]
    confirmed_statements = [statement for statement in statements if statement["confirmed_execution"]]

    return {
        "actions": actions,
        "corpora_manifest": str(corpora_path),
        "corpora_manifest_sha256": sha256_file(corpora_path),
        "database_counts": dict(sorted(database_counts.items())),
        "invalid_payload_objects": invalid_objects,
        "invocations": invocations,
        "raw_laplace_surface_call_counts": dict(sorted(raw_surface_calls.items())),
        "repeated_exact_statements": repeated_exact,
        "repeated_statement_shapes": repeated_shapes,
        "result_id_conflicts": result_conflicts,
        "schema": SCHEMA,
        "source_kind_counts": dict(sorted(source_kinds.items())),
        "statements": statements,
        "summary": {
            "attempted_database_invocation_count": len(invocations),
            "attempted_statement_count": len(statements),
            "client_timeout_declaration_count": len(limits),
            "client_timeout_declared_seconds_max": max(limits, default=0),
            "client_timeout_declared_seconds_total": sum(limits),
            "confirmed_database_invocation_count": sum(
                invocation["confirmed_execution"] for invocation in invocations
            ),
            "confirmed_statement_count": len(confirmed_statements),
            "database_count": len(database_counts),
            "explain_analyze_statement_count": sum(
                statement["has_explain_analyze"] for statement in confirmed_statements
            ),
            "invalid_payload_object_count": len(invalid_objects),
            "malformed_source_record_count": malformed_record_count,
            "payload_object_bytes": sum(path.stat().st_size for path in object_files),
            "payload_object_count": len(object_files),
            "postgis_or_4d_statement_count": sum(
                statement["has_4d_or_postgis"] for statement in confirmed_statements
            ),
            "repeated_exact_statement_execution_count": sum(
                group["count"] for group in repeated_exact
            ),
            "repeated_exact_statement_group_count": len(repeated_exact),
            "repeated_statement_shape_execution_count": sum(
                group["count"] for group in repeated_shapes
            ),
            "repeated_statement_shape_group_count": len(repeated_shapes),
            "result_id_conflict_count": len(result_conflicts),
            "selected_unique_log_count": len(logs),
            "set_statement_count": sum(
                statement["operation"] == "set" for statement in confirmed_statements
            ),
            "timed_out_invocation_count": sum(
                invocation["confirmed_execution"] and invocation["result"]["timed_out"]
                for invocation in invocations
            ),
            "tool_result_sql_error_count": sum(
                invocation["result"]["error_count"]
                for invocation in invocations
                if invocation["confirmed_execution"]
            ),
        },
    }


def write_manifest(output: pathlib.Path, manifest: dict[str, Any]) -> pathlib.Path:
    path = output / "manifest.json"
    encoded = (json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode()
    descriptor, temporary_name = tempfile.mkstemp(prefix=".manifest.", dir=output)
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()
    return path


def main(argv: Iterable[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpora-manifest", required=True, type=pathlib.Path)
    parser.add_argument("--output-directory", required=True, type=pathlib.Path)
    arguments = parser.parse_args(list(argv))
    try:
        output = arguments.output_directory.resolve()
        output.mkdir(parents=True, exist_ok=True)
        manifest = inventory(arguments.corpora_manifest.resolve(), output)
        path = write_manifest(output, manifest)
        print(json.dumps(manifest["summary"], sort_keys=True))
        print(f"manifest_sha256={sha256_file(path)}")
        failures = (
            manifest["summary"]["invalid_payload_object_count"]
            + manifest["summary"]["result_id_conflict_count"]
        )
        return 2 if failures else 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
