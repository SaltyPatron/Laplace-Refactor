#!/usr/bin/env python3
"""Freeze an exact Git observation without parsing or asserting its contents.

The Git tree, blob objects and checkout bytes must agree. Paths are provenance;
native source admission remains the owner of canonical content and receipts.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
from urllib.parse import urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from dependencies.git_checkout import GitCheckoutError, snapshot


class GitCorpusError(RuntimeError):
    pass


def require(value: bool, message: str) -> None:
    if not value:
        raise GitCorpusError(message)


def canonical(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":")).encode("utf-8")


def digest(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def git(root: Path, *arguments: str) -> bytes:
    result = subprocess.run(
        ["git", "--no-optional-locks", "-C", str(root), *arguments],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        check=False, timeout=120,
        env={**os.environ, "GIT_CONFIG_NOSYSTEM": "1", "GIT_TERMINAL_PROMPT": "0",
             "GIT_NO_REPLACE_OBJECTS": "1"},
    )
    require(result.returncode == 0,
            "Git verification failed: " + result.stderr.decode("utf-8", "replace")[-2000:])
    return result.stdout


def exact_file(root: Path, relative: str, maximum_bytes: int, *, allow_empty: bool = False) -> bytes:
    path = PurePosixPath(relative)
    require(bool(path.parts) and not path.is_absolute() and
            all(part not in ("", ".", "..") for part in path.parts),
            "unsafe tracked path")
    current = root
    for part in path.parts:
        current /= part
        require(not current.is_symlink(), "tracked path contains a symlink: " + relative)
    fd = os.open(current, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        before = os.fstat(fd)
        require(stat.S_ISREG(before.st_mode) and
                (0 if allow_empty else 1) <= before.st_size <= maximum_bytes,
                "unsupported empty, special or oversized artifact: " + relative)
        with os.fdopen(fd, "rb", closefd=False) as stream:
            data = stream.read(maximum_bytes + 1)
        after = os.fstat(fd)
        require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns,
                 before.st_ctime_ns) ==
                (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns,
                 after.st_ctime_ns) and len(data) == before.st_size,
                "artifact changed while reading: " + relative)
        return data
    finally:
        os.close(fd)


def observe(checkout: Path, upstream: str, commit: str, *,
            cpp_suffixes: tuple[str, ...] = (".cpp", ".h", ".hpp", ".cc", ".cxx", ".hxx"),
            maximum_files: int = 4096, maximum_bytes: int = 64 * 1024 * 1024,
            maximum_file_bytes: int = 8 * 1024 * 1024,
            expected_archive_sha256: str | None = None) -> tuple[dict, dict[str, bytes]]:
    require(re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", commit) is not None,
            "an exact Git commit is required")
    url = urlsplit(upstream)
    require(url.scheme == "https" and bool(url.hostname) and url.username is None and
            url.password is None and not url.query and not url.fragment and
            not any(c.isspace() for c in upstream),
            "an exact HTTPS upstream is required")
    require(checkout.is_absolute() and not checkout.is_symlink(),
            "checkout must be an absolute non-symlink directory")
    root = checkout.resolve(strict=True)
    require(git(root, "rev-parse", "--show-toplevel").decode().strip() == str(root),
            "checkout must be the Git top-level directory")
    origin = git(root, "remote", "get-url", "origin").decode().strip()
    direct = origin.removesuffix(".git") == upstream.removesuffix(".git")
    origin_url = urlsplit(origin)
    local_import = Path(origin).is_absolute() or (
        origin_url.scheme == "file" and not origin_url.netloc and
        Path(origin_url.path).is_absolute() and not origin_url.query and not origin_url.fragment)
    require(direct or (local_import and expected_archive_sha256 is not None),
            "checkout origin differs from selected upstream without locked local-import lineage")
    require(git(root, "rev-parse", "HEAD").decode().strip() == commit and
            git(root, "rev-parse", "--verify", commit + "^{commit}").decode().strip() == commit,
            "checkout HEAD differs from selected commit")
    require(not git(root, "diff", "--no-ext-diff", "--no-textconv", "--name-only", commit, "--"),
            "tracked checkout bytes differ from the selected commit")
    provenance = {}
    if expected_archive_sha256 is not None:
        require(re.fullmatch(r"[0-9a-f]{64}", expected_archive_sha256) is not None and
                digest(git(root, "archive", "--format=tar", commit)) == expected_archive_sha256,
                "selected Git archive differs from its exact import lock")
        provenance = {"checkout_origin": origin, "git_archive_sha256": expected_archive_sha256,
                      "acquisition": "upstream-checkout" if direct else "verified-local-import"}
    try:
        observation, files = snapshot(root, commit, retain_bytes=True,
            maximum_files=maximum_files, maximum_bytes=maximum_bytes,
            maximum_file_bytes=maximum_file_bytes)
    except GitCheckoutError as error:
        raise GitCorpusError(str(error)) from error
    tree = observation["tree"]
    artifacts = []
    total = observation["byte_count"]
    cpp_count = 0
    for item in observation["artifacts"]:
        name = item["path"]
        content = files[name]
        require(bool(content), "unsupported empty artifact: " + name)
        try:
            content.decode("utf-8", "strict")
        except UnicodeDecodeError as error:
            raise GitCorpusError("unsupported non-UTF-8 artifact: " + name) from error
        require(b"\0" not in content, "unsupported binary artifact: " + name)
        media = "text/x-c++" if PurePosixPath(name).suffix in cpp_suffixes else "text/plain"
        cpp_count += media == "text/x-c++"
        artifacts.append({**item, "media_type": media,
                          "interpretation": "concrete-syntax" if media == "text/x-c++" else "exact-text-observation"})
    require(cpp_count > 0, "selected Git snapshot has no declared C++ artifacts")
    require(git(root, "rev-parse", "HEAD").decode().strip() == commit and
            not git(root, "diff", "--no-ext-diff", "--no-textconv", "--name-only", commit, "--"),
            "checkout changed during the observation")
    artifacts.sort(key=lambda item: item["path"])
    return {
        "schema": "laplace.verified-git-corpus/v1", "phase": "verified-source-input",
        "upstream": upstream, "commit": commit, "tree": tree,
        **provenance,
        "checkout": str(root), "artifacts": artifacts,
        "file_count": len(artifacts), "byte_count": total, "cpp_file_count": cpp_count,
        "selection": "every regular tracked blob; untracked build outputs are outside this Git tree",
        "source_class": "observation", "evidence_source_type": "corpus",
        "semantic_testimony_count": 0,
        "canonical_admission_completed": False,
        "executable_semantics_verified": False,
        "bounds": {"maximum_files": maximum_files, "maximum_bytes": maximum_bytes,
                   "maximum_file_bytes": maximum_file_bytes},
    }, files


def freeze(manifest: dict, files: dict[str, bytes], destination: Path) -> Path:
    require(destination.is_absolute() and not destination.exists() and not destination.is_symlink(),
            "snapshot destination must be a new absolute path")
    destination.mkdir(parents=True, mode=0o750)
    for relative, data in files.items():
        target = destination / "files" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        target.chmod(0o440)
    path = destination / "manifest.json"
    with path.open("xb") as stream:
        stream.write(canonical(manifest) + b"\n")
        stream.flush()
        os.fsync(stream.fileno())
    path.chmod(0o440)
    fd = os.open(destination, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkout", required=True, type=Path)
    parser.add_argument("--upstream", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--expected-archive-sha256")
    args = parser.parse_args()
    try:
        manifest, files = observe(args.checkout, args.upstream, args.commit,
                                  expected_archive_sha256=args.expected_archive_sha256)
        path = freeze(manifest, files, args.output)
        print(json.dumps({"manifest": str(path), "manifest_sha256": digest(path.read_bytes()),
                          "phase": manifest["phase"], "file_count": manifest["file_count"],
                          "byte_count": manifest["byte_count"]}, sort_keys=True))
        return 0
    except (GitCorpusError, OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
