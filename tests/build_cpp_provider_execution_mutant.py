#!/usr/bin/env python3
"""Build a test-only historical provider from exact verified grammar bytes.

The only compiler-policy mutation is removal of source prefix normalization.
The resulting actual shared object is loaded directly by the PostgreSQL test.
This does not create a selected or installed production grammar provider.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/sources"))
import qualify_grammar as Q
from verified_git import GitCorpusError, canonical, digest, exact_file, require


def verify_frozen(root: Path, files: dict[str, bytes]) -> None:
    require(root.is_dir() and not root.is_symlink(), "frozen source root is unavailable")
    actual = set()
    for path in root.rglob("*"):
        require(not path.is_symlink(), "frozen source contains a symlink")
        if path.is_dir():
            continue
        require(path.is_file(), "frozen source contains a special file")
        actual.add(path.relative_to(root).as_posix())
    require(actual == set(files), "frozen source inventory differs from the authenticated Git tree")
    for name, body in files.items():
        require(exact_file(root, name, 128 * 1024 * 1024, allow_empty=True) == body,
                "frozen source bytes differ from the authenticated Git tree: " + name)


def build(receipt_path: Path, receipt_sha256: str, grammar_root: Path,
          grammar_lock: Path, dependency_lock: Path, output_root: Path,
          grammar_name: str = "tree-sitter-cpp") -> Path:
    receipt_path = receipt_path.resolve(strict=True)
    receipt_bytes = receipt_path.read_bytes()
    require(digest(receipt_bytes) == receipt_sha256, "first provider receipt SHA-256 differs")
    first = json.loads(receipt_bytes)
    require(first.get("schema") == "laplace.grammar-provider-build/v1" and
            first.get("phase") == "source-built" and first.get("exit_code") == 0 and
            first.get("grammar_name") == grammar_name,
            "first provider is not the selected successful source build")
    require(first.get("implementation_sha256") == digest(Path(Q.__file__).read_bytes()),
            "first provider was not built by the current canonical owner")
    require(first.get("grammar_lock_sha256") == digest(grammar_lock.read_bytes()) and
            first.get("dependency_lock_sha256") == digest(dependency_lock.read_bytes()),
            "first provider lock bytes differ")
    grammars = json.loads(grammar_lock.read_bytes())["repositories"]
    selected = [entry for entry in grammars if entry["name"] == grammar_name]
    require(len(selected) == 1, "grammar is not uniquely locked")
    grammar = selected[0]
    runtime = json.loads(dependency_lock.read_bytes())["dependencies"]["tree-sitter"]
    for section, expected in (("grammar", grammar), ("runtime", runtime)):
        require(all(first[section].get(key) == expected[key]
                    for key in ("upstream", "revision", "git_archive_sha256")),
                "first provider source/runtime identity differs from its lock")
    generated = grammar["generated_parsers"] + grammar["external_scanners"]
    require(first.get("generated_sources") == generated and len(grammar["generated_parsers"]) == 1,
            "first provider generated-source declarations differ")
    grammar_root = Q.physical_source_root(grammar_root)
    source_identity = Q.verify_source(grammar_root, grammar)
    files = Q.tracked_files(grammar_root)
    first_library = Path(first["library"]["path"])
    require(first_library.is_absolute() and first_library.name == "provider.so" and
            first_library.parent.resolve(strict=True) == receipt_path.parent and
            not first_library.is_symlink(),
            "first provider library is not its owned direct output")
    # Keep the actual lexical compiler paths even when an accepted parent alias
    # leads to the same physical receipt/source directory.
    first_root = first_library.parent
    first_frozen = first_root / "source"
    verify_frozen(first_frozen, files)
    for item in generated:
        require(item["path"] in files and digest(files[item["path"]]) == item["sha256"],
                "generated compiler input differs from lock")
    first_library_bytes = first_library.read_bytes()
    require(digest(first_library_bytes) == first["library"]["sha256"] and
            len(first_library_bytes) == first["library"]["byte_count"],
            "first provider library bytes differ from receipt")
    compiler = Path(first["compiler"]["path"])
    require(compiler.is_absolute() and compiler.resolve(strict=True) == compiler and
            digest(compiler.read_bytes()) == first["compiler"]["sha256"],
            "first provider compiler identity changed")
    expected_first_command = [
        str(compiler), "-shared", "-fPIC", "-O2",
        f"-ffile-prefix-map={first_frozen}=.", "-I", str(first_frozen / "src"),
        *(str(first_frozen / item["path"]) for item in generated), "-o", str(first_library)]
    require(first["command"] == expected_first_command,
            "first compiler command does not match the canonical normalized build")
    inputs = {
        "first_receipt_path": str(receipt_path), "first_receipt_sha256": receipt_sha256,
        "first_library": first["library"], "compiler": first["compiler"],
        "grammar": first["grammar"], "runtime": first["runtime"],
        "grammar_lock_sha256": first["grammar_lock_sha256"],
        "dependency_lock_sha256": first["dependency_lock_sha256"],
        "source_checkout": str(grammar_root), "verified_source": source_identity,
        "generated_sources": generated,
        "tracked_files": [{"path": name, "sha256": digest(body), "byte_count": len(body)}
                          for name, body in sorted(files.items())],
        "helper_sha256": digest(Path(__file__).read_bytes()),
        "mutation": "remove only -ffile-prefix-map; retain actual source/compiler/library identity",
        "production_provider_selected": False,
    }
    require(output_root.is_absolute() and not output_root.is_symlink(),
            "test output root must be absolute and non-symlink")
    output_root.mkdir(parents=True, exist_ok=True)
    output_root = output_root.resolve(strict=True)
    output = output_root / digest(canonical(inputs))
    frozen = output / "source"
    library = output / "provider.so"
    result_path = output / "test-build-receipt.json"
    command = [str(compiler), "-shared", "-fPIC", "-O2", "-I", str(frozen / "src"),
               *(str(frozen / item["path"]) for item in generated), "-o", str(library)]
    if output.exists():
        require(not output.is_symlink() and result_path.is_file() and not result_path.is_symlink(),
                "prior test build is incomplete; retained evidence cannot be overwritten")
        result = json.loads(result_path.read_bytes())
        require(result.get("schema") == "laplace.grammar-provider-execution-mutant/v1" and
                result.get("inputs") == inputs and result.get("command") == command and
                result.get("status") == "completed" and result.get("exit_code") == 0,
                "retained test provider receipt differs from this exact build")
        verify_frozen(frozen, files)
        require(not library.is_symlink(), "retained test library is a symlink")
        body = library.read_bytes()
        require(result["library"] == {"path": str(library), "sha256": digest(body), "byte_count": len(body)}
                and digest(body) != first["library"]["sha256"],
                "retained test provider bytes are invalid or equal to the first provider")
        for channel in ("stdout", "stderr"):
            require(digest(exact_file(output, "build." + channel + ".log", 16 * 1024 * 1024,
                                      allow_empty=True)) == result[channel + "_sha256"],
                    "retained test compiler log differs")
        return result_path

    output.mkdir(mode=0o750)
    for name, body in files.items():
        target = frozen / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(body)
        target.chmod(0o440)
    result = {"schema": "laplace.grammar-provider-execution-mutant/v1",
              "inputs": inputs, "command": command, "status": "running"}
    result_path.write_bytes(canonical(result) + b"\n")
    build_env = {key: value for key, value in os.environ.items() if key not in
                 {"CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
                  "LIBRARY_PATH", "COMPILER_PATH", "GCC_EXEC_PREFIX", "LD_PRELOAD", "LD_LIBRARY_PATH"}}
    try:
        executed = subprocess.run(command, capture_output=True, check=False, timeout=300,
                                  cwd=output, env={**build_env, "LC_ALL": "C"})
        (output / "build.stdout.log").write_bytes(executed.stdout)
        (output / "build.stderr.log").write_bytes(executed.stderr)
        result.update({"exit_code": executed.returncode, "stdout_sha256": digest(executed.stdout),
                       "stderr_sha256": digest(executed.stderr)})
        require(executed.returncode == 0, "actual historical-mutant compiler failed; inspect retained logs")
        body = library.read_bytes()
        result["library"] = {"path": str(library), "sha256": digest(body), "byte_count": len(body)}
        require(digest(body) != first["library"]["sha256"],
                "actual second provider equals the first; fixture did not reproduce path-sensitive bytes")
        verify_frozen(frozen, files)
        verify_frozen(first_frozen, files)
        require(Q.verify_source(grammar_root, grammar) == source_identity and
                Q.tracked_files(grammar_root) == files,
                "authenticated source changed during second-provider build")
        require(receipt_path.read_bytes() == receipt_bytes and first_library.read_bytes() == first_library_bytes
                and digest(compiler.read_bytes()) == first["compiler"]["sha256"]
                and digest(grammar_lock.read_bytes()) == inputs["grammar_lock_sha256"]
                and digest(dependency_lock.read_bytes()) == inputs["dependency_lock_sha256"],
                "provider/compiler/lock input changed during second-provider build")
        result["status"] = "completed"
        library.chmod(0o440)
    except BaseException as error:
        result["status"] = "failed"
        result["failure"] = {"type": type(error).__name__, "message": str(error)}
        raise
    finally:
        result_path.write_bytes(canonical(result) + b"\n")
    result_path.chmod(0o440)
    return result_path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("receipt", "grammar-root", "grammar-lock", "dependency-lock", "output-root"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--receipt-sha256", required=True)
    args = parser.parse_args()
    try:
        result = build(args.receipt, args.receipt_sha256, args.grammar_root,
                       args.grammar_lock, args.dependency_lock, args.output_root)
        receipt = json.loads(result.read_bytes())
        print(json.dumps({"receipt": str(result), "receipt_sha256": digest(result.read_bytes()),
                          "library": receipt["library"]}, sort_keys=True))
    except (GitCorpusError, OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
