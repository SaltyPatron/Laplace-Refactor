#!/usr/bin/env python3
"""Index executable destructive shell actions in verified Claude transcripts.

This deliberately distinguishes an executable command position from text merely
passed to a search/report command or carried in an inert here-document.  It also
keeps tool-transport outcome separate from the stronger, later question of what
filesystem or Git state the command actually changed.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import importlib.util
import json
import os
import ntpath
import pathlib
import re
import shlex
import shutil
import sys
import tempfile
from dataclasses import dataclass
from typing import Any, Iterable


SCHEMA = "laplace.recovery.claude-destructive-shell-index/v1"
ACTION_SCHEMA = "laplace.recovery.claude-destructive-shell-action/v1"
INVOCATION_SCHEMA = "laplace.recovery.destructive-shell-invocation/v1"

CONTROL = {";", "&&", "||", "|", "&", "\n", "(", ")", "{", "}"}
LEADING_RESERVED = {
    "!", "if", "then", "elif", "else", "fi", "while", "until", "do", "done",
    "for", "in", "case", "esac", "select", "function", "time", "coproc",
}
WRAPPERS = {"sudo", "command", "builtin", "nohup", "nice", "env", "exec"}
ASSIGNMENT_TOKEN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=.*$", re.S)
VARIABLE_TOKEN = re.compile(r"\$(?:\{([A-Za-z_][A-Za-z0-9_]*)\}|([A-Za-z_][A-Za-z0-9_]*))")
EXIT_CODE = re.compile(r"(?im)^Exit code\s+(-?\d+)\s*$")
POWERSHELL_EXIT = re.compile(r"(?im)(?:process exited with code|exit code:)\s*(-?\d+)")
DESTRUCTIVE_LINE = re.compile(
    r"(?i)(?:^|(?:&&|\|\||;|\|)\s*)"
    r"(?:sudo\s+|command\s+|env(?:\s+[A-Za-z_][A-Za-z0-9_]*=[^\s]+)*\s+)?"
    r"(?:rm|rmdir|unlink|shred|find\b[^\r\n]*\s-delete\b|"
    r"git\s+(?:reset|checkout|restore|clean|branch|tag|update-ref|worktree|stash|push|rm)\b)"
)
POWERSHELL_HERE_SINGLE = re.compile(r"(?ms)@'(?=\r?$).*?^\s*'@")
POWERSHELL_HERE_DOUBLE = re.compile(r'(?ms)@"(?=\r?$).*?^\s*"@')
POWERSHELL_DESTRUCTIVE = re.compile(
    r"(?i)\b(?:Remove-Item|Remove-ItemProperty|Clear-Content|Set-Content|"
    r"git\s+(?:reset|checkout|restore|clean|branch|tag|update-ref|worktree|stash|push|rm))\b"
)


def load_sibling(name: str) -> Any:
    path = pathlib.Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load sibling tool: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


SHELL = load_sibling("inventory-claude-shell-mutations.py")
EVIDENCE = load_sibling("index-claude-evidence.py")


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


def select_source(group: dict[str, Any]) -> pathlib.Path:
    expected_bytes = int(group["bytes"])
    for value in group.get("paths", []):
        path = pathlib.Path(value)
        if path.is_file() and path.stat().st_size == expected_bytes:
            return path
    raise RuntimeError(
        f"no readable source for content {group.get('content_sha256', '<missing>')}"
    )


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


def shell_tokens(command: str) -> tuple[list[str], str]:
    """Return top-level shell tokens after removing here-document bodies."""
    documents, scrubbed = SHELL.extract_heredocs(command, ".")
    lexer = shlex.shlex(scrubbed, posix=True, punctuation_chars=";&|()<>")
    lexer.commenters = "#"
    lexer.whitespace = " \t\r"
    lexer.wordchars += "./$:{}[]*?~=-+,@%"
    try:
        return list(lexer), ""
    except ValueError as error:
        return [], str(error)


def command_segments(tokens: list[str]) -> list[list[str]]:
    segments: list[list[str]] = []
    current: list[str] = []
    for token in tokens:
        if token in CONTROL:
            if current:
                segments.append(current)
                current = []
            continue
        current.append(token)
    if current:
        segments.append(current)
    return segments


def strip_redirections(tokens: list[str]) -> list[str]:
    result: list[str] = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token.isdigit() and index + 1 < len(tokens) and tokens[index + 1] in {
            ">", ">>", ">|", ">&", "<", "<<", "<<<", "<>" , "<&",
        }:
            index += 3
            continue
        if token in {">", ">>", ">|", ">&", "&>", "&>>", "<", "<<", "<<<", "<>", "<&"}:
            index += 2
            continue
        if re.fullmatch(r"\d*(?:>|>>|>\||>&|<|<<|<<<|<>|<&)", token):
            index += 2
            continue
        result.append(token)
        index += 1
    return result


def unwrap_command(tokens: list[str]) -> tuple[str, list[str]]:
    """Resolve the executable position without searching arbitrary arguments."""
    values = strip_redirections(tokens)
    index = 0
    while index < len(values) and (
        values[index] in LEADING_RESERVED or ASSIGNMENT_TOKEN.match(values[index])
    ):
        index += 1
    while index < len(values):
        executable = pathlib.PurePath(values[index]).name
        if executable not in WRAPPERS:
            return executable, values[index + 1 :]
        index += 1
        while index < len(values) and (
            values[index].startswith("-") or ASSIGNMENT_TOKEN.match(values[index])
        ):
            index += 1
    return "", []


def nested_segments(executable: str, arguments: list[str]) -> list[list[str]]:
    nested: list[list[str]] = []
    if executable in {"bash", "sh", "zsh", "dash"}:
        for index, value in enumerate(arguments[:-1]):
            if value in {"-c", "--command"}:
                tokens, error = shell_tokens(arguments[index + 1])
                if not error:
                    nested.extend(command_segments(tokens))
                break
    if executable == "xargs":
        index = 0
        while index < len(arguments) and arguments[index].startswith("-"):
            if arguments[index] in {"-I", "--replace", "-n", "--max-args", "-P", "--max-procs"}:
                index += 2
            else:
                index += 1
        if index < len(arguments):
            nested.append(arguments[index:])
    if executable == "find":
        for index, value in enumerate(arguments):
            if value in {"-exec", "-execdir", "-ok", "-okdir"} and index + 1 < len(arguments):
                end = index + 1
                while end < len(arguments) and arguments[end] not in {";", "+"}:
                    end += 1
                nested.append(arguments[index + 1 : end])
    return nested


def git_subcommand(arguments: list[str]) -> tuple[str, list[str], str]:
    index = 0
    repository_hint = ""
    while index < len(arguments):
        value = arguments[index]
        if value == "-C" and index + 1 < len(arguments):
            repository_hint = arguments[index + 1]
            index += 2
            continue
        if value in {"-c", "--git-dir", "--work-tree", "--namespace"} and index + 1 < len(arguments):
            index += 2
            continue
        if value.startswith("-"):
            index += 1
            continue
        return value, arguments[index + 1 :], repository_hint
    return "", [], repository_hint


def option_present(arguments: list[str], short: str, long: str) -> bool:
    return any(
        value == long or value == short or (
            value.startswith("-") and not value.startswith("--") and short[1:] in value[1:]
        )
        for value in arguments
    )


def positional(arguments: list[str], options_with_values: set[str] | None = None) -> list[str]:
    result: list[str] = []
    options_with_values = options_with_values or set()
    index = 0
    literal = False
    while index < len(arguments):
        value = arguments[index]
        if literal:
            result.append(value)
        elif value == "--":
            literal = True
        elif value in options_with_values:
            index += 1
        elif value.startswith("-"):
            pass
        else:
            result.append(value)
        index += 1
    return result


def classify_invocation(executable: str, arguments: list[str]) -> dict[str, Any] | None:
    """Classify only commands that can remove or overwrite existing state."""
    if executable == "git":
        operation, rest, repository_hint = git_subcommand(arguments)
        category = ""
        targets: list[str] = []
        subjects: list[str] = []
        if operation == "reset" and any(value in {"--hard", "--merge", "--keep"} for value in rest):
            category = "git-worktree-reset"
            subjects = positional(rest)
        elif operation in {"checkout", "restore"}:
            has_path_separator = "--" in rest
            forced = any(value in {"-f", "--force"} for value in rest)
            source_declared = any(value == "--source" or value.startswith("--source=") for value in rest)
            if has_path_separator or forced or operation == "restore" or source_declared:
                category = "git-path-overwrite"
                if has_path_separator:
                    targets = rest[rest.index("--") + 1 :]
                else:
                    targets = positional(rest, {"-b", "-B", "--orphan", "--source"})
        elif (
            operation == "clean"
            and option_present(rest, "-f", "--force")
            and not option_present(rest, "-n", "--dry-run")
        ):
            category = "git-untracked-delete"
            targets = positional(rest, {"-e", "--exclude"})
        elif operation == "branch" and option_present(rest, "-D", "--delete-force"):
            category = "git-ref-force-delete"
            subjects = positional(rest)
        elif operation == "branch" and option_present(rest, "-f", "--force"):
            category = "git-ref-force-update"
            subjects = positional(rest)
        elif operation == "tag" and option_present(rest, "-f", "--force"):
            category = "git-ref-force-update"
            subjects = positional(rest)
        elif operation == "update-ref" and rest:
            category = "git-ref-delete" if "-d" in rest or "--delete" in rest else "git-ref-update"
            subjects = positional(rest, {"-m"})
        elif operation == "worktree" and rest and rest[0] == "remove":
            category = "git-worktree-delete"
            targets = positional(rest[1:])
        elif operation == "stash" and rest and rest[0] in {"drop", "clear"}:
            category = "git-stash-delete"
            subjects = rest[1:]
        elif operation == "push" and (
            "--delete" in rest or "-d" in rest or any(value.startswith(":") for value in rest)
        ):
            category = "git-remote-ref-delete"
            subjects = positional(rest)
        elif operation == "rm":
            category = "git-tracked-delete"
            targets = positional(rest, {"--pathspec-from-file"})
        elif operation in {"cherry-pick", "merge", "rebase", "am"} and "--abort" in rest:
            category = "git-operation-abort"
        if not category:
            return None
        return {
            "category": category,
            "executable": executable,
            "operation": operation,
            "arguments": rest,
            "target_tokens": targets,
            "subject_tokens": subjects,
            "repository_hint": repository_hint,
        }

    if executable in {"rm", "rmdir", "unlink", "shred"}:
        return {
            "category": "filesystem-delete",
            "executable": executable,
            "operation": executable,
            "arguments": arguments,
            "target_tokens": positional(arguments),
            "subject_tokens": [],
            "recursive": executable == "rm" and option_present(arguments, "-r", "--recursive"),
            "forced": executable == "rm" and option_present(arguments, "-f", "--force"),
        }

    if executable == "find" and "-delete" in arguments:
        roots = []
        for value in arguments:
            if value.startswith("-") or value in {"!", "(" , ")"}:
                break
            roots.append(value)
        return {
            "category": "filesystem-find-delete",
            "executable": executable,
            "operation": "-delete",
            "arguments": arguments,
            "target_tokens": roots,
            "subject_tokens": [],
        }
    return None


def expand_token(token: str, variables: dict[str, str]) -> tuple[str, list[str]]:
    unresolved: set[str] = set()

    def replace(match: re.Match[str]) -> str:
        name = match.group(1) or match.group(2)
        if name not in variables:
            unresolved.add(name)
            return match.group(0)
        return variables[name]

    expanded = VARIABLE_TOKEN.sub(replace, token)
    if any(marker in expanded for marker in ("$(", "`")):
        unresolved.add("dynamic-expression")
    if any(character in expanded for character in "*?["):
        unresolved.add("glob")
    return expanded, sorted(unresolved)


def resolve_target(
    token: str,
    working_directory: pathlib.Path,
    repository: pathlib.Path,
    variables: dict[str, str],
) -> dict[str, Any]:
    expanded, unresolved = expand_token(token, variables)
    resolved = ""
    target_class = "unresolved"
    if not unresolved and expanded:
        if re.match(r"^[A-Za-z]:[\\/]", expanded) or re.match(
            r"^[A-Za-z]:[\\/]", str(working_directory)
        ):
            resolved = ntpath.normpath(
                expanded
                if ntpath.isabs(expanded)
                else ntpath.join(str(working_directory), expanded)
            )
            return {
                "token": token,
                "expanded": expanded,
                "resolved": resolved,
                "unresolved": [],
                "target_class": "external-windows",
            }
        candidate = pathlib.Path(os.path.expanduser(expanded))
        candidate = candidate if candidate.is_absolute() else working_directory / candidate
        resolved = os.path.normpath(str(candidate))
        repository_text = str(repository)
        git_text = str(repository / ".git")
        if resolved == git_text or resolved.startswith(git_text + os.sep):
            target_class = "repository-git-metadata"
        elif resolved == repository_text or resolved.startswith(repository_text + os.sep):
            target_class = "repository-worktree"
        elif resolved.startswith("/tmp/claude-"):
            target_class = "claude-scratch"
        elif resolved == "/tmp" or resolved.startswith("/tmp/"):
            target_class = "temporary"
        elif resolved == "/vault" or resolved.startswith("/vault/"):
            target_class = "preserved-archive"
        elif pathlib.Path(resolved).is_absolute():
            target_class = "external-absolute"
    return {
        "token": token,
        "expanded": expanded,
        "resolved": resolved,
        "unresolved": unresolved,
        "target_class": target_class,
    }


def resolve_subject(token: str, variables: dict[str, str]) -> dict[str, Any]:
    expanded, unresolved = expand_token(token, variables)
    return {"token": token, "expanded": expanded, "unresolved": unresolved}


def assignment_values(segments: list[list[str]]) -> dict[str, str]:
    known: dict[str, str] = {}
    for segment in segments:
        for token in segment:
            if not ASSIGNMENT_TOKEN.match(token):
                break
            name, value = token.split("=", 1)
            expanded, unresolved = expand_token(value, known)
            if not unresolved:
                known[name] = expanded
    return known


def invocation_records(
    command: str, working_directory: str, repository: pathlib.Path
) -> tuple[list[dict[str, Any]], str]:
    tokens, parse_error = shell_tokens(command)
    if parse_error:
        return [], parse_error
    segments = command_segments(tokens)
    variables = assignment_values(segments)
    current = pathlib.Path(working_directory or str(repository))
    records: list[dict[str, Any]] = []
    queue: list[tuple[list[str], str]] = [(segment, "top-level") for segment in segments]
    documents, _ = SHELL.extract_heredocs(command, working_directory or str(repository))
    command_lines = command.splitlines()
    for document in documents:
        header_line = int(document.get("header_line", 0))
        if document.get("sink") or not (0 < header_line <= len(command_lines)):
            continue
        header_tokens, header_error = shell_tokens(command_lines[header_line - 1])
        if header_error:
            continue
        header_segments = command_segments(header_tokens)
        if not header_segments:
            continue
        header_executable, _ = unwrap_command(header_segments[0])
        if header_executable not in {"bash", "sh", "zsh", "dash"}:
            continue
        source_body = document.get("source_body")
        if not isinstance(source_body, bytes):
            continue
        body_tokens, body_error = shell_tokens(source_body.decode("utf-8", errors="replace"))
        if not body_error:
            queue.extend(
                (segment, "nested:shell-heredoc-source")
                for segment in command_segments(body_tokens)
            )
    queue_index = 0
    while queue_index < len(queue):
        segment, origin = queue[queue_index]
        queue_index += 1
        executable, arguments = unwrap_command(segment)
        if not executable:
            continue
        if executable == "cd" and arguments:
            expanded, unresolved = expand_token(arguments[0], variables)
            if not unresolved:
                path = pathlib.Path(os.path.expanduser(expanded))
                current = pathlib.Path(os.path.normpath(str(path if path.is_absolute() else current / path)))
            continue
        for nested in nested_segments(executable, arguments):
            queue.append((nested, f"nested:{executable}"))
        classified = classify_invocation(executable, arguments)
        if classified is None:
            continue
        repository_base = current
        repository_hint = classified.get("repository_hint", "")
        if repository_hint:
            expanded, unresolved = expand_token(repository_hint, variables)
            if not unresolved:
                hinted = pathlib.Path(os.path.expanduser(expanded))
                repository_base = pathlib.Path(
                    os.path.normpath(str(hinted if hinted.is_absolute() else current / hinted))
                )
        targets = [
            resolve_target(token, repository_base, repository, variables)
            for token in classified.pop("target_tokens")
        ]
        subjects = [
            resolve_subject(token, variables)
            for token in classified.pop("subject_tokens")
        ]
        records.append(
            {
                "schema": INVOCATION_SCHEMA,
                "origin": origin,
                "working_directory": str(current),
                **classified,
                "subjects": subjects,
                "targets": targets,
            }
        )
    return records, ""


def powershell_segments(command: str) -> list[str]:
    """Split PowerShell at executable boundaries while retaining quoted targets."""
    command = POWERSHELL_HERE_SINGLE.sub("'<here-string>'", command)
    command = POWERSHELL_HERE_DOUBLE.sub("'<here-string>'", command)
    segments: list[str] = []
    current: list[str] = []
    quote = ""
    index = 0
    while index < len(command):
        character = command[index]
        if quote == "'":
            current.append(character)
            if character == "'":
                if index + 1 < len(command) and command[index + 1] == "'":
                    current.append(command[index + 1])
                    index += 1
                else:
                    quote = ""
        elif quote == '"':
            current.append(character)
            if character == "`" and index + 1 < len(command):
                current.append(command[index + 1])
                index += 1
            elif character == '"':
                quote = ""
        else:
            if character in {"'", '"'}:
                quote = character
                current.append(character)
            elif character == "`" and index + 1 < len(command):
                if command[index + 1] in "\r\n":
                    if command[index + 1] == "\r" and index + 2 < len(command) and command[index + 2] == "\n":
                        index += 1
                    current.append(" ")
                    index += 1
                else:
                    current.extend((character, command[index + 1]))
                    index += 1
            elif character in ";\r\n|{}":
                value = "".join(current).strip()
                if value:
                    segments.append(value)
                current = []
            else:
                current.append(character)
        index += 1
    value = "".join(current).strip()
    if value:
        segments.append(value)
    return segments


def powershell_tokens(segment: str) -> tuple[list[str], str]:
    values: list[str] = []
    current: list[str] = []
    quote = ""
    index = 0
    while index < len(segment):
        character = segment[index]
        if quote == "'":
            if character == "'":
                if index + 1 < len(segment) and segment[index + 1] == "'":
                    current.append("'")
                    index += 1
                else:
                    quote = ""
            else:
                current.append(character)
        elif quote == '"':
            if character == "`" and index + 1 < len(segment):
                current.append(segment[index + 1])
                index += 1
            elif character == '"':
                quote = ""
            else:
                current.append(character)
        elif character in {"'", '"'}:
            quote = character
        elif character.isspace():
            if current:
                values.append("".join(current))
                current = []
        else:
            current.append(character)
        index += 1
    if quote:
        return [], "unclosed PowerShell quote"
    if current:
        values.append("".join(current))
    return values, ""


def powershell_option_value(arguments: list[str], names: set[str]) -> str:
    for index, value in enumerate(arguments[:-1]):
        if value.lower() in names:
            return arguments[index + 1]
    return ""


def powershell_invocation_records(
    command: str, working_directory: str, repository: pathlib.Path
) -> tuple[list[dict[str, Any]], str]:
    current = working_directory
    variables: dict[str, str] = {}
    records: list[dict[str, Any]] = []
    errors: list[str] = []
    for segment_index, segment in enumerate(powershell_segments(command)):
        tokens, error = powershell_tokens(segment)
        if error:
            errors.append(f"segment {segment_index}: {error}")
            continue
        if not tokens:
            continue
        assignment = re.match(r"^\$([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$", segment, re.S)
        if assignment:
            value_tokens, value_error = powershell_tokens(assignment.group(2).strip())
            if not value_error and len(value_tokens) == 1 and not value_tokens[0].startswith("$("):
                variables[assignment.group(1)] = value_tokens[0]
            continue
        executable_index = 0
        while executable_index < len(tokens) and tokens[executable_index].lower() in {
            "&", ".", "if", "else", "foreach", "for", "while",
        }:
            executable_index += 1
        if executable_index >= len(tokens):
            continue
        executable = pathlib.PurePath(tokens[executable_index]).name
        arguments = tokens[executable_index + 1 :]
        lowered = executable.lower()
        if lowered in {"cd", "set-location", "sl"} and arguments:
            expanded, unresolved = expand_token(arguments[0], variables)
            if not unresolved:
                current = expanded
            continue

        classified: dict[str, Any] | None = None
        if lowered in {"remove-item", "rm", "ri", "del", "erase", "rmdir", "rd"}:
            target = powershell_option_value(arguments, {"-path", "-literalpath"})
            targets = [target] if target else positional(arguments)
            classified = {
                "category": "powershell-filesystem-delete",
                "executable": executable,
                "operation": executable,
                "arguments": arguments,
                "target_tokens": targets,
                "subject_tokens": [],
                "recursive": any(value.lower() == "-recurse" for value in arguments),
                "forced": any(value.lower() == "-force" for value in arguments),
            }
        elif lowered == "remove-itemproperty":
            target = powershell_option_value(arguments, {"-path", "-literalpath"})
            classified = {
                "category": "powershell-registry-delete",
                "executable": executable,
                "operation": executable,
                "arguments": arguments,
                "target_tokens": [],
                "subject_tokens": [target] if target else [],
            }
        elif lowered in {"clear-content", "set-content"}:
            target = powershell_option_value(arguments, {"-path", "-literalpath"})
            targets = [target] if target else positional(arguments)
            classified = {
                "category": "powershell-filesystem-overwrite",
                "executable": executable,
                "operation": executable,
                "arguments": arguments,
                "target_tokens": targets[:1],
                "subject_tokens": [],
            }
        elif lowered in {"git", "git.exe"}:
            classified = classify_invocation("git", arguments)
        if classified is None:
            continue
        base = pathlib.Path(current) if not re.match(r"^[A-Za-z]:[\\/]", current) else pathlib.Path(current)
        targets = [
            resolve_target(token, base, repository, variables)
            for token in classified.pop("target_tokens")
            if token
        ]
        subjects = [
            resolve_subject(token, variables)
            for token in classified.pop("subject_tokens")
            if token
        ]
        records.append(
            {
                "schema": INVOCATION_SCHEMA,
                "origin": f"powershell-segment:{segment_index}",
                "working_directory": current,
                **classified,
                "subjects": subjects,
                "targets": targets,
            }
        )
    return records, "; ".join(errors)


def powershell_has_unresolved_destructive_candidate(command: str) -> bool:
    normalized = POWERSHELL_HERE_SINGLE.sub("'<here-string>'", command)
    normalized = POWERSHELL_HERE_DOUBLE.sub("'<here-string>'", normalized)
    return bool(POWERSHELL_DESTRUCTIVE.search(normalized))


def recover_invocations_by_logical_line(
    command: str, working_directory: str, repository: pathlib.Path
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Recover independently parseable lines after a whole-command parse error."""
    _, scrubbed = SHELL.extract_heredocs(command, working_directory or str(repository))
    logical_lines: list[tuple[int, str, bool]] = []
    pending = ""
    pending_line = 0
    pending_executable = True
    quote = ""
    for line_number, line in enumerate(scrubbed.splitlines(), start=1):
        value = line
        if not pending:
            pending_line = line_number
            pending_executable = not quote
        index = 0
        while index < len(value):
            character = value[index]
            if quote == "'":
                if character == "'":
                    quote = ""
            elif quote == '"':
                if character == "\\":
                    index += 1
                elif character == '"':
                    quote = ""
            else:
                if character == "#":
                    break
                if character in {"'", '"'}:
                    quote = character
                elif character == "\\":
                    index += 1
            index += 1
        if value.rstrip().endswith("\\"):
            pending += value.rstrip()[:-1] + " "
            continue
        logical_lines.append((pending_line, pending + value, pending_executable))
        pending = ""
    if pending:
        logical_lines.append((pending_line, pending, pending_executable))

    recovered: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    for line_number, line, executable_context in logical_lines:
        if not executable_context or not DESTRUCTIVE_LINE.search(line):
            continue
        records, error = invocation_records(line, working_directory, repository)
        if error:
            unresolved.append(
                {
                    "line_number": line_number,
                    "line_sha256": sha256_bytes(line.encode("utf-8")),
                    "error": error,
                }
            )
            continue
        for record in records:
            record["origin"] = f"line-recovery:{line_number}:{record['origin']}"
        recovered.extend(records)
    return recovered, unresolved


def result_text(block: dict[str, Any]) -> str:
    value = block.get("content", "")
    if isinstance(value, str):
        return value
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def result_descriptor(
    record: dict[str, Any],
    block: dict[str, Any],
    source_content_sha256: str = "",
    source_line: int = 0,
    block_index: int = 0,
) -> dict[str, Any]:
    text = result_text(block)
    match = EXIT_CODE.search(text) or POWERSHELL_EXIT.search(text)
    explicit_exit = int(match.group(1)) if match else None
    is_error = bool(block.get("is_error", False))
    if explicit_exit is not None:
        outcome = "reported-zero-exit" if explicit_exit == 0 else "reported-nonzero-exit"
    elif is_error:
        outcome = "tool-reported-error"
    else:
        outcome = "tool-reported-success-without-explicit-exit"
    encoded = canonical_json_bytes(block.get("content"))
    return {
        "source_content_sha256": source_content_sha256,
        "source_line": source_line,
        "block_index": block_index,
        "tool_use_id": str(block.get("tool_use_id", "")),
        "message_id": str(record.get("uuid", "")),
        "timestamp": str(record.get("timestamp", "")),
        "is_error": is_error,
        "explicit_exit_code": explicit_exit,
        "outcome": outcome,
        "content_bytes": len(encoded),
        "content_sha256": sha256_bytes(encoded),
    }


def build_index(arguments: argparse.Namespace) -> dict[str, Any]:
    corpora_path = pathlib.Path(arguments.corpora_manifest).resolve()
    repository = pathlib.Path(arguments.repository).resolve()
    destination = pathlib.Path(arguments.output_directory).resolve()
    if destination.exists():
        raise RuntimeError(f"output already exists: {destination}")
    corpora = json.loads(corpora_path.read_text(encoding="utf-8"))
    groups = sorted(
        corpora.get("content_groups", []), key=lambda item: str(item.get("content_sha256", ""))
    )
    if not groups:
        raise RuntimeError("corpora manifest contains no content groups")
    if not (repository / ".git").exists():
        raise RuntimeError(f"Git repository does not exist: {repository}")

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = pathlib.Path(tempfile.mkdtemp(prefix=f".{destination.name}.", dir=destination.parent))
    objects = temporary / "objects"
    objects.mkdir()
    calls: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    results: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    sources: list[dict[str, Any]] = []
    malformed = 0
    parse_errors: list[dict[str, Any]] = []
    unresolved_parse_candidates = 0
    category_counts: collections.Counter[str] = collections.Counter()
    target_counts: collections.Counter[str] = collections.Counter()
    outcome_counts: collections.Counter[str] = collections.Counter()
    try:
        for group in groups:
            expected_digest = str(group["content_sha256"])
            expected_bytes = int(group["bytes"])
            path = select_source(group)
            digest = hashlib.sha256()
            observed_bytes = 0
            record_count = 0
            with path.open("rb") as stream:
                for line_number, raw in enumerate(stream, start=1):
                    digest.update(raw)
                    observed_bytes += len(raw)
                    if not raw.strip():
                        continue
                    record_count += 1
                    try:
                        record = json.loads(raw)
                    except (UnicodeDecodeError, json.JSONDecodeError):
                        malformed += 1
                        continue
                    if not isinstance(record, dict):
                        continue
                    message = record.get("message")
                    blocks = message.get("content") if isinstance(message, dict) else None
                    if not isinstance(blocks, list):
                        continue
                    for block_index, block in enumerate(blocks):
                        if not isinstance(block, dict):
                            continue
                        block_type = block.get("type")
                        tool_id = str(block.get("id" if block_type == "tool_use" else "tool_use_id", ""))
                        if not tool_id:
                            continue
                        action_id = EVIDENCE.tool_action_id(expected_digest, tool_id)
                        if block_type == "tool_result":
                            results[action_id].append(
                                result_descriptor(
                                    record,
                                    block,
                                    expected_digest,
                                    line_number,
                                    block_index,
                                )
                            )
                            continue
                        tool_name = str(block.get("name", ""))
                        if block_type != "tool_use" or tool_name not in {"Bash", "PowerShell"}:
                            continue
                        tool_input = block.get("input")
                        command = tool_input.get("command") if isinstance(tool_input, dict) else None
                        if not isinstance(command, str):
                            continue
                        if tool_name == "Bash":
                            invocations, parse_error = invocation_records(
                                command, str(record.get("cwd", "")), repository
                            )
                        else:
                            invocations, parse_error = powershell_invocation_records(
                                command, str(record.get("cwd", "")), repository
                            )
                        if parse_error:
                            recovered, unresolved = (
                                recover_invocations_by_logical_line(
                                    command, str(record.get("cwd", "")), repository
                                )
                                if tool_name == "Bash"
                                else (
                                    [],
                                    ([{"error": parse_error}]
                                     if powershell_has_unresolved_destructive_candidate(command)
                                     else []),
                                )
                            )
                            if recovered:
                                invocations = recovered
                            unresolved_parse_candidates += len(unresolved)
                            parse_errors.append(
                                {
                                    "source_content_sha256": expected_digest,
                                    "source_line": line_number,
                                    "block_index": block_index,
                                    "tool_action_id": action_id,
                                    "error": parse_error,
                                    "command_sha256": sha256_bytes(command.encode("utf-8")),
                                    "line_recovered_invocation_count": len(recovered),
                                    "unresolved_candidate_lines": unresolved,
                                }
                            )
                        if not invocations:
                            continue
                        command_bytes = command.encode("utf-8")
                        command_digest = store_object(objects, command_bytes)
                        calls[action_id].append(
                            {
                                "source_content_sha256": expected_digest,
                                "source_paths": sorted(str(value) for value in group.get("paths", [])),
                                "source_corpora": sorted(str(value) for value in group.get("corpora", [])),
                                "source_line": line_number,
                                "block_index": block_index,
                                "timestamp": str(record.get("timestamp", "")),
                                "session_id": str(record.get("sessionId", "")),
                                "message_id": str(record.get("uuid", "")),
                                "parent_message_id": str(record.get("parentUuid", "")),
                                "working_directory": str(record.get("cwd", "")),
                                "git_branch": str(record.get("gitBranch", "")),
                                "tool_use_id": tool_id,
                                "tool_name": tool_name,
                                "command_bytes": len(command_bytes),
                                "command_sha256": command_digest,
                                "invocations": invocations,
                            }
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
                    "paths": sorted(str(value) for value in group.get("paths", [])),
                    "corpora": sorted(str(value) for value in group.get("corpora", [])),
                }
            )

        actions_path = temporary / "destructive-actions.jsonl"
        with actions_path.open("wb") as stream:
            for action_id in sorted(calls):
                call_records = calls[action_id]
                result_records = results.get(action_id, [])
                call_variants = {
                    (item["message_id"], item["command_sha256"], canonical_json_bytes(item["invocations"]))
                    for item in call_records
                }
                result_variants = {
                    (item["message_id"], item["is_error"], item["content_sha256"])
                    for item in result_records
                }
                conflicting = len(call_variants) > 1 or len(result_variants) > 1
                if not result_records:
                    status = "unpaired-call"
                    outcome = "unpaired"
                elif conflicting:
                    status = "conflicting-records"
                    outcome = "conflicting"
                elif len(call_records) > 1 or len(result_records) > 1:
                    status = "paired-repeated-records"
                    outcome = result_records[0]["outcome"]
                else:
                    status = "paired-single-records"
                    outcome = result_records[0]["outcome"]
                outcome_counts[outcome] += 1
                canonical_call = call_records[0]
                for invocation in canonical_call["invocations"]:
                    category_counts[invocation["category"]] += 1
                    for target in invocation["targets"]:
                        target_counts[target["target_class"]] += 1
                action = {
                    "schema": ACTION_SCHEMA,
                    "tool_action_id": action_id,
                    "tool_use_id": canonical_call["tool_use_id"],
                    "status": status,
                    "outcome": outcome,
                    "conflicting": conflicting,
                    "call_record_count": len(call_records),
                    "call_variant_count": len(call_variants),
                    "result_record_count": len(result_records),
                    "result_variant_count": len(result_variants),
                    "calls": call_records,
                    "results": result_records,
                }
                stream.write(canonical_json_bytes(action) + b"\n")

        object_files = sorted(path for path in objects.iterdir() if path.is_file())
        invalid_objects = [path.name for path in object_files if sha256_file(path) != path.name]
        file_record = {
            "path": actions_path.name,
            "bytes": actions_path.stat().st_size,
            "sha256": sha256_file(actions_path),
            "line_count": len(calls),
        }
        summary = {
            "content_group_count": len(groups),
            "source_occurrence_count": sum(len(group.get("paths", [])) for group in groups),
            "source_record_count": sum(item["record_count"] for item in sources),
            "malformed_json_count": malformed,
            "shell_parse_error_count": len(parse_errors),
            "line_recovered_parse_error_count": sum(
                1 for item in parse_errors if item["line_recovered_invocation_count"]
            ),
            "unresolved_parse_candidate_line_count": unresolved_parse_candidates,
            "destructive_tool_action_count": len(calls),
            "destructive_invocation_count": sum(category_counts.values()),
            "category_counts": dict(sorted(category_counts.items())),
            "target_class_counts": dict(sorted(target_counts.items())),
            "outcome_counts": dict(sorted(outcome_counts.items())),
            "payload_object_count": len(object_files),
            "payload_object_bytes": sum(path.stat().st_size for path in object_files),
            "invalid_payload_object_count": len(invalid_objects),
        }
        manifest = {
            "schema": SCHEMA,
            "inputs": {
                "corpora_manifest": str(corpora_path),
                "corpora_manifest_sha256": sha256_file(corpora_path),
                "repository": str(repository),
            },
            "summary": summary,
            "sources": sources,
            "parse_errors": parse_errors,
            "invalid_payload_objects": invalid_objects,
            "files": [file_record],
        }
        (temporary / "manifest.json").write_bytes(canonical_json_bytes(manifest) + b"\n")
        os.replace(temporary, destination)
        return manifest
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpora-manifest", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--output-directory", required=True)
    arguments = parser.parse_args(list(argv) if argv is not None else None)
    try:
        manifest = build_index(arguments)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(manifest["summary"], sort_keys=True))
    return 2 if (
        manifest["summary"]["invalid_payload_object_count"]
        or manifest["summary"]["unresolved_parse_candidate_line_count"]
    ) else 0


if __name__ == "__main__":
    raise SystemExit(main())
