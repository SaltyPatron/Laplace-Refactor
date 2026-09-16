"""Content identity for package inputs; workflow orchestration is recorded separately."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time


_GIT_OBJECT = re.compile(r"[0-9a-f]{40}\Z")
_GIT_TIMEOUT_SECONDS = 30
_COMMITTED_KINDS = {b"100644": "file", b"100755": "executable", b"120000": "symlink"}


def _included(name: str) -> bool:
    # All other repository inputs remain conservative build dependencies.
    return not name.startswith(".github/workflows/")


def _record(name: str, kind: str, content: bytes) -> list[str]:
    return [name, kind, hashlib.sha256(content).hexdigest()]


def _fingerprint(records: list[list[str]]) -> str:
    encoded = json.dumps(["laplace.repository-build-inputs/v1", records],
                         ensure_ascii=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def repository_build_fingerprint(repository: Path) -> str:
    names = subprocess.run(
        ["git", "-c", f"safe.directory={repository}", "-C", str(repository),
         "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        check=True, stdout=subprocess.PIPE,
    ).stdout.split(b"\0")
    records = []
    for raw in sorted(set(names)):
        if not raw:
            continue
        name = os.fsdecode(raw)
        if not _included(name):
            continue
        path = repository / name
        if path.is_symlink():
            content = os.fsencode(os.readlink(path))
            kind = "symlink"
        elif path.is_file():
            content = path.read_bytes()
            kind = "executable" if path.stat().st_mode & 0o111 else "file"
        elif not path.exists():
            content, kind = b"", "deleted"
        else:
            raise ValueError(f"Unsupported repository build input: {name}")
        records.append(_record(name, kind, content))
    return _fingerprint(records)


def _read_git_objects(repository: Path, identities: list[bytes], kind: bytes,
                      deadline: float) -> dict[bytes, bytes]:
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise ValueError("Immutable repository build input deadline exhausted")
    environment = os.environ.copy()
    for name in ("GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_OBJECT_DIRECTORY",
                 "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_INDEX_FILE", "GIT_NAMESPACE"):
        environment.pop(name, None)
    try:
        response = subprocess.run(
            ["git", "--no-replace-objects", "-c", f"safe.directory={repository}",
             "-C", str(repository), "cat-file", "--batch"],
            input=b"".join(identity + b"\n" for identity in identities), check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment,
            timeout=min(_GIT_TIMEOUT_SECONDS, remaining),
        ).stdout
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        raise ValueError("Cannot read immutable repository build inputs") from error
    contents = {}
    offset = 0
    for identity in identities:
        end = response.find(b"\n", offset)
        if end < 0:
            raise ValueError("Committed object response is incomplete")
        header = response[offset:end].split(b" ")
        if (len(header) != 3 or header[0] != identity or header[1] != kind or
                re.fullmatch(b"[0-9]+", header[2]) is None):
            raise ValueError("Committed object response identity or type differs")
        size = int(header[2])
        offset = end + 1
        end = offset + size
        if end >= len(response) or response[end:end + 1] != b"\n":
            raise ValueError("Committed object response size differs")
        content = response[offset:end]
        git_header = kind + b" " + str(size).encode("ascii") + b"\0"
        if hashlib.sha1(git_header + content).hexdigest().encode("ascii") != identity:
            raise ValueError("Committed contents differ from their Git identity")
        contents[identity] = content
        offset = end + 1
    if offset != len(response):
        raise ValueError("Committed object response has unexpected trailing data")
    return contents


def repository_build_fingerprint_at_commit(repository: Path, exact_commit: str) -> str:
    """Apply the existing input policy to immutable Git blobs, without a checkout."""
    if not isinstance(exact_commit, str) or _GIT_OBJECT.fullmatch(exact_commit) is None:
        raise ValueError("Repository build inputs require an exact lowercase 40-character commit")
    deadline = time.monotonic() + 2 * _GIT_TIMEOUT_SECONDS
    commit_identity = exact_commit.encode("ascii")
    commit = _read_git_objects(repository, [commit_identity], b"commit", deadline)[commit_identity]
    tree_header = commit.split(b"\n", 1)[0]
    if re.fullmatch(b"tree [0-9a-f]{40}", tree_header) is None:
        raise ValueError("Committed repository has no exact root tree identity")
    pending = [(b"", tree_header[5:])]
    entries = {}
    while pending:
        identities = list(dict.fromkeys(identity for _, identity in pending))
        trees = _read_git_objects(repository, identities, b"tree", deadline)
        children = []
        for prefix, identity in pending:
            # Read raw tree modes: ls-tree normalizes unsupported mode bits.
            tree = trees[identity]
            offset = 0
            while offset < len(tree):
                space = tree.find(b" ", offset)
                end = tree.find(b"\0", space + 1) if space >= 0 else -1
                if space < 0 or end < 0 or end + 21 > len(tree):
                    raise ValueError("Committed repository tree entry is malformed")
                mode = tree[offset:space]
                name = tree[space + 1:end]
                identity = tree[end + 1:end + 21].hex().encode("ascii")
                offset = end + 21
                path = prefix + name
                if not name or name in (b".", b"..") or b"/" in name or path in entries:
                    raise ValueError("Committed repository input path is invalid or duplicated")
                if mode == b"40000":
                    entries[path] = None
                    children.append((path + b"/", identity))
                elif mode in _COMMITTED_KINDS:
                    entries[path] = (mode, identity)
                else:
                    raise ValueError("Unsupported committed repository input mode: " + os.fsdecode(path))
        pending = children
    selected = [(name, *entries[name]) for name in sorted(entries)
                if entries[name] is not None and _included(os.fsdecode(name))]
    identities = list(dict.fromkeys(identity for _, _, identity in selected))
    if not identities:
        return _fingerprint([])
    blobs = _read_git_objects(repository, identities, b"blob", deadline)
    return _fingerprint([_record(os.fsdecode(name), _COMMITTED_KINDS[mode], blobs[identity])
                         for name, mode, identity in selected])
