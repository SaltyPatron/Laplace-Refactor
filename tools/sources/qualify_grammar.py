#!/usr/bin/env python3
"""Build a locked Tree-sitter syntax provider and retain exact build provenance."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

from verified_git import GitCorpusError, canonical, digest, exact_file, git, local_import_origin, require
from dependencies.git_checkout import GitCheckoutError, snapshot


def tracked_files(root: Path) -> dict[str, bytes]:
    try:
        _, files = snapshot(root, retain_bytes=True, maximum_files=10000,
                            maximum_bytes=1024 * 1024 * 1024,
                            maximum_file_bytes=128 * 1024 * 1024)
        return files
    except GitCheckoutError as error:
        raise GitCorpusError(str(error)) from error


def physical_source_root(root: Path) -> Path:
    require(root.is_absolute() and not root.is_symlink(), "source root must be absolute and non-symlink")
    physical = root.resolve(strict=True)
    require(physical.is_dir(), "source root must be a directory")
    return physical


def verify_source(root: Path, entry: dict) -> dict:
    root = physical_source_root(root)
    origin = git(root, "remote", "get-url", "origin").decode().strip()
    require(origin.removesuffix(".git") == entry["upstream"].removesuffix(".git") or local_import_origin(origin),
            "grammar/runtime origin differs from its lock")
    require(git(root, "rev-parse", "HEAD").decode().strip() == entry["revision"],
            "grammar/runtime revision differs from its lock")
    require(not git(root, "diff", "--no-ext-diff", "--no-textconv", "--name-only", "HEAD", "--"),
            "locked grammar/runtime checkout has tracked changes")
    archive_sha = digest(git(root, "archive", "--format=tar", "HEAD"))
    require(archive_sha == entry["git_archive_sha256"], "grammar/runtime archive differs from its lock")
    tracked_files(root)  # Git index flags must never hide changed compiler inputs.
    for notice in entry.get("license_files", entry.get("licenses", [])):
        require(digest(exact_file(root, notice["path"], 1024 * 1024)) == notice["sha256"],
                "grammar/runtime license bytes changed")
    return {"upstream": entry["upstream"], "revision": entry["revision"],
            "git_archive_sha256": archive_sha, "checkout_origin": origin}


def build(grammar_root: Path, runtime_root: Path, grammar_lock: Path,
          dependency_lock: Path, grammar_name: str, output: Path,
          compiler: str = "cc") -> Path:
    # Resolve accepted parent aliases once; all verification and compiler inputs
    # remain bound to these physical checkouts throughout this build.
    grammar_root = physical_source_root(grammar_root)
    runtime_root = physical_source_root(runtime_root)
    grammar_document = json.loads(grammar_lock.read_text())
    entries = grammar_document.get("repositories", grammar_document.get("grammars", []))
    matches = [entry for entry in entries if entry["name"] == grammar_name]
    require(len(matches) == 1, "selected grammar is not uniquely source-locked")
    grammar = matches[0]
    runtime_document = json.loads(dependency_lock.read_text())
    runtime = runtime_document["dependencies"]["tree-sitter"]
    grammar_source = verify_source(grammar_root, grammar)
    runtime_source = verify_source(runtime_root, runtime)
    generated = grammar["generated_parsers"] + grammar["external_scanners"]
    require(len(grammar["generated_parsers"]) == 1,
            "this physical provider build requires one declared generated parser")
    for item in generated:
        require(digest(exact_file(grammar_root, item["path"], 128 * 1024 * 1024)) == item["sha256"],
                "generated parser/scanner differs from its lock")
    compiler_path = shutil.which(compiler)
    require(compiler_path is not None, "C compiler is unavailable")
    compiler_path = str(Path(compiler_path).resolve(strict=True))
    compiler_version = subprocess.run([compiler_path, "--version"], check=True,
        capture_output=True, text=True, timeout=30).stdout
    require(output.is_absolute() and not output.exists() and not output.is_symlink(),
            "grammar output must be a new absolute directory")
    output.mkdir(parents=True, mode=0o750)
    # Compile only a frozen tree of verified Git blobs. Untracked checkout headers
    # and ignore rules cannot introduce include shadowing into this build.
    frozen = output / "source"
    for relative, data in tracked_files(grammar_root).items():
        target = frozen / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        target.chmod(0o440)
    library = output / "provider.so"
    command = [compiler_path, "-shared", "-fPIC", "-O2", "-I", str(frozen / "src"),
               *(str(frozen / item["path"]) for item in generated), "-o", str(library)]
    build_env = {key: value for key, value in os.environ.items() if key not in
                 {"CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
                  "LIBRARY_PATH", "COMPILER_PATH", "GCC_EXEC_PREFIX", "LD_PRELOAD", "LD_LIBRARY_PATH"}}
    result = subprocess.run(command, capture_output=True, check=False, timeout=300,
        cwd=output, env={**build_env, "LC_ALL": "C"})
    (output / "build.stdout.log").write_bytes(result.stdout)
    (output / "build.stderr.log").write_bytes(result.stderr)
    require(result.returncode == 0, "locked grammar build failed; inspect retained build logs")
    library_bytes = library.read_bytes()
    library.chmod(0o440)
    require(verify_source(grammar_root, grammar) == grammar_source and
            verify_source(runtime_root, runtime) == runtime_source,
            "grammar/runtime source changed during build")
    receipt = {
        "schema": "laplace.grammar-provider-build/v1", "phase": "source-built",
        "implementation_sha256": digest(Path(__file__).read_bytes()),
        "grammar_name": grammar_name, "grammar": grammar_source, "runtime": runtime_source,
        "generated_sources": generated, "grammar_lock_sha256": digest(grammar_lock.read_bytes()),
        "dependency_lock_sha256": digest(dependency_lock.read_bytes()),
        "compiler": {"path": compiler_path, "sha256": digest(Path(compiler_path).read_bytes()),
                     "version_output": compiler_version},
        "command": command, "exit_code": result.returncode,
        "stdout_sha256": digest(result.stdout), "stderr_sha256": digest(result.stderr),
        "library": {"path": str(library), "byte_count": len(library_bytes),
                    "sha256": digest(library_bytes)},
        "grammar_runtime_execution_completed": False,
        "canonical_corpus_admission_completed": False,
    }
    path = output / "build-receipt.json"
    with path.open("xb") as stream:
        stream.write(canonical(receipt) + b"\n")
        stream.flush()
        os.fsync(stream.fileno())
    path.chmod(0o440)
    return path


def acquire(grammar_root: Path, grammar_lock: Path, grammar_name: str) -> None:
    entries = json.loads(grammar_lock.read_text())["repositories"]
    matches = [entry for entry in entries if entry["name"] == grammar_name]
    require(len(matches) == 1, "selected grammar is not uniquely source-locked")
    entry = matches[0]
    require(grammar_root.is_absolute() and not grammar_root.is_symlink(),
            "grammar checkout must be an absolute non-symlink path")
    if grammar_root.exists():
        verify_source(grammar_root, entry)
        return
    grammar_root.mkdir(parents=True)
    grammar_root = physical_source_root(grammar_root)
    for arguments in (["init", "-q"], ["remote", "add", "origin", entry["upstream"]],
                      ["fetch", "--depth=1", "--no-tags", "origin", entry["revision"]],
                      ["checkout", "--detach", entry["revision"]]):
        git(grammar_root, *arguments)
    verify_source(grammar_root, entry)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--grammar-root", required=True, type=Path)
    parser.add_argument("--runtime-root", required=True, type=Path)
    parser.add_argument("--grammar-lock", required=True, type=Path)
    parser.add_argument("--dependency-lock", required=True, type=Path)
    parser.add_argument("--grammar", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--compiler", default="cc")
    parser.add_argument("--acquire", action="store_true",
                        help="acquire only the selected locked grammar into the specified checkout")
    args = parser.parse_args()
    try:
        if args.acquire:
            acquire(args.grammar_root, args.grammar_lock, args.grammar)
        path = build(args.grammar_root, args.runtime_root, args.grammar_lock,
                     args.dependency_lock, args.grammar, args.output, args.compiler)
        print(json.dumps({"receipt": str(path), "sha256": digest(path.read_bytes())}))
        return 0
    except (GitCorpusError, OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
