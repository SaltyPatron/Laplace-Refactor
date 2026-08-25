#!/usr/bin/env python3
"""Reconstruct Claude PostgreSQL client activity and repeated server work."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import pathlib
import re
import shlex
import sys
import tempfile
from typing import Any, Iterable


SCHEMA = "laplace.claude-sql-activity/v1"
PUNCTUATION = {";", "&", "|", "||", "&&"}
PSQL_PROGRAM = re.compile(r"(?:^|[\\/])psql(?:\.exe)?$", re.I)
SHELL_ASSIGNMENT = re.compile(r"^\$?([A-Za-z_][A-Za-z0-9_]*)=(.*)$", re.S)
SHELL_VARIABLE_REFERENCE = re.compile(
    r"^\$(?:\{([A-Za-z_][A-Za-z0-9_]*)\}|([A-Za-z_][A-Za-z0-9_]*))$"
)
SQL_REFERENCE = re.compile(
    r"\b(?:from|join|update|into|delete\s+from|truncate(?:\s+table)?)\s+"
    r"(?:only\s+)?([a-z_][a-z0-9_$]*(?:\.[a-z_][a-z0-9_$]*)?)",
    re.I,
)
SQL_FUNCTION = re.compile(r"\b([a-z_][a-z0-9_$]*(?:\.[a-z_][a-z0-9_$]*)?)\s*\(", re.I)
SQL_CAST = re.compile(r"::\s*([a-z_][a-z0-9_$]*(?:\.[a-z_][a-z0-9_$]*)?)", re.I)
NON_FUNCTION_WORDS = {
    "all",
    "and",
    "array",
    "as",
    "case",
    "coalesce",
    "distinct",
    "exists",
    "filter",
    "from",
    "greatest",
    "in",
    "least",
    "nullif",
    "over",
    "select",
    "values",
    "when",
    "where",
    "with",
}


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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


def load_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise RuntimeError(f"expected JSON object: {path}")
    return value


def shell_tokens(command: str, powershell: bool = False) -> list[str]:
    if powershell:
        command = re.sub(r"`\r?\n[ \t]*", " ", command)
    prepared: list[str] = []
    single = False
    double = False
    escaped = False
    for char in command:
        if escaped:
            prepared.append(char)
            escaped = False
            continue
        if char == "\\" and not single:
            prepared.append(char)
            escaped = True
            continue
        if char == "'" and not double:
            single = not single
        elif char == '"' and not single:
            double = not double
        if char == "\n" and not single and not double:
            prepared.append(" ; ")
        else:
            prepared.append(char)
    lexer = shlex.shlex("".join(prepared), posix=True, punctuation_chars=";&|<>")
    lexer.whitespace_split = True
    lexer.commenters = ""
    try:
        return list(lexer)
    except ValueError:
        return []


def is_psql_execution(tokens: list[str], index: int) -> bool:
    start = prior_command_start(tokens, index)
    prefix = tokens[start:index]
    while prefix and (
        prefix[0] in {"!", "do", "then", "{"}
        or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*=.*", prefix[0])
    ):
        prefix = prefix[1:]
    if not prefix:
        return True
    if prefix[0] in {"command", "exec", "time"}:
        prefix = prefix[1:]
    if prefix and prefix[0] == "timeout":
        prefix = prefix[1:]
        while prefix and prefix[0].startswith("-"):
            prefix = prefix[1:]
        if prefix:
            prefix = prefix[1:]
    elif prefix and prefix[0] in {"env", "sudo"}:
        prefix = prefix[1:]
        while prefix and (prefix[0].startswith("-") or "=" in prefix[0]):
            prefix = prefix[1:]
    elif prefix and prefix[0] in {"xargs", "watch"}:
        prefix = []
    return not prefix


def psql_indices(tokens: list[str]) -> list[int]:
    psql_variables: set[str] = set()
    indices: list[int] = []
    for index, token in enumerate(tokens):
        assignment = SHELL_ASSIGNMENT.fullmatch(token)
        if assignment:
            name, value = assignment.groups()
            if "psql" in name.lower() or re.search(r"psql(?:\.exe)?", value, re.I):
                psql_variables.add(name)
            continue

        variable = SHELL_VARIABLE_REFERENCE.fullmatch(token)
        if variable:
            name = variable.group(1) or variable.group(2)
            if index + 1 < len(tokens) and tokens[index + 1] == "=":
                if "psql" in name.lower():
                    psql_variables.add(name)
                continue
            if (
                (name in psql_variables or "psql" in name.lower())
                and is_psql_execution(tokens, index)
            ):
                indices.append(index)
            continue

        if PSQL_PROGRAM.search(token) and is_psql_execution(tokens, index):
            indices.append(index)
    return indices


def invocation_end(tokens: list[str], start: int) -> int:
    for index in range(start + 1, len(tokens)):
        if tokens[index] in PUNCTUATION:
            return index
    return len(tokens)


def prior_command_start(tokens: list[str], index: int) -> int:
    for position in range(index - 1, -1, -1):
        if tokens[position] in PUNCTUATION:
            return position + 1
    return 0


def option_value(tokens: list[str], short: str, long: str) -> str:
    for index, token in enumerate(tokens):
        if token in (short, long) and index + 1 < len(tokens):
            return tokens[index + 1]
        if token.startswith(long + "="):
            return token.split("=", 1)[1]
        if token.startswith(short) and len(token) > len(short) and short in ("-d", "-h", "-U"):
            return token[len(short) :]
    return ""


def command_values(tokens: list[str]) -> list[str]:
    values: list[str] = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token in ("-c", "--command") and index + 1 < len(tokens):
            values.append(tokens[index + 1])
            index += 2
            continue
        if token.startswith("--command="):
            values.append(token.split("=", 1)[1])
            index += 1
            continue
        if re.fullmatch(r"-[A-Za-z]*c", token) and index + 1 < len(tokens):
            values.append(tokens[index + 1])
            index += 2
            continue
        if token.startswith("-c") and len(token) > 2:
            values.append(token[2:])
        index += 1
    return values


def file_values(tokens: list[str]) -> list[str]:
    values: list[str] = []
    for index, token in enumerate(tokens):
        if token in ("-f", "--file") and index + 1 < len(tokens):
            values.append(tokens[index + 1])
        elif token.startswith("--file="):
            values.append(token.split("=", 1)[1])
        elif re.fullmatch(r"-[A-Za-z]*f", token) and index + 1 < len(tokens):
            values.append(tokens[index + 1])
        elif token.startswith("-f") and len(token) > 2:
            values.append(token[2:])
    return values


def client_timeout(tokens: list[str], psql_index: int) -> float | None:
    start = prior_command_start(tokens, psql_index)
    prefix = tokens[start:psql_index]
    for index, token in enumerate(prefix):
        if os.path.basename(token) != "timeout" or index + 1 >= len(prefix):
            continue
        match = re.fullmatch(r"([0-9]+(?:\.[0-9]+)?)([smhd]?)", prefix[index + 1], re.I)
        if not match:
            continue
        value = float(match.group(1))
        multiplier = {"": 1.0, "s": 1.0, "m": 60.0, "h": 3600.0, "d": 86400.0}
        return value * multiplier[match.group(2).lower()]
    return None


def dollar_tag(sql: str, index: int) -> str:
    match = re.match(r"\$[A-Za-z_0-9]*\$", sql[index:])
    return match.group(0) if match else ""


def normalize_sql(sql: str, replace_literals: bool) -> str:
    output: list[str] = []
    index = 0
    pending_space = False
    length = len(sql)
    while index < length:
        char = sql[index]
        if char.isspace():
            pending_space = True
            index += 1
            continue
        if sql.startswith("--", index):
            newline = sql.find("\n", index + 2)
            index = length if newline < 0 else newline + 1
            pending_space = True
            continue
        if sql.startswith("/*", index):
            end = sql.find("*/", index + 2)
            index = length if end < 0 else end + 2
            pending_space = True
            continue
        if pending_space and output and output[-1] not in "(.,=<>+-*/:":
            output.append(" ")
        pending_space = False
        if char == "'":
            end = index + 1
            while end < length:
                if sql[end] == "'":
                    if end + 1 < length and sql[end + 1] == "'":
                        end += 2
                        continue
                    end += 1
                    break
                end += 1
            output.append("'?" + "'" if replace_literals else sql[index:end])
            index = end
            continue
        if char == '"':
            end = index + 1
            while end < length:
                if sql[end] == '"':
                    if end + 1 < length and sql[end + 1] == '"':
                        end += 2
                        continue
                    end += 1
                    break
                end += 1
            output.append(sql[index:end])
            index = end
            continue
        if char == "$":
            tag = dollar_tag(sql, index)
            if tag:
                end = sql.find(tag, index + len(tag))
                end = length if end < 0 else end + len(tag)
                output.append("$body$" if replace_literals else sql[index:end])
                index = end
                continue
            parameter = re.match(r"\$[0-9]+", sql[index:])
            if parameter:
                output.append("$n" if replace_literals else parameter.group(0))
                index += len(parameter.group(0))
                continue
        if char in "(,.=<>+-*/:":
            if output and output[-1] == " ":
                output.pop()
            output.append(char)
            index += 1
            continue
        number = None
        if index == 0 or not re.match(r"[A-Za-z0-9_$]", sql[index - 1]):
            number = re.match(r"(?:0x[0-9a-f]+|\d+(?:\.\d+)?(?:e[+-]?\d+)?)", sql[index:], re.I)
        if number:
            output.append("#" if replace_literals else number.group(0).lower())
            index += len(number.group(0))
            continue
        output.append(char.lower() if "A" <= char <= "Z" else char)
        index += 1
    return "".join(output).strip().rstrip(";").strip()


def split_statements(sql: str) -> list[str]:
    statements: list[str] = []
    start = 0
    index = 0
    single = False
    double = False
    while index < len(sql):
        if not single and not double and sql.startswith("--", index):
            newline = sql.find("\n", index + 2)
            index = len(sql) if newline < 0 else newline + 1
            continue
        if not single and not double and sql.startswith("/*", index):
            end = sql.find("*/", index + 2)
            index = len(sql) if end < 0 else end + 2
            continue
        if not single and not double and sql[index] == "$":
            tag = dollar_tag(sql, index)
            if tag:
                end = sql.find(tag, index + len(tag))
                index = len(sql) if end < 0 else end + len(tag)
                continue
        if sql[index] == "'" and not double:
            if single and index + 1 < len(sql) and sql[index + 1] == "'":
                index += 2
                continue
            single = not single
        elif sql[index] == '"' and not single:
            if double and index + 1 < len(sql) and sql[index + 1] == '"':
                index += 2
                continue
            double = not double
        elif sql[index] == ";" and not single and not double:
            value = sql[start:index].strip()
            if value:
                statements.append(value)
            start = index + 1
        index += 1
    value = sql[start:].strip()
    if value:
        statements.append(value)
    return statements


def statement_operation(normalized: str) -> str:
    if normalized.startswith("\\"):
        return "psql-meta"
    match = re.match(r"([a-z]+)", normalized)
    if not match:
        return "unknown"
    first = match.group(1)
    if first != "with":
        return first
    matches = list(re.finditer(r"\b(select|insert|update|delete|merge)\b", normalized))
    return matches[-1].group(1) if matches else "with"


def statement_facts(normalized: str) -> dict[str, Any]:
    operation = statement_operation(normalized)
    functions = sorted(
        {
            value.lower()
            for value in SQL_FUNCTION.findall(normalized)
            if value.lower().split(".")[-1] not in NON_FUNCTION_WORDS
        }
    )
    return {
        "operation": operation,
        "relations": sorted({value.lower() for value in SQL_REFERENCE.findall(normalized)}),
        "functions": functions,
        "casts": sorted({value.lower() for value in SQL_CAST.findall(normalized)}),
        "has_explain_analyze": bool(re.search(r"\bexplain\s*\([^)]*analyze|\bexplain\s+analyze\b", normalized)),
        "has_grouping": bool(re.search(r"\bgroup\s+by\b|\bcount\s*\(|\bsum\s*\(|\bavg\s*\(", normalized)),
        "has_system_catalog": bool(re.search(r"\b(?:pg_catalog\.|pg_class\b|pg_namespace\b|pg_indexes\b|information_schema\.)", normalized)),
        "has_4d_or_postgis": bool(re.search(r"\b(?:st_[a-z0-9_]+|geometry\b|geography\b|coord_[xyzm]\b|angular_distance_4d\b|frechet_4d\b|hilbert)\b", normalized)),
    }


def result_texts(claude_root: pathlib.Path, project_key: str) -> dict[str, str]:
    project_directory = claude_root / "projects" / project_key
    results: dict[str, str] = {}
    for path in sorted(project_directory.rglob("*.jsonl")):
        with path.open("r", encoding="utf-8") as stream:
            for raw_line in stream:
                if not raw_line.strip():
                    continue
                record = json.loads(raw_line)
                if record.get("type") != "user":
                    continue
                blocks = (record.get("message") or {}).get("content")
                if not isinstance(blocks, list):
                    continue
                for block in blocks:
                    if not isinstance(block, dict) or block.get("type") != "tool_result":
                        continue
                    content = block.get("content", "")
                    if isinstance(content, str):
                        text = content
                    elif isinstance(content, list):
                        text = "\n".join(
                            str(item.get("text", item.get("content", "")))
                            for item in content
                            if isinstance(item, dict)
                        )
                    else:
                        text = json.dumps(content, ensure_ascii=False, sort_keys=True)
                    results[str(block.get("tool_use_id", ""))] = text
    return results


def result_signals(text: str) -> dict[str, Any]:
    real_seconds = []
    for minutes, seconds in re.findall(r"(?m)^real\s+(\d+)m([0-9.]+)s", text):
        real_seconds.append(int(minutes) * 60.0 + float(seconds))
    server_ms = [float(value) for value in re.findall(r"(?im)^Time:\s*([0-9.]+)\s*ms", text)]
    return {
        "timed_out": bool(re.search(r"timed out|statement timeout|canceling statement due to", text, re.I)),
        "error_count": len(re.findall(r"(?m)^(?:ERROR|FATAL):", text)),
        "real_seconds": real_seconds,
        "server_milliseconds": server_ms,
        "output_bytes": len(text.encode("utf-8")),
        "output_sha256": sha256_bytes(text.encode("utf-8")),
    }


def server_timeout_seconds(sql: str) -> list[float]:
    values = []
    for amount, unit in re.findall(
        r"statement_timeout\s*=\s*'?([0-9]+(?:\.[0-9]+)?)(ms|s|min|m|h)?", sql, re.I
    ):
        multiplier = {"": 0.001, "ms": 0.001, "s": 1.0, "m": 60.0, "min": 60.0, "h": 3600.0}
        values.append(float(amount) * multiplier[(unit or "").lower()])
    return values


def command_without_heredoc_bodies(action: dict[str, Any], command: str) -> str:
    lines = command.splitlines(keepends=True)
    removed: set[int] = set()
    for document in action.get("heredocs", []):
        if document.get("status") != "complete":
            continue
        start = int(document.get("body_start_line", 0)) - 1
        terminator = int(document.get("body_end_line", 0))
        removed.update(range(max(start, 0), min(terminator + 1, len(lines))))
    return "".join(line for index, line in enumerate(lines) if index not in removed)


def heredoc_sql_sources(
    action: dict[str, Any], command: str, shell_objects: pathlib.Path
) -> list[dict[str, Any]]:
    lines = command.splitlines()
    values = []
    for document in action.get("heredocs", []):
        line_number = int(document.get("header_line", 0))
        if not 0 < line_number <= len(lines):
            continue
        prefix = "\n".join(lines[:line_number])
        prefix_tokens = shell_tokens(
            prefix, powershell=action.get("tool_name") == "PowerShell"
        )
        prefix_indices = psql_indices(prefix_tokens)
        delimiter = str(document.get("delimiter", ""))
        matching_ordinal = -1
        for ordinal, psql_index in reversed(list(enumerate(prefix_indices))):
            invocation_tokens = prefix_tokens[
                psql_index + 1 : invocation_end(prefix_tokens, psql_index)
            ]
            redirected = [
                invocation_tokens[index + 1]
                for index, token in enumerate(invocation_tokens[:-1])
                if token in {"<<", "<<-"}
            ]
            if delimiter in redirected:
                matching_ordinal = ordinal
                break
        if matching_ordinal < 0:
            continue
        digest = document.get("delivered_body_sha256") or document.get("source_body_sha256")
        if not digest:
            continue
        values.append(
            {
                "kind": "heredoc-stdin",
                "sql": (shell_objects / digest).read_text(encoding="utf-8"),
                "execution_body_exact": bool(document.get("execution_body_exact")),
                "body_sha256": digest,
                "invocation_ordinal": matching_ordinal,
            }
        )
    return values


def surface_kind(facts: dict[str, Any], distinct_literals: bool) -> str:
    operation = facts["operation"]
    if operation == "psql-meta" or facts["has_system_catalog"]:
        return "catalog-inspection-surface"
    if operation in {"insert", "update", "delete", "merge", "truncate", "create", "alter", "drop"}:
        return "operational-procedure"
    if facts["has_grouping"]:
        return "diagnostic-query-surface"
    if distinct_literals:
        return "parameterized-query-surface"
    return "reusable-query-surface"


def inventory(arguments: argparse.Namespace) -> dict[str, Any]:
    shell_manifest_path = pathlib.Path(arguments.shell_manifest).resolve()
    shell_manifest = load_json(shell_manifest_path)
    shell_objects = shell_manifest_path.parent / "objects"
    claude_root = pathlib.Path(arguments.claude_root).resolve()
    output_directory = pathlib.Path(arguments.output_directory).resolve()
    objects_directory = output_directory / "objects"
    objects_directory.mkdir(parents=True, exist_ok=True)
    results = result_texts(claude_root, arguments.project_key)
    referenced_objects: set[str] = set()
    invocations: list[dict[str, Any]] = []
    statements: list[dict[str, Any]] = []
    databases: collections.Counter[str] = collections.Counter()
    source_kinds: collections.Counter[str] = collections.Counter()

    for action in shell_manifest.get("actions", []):
        if not action.get("succeeded"):
            continue
        command = (shell_objects / action["command_sha256"]).read_text(encoding="utf-8")
        tokens = shell_tokens(command_without_heredoc_bodies(action, command))
        indices = psql_indices(tokens)
        heredoc_sources = heredoc_sql_sources(action, command, shell_objects)
        for ordinal, psql_index in enumerate(indices):
            end = invocation_end(tokens, psql_index)
            invocation_tokens = tokens[psql_index + 1 : end]
            database = option_value(invocation_tokens, "-d", "--dbname") or "unspecified"
            databases[database] += 1
            sources: list[dict[str, Any]] = [
                {"kind": "command-option", "sql": value, "execution_body_exact": True}
                for value in command_values(invocation_tokens)
            ]
            sources.extend(
                {key: value for key, value in source.items() if key != "invocation_ordinal"}
                for source in heredoc_sources
                if source["invocation_ordinal"] == ordinal
            )
            sources.extend(
                {
                    "kind": "file-reference",
                    "path": value,
                    "execution_body_exact": False,
                }
                for value in file_values(invocation_tokens)
            )
            if not sources:
                sources.append({"kind": "unresolved-stdin", "execution_body_exact": False})
            signal = result_signals(results.get(action["tool_use_id"], ""))
            invocation_record: dict[str, Any] = {
                "source_log": action["source_log"],
                "source_line": action["source_line"],
                "session_id": action["session_id"],
                "timestamp": action["timestamp"],
                "tool_use_id": action["tool_use_id"],
                "invocation_ordinal": ordinal,
                "command_sha256": action["command_sha256"],
                "database": database,
                "host": option_value(invocation_tokens, "-h", "--host") or "unspecified",
                "user": option_value(invocation_tokens, "-U", "--username") or "unspecified",
                "client_timeout_seconds": client_timeout(tokens, psql_index),
                "timed_wrapper": "time" in tokens[prior_command_start(tokens, psql_index) : psql_index],
                "result": signal,
                "sources": [],
            }
            for source_ordinal, source in enumerate(sources):
                source_kinds[source["kind"]] += 1
                encoded_source = {key: value for key, value in source.items() if key != "sql"}
                sql = source.get("sql")
                if isinstance(sql, str):
                    raw = sql.encode("utf-8")
                    raw_hash = store_object(objects_directory, raw)
                    referenced_objects.add(raw_hash)
                    normalized = normalize_sql(sql, replace_literals=False)
                    shape = normalize_sql(sql, replace_literals=True)
                    normalized_hash = store_object(objects_directory, normalized.encode("utf-8"))
                    shape_hash = store_object(objects_directory, shape.encode("utf-8"))
                    referenced_objects.update({normalized_hash, shape_hash})
                    encoded_source.update(
                        {
                            "sql_bytes": len(raw),
                            "sql_sha256": raw_hash,
                            "normalized_sha256": normalized_hash,
                            "shape_sha256": shape_hash,
                            "server_timeout_seconds": server_timeout_seconds(sql),
                        }
                    )
                    for statement_ordinal, statement in enumerate(split_statements(sql)):
                        statement_normalized = normalize_sql(statement, replace_literals=False)
                        statement_shape = normalize_sql(statement, replace_literals=True)
                        if not statement_normalized:
                            continue
                        statement_hash = store_object(
                            objects_directory, statement_normalized.encode("utf-8")
                        )
                        shape_statement_hash = store_object(
                            objects_directory, statement_shape.encode("utf-8")
                        )
                        referenced_objects.update({statement_hash, shape_statement_hash})
                        facts = statement_facts(statement_normalized)
                        statements.append(
                            {
                                "tool_use_id": action["tool_use_id"],
                                "invocation_key": f"{action['tool_use_id']}:{ordinal}",
                                "invocation_ordinal": ordinal,
                                "source_ordinal": source_ordinal,
                                "statement_ordinal": statement_ordinal,
                                "timestamp": action["timestamp"],
                                "database": database,
                                "normalized_sha256": statement_hash,
                                "shape_sha256": shape_statement_hash,
                                "normalized_bytes": len(statement_normalized.encode("utf-8")),
                                **facts,
                            }
                        )
                invocation_record["sources"].append(encoded_source)
            invocations.append(invocation_record)

    normalized_groups: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    shape_groups: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    for statement in statements:
        if statement["operation"] == "set":
            continue
        normalized_groups[statement["normalized_sha256"]].append(statement)
        shape_groups[statement["shape_sha256"]].append(statement)

    repeated_exact = []
    for digest, members in normalized_groups.items():
        if len(members) < 2:
            continue
        repeated_exact.append(
            {
                "normalized_sha256": digest,
                "count": len(members),
                "databases": dict(sorted(collections.Counter(item["database"] for item in members).items())),
                "tool_use_ids": [item["tool_use_id"] for item in members],
                "invocation_keys": [item["invocation_key"] for item in members],
                "first_timestamp": min(item["timestamp"] for item in members),
                "last_timestamp": max(item["timestamp"] for item in members),
                "facts": {key: members[0][key] for key in ("operation", "relations", "functions", "casts", "has_explain_analyze", "has_grouping", "has_system_catalog", "has_4d_or_postgis")},
            }
        )
    repeated_exact.sort(key=lambda item: (-item["count"], item["normalized_sha256"]))

    repeated_shapes = []
    for digest, members in shape_groups.items():
        if len(members) < 2:
            continue
        normalized_variants = sorted({item["normalized_sha256"] for item in members})
        repeated_shapes.append(
            {
                "shape_sha256": digest,
                "count": len(members),
                "distinct_normalized_count": len(normalized_variants),
                "normalized_variants": normalized_variants,
                "databases": dict(sorted(collections.Counter(item["database"] for item in members).items())),
                "tool_use_ids": [item["tool_use_id"] for item in members],
                "invocation_keys": [item["invocation_key"] for item in members],
                "first_timestamp": min(item["timestamp"] for item in members),
                "last_timestamp": max(item["timestamp"] for item in members),
                "facts": {key: members[0][key] for key in ("operation", "relations", "functions", "casts", "has_explain_analyze", "has_grouping", "has_system_catalog", "has_4d_or_postgis")},
                "surface_candidate": surface_kind(members[0], len(normalized_variants) > 1),
            }
        )
    repeated_shapes.sort(key=lambda item: (-item["count"], item["shape_sha256"]))

    expensive_repeats = []
    signals_by_invocation = {
        f"{item['tool_use_id']}:{item['invocation_ordinal']}": item for item in invocations
    }
    for group in repeated_shapes:
        client_limits = []
        timed_out = 0
        errors = 0
        for invocation_key in group["invocation_keys"]:
            invocation = signals_by_invocation.get(invocation_key)
            if invocation is None:
                continue
            if invocation["client_timeout_seconds"] is not None:
                client_limits.append(invocation["client_timeout_seconds"])
            timed_out += int(invocation["result"]["timed_out"])
            errors += invocation["result"]["error_count"]
        if timed_out or any(value >= 60 for value in client_limits):
            expensive_repeats.append(
                {
                    "shape_sha256": group["shape_sha256"],
                    "count": group["count"],
                    "surface_candidate": group["surface_candidate"],
                    "client_timeout_seconds": client_limits,
                    "timed_out_result_count": timed_out,
                    "error_count": errors,
                    "facts": group["facts"],
                }
            )

    object_files = sorted(path for path in objects_directory.iterdir() if path.is_file())
    invalid_objects = [path.name for path in object_files if sha256_file(path) != path.name]
    unreferenced_objects = [path.name for path in object_files if path.name not in referenced_objects]
    client_limits = [
        item["client_timeout_seconds"]
        for item in invocations
        if item["client_timeout_seconds"] is not None
    ]
    return {
        "schema": SCHEMA,
        "shell_manifest": str(shell_manifest_path),
        "shell_manifest_sha256": sha256_file(shell_manifest_path),
        "claude_root": str(claude_root),
        "project_key": arguments.project_key,
        "summary": {
            "psql_invocation_count": len(invocations),
            "sql_source_count": sum(len(item["sources"]) for item in invocations),
            "resolved_sql_source_count": sum(
                1 for item in invocations for source in item["sources"] if "sql_sha256" in source
            ),
            "statement_count": len(statements),
            "set_statement_count": sum(1 for item in statements if item["operation"] == "set"),
            "explain_analyze_statement_count": sum(1 for item in statements if item["has_explain_analyze"]),
            "postgis_or_4d_statement_count": sum(1 for item in statements if item["has_4d_or_postgis"]),
            "distinct_normalized_statement_count": len(normalized_groups),
            "distinct_statement_shape_count": len(shape_groups),
            "repeated_exact_statement_group_count": len(repeated_exact),
            "repeated_exact_statement_execution_count": sum(item["count"] for item in repeated_exact),
            "repeated_statement_shape_group_count": len(repeated_shapes),
            "repeated_statement_shape_execution_count": sum(item["count"] for item in repeated_shapes),
            "expensive_repeat_group_count": len(expensive_repeats),
            "timed_out_invocation_count": sum(1 for item in invocations if item["result"]["timed_out"]),
            "invocation_error_count": sum(item["result"]["error_count"] for item in invocations),
            "client_timeout_declaration_count": len(client_limits),
            "client_timeout_declared_seconds_total": sum(client_limits),
            "client_timeout_declared_seconds_max": max(client_limits, default=0),
            "database_count": len(databases),
            "payload_object_count": len(object_files),
            "payload_object_bytes": sum(path.stat().st_size for path in object_files),
            "invalid_payload_object_count": len(invalid_objects),
            "unreferenced_payload_object_count": len(unreferenced_objects),
        },
        "database_counts": dict(sorted(databases.items())),
        "source_kind_counts": dict(sorted(source_kinds.items())),
        "invocations": invocations,
        "statements": statements,
        "repeated_exact_statements": repeated_exact,
        "repeated_statement_shapes": repeated_shapes,
        "expensive_repeated_statement_shapes": expensive_repeats,
        "invalid_payload_objects": invalid_objects,
        "unreferenced_payload_objects": unreferenced_objects,
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
    parser.add_argument("--shell-manifest", required=True)
    parser.add_argument("--claude-root", required=True)
    parser.add_argument("--project-key", required=True)
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
            manifest["summary"]["invalid_payload_object_count"]
            + manifest["summary"]["unreferenced_payload_object_count"]
        )
        return 2 if failures else 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
