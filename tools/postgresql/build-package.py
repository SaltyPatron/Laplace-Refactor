#!/usr/bin/env python3
"""Build and inspect the selected PostgreSQL package without ambient build state."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path
from typing import Any, Mapping, Sequence


CONTRACT_SCHEMA = "laplace.postgresql-build-contract/v1"
PLAN_SCHEMA = "laplace.postgresql-build-plan/v1"
PACKAGE_SCHEMA = "laplace.postgresql-package-receipt/v1"
NEEDED_PATTERN = re.compile(r"Shared library: \[([^]]+)\]")


class BuildError(RuntimeError):
    pass


def reject_duplicate_object_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    document: dict[str, Any] = {}
    for key, value in pairs:
        if key in document:
            raise BuildError(f"duplicate JSON object key: {key}")
        document[key] = value
    return document


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_object_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise BuildError(f"cannot read JSON document {path}: {error}") from error
    if not isinstance(value, dict):
        raise BuildError(f"JSON document must be an object: {path}")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def canonical_sha256(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def build_recipe_identity(
    contract: dict[str, Any], repository: Path, driver_path: Path | None = None
) -> dict[str, Any]:
    driver = (driver_path or Path(__file__)).resolve()
    verifier = (repository / "tools/dependencies/release-assets.py").resolve()
    for name, path in (("build driver", driver), ("release verifier", verifier)):
        if not path.is_file():
            raise BuildError(f"{name} is missing: {path}")
    return {
        "contract_sha256": canonical_sha256(contract),
        "build_driver": {
            "path": "tools/postgresql/build-package.py",
            "sha256": sha256_file(driver),
        },
        "release_verifier": {
            "path": "tools/dependencies/release-assets.py",
            "sha256": sha256_file(verifier),
        },
    }


def require_string(value: object, name: str) -> str:
    if not isinstance(value, str) or not value:
        raise BuildError(f"{name} must be a non-empty string")
    return value


def require_string_array(value: object, name: str) -> list[str]:
    if not isinstance(value, list) or not value or any(not isinstance(item, str) or not item for item in value):
        raise BuildError(f"{name} must be a non-empty string array")
    if len(set(value)) != len(value):
        raise BuildError(f"{name} contains duplicates")
    return value


def validate_contract(document: dict[str, Any]) -> None:
    if document.get("schema") != CONTRACT_SCHEMA:
        raise BuildError(f"contract schema must be {CONTRACT_SCHEMA}")
    source = document.get("source")
    toolchain = document.get("toolchain")
    input_closure = document.get("input_closure")
    build = document.get("build")
    environment = document.get("environment")
    closure = document.get("runtime_closure")
    if not all(
        isinstance(section, dict)
        for section in (source, toolchain, input_closure, build, environment, closure)
    ):
        raise BuildError(
            "contract source, toolchain, input_closure, build, environment, and runtime_closure must be objects"
        )
    require_string(source.get("release_lock"), "source.release_lock")
    require_string(source.get("entry"), "source.entry")
    require_string(source.get("version"), "source.version")
    for role in ("c", "cxx"):
        compiler = Path(require_string(toolchain.get(f"{role}_compiler"), f"toolchain.{role}_compiler"))
        if not compiler.is_absolute():
            raise BuildError(f"toolchain.{role}_compiler must be absolute")
        compiler_sha = require_string(
            toolchain.get(f"{role}_compiler_sha256"), f"toolchain.{role}_compiler_sha256"
        )
        if not re.fullmatch(r"[0-9a-f]{64}", compiler_sha):
            raise BuildError(f"toolchain.{role}_compiler_sha256 must be lowercase SHA-256")
    require_string(toolchain.get("version_line"), "toolchain.version_line")
    require_string(toolchain.get("target"), "toolchain.target")
    if input_closure.get("status") != "incomplete":
        raise BuildError("input_closure.status must remain incomplete until every build input is selected")
    require_string_array(
        input_closure.get("selected_exact_inputs"), "input_closure.selected_exact_inputs"
    )
    require_string_array(
        input_closure.get("unselected_host_inputs"), "input_closure.unselected_host_inputs"
    )
    if input_closure.get("activation_policy") != (
        "blocked-until-all-build-and-runtime-inputs-are-selected-packaged-and-receipted"
    ):
        raise BuildError("input_closure.activation_policy must be fail-closed")
    configure = require_string_array(build.get("configure_arguments"), "build.configure_arguments")
    if "--enable-tap-tests" not in configure:
        raise BuildError("PostgreSQL build must enable TAP tests")
    flags = require_string_array(build.get("c_flags"), "build.c_flags")
    for required in ("-fno-fast-math", "-ffp-contract=off"):
        if required not in flags:
            raise BuildError(f"build.c_flags must contain {required}")
    cxx_flags = require_string_array(build.get("cxx_flags"), "build.cxx_flags")
    for required in ("-fno-fast-math", "-ffp-contract=off"):
        if required not in cxx_flags:
            raise BuildError(f"build.cxx_flags must contain {required}")
    require_string_array(build.get("linker_flags"), "build.linker_flags")
    targets = require_string_array(build.get("make_targets"), "build.make_targets")
    if targets != ["world-bin", "check-world", "install-world-bin"]:
        raise BuildError("build.make_targets must build, test, then install the complete binary world")
    if not isinstance(build.get("parallel_jobs"), int) or build["parallel_jobs"] < 1:
        raise BuildError("build.parallel_jobs must be positive")
    if build.get("build_directory_mode") != "0700":
        raise BuildError("build.build_directory_mode must be 0700")
    require_string(build.get("pkg_config_libdir"), "build.pkg_config_libdir")
    require_string(build.get("python"), "build.python")
    rejected = require_string_array(environment.get("rejected_nonempty"), "environment.rejected_nonempty")
    for name in ("PGXS", "LD_LIBRARY_PATH", "CMAKE_PREFIX_PATH", "PKG_CONFIG_PATH"):
        if name not in rejected:
            raise BuildError(f"environment.rejected_nonempty must include {name}")
    categories = (
        require_string_array(closure.get("package_sonames"), "runtime_closure.package_sonames"),
        require_string_array(closure.get("system_abi_sonames"), "runtime_closure.system_abi_sonames"),
        require_string_array(
            closure.get("selected_but_unpacked_sonames"),
            "runtime_closure.selected_but_unpacked_sonames",
        ),
    )
    flattened = [item for category in categories for item in category]
    if len(flattened) != len(set(flattened)):
        raise BuildError("runtime closure SONAME categories overlap")
    policy = require_string(closure.get("activation_policy"), "runtime_closure.activation_policy")
    if policy != "blocked-until-selected-libraries-are-packaged-and-recursive-elf-closure-is-clean":
        raise BuildError("runtime closure activation policy is not fail-closed")


def validate_environment(contract: dict[str, Any], environment: Mapping[str, str]) -> None:
    contaminated = sorted(
        name for name in contract["environment"]["rejected_nonempty"] if environment.get(name)
    )
    if contaminated:
        raise BuildError(f"ambient build environment is contaminated: {', '.join(contaminated)}")


def validate_compiler(contract: dict[str, Any], role: str) -> dict[str, str]:
    toolchain = contract["toolchain"]
    compiler = Path(toolchain[f"{role}_compiler"])
    if not compiler.is_file() or not os.access(compiler, os.X_OK):
        raise BuildError(f"selected compiler is not executable: {compiler}")
    observed_sha = sha256_file(compiler)
    if observed_sha != toolchain[f"{role}_compiler_sha256"]:
        raise BuildError(
            f"selected {role} compiler digest mismatch: expected {toolchain[f'{role}_compiler_sha256']}, observed {observed_sha}"
        )
    result = subprocess.run(
        [str(compiler), "--version"], check=True, text=True, capture_output=True
    )
    lines = result.stdout.splitlines()
    if not lines or lines[0] != toolchain["version_line"]:
        raise BuildError("selected compiler version line does not match the contract")
    if f"Target: {toolchain['target']}" not in lines:
        raise BuildError("selected compiler target does not match the contract")
    return {"path": str(compiler), "sha256": observed_sha, "version_line": lines[0]}


def ensure_external(path: Path, repository: Path, name: str) -> Path:
    resolved = path.resolve()
    try:
        resolved.relative_to(repository.resolve())
    except ValueError:
        return resolved
    raise BuildError(f"{name} must be outside the repository: {resolved}")


def selected_release(contract: dict[str, Any], repository: Path) -> dict[str, Any]:
    lock_path = repository / contract["source"]["release_lock"]
    lock = read_json(lock_path)
    entry_name = contract["source"]["entry"]
    entry = lock.get("archives", {}).get(entry_name)
    if not isinstance(entry, dict):
        raise BuildError(f"release lock does not contain {entry_name}")
    if entry.get("version") != contract["source"]["version"]:
        raise BuildError("PostgreSQL release lock version differs from the build contract")
    return entry


def verify_release_import(
    contract: dict[str, Any], repository: Path, archive_root: Path, source_root: Path
) -> dict[str, Any]:
    entry_name = contract["source"]["entry"]
    if source_root.name != entry_name:
        raise BuildError(f"source root must be the {entry_name} member of a verified release import")
    release_tool = repository / "tools/dependencies/release-assets.py"
    lock_path = repository / contract["source"]["release_lock"]
    result = subprocess.run(
        [
            sys.executable,
            str(release_tool),
            "verify-import",
            "--lock",
            str(lock_path),
            "--archive-root",
            str(archive_root.resolve()),
            "--destination",
            str(source_root.parent.resolve()),
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    receipt = json.loads(result.stdout)
    archives = receipt.get("archives")
    if not isinstance(archives, list) or not any(
        isinstance(item, dict)
        and item.get("name") == entry_name
        and item.get("version") == contract["source"]["version"]
        for item in archives
    ):
        raise BuildError("release verification receipt omits the selected PostgreSQL source")
    return receipt


def create_plan(
    contract: dict[str, Any],
    repository: Path,
    archive_root: Path,
    source_root: Path,
    build_root: Path,
    stage_root: Path,
) -> dict[str, Any]:
    validate_contract(contract)
    release = selected_release(contract, repository)
    compilers = {
        "c": validate_compiler(contract, "c"),
        "cxx": validate_compiler(contract, "cxx"),
    }
    recipe = build_recipe_identity(contract, repository)
    source = ensure_external(source_root, repository, "source root")
    source_receipt = verify_release_import(contract, repository, archive_root, source)
    build_base = ensure_external(build_root, repository, "build root")
    stage_base = ensure_external(stage_root, repository, "stage root")
    if not (source / "configure").is_file():
        raise BuildError(f"PostgreSQL configure script is missing: {source}")
    identity_input = {
        "contract": contract,
        "release": {
            "version": release["version"],
            "archive_sha256": release["sha256"],
            "tree_sha256": release["tree_sha256"],
        },
        "compilers": compilers,
        "recipe": recipe,
        "source_verification_sha256": canonical_sha256(source_receipt),
        "execution_paths": {
            "source_root": str(source),
            "build_root": str(build_base),
            "stage_root": str(stage_base),
        },
    }
    build_id = canonical_sha256(identity_input)
    build_directory = build_base / build_id
    prefix = stage_base / build_id / "postgresql-18"
    configure_command = [
        str(source / "configure"),
        f"--prefix={prefix}",
        *contract["build"]["configure_arguments"],
    ]
    return {
        "schema": PLAN_SCHEMA,
        "build_input_id": build_id,
        "source_root": str(source),
        "source_verification_sha256": canonical_sha256(source_receipt),
        "build_directory": str(build_directory),
        "prefix": str(prefix),
        "compilers": compilers,
        "recipe": recipe,
        "configure_command": configure_command,
        "c_flags": contract["build"]["c_flags"],
        "cxx_flags": contract["build"]["cxx_flags"],
        "linker_flags": contract["build"]["linker_flags"],
        "make_targets": contract["build"]["make_targets"],
        "parallel_jobs": contract["build"]["parallel_jobs"],
    }


def build_environment(contract: dict[str, Any], home_directory: Path) -> dict[str, str]:
    compiler_directory = str(Path(contract["toolchain"]["c_compiler"]).parent)
    home_directory.mkdir(exist_ok=True)
    os.chmod(home_directory, 0o700)
    if stat.S_IMODE(home_directory.stat().st_mode) != 0o700:
        raise BuildError(f"build HOME must be private: {home_directory}")
    return {
        "HOME": str(home_directory.resolve()),
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        "PATH": f"{compiler_directory}:/usr/bin:/bin",
        "CC": contract["toolchain"]["c_compiler"],
        "CXX": contract["toolchain"]["cxx_compiler"],
        "CFLAGS": " ".join(contract["build"]["c_flags"]),
        "CXXFLAGS": " ".join(contract["build"]["cxx_flags"]),
        "LDFLAGS": " ".join(contract["build"]["linker_flags"]),
        "PKG_CONFIG_LIBDIR": contract["build"]["pkg_config_libdir"],
        "PYTHON": contract["build"]["python"],
    }


def run_logged(
    command: Sequence[str], cwd: Path, environment: Mapping[str, str], log_path: Path
) -> None:
    with log_path.open("a", encoding="utf-8") as log:
        log.write(f"$ {json.dumps(list(command), separators=(',', ':'))}\n")
        log.flush()
        process = subprocess.Popen(
            list(command),
            cwd=cwd,
            env=dict(environment),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            log.write(line)
        return_code = process.wait()
        if return_code != 0:
            raise BuildError(f"command exited {return_code}: {command[0]}")


def create_private_build_directory(path: Path) -> None:
    """Create an owner-only build boundary even below a shared setgid parent."""
    path.mkdir(parents=True)
    os.chmod(path, 0o700)
    observed_mode = stat.S_IMODE(path.stat().st_mode)
    if observed_mode != 0o700:
        raise BuildError(
            f"build directory mode must be 0700 without inherited setgid: {path} is {observed_mode:04o}"
        )


def prepare_build_directory(plan: dict[str, Any], resume: bool) -> Path:
    build_directory = Path(plan["build_directory"])
    prefix = Path(plan["prefix"])
    plan_path = build_directory / "build-plan.json"
    if resume:
        if not build_directory.is_dir() or not plan_path.is_file():
            raise BuildError("resume requires an existing build directory and build-plan.json")
        if prefix.exists():
            raise BuildError("resume refuses a build with an existing package prefix")
        if read_json(plan_path) != plan:
            raise BuildError("resume build plan differs from the requested exact plan")
        observed_mode = stat.S_IMODE(build_directory.stat().st_mode)
        if observed_mode != 0o700:
            raise BuildError(
                f"resume build directory mode must be 0700: observed {observed_mode:04o}"
            )
    else:
        if build_directory.exists() or prefix.exists():
            raise BuildError("build and package destinations must not already exist")
        create_private_build_directory(build_directory)
        plan_path.write_text(
            json.dumps(plan, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    return build_directory


def execute_plan(
    contract: dict[str, Any], plan: dict[str, Any], resume: bool = False
) -> dict[str, Any]:
    build_directory = prepare_build_directory(plan, resume)
    prefix = Path(plan["prefix"])
    log_path = build_directory / "build.log"
    environment = build_environment(contract, build_directory / ".home")
    run_logged(plan["configure_command"], build_directory, environment, log_path)
    jobs = str(plan["parallel_jobs"])
    for target in plan["make_targets"]:
        run_logged(["make", f"-j{jobs}", target], build_directory, environment, log_path)
    receipt = verify_package(contract, prefix)
    receipt.update(
        {
            "build_input_id": plan["build_input_id"],
            "build_plan_sha256": canonical_sha256(plan),
            "build_log_sha256": sha256_file(log_path),
            "completed_targets": list(plan["make_targets"]),
            "recipe": plan["recipe"],
        }
    )
    (build_directory / "package-receipt.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return receipt


def elf_needed(path: Path) -> set[str]:
    with path.open("rb") as source:
        if source.read(4) != b"\x7fELF":
            return set()
    result = subprocess.run(["readelf", "-d", str(path)], text=True, capture_output=True)
    if result.returncode != 0:
        raise BuildError(f"readelf failed for {path}: {result.stderr.strip()}")
    return set(NEEDED_PATTERN.findall(result.stdout))


def package_tree(prefix: Path) -> tuple[str, int, int, set[str]]:
    digest = hashlib.sha256()
    file_count = 0
    total_bytes = 0
    needed: set[str] = set()
    for path in sorted(prefix.rglob("*"), key=lambda item: item.relative_to(prefix).as_posix()):
        relative = path.relative_to(prefix).as_posix().encode("utf-8")
        mode = stat.S_IMODE(path.lstat().st_mode)
        if path.is_symlink():
            kind = b"symlink"
            content = os.readlink(path).encode("utf-8")
        elif path.is_file():
            kind = b"file"
            content = sha256_file(path).encode("ascii")
            file_count += 1
            total_bytes += path.stat().st_size
            needed.update(elf_needed(path))
        elif path.is_dir():
            kind = b"directory"
            content = b""
        else:
            raise BuildError(f"package contains unsupported filesystem object: {path}")
        for field in (relative, kind, str(mode).encode("ascii"), content):
            digest.update(len(field).to_bytes(8, "big"))
            digest.update(field)
    return digest.hexdigest(), file_count, total_bytes, needed


def classify_needed(contract: dict[str, Any], needed: set[str]) -> dict[str, list[str]]:
    closure = contract["runtime_closure"]
    package = set(closure["package_sonames"])
    system = set(closure["system_abi_sonames"])
    unpacked = set(closure["selected_but_unpacked_sonames"])
    return {
        "package": sorted(needed & package),
        "system_abi": sorted(needed & system),
        "selected_but_unpacked": sorted(needed & unpacked),
        "unknown": sorted(needed - package - system - unpacked),
    }


def verify_package(contract: dict[str, Any], prefix: Path) -> dict[str, Any]:
    validate_contract(contract)
    prefix = prefix.resolve()
    pg_config = prefix / "bin/pg_config"
    postgres = prefix / "bin/postgres"
    if not pg_config.is_file() or not postgres.is_file():
        raise BuildError("package does not contain pg_config and postgres")
    version = subprocess.run(
        [str(pg_config), "--version"], check=True, text=True, capture_output=True
    ).stdout.strip()
    if version != f"PostgreSQL {contract['source']['version']}":
        raise BuildError(f"installed PostgreSQL version mismatch: {version}")
    configure = subprocess.run(
        [str(pg_config), "--configure"], check=True, text=True, capture_output=True
    ).stdout.strip()
    if "--enable-tap-tests" not in configure:
        raise BuildError("installed PostgreSQL was not built with TAP-test support")
    tree_sha, file_count, total_bytes, needed = package_tree(prefix)
    closure = classify_needed(contract, needed)
    if closure["unknown"]:
        disposition = "blocked: package has undeclared direct ELF dependencies"
    elif closure["selected_but_unpacked"]:
        disposition = "blocked: selected runtime libraries remain outside the package"
    else:
        disposition = "blocked: recursive ELF closure has not been packaged and verified"
    return {
        "schema": PACKAGE_SCHEMA,
        "prefix": str(prefix),
        "version": version,
        "configure": configure,
        "tree_sha256": tree_sha,
        "file_count": file_count,
        "total_file_bytes": total_bytes,
        "direct_elf_needed": closure,
        "build_input_closure_complete": False,
        "recursive_elf_closure_verified": False,
        "activation_eligible": False,
        "activation_disposition": disposition,
    }


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", default="contracts/postgresql-build.json")
    parser.add_argument("--repository", default=".")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("validate-contract")
    plan = subparsers.add_parser("plan")
    plan.add_argument("--archive-root", required=True)
    plan.add_argument("--source-root", required=True)
    plan.add_argument("--build-root", required=True)
    plan.add_argument("--stage-root", required=True)
    build = subparsers.add_parser("build")
    build.add_argument("--archive-root", required=True)
    build.add_argument("--source-root", required=True)
    build.add_argument("--build-root", required=True)
    build.add_argument("--stage-root", required=True)
    resume = subparsers.add_parser("resume")
    resume.add_argument("--archive-root", required=True)
    resume.add_argument("--source-root", required=True)
    resume.add_argument("--build-root", required=True)
    resume.add_argument("--stage-root", required=True)
    package = subparsers.add_parser("verify-package")
    package.add_argument("--prefix", required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_arguments(argv)
    repository = Path(arguments.repository).resolve()
    contract_path = Path(arguments.contract)
    if not contract_path.is_absolute():
        contract_path = repository / contract_path
    contract = read_json(contract_path)
    validate_contract(contract)
    if arguments.command == "validate-contract":
        selected_release(contract, repository)
        print(json.dumps({"schema": CONTRACT_SCHEMA, "status": "valid"}, sort_keys=True))
        return 0
    if arguments.command in ("plan", "build", "resume"):
        validate_environment(contract, os.environ)
        plan = create_plan(
            contract,
            repository,
            Path(arguments.archive_root),
            Path(arguments.source_root),
            Path(arguments.build_root),
            Path(arguments.stage_root),
        )
        if arguments.command in ("build", "resume"):
            receipt = execute_plan(contract, plan, resume=arguments.command == "resume")
            print(json.dumps({"plan": plan, "package": receipt}, indent=2, sort_keys=True))
        else:
            print(json.dumps(plan, indent=2, sort_keys=True))
        return 0
    if arguments.command == "verify-package":
        print(json.dumps(verify_package(contract, Path(arguments.prefix)), indent=2, sort_keys=True))
        return 0
    raise BuildError(f"unknown command: {arguments.command}")


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (BuildError, subprocess.CalledProcessError) as error:
        print(f"postgresql-build: {error}", file=sys.stderr)
        raise SystemExit(1) from error
