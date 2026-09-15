"""Verify compiler input bytes against an exact Git tree without trusting its index.

This is a read-only filesystem/Git observation. It does not parse source content,
copy the checkout, or claim that arbitrary untracked build inputs are source-locked.
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import stat
import subprocess


class GitCheckoutError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise GitCheckoutError(message)


def git(root: Path, *arguments: str) -> bytes:
    process = subprocess.run(
        ["git", "--no-optional-locks", "--no-replace-objects", "-c",
         "safe.directory=" + str(root), "-C", str(root), *arguments],
        stdin=subprocess.DEVNULL, capture_output=True, check=False, timeout=120,
        env={**os.environ, "GIT_NO_REPLACE_OBJECTS": "1", "GIT_TERMINAL_PROMPT": "0"})
    require(process.returncode == 0, "read-only Git checkout verification failed")
    return process.stdout


def fingerprint(info: os.stat_result) -> tuple:
    return (info.st_dev, info.st_ino, info.st_mode, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns)


def snapshot(checkout: Path, revision: str | None = None, *,
             retain_bytes: bool = False, maximum_files: int | None = None,
             maximum_bytes: int | None = None,
             maximum_file_bytes: int | None = None) -> tuple[dict, dict[str, bytes]]:
    """Return ordered Git/byte identities, optionally retaining the verified bytes.

    Empty and binary regular files are supported. Symlinks, submodules and special
    files fail explicitly because their target compiler inputs need their own proof.
    POSIX executable modes are checked directly, independent of core.fileMode.
    """
    root = checkout.resolve(strict=True)
    require(git(root, "rev-parse", "--show-toplevel").decode().strip() == str(root),
            "source must be the Git checkout root")
    head = git(root, "rev-parse", "HEAD").decode().strip()
    require(re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", head) is not None,
            "source HEAD is not an exact Git commit")
    require(revision is None or head == revision, "source revision differs from selected commit")
    tree = git(root, "rev-parse", head + "^{tree}").decode().strip()
    records = git(root, "ls-tree", "-rz", "--full-tree", head).split(b"\0")
    require(records[-1] == b"", "Git tree output is not terminated")
    records.pop()
    require(maximum_files is None or len(records) <= maximum_files, "tracked file bound exceeded")
    artifacts, files, observations = [], {}, {}
    total = 0
    for record in records:
        metadata, name = record.split(b"\t", 1)
        mode, kind, oid = metadata.decode("ascii").split(" ")
        require(kind == "blob" and mode in ("100644", "100755"),
                "tracked symlink, submodule or special file is unsupported")
        relative = name.decode("utf-8", "strict")
        parts = relative.split("/")
        require(all(part not in ("", ".", "..") for part in parts)
                and "\\" not in relative and not Path(relative).is_absolute(), "unsafe tracked path")
        require(relative not in observations, "duplicate tracked path")
        path = root
        for part in parts:
            path /= part
            require(not path.is_symlink(), "tracked path has a symlink: " + relative)
        before_path = path.lstat()
        require(stat.S_ISREG(before_path.st_mode), "tracked path is not a regular file: " + relative)
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
        with os.fdopen(os.open(path, flags), "rb") as stream:
            before = os.fstat(stream.fileno())
            require(stat.S_ISREG(before.st_mode) and fingerprint(before) == fingerprint(before_path),
                    "tracked path changed while opening: " + relative)
            require(maximum_file_bytes is None or before.st_size <= maximum_file_bytes,
                    "tracked file byte bound exceeded: " + relative)
            if os.name != "nt":
                require(bool(before.st_mode & stat.S_IXUSR) == (mode == "100755"),
                        "tracked executable mode differs from Git: " + relative)
            total += before.st_size
            require(maximum_bytes is None or total <= maximum_bytes, "tracked total byte bound exceeded")
            blob = hashlib.sha1() if len(oid) == 40 else hashlib.sha256()
            blob.update(f"blob {before.st_size}\0".encode("ascii"))
            content_hash = hashlib.sha256()
            chunks = [] if retain_bytes else None
            count = 0
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                count += len(chunk)
                require(count <= before.st_size and (maximum_file_bytes is None or count <= maximum_file_bytes),
                        "tracked file grew beyond its observed byte bound: " + relative)
                blob.update(chunk)
                content_hash.update(chunk)
                if chunks is not None:
                    chunks.append(chunk)
            require(fingerprint(before) == fingerprint(os.fstat(stream.fileno()))
                    == fingerprint(path.lstat()) and count == before.st_size,
                    "tracked file changed during observation: " + relative)
        require(blob.hexdigest() == oid, "tracked bytes differ from committed Git blob: " + relative)
        observations[relative] = fingerprint(before)
        if chunks is not None:
            files[relative] = b"".join(chunks)
        artifacts.append({"path": relative, "git_mode": mode, "git_blob": oid,
                          "sha256": content_hash.hexdigest(), "byte_count": count})
    require(git(root, "rev-parse", "HEAD").decode().strip() == head
            and git(root, "rev-parse", head + "^{tree}").decode().strip() == tree,
            "Git source identity changed during observation")
    for relative, observed in observations.items():
        require(fingerprint((root / relative).lstat()) == observed,
                "tracked file changed before observation completed: " + relative)
    return {"commit": head, "tree": tree, "artifacts": artifacts,
            "file_count": len(artifacts), "byte_count": total,
            "posix_executable_modes_verified": os.name != "nt"}, files
