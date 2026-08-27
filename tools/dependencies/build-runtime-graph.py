#!/usr/bin/env python3
"""Build selected PostgreSQL runtime leaves into one inert staged package."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shlex
import shutil
import stat
import subprocess
import sys
from pathlib import Path
from typing import Any, Mapping, Sequence


SCHEMA = "laplace.postgresql-runtime-build/v1"
PLAN_SCHEMA = "laplace.postgresql-runtime-plan/v1"
RECEIPT_SCHEMA = "laplace.postgresql-runtime-package/v1"
CHECKPOINT_SCHEMA = "laplace.postgresql-runtime-component-checkpoint/v1"
TREE_SCHEMA = "laplace.package-tree-state/v1"
TOOLCHAIN_RECEIPT_SCHEMA = "laplace.toolchain-package-receipt/v1"
TOOLCHAIN_MANIFEST_SCHEMA = "laplace.toolchain-consumer-manifest/v1"
PROVIDERS = {"cmake", "autotools", "openssl", "source-copy-make"}
TESTS = {
    "ctest",
    "make-check",
    "make-test",
    "make-runtests",
    "source-copy-make-check",
}
LANGUAGES = {"C", "CXX", "ASM"}
NEEDED_PATTERN = re.compile(r"Shared library: \[([^]]+)\]")
RUNPATH_PATTERN = re.compile(r"Library r(?:un)?path: \[([^]]*)\]", re.IGNORECASE)
ELF_TYPE_PATTERN = re.compile(r"^\s*Type:\s+([A-Z]+)\b", re.MULTILINE)
INTERPRETER_PATTERN = re.compile(r"Requesting program interpreter:\s*([^]\n]+)")
COPY_RELOCATION_PATTERN = re.compile(r"\b(R_[A-Z0-9_]+_COPY)\b")
COMPILER_TEMPORARY_PATTERN = re.compile(
    r"/(?:tmp|var/tmp)/(?:icx|icpx)-[A-Za-z0-9]+/[^\"'\s]+\.o"
)


class GraphError(RuntimeError):
    pass


def reject_duplicate_object_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise GraphError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_object_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise GraphError(f"cannot read JSON document {path}: {error}") from error
    if not isinstance(value, dict):
        raise GraphError(f"JSON document must be an object: {path}")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def canonical_sha256(value: object) -> str:
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()


def write_json_atomic(path: Path, value: dict[str, Any]) -> None:
    temporary = path.with_name(f".{path.name}.temporary")
    try:
        with temporary.open("x", encoding="utf-8") as output:
            output.write(json.dumps(value, indent=2, sort_keys=True) + "\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


def tree_state(root: Path) -> dict[str, Any]:
    if not root.exists() and not root.is_symlink():
        return {
            "schema": TREE_SCHEMA,
            "exists": False,
            "tree_sha256": canonical_sha256([]),
            "entry_count": 0,
            "file_count": 0,
            "total_file_bytes": 0,
        }
    if not root.is_dir() or root.is_symlink():
        raise GraphError(f"package tree root is not a physical directory: {root}")
    records: list[dict[str, Any]] = []
    file_count = 0
    total_file_bytes = 0
    for path in sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix()):
        relative = path.relative_to(root).as_posix()
        mode = f"{stat.S_IMODE(path.lstat().st_mode):04o}"
        if path.is_symlink():
            record = {
                "path": relative,
                "kind": "symlink",
                "mode": mode,
                "target": os.readlink(path),
            }
        elif path.is_file():
            size = path.stat().st_size
            file_count += 1
            total_file_bytes += size
            record = {
                "path": relative,
                "kind": "file",
                "mode": mode,
                "size": size,
                "sha256": sha256_file(path),
            }
        elif path.is_dir():
            record = {"path": relative, "kind": "directory", "mode": mode}
        else:
            raise GraphError(f"unsupported package tree object: {path}")
        records.append(record)
    return {
        "schema": TREE_SCHEMA,
        "exists": True,
        "tree_sha256": canonical_sha256(records),
        "entry_count": len(records),
        "file_count": file_count,
        "total_file_bytes": total_file_bytes,
    }


def require_string(value: object, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise GraphError(f"{field} must be a non-empty string")
    return value


def require_string_list(value: object, field: str, allow_empty: bool = False) -> list[str]:
    if not isinstance(value, list) or (not value and not allow_empty):
        raise GraphError(f"{field} must be a string array")
    if any(not isinstance(item, str) or not item for item in value):
        raise GraphError(f"{field} contains an invalid string")
    if len(value) != len(set(value)):
        raise GraphError(f"{field} contains duplicates")
    return value


def validate_contract(contract: dict[str, Any]) -> None:
    if contract.get("schema") != SCHEMA:
        raise GraphError(f"contract schema must be {SCHEMA}")
    require_string(contract.get("release_lock"), "release_lock")
    compiler = contract.get("compiler")
    build_toolchain = contract.get("build_toolchain")
    execution = contract.get("execution")
    closure = contract.get("input_closure")
    components = contract.get("components")
    if not all(isinstance(item, dict) for item in (compiler, build_toolchain, execution)):
        raise GraphError("compiler, build_toolchain, and execution must be objects")
    if not isinstance(closure, dict) or not isinstance(components, list) or not components:
        raise GraphError("input_closure and non-empty components are required")
    for name in ("c_compiler", "cxx_compiler"):
        tool = compiler.get(name)
        if not isinstance(tool, dict):
            raise GraphError(f"compiler.{name} must be an object")
        path = Path(require_string(tool.get("path"), f"compiler.{name}.path"))
        digest = require_string(tool.get("sha256"), f"compiler.{name}.sha256")
        if not path.is_absolute() or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise GraphError(f"compiler.{name} path or digest is invalid")
    if build_toolchain.get("receipt_schema") != TOOLCHAIN_RECEIPT_SCHEMA:
        raise GraphError("build_toolchain.receipt_schema is invalid")
    if build_toolchain.get("consumer_manifest_schema") != TOOLCHAIN_MANIFEST_SCHEMA:
        raise GraphError("build_toolchain.consumer_manifest_schema is invalid")
    require_string_list(build_toolchain.get("required_tools"), "build_toolchain.required_tools")
    if not isinstance(execution.get("jobs"), int) or execution["jobs"] < 1:
        raise GraphError("execution.jobs must be positive")
    source_date_epoch = require_string(
        execution.get("source_date_epoch"), "execution.source_date_epoch"
    )
    if not source_date_epoch.isdecimal():
        raise GraphError("execution.source_date_epoch must be decimal")
    for field in ("c_flags", "cxx_flags", "link_flags", "rejected_environment"):
        require_string_list(execution.get(field), f"execution.{field}")
    executable_elf = execution.get("executable_elf")
    if not isinstance(executable_elf, dict):
        raise GraphError("execution.executable_elf must be an object")
    if executable_elf.get("position_independent") is not True:
        raise GraphError("runtime executables must be position independent")
    if executable_elf.get("copy_relocations") != "forbidden":
        raise GraphError("runtime executable COPY relocations must be forbidden")
    if "-fPIE" not in execution["c_flags"] or "-fPIE" not in execution["cxx_flags"]:
        raise GraphError("runtime C and C++ compilation must select -fPIE")
    if "-pie" not in execution["link_flags"]:
        raise GraphError("runtime executable linking must select -pie")
    install_runpath = require_string(
        execution.get("install_runpath"), "execution.install_runpath"
    )
    if install_runpath != "$ORIGIN/../lib":
        raise GraphError(
            "execution.install_runpath must be the package-relative $ORIGIN/../lib"
        )
    for field in ("final_prefix_root", "build_root", "stage_root"):
        if not Path(require_string(execution.get(field), f"execution.{field}")).is_absolute():
            raise GraphError(f"execution.{field} must be absolute")
    if closure.get("status") != "incomplete":
        raise GraphError("input_closure must remain incomplete until all tool inputs close")
    require_string_list(closure.get("unresolved"), "input_closure.unresolved")
    if closure.get("activation_policy") != (
        "blocked-until-complete-toolchain-static-link-and-recursive-runtime-closure"
    ):
        raise GraphError("input_closure activation policy must fail closed")

    identifiers: set[str] = set()
    sources: set[str] = set()
    for component in components:
        if not isinstance(component, dict):
            raise GraphError("component must be an object")
        identifier = require_string(component.get("id"), "component.id")
        source = require_string(component.get("source"), f"{identifier}.source")
        if identifier in identifiers:
            raise GraphError(f"duplicate component: {identifier}")
        identifiers.add(identifier)
        sources.add(source)
        provider = require_string(component.get("provider"), f"{identifier}.provider")
        if provider not in PROVIDERS:
            raise GraphError(f"unsupported provider for {identifier}: {provider}")
        languages = require_string_list(
            component.get("languages"), f"{identifier}.languages"
        )
        unknown_languages = sorted(set(languages) - LANGUAGES)
        if unknown_languages:
            raise GraphError(
                f"unsupported language for {identifier}: {', '.join(unknown_languages)}"
            )
        require_string(component.get("source_subdirectory"), f"{identifier}.source_subdirectory")
        dependencies = require_string_list(
            component.get("depends_on"), f"{identifier}.depends_on", allow_empty=True
        )
        unknown = sorted(set(dependencies) - identifiers)
        if unknown:
            raise GraphError(
                f"{identifier} dependencies must precede it in the build graph: {', '.join(unknown)}"
            )
        require_string_list(
            component.get("configure_arguments"),
            f"{identifier}.configure_arguments",
            allow_empty=True,
        )
        test = require_string(component.get("test"), f"{identifier}.test")
        if test not in TESTS:
            raise GraphError(f"unsupported test contract for {identifier}: {test}")
        permitted_tests = {
            "cmake": {"ctest", "source-copy-make-check"},
            "autotools": {"make-check"},
            "openssl": {"make-test"},
            "source-copy-make": {"make-runtests"},
        }
        if test not in permitted_tests[provider]:
            raise GraphError(
                f"test contract {test} is incompatible with provider {provider} for {identifier}"
            )


def validate_environment(contract: dict[str, Any], environment: Mapping[str, str]) -> None:
    contaminated = sorted(
        key for key in contract["execution"]["rejected_environment"] if environment.get(key)
    )
    if contaminated:
        raise GraphError(f"ambient build environment is contaminated: {', '.join(contaminated)}")


def verify_compilers(contract: dict[str, Any]) -> dict[str, dict[str, str]]:
    result: dict[str, dict[str, str]] = {}
    for name, selected in contract["compiler"].items():
        path = Path(selected["path"])
        if not path.is_file() or not os.access(path, os.X_OK):
            raise GraphError(f"selected tool is not executable: {path}")
        observed = sha256_file(path)
        if observed != selected["sha256"]:
            raise GraphError(f"selected tool digest mismatch: {name}")
        result[name] = {"path": str(path), "sha256": observed}
    return result


def path_is_within(path: Path, prefix: Path) -> bool:
    try:
        path.resolve().relative_to(prefix.resolve())
    except ValueError:
        return False
    return True


def verify_toolchain_receipt(
    contract: dict[str, Any], receipt_path: Path
) -> dict[str, Any]:
    receipt = read_json(receipt_path)
    expected = contract["build_toolchain"]
    if receipt.get("schema") != expected["receipt_schema"]:
        raise GraphError("toolchain receipt schema mismatch")
    build_input_id = require_string(receipt.get("build_input_id"), "toolchain.build_input_id")
    if not re.fullmatch(r"[0-9a-f]{64}", build_input_id):
        raise GraphError("toolchain build_input_id must be lowercase SHA-256")
    package = receipt.get("package")
    manifest = receipt.get("consumer_manifest")
    activation = receipt.get("activation")
    if not all(isinstance(item, dict) for item in (package, manifest, activation)):
        raise GraphError("toolchain receipt package, consumer_manifest, and activation are required")
    prefix = Path(require_string(package.get("prefix"), "toolchain.package.prefix"))
    if not prefix.is_absolute() or not prefix.is_dir():
        raise GraphError("toolchain package prefix must be an existing absolute directory")
    if manifest.get("schema") != expected["consumer_manifest_schema"]:
        raise GraphError("toolchain consumer manifest schema mismatch")
    if manifest.get("build_input_id") != build_input_id:
        raise GraphError("toolchain receipt and consumer manifest build_input_id differ")
    if manifest.get("prefix") != str(prefix):
        raise GraphError("toolchain receipt and consumer manifest prefix differ")
    if activation.get("scope") != "build-toolchain-only":
        raise GraphError("toolchain activation scope must be build-toolchain-only")
    if activation.get("product_runtime_activation_eligible") is not False:
        raise GraphError("build toolchain cannot be product-runtime activation eligible")
    tools = manifest.get("tools")
    if not isinstance(tools, dict):
        raise GraphError("toolchain consumer manifest tools must be an object")
    selected: dict[str, dict[str, str]] = {}
    for name in expected["required_tools"]:
        tool = tools.get(name)
        if not isinstance(tool, dict):
            raise GraphError(f"toolchain consumer manifest omits required tool: {name}")
        path = Path(require_string(tool.get("path"), f"toolchain.tools.{name}.path"))
        digest = require_string(tool.get("sha256"), f"toolchain.tools.{name}.sha256")
        version = require_string(tool.get("version"), f"toolchain.tools.{name}.version")
        if not path.is_absolute() or not path_is_within(path, prefix):
            raise GraphError(f"toolchain tool is outside its package prefix: {name}")
        if not path.is_file() or not os.access(path, os.X_OK):
            raise GraphError(f"toolchain tool is not executable: {name}")
        if not re.fullmatch(r"[0-9a-f]{64}", digest) or sha256_file(path) != digest:
            raise GraphError(f"toolchain tool digest mismatch: {name}")
        selected[name] = {"path": str(path), "sha256": digest, "version": version}
    return {
        "receipt_path": str(receipt_path.resolve()),
        "receipt_sha256": sha256_file(receipt_path),
        "build_input_id": build_input_id,
        "prefix": str(prefix),
        "tools": selected,
    }


def normalize_compiler_driver_trace(trace: str) -> str:
    """Remove compiler-created temporary object names from a semantic driver trace."""

    return COMPILER_TEMPORARY_PATTERN.sub("<compiler-temporary-object>", trace)


def compiler_driver_trace(
    contract: dict[str, Any], compilers: dict[str, dict[str, str]], toolchain: dict[str, Any]
) -> dict[str, Any]:
    bin_directory = Path(toolchain["prefix"]) / "bin"
    selected_linker = toolchain["tools"]["ld"]["path"]
    result: dict[str, Any] = {}
    for role, language, flag_field in (
        ("c_compiler", "c", "c_flags"),
        ("cxx_compiler", "c++", "cxx_flags"),
    ):
        command = [
            compilers[role]["path"],
            "-###",
            f"-B{bin_directory}",
            *contract["execution"][flag_field],
            *contract["execution"]["link_flags"],
            "-x",
            language,
            "/dev/null",
            "-o",
            "/dev/null",
        ]
        process = subprocess.run(command, cwd="/", text=True, capture_output=True)
        trace = process.stderr + process.stdout
        if process.returncode != 0:
            raise GraphError(f"{role} driver trace failed")
        if selected_linker not in trace:
            raise GraphError(f"{role} driver trace did not select the packaged linker")
        absolute_paths: set[str] = set()
        linker_arguments: list[str] | None = None
        for line in trace.splitlines():
            try:
                tokens = shlex.split(line)
            except ValueError:
                continue
            absolute_paths.update(token for token in tokens if token.startswith("/"))
            if selected_linker in tokens:
                linker_arguments = tokens
        if linker_arguments is None or "-pie" not in linker_arguments:
            raise GraphError(f"{role} driver trace did not select PIE linking")
        absolute_inputs = []
        for path_text in sorted(absolute_paths):
            path = Path(path_text)
            if not path.is_file():
                continue
            absolute_inputs.append(
                {
                    "path": path_text,
                    "sha256": sha256_file(path),
                    "size": path.stat().st_size,
                }
            )
        normalized_trace = normalize_compiler_driver_trace(trace)
        result[role] = {
            "command": command,
            "trace_normalization": "laplace.compiler-driver-trace/v1",
            "trace_sha256": hashlib.sha256(normalized_trace.encode("utf-8")).hexdigest(),
            "selected_linker": selected_linker,
            "pie_link_selected": True,
            "absolute_inputs": absolute_inputs,
        }
    return result


def ensure_external(path: Path, repository: Path, field: str) -> Path:
    resolved = path.resolve()
    try:
        resolved.relative_to(repository.resolve())
    except ValueError:
        return resolved
    raise GraphError(f"{field} must be outside the repository: {resolved}")


def verify_release_generation(
    repository: Path, lock_path: Path, archive_root: Path, source_generation: Path
) -> str:
    lock_sha = sha256_file(lock_path)
    if source_generation.name != lock_sha:
        raise GraphError("release source generation name must equal the exact release-lock SHA-256")
    verifier = repository / "tools/dependencies/release-assets.py"
    subprocess.run(
        [
            sys.executable,
            str(verifier),
            "verify-import",
            "--lock",
            str(lock_path),
            "--archive-root",
            str(archive_root),
            "--destination",
            str(source_generation),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    return lock_sha


def load_release_module(repository: Path) -> Any:
    path = repository / "tools/dependencies/release-assets.py"
    spec = importlib.util.spec_from_file_location("laplace_runtime_release_assets", path)
    if spec is None or spec.loader is None:
        raise GraphError(f"cannot load release verifier: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def prepare_private_sources(
    contract: dict[str, Any], plan: dict[str, Any], repository: Path, build_root: Path
) -> Path:
    sources_root = build_root / "sources"
    private_directory(sources_root)
    lock = read_json(repository / contract["release_lock"])
    archives = lock.get("archives")
    if not isinstance(archives, dict):
        raise GraphError("release lock archives must be an object")
    release = load_release_module(repository)
    archive_root = Path(plan["archive_root"])
    for source_id in dict.fromkeys(component["source"] for component in plan["components"]):
        entry = archives.get(source_id)
        if not isinstance(entry, dict):
            raise GraphError(f"release lock does not contain component source: {source_id}")
        try:
            release.import_entry(source_id, entry, archive_root, sources_root)
            release.verify_imported_entry(source_id, entry, archive_root, sources_root)
            normalize_tree_timestamps(
                sources_root / source_id, int(contract["execution"]["source_date_epoch"])
            )
        except Exception as error:
            raise GraphError(f"private source extraction failed for {source_id}: {error}") from error
    return sources_root


def verify_private_sources(
    contract: dict[str, Any], plan: dict[str, Any], repository: Path, build_root: Path
) -> Path:
    sources_root = build_root / "sources"
    if (
        not sources_root.is_dir()
        or sources_root.is_symlink()
        or stat.S_IMODE(sources_root.stat().st_mode) != 0o700
    ):
        raise GraphError("resumed private source root is missing, linked, or not mode 0700")
    lock = read_json(repository / contract["release_lock"])
    archives = lock.get("archives")
    if not isinstance(archives, dict):
        raise GraphError("release lock archives must be an object")
    source_ids = list(dict.fromkeys(component["source"] for component in plan["components"]))
    observed = sorted(path.name for path in sources_root.iterdir())
    if observed != sorted(source_ids):
        raise GraphError("resumed private source set differs from the build plan")
    release = load_release_module(repository)
    archive_root = Path(plan["archive_root"])
    for source_id in source_ids:
        entry = archives.get(source_id)
        if not isinstance(entry, dict):
            raise GraphError(f"release lock does not contain component source: {source_id}")
        try:
            release.verify_imported_entry(source_id, entry, archive_root, sources_root)
        except Exception as error:
            raise GraphError(
                f"resumed private source verification failed for {source_id}: {error}"
            ) from error
    return sources_root


def normalize_tree_timestamps(root: Path, epoch_seconds: int) -> None:
    if epoch_seconds < 0 or not root.is_dir() or root.is_symlink():
        raise GraphError(f"cannot normalize private source tree: {root}")
    timestamp = epoch_seconds * 1_000_000_000
    paths = sorted(root.rglob("*"), key=lambda path: len(path.parts), reverse=True)
    for path in [*paths, root]:
        if not path.is_symlink():
            os.utime(path, ns=(timestamp, timestamp))


def create_plan(
    contract: dict[str, Any],
    repository: Path,
    archive_root: Path,
    source_generation: Path,
    toolchain_receipt: Path,
) -> dict[str, Any]:
    validate_contract(contract)
    compilers = verify_compilers(contract)
    toolchain = verify_toolchain_receipt(contract, toolchain_receipt)
    driver_traces = compiler_driver_trace(contract, compilers, toolchain)
    lock_path = (repository / contract["release_lock"]).resolve()
    release_lock_sha = verify_release_generation(
        repository, lock_path, archive_root.resolve(), source_generation.resolve()
    )
    driver = Path(__file__).resolve()
    identity = {
        "contract_sha256": canonical_sha256(contract),
        "driver_sha256": sha256_file(driver),
        "release_lock_sha256": release_lock_sha,
        "compilers": compilers,
        "toolchain": toolchain,
        "compiler_driver_traces": driver_traces,
    }
    build_id = canonical_sha256(identity)
    execution = contract["execution"]
    final_prefix = ensure_external(
        Path(execution["final_prefix_root"]) / build_id, repository, "final prefix"
    )
    build_directory = ensure_external(
        Path(execution["build_root"]) / build_id, repository, "build directory"
    )
    stage_directory = ensure_external(
        Path(execution["stage_root"]) / build_id, repository, "stage directory"
    )
    staged_prefix = stage_directory / "root" / final_prefix.relative_to("/")
    return {
        "schema": PLAN_SCHEMA,
        "build_input_id": build_id,
        "identity": identity,
        "source_generation": str(source_generation.resolve()),
        "archive_root": str(archive_root.resolve()),
        "build_directory": str(build_directory),
        "stage_directory": str(stage_directory),
        "final_prefix": str(final_prefix),
        "staged_prefix": str(staged_prefix),
        "components": contract["components"],
        "compilers": compilers,
        "tools": toolchain["tools"],
        "toolchain_prefix": toolchain["prefix"],
        "compiler_driver_traces": driver_traces,
        "activation_eligible": False,
    }


def private_directory(path: Path) -> None:
    path.mkdir(parents=True)
    path.chmod(0o700)
    if stat.S_IMODE(path.stat().st_mode) != 0o700:
        raise GraphError(f"private directory mode is not 0700: {path}")


def build_environment(
    contract: dict[str, Any], plan: dict[str, Any], home: Path
) -> dict[str, str]:
    private_directory(home)
    compilers = contract["compiler"]
    tools = plan["tools"]
    staged = Path(plan["staged_prefix"])
    tool_directories = []
    for selected in tools.values():
        directory = str(Path(selected["path"]).parent)
        if directory not in tool_directories:
            tool_directories.append(directory)
    flags = contract["execution"]
    return {
        "HOME": str(home),
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        "SOURCE_DATE_EPOCH": contract["execution"]["source_date_epoch"],
        "PATH": ":".join([*tool_directories, "/usr/bin", "/bin"]),
        "CC": compilers["c_compiler"]["path"],
        "CXX": compilers["cxx_compiler"]["path"],
        "CFLAGS": " ".join([f"-B{Path(plan['toolchain_prefix']) / 'bin'}", *flags["c_flags"]]),
        "CXXFLAGS": " ".join([f"-B{Path(plan['toolchain_prefix']) / 'bin'}", *flags["cxx_flags"]]),
        "CPPFLAGS": f"-I{staged / 'include'}",
        "LDFLAGS": " ".join(
            [
                f"-L{staged / 'lib'}",
                f"-Wl,-rpath,'{flags['install_runpath']}'",
                *flags["link_flags"],
            ]
        ),
        "PKG_CONFIG_LIBDIR": ":".join(
            [str(staged / "lib/pkgconfig"), str(staged / "share/pkgconfig")]
        ),
        "PKG_CONFIG_SYSROOT_DIR": str(Path(plan["stage_directory"]) / "root"),
        "PKG_CONFIG": tools["pkgconf"]["path"],
        "CMAKE_PREFIX_PATH": str(staged),
        "AR": tools["ar"]["path"],
        "AS": tools["as"]["path"],
        "LD": tools["ld"]["path"],
        "NM": tools["nm"]["path"],
        "RANLIB": tools["ranlib"]["path"],
        "STRIP": tools["strip"]["path"],
        "OBJCOPY": tools["objcopy"]["path"],
        "OBJDUMP": tools["objdump"]["path"],
    }


def run_logged(
    command: Sequence[str], cwd: Path, environment: Mapping[str, str], log: Path
) -> None:
    with log.open("a", encoding="utf-8") as output:
        output.write("$ " + json.dumps(list(command), separators=(",", ":")) + "\n")
        output.flush()
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
            output.write(line)
        code = process.wait()
        if code != 0:
            raise GraphError(f"command exited {code}: {command[0]}")


def cmake_component(
    contract: dict[str, Any],
    plan: dict[str, Any],
    component: dict[str, Any],
    source: Path,
    build: Path,
    environment: dict[str, str],
    log: Path,
) -> None:
    tools = plan["tools"]
    compilers = contract["compiler"]
    final = Path(plan["final_prefix"])
    staged = Path(plan["staged_prefix"])
    cmake = tools["cmake"]["path"]
    ninja = tools["ninja"]["path"]
    compiler_arguments: list[str] = []
    if "C" in component["languages"]:
        compiler_arguments.append(
            f"-DCMAKE_C_COMPILER={compilers['c_compiler']['path']}"
        )
    if "CXX" in component["languages"]:
        compiler_arguments.append(
            f"-DCMAKE_CXX_COMPILER={compilers['cxx_compiler']['path']}"
        )
    if "ASM" in component["languages"]:
        compiler_arguments.append(
            f"-DCMAKE_ASM_COMPILER={compilers['c_compiler']['path']}"
        )
    run_logged(
        [
            cmake,
            "-S",
            str(source),
            "-B",
            str(build),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
            f"-DCMAKE_MAKE_PROGRAM={ninja}",
            f"-DCMAKE_INSTALL_PREFIX={final}",
            *compiler_arguments,
            f"-DCMAKE_AR={tools['ar']['path']}",
            f"-DCMAKE_LINKER={tools['ld']['path']}",
            f"-DCMAKE_NM={tools['nm']['path']}",
            f"-DCMAKE_OBJCOPY={tools['objcopy']['path']}",
            f"-DCMAKE_OBJDUMP={tools['objdump']['path']}",
            f"-DCMAKE_RANLIB={tools['ranlib']['path']}",
            f"-DCMAKE_STRIP={tools['strip']['path']}",
            f"-DCMAKE_BUILD_RPATH={staged / 'lib'}",
            f"-DCMAKE_INSTALL_RPATH={contract['execution']['install_runpath']}",
            *component["configure_arguments"],
        ],
        build.parent,
        environment,
        log,
    )
    run_logged(
        [cmake, "--build", str(build), "--parallel", str(contract["execution"]["jobs"])],
        build.parent,
        environment,
        log,
    )
    if component["test"] == "ctest":
        run_logged(
            [tools["ctest"]["path"], "--test-dir", str(build), "--output-on-failure"],
            build.parent,
            environment,
            log,
        )
    elif component["test"] == "source-copy-make-check":
        test_source = build.parent / "test-source"
        shutil.copytree(source.parents[1], test_source, symlinks=True)
        run_logged(
            [tools["make"]["path"], f"-j{contract['execution']['jobs']}", "check"],
            test_source,
            environment,
            log,
        )
    install_environment = {**environment, "DESTDIR": str(Path(plan["stage_directory"]) / "root")}
    run_logged([cmake, "--install", str(build)], build.parent, install_environment, log)


def autotools_component(
    contract: dict[str, Any],
    plan: dict[str, Any],
    component: dict[str, Any],
    source: Path,
    build: Path,
    environment: dict[str, str],
    log: Path,
) -> None:
    make = plan["tools"]["make"]["path"]
    run_logged(
        [str(source / "configure"), f"--prefix={plan['final_prefix']}", *component["configure_arguments"]],
        build,
        environment,
        log,
    )
    run_logged([make, f"-j{contract['execution']['jobs']}"], build, environment, log)
    if component["test"] == "make-check":
        run_logged([make, f"-j{contract['execution']['jobs']}", "check"], build, environment, log)
    install_environment = {**environment, "DESTDIR": str(Path(plan["stage_directory"]) / "root")}
    run_logged([make, "install"], build, install_environment, log)


def openssl_component(
    contract: dict[str, Any],
    plan: dict[str, Any],
    component: dict[str, Any],
    source: Path,
    build: Path,
    environment: dict[str, str],
    log: Path,
) -> None:
    make = plan["tools"]["make"]["path"]
    perl = plan["tools"]["perl"]["path"]
    final = Path(plan["final_prefix"])
    run_logged(
        [
            perl,
            str(source / "Configure"),
            *component["configure_arguments"],
            f"--prefix={final}",
            f"--openssldir={final / 'etc/ssl'}",
            "--libdir=lib",
        ],
        build,
        environment,
        log,
    )
    run_logged([make, f"-j{contract['execution']['jobs']}"], build, environment, log)
    run_logged([make, f"-j{contract['execution']['jobs']}", "test"], build, environment, log)
    install_environment = {**environment, "DESTDIR": str(Path(plan["stage_directory"]) / "root")}
    run_logged([make, "install_sw", "install_ssldirs"], build, install_environment, log)


def source_copy_make_component(
    contract: dict[str, Any],
    plan: dict[str, Any],
    component: dict[str, Any],
    source: Path,
    build: Path,
    environment: dict[str, str],
    log: Path,
) -> None:
    make = plan["tools"]["make"]["path"]
    copied = build / "source"
    shutil.copytree(source, copied, symlinks=True)
    run_logged(
        [str(copied / "configure"), f"--prefix={plan['final_prefix']}", *component["configure_arguments"]],
        copied,
        environment,
        log,
    )
    run_logged([make, f"-j{contract['execution']['jobs']}", "all"], copied, environment, log)
    if component["test"] == "make-runtests":
        run_logged([make, "runtests"], copied, environment, log)
    install_environment = {**environment, "DESTDIR": str(Path(plan["stage_directory"]) / "root")}
    run_logged([make, "install"], copied, install_environment, log)


def elf_metadata(path: Path, readelf: Path) -> dict[str, Any] | None:
    with path.open("rb") as source:
        if source.read(4) != b"\x7fELF":
            return None
    outputs: dict[str, str] = {}
    for name, arguments in (
        ("header", ("-hW",)),
        ("program_headers", ("-lW",)),
        ("dynamic", ("-dW",)),
        ("relocations", ("-rW",)),
    ):
        process = subprocess.run(
            [str(readelf), *arguments, str(path)], text=True, capture_output=True
        )
        if process.returncode != 0:
            raise GraphError(f"readelf {name} failed for {path}: {process.stderr.strip()}")
        outputs[name] = process.stdout
    elf_type_match = ELF_TYPE_PATTERN.search(outputs["header"])
    if elf_type_match is None:
        raise GraphError(f"readelf did not report an ELF type for {path}")
    elf_type = elf_type_match.group(1)
    interpreter_match = INTERPRETER_PATTERN.search(outputs["program_headers"])
    interpreter = interpreter_match.group(1) if interpreter_match else None
    executable = interpreter is not None or elf_type == "EXEC"
    copy_relocations = COPY_RELOCATION_PATTERN.findall(outputs["relocations"])
    if executable and elf_type != "DYN":
        raise GraphError(f"packaged ELF executable is not PIE: {path}")
    if executable and copy_relocations:
        raise GraphError(f"packaged ELF executable contains COPY relocations: {path}")
    runpaths: list[str] = []
    for match in RUNPATH_PATTERN.finditer(outputs["dynamic"]):
        runpaths.extend(item for item in match.group(1).split(":") if item)
    return {
        "type": elf_type,
        "executable": executable,
        "interpreter": interpreter,
        "copy_relocation_count": len(copy_relocations),
        "copy_relocation_types": sorted(set(copy_relocations)),
        "needed": sorted(set(NEEDED_PATTERN.findall(outputs["dynamic"]))),
        "runpaths": sorted(set(runpaths)),
    }


def package_receipt(contract: dict[str, Any], plan: dict[str, Any]) -> dict[str, Any]:
    prefix = Path(plan["staged_prefix"])
    if not prefix.is_dir():
        raise GraphError("staged runtime prefix is missing")
    digest = hashlib.sha256()
    files: list[dict[str, Any]] = []
    total_bytes = 0
    for path in sorted(prefix.rglob("*"), key=lambda item: item.relative_to(prefix).as_posix()):
        relative = path.relative_to(prefix).as_posix()
        mode = stat.S_IMODE(path.lstat().st_mode)
        if path.is_symlink():
            kind = "symlink"
            content = os.readlink(path)
            size = 0
            file_sha = None
            elf = None
        elif path.is_file():
            kind = "file"
            size = path.stat().st_size
            total_bytes += size
            file_sha = sha256_file(path)
            content = file_sha
            elf = elf_metadata(path, Path(plan["tools"]["readelf"]["path"]))
            if elf is not None:
                permitted_runpath = contract["execution"]["install_runpath"]
                invalid_runpaths = [
                    runpath
                    for runpath in elf["runpaths"]
                    if runpath != permitted_runpath or Path(runpath).is_absolute()
                ]
                if invalid_runpaths:
                    raise GraphError(
                        f"packaged ELF has a non-package-relative RUNPATH: {path}: "
                        + ", ".join(invalid_runpaths)
                    )
        elif path.is_dir():
            kind = "directory"
            content = ""
            size = 0
            file_sha = None
            elf = None
        else:
            raise GraphError(f"unsupported package object: {path}")
        record = {
            "path": relative,
            "kind": kind,
            "mode": f"{mode:04o}",
            "size": size,
            "sha256": file_sha,
            "target": content if kind == "symlink" else None,
            "elf": elf,
        }
        encoded = json.dumps(record, sort_keys=True, separators=(",", ":")).encode("utf-8")
        digest.update(len(encoded).to_bytes(8, "big"))
        digest.update(encoded)
        files.append(record)
    return {
        "schema": RECEIPT_SCHEMA,
        "build_input_id": plan["build_input_id"],
        "final_prefix": plan["final_prefix"],
        "staged_prefix": plan["staged_prefix"],
        "tree_sha256": digest.hexdigest(),
        "file_count": sum(1 for item in files if item["kind"] == "file"),
        "total_file_bytes": total_bytes,
        "files": files,
        "build_input_closure_complete": False,
        "static_link_closure_verified": False,
        "recursive_runtime_closure_verified": False,
        "activation_eligible": False,
        "activation_disposition": "blocked by declared incomplete toolchain, static-link, and recursive runtime closure",
    }


def checkpoint_path(build_root: Path, component_id: str) -> Path:
    return build_root / "components" / component_id / "checkpoint.json"


def checkpoint_identity(checkpoint: dict[str, Any]) -> str:
    payload = dict(checkpoint)
    payload.pop("checkpoint_sha256", None)
    return canonical_sha256(payload)


def write_component_checkpoint(
    plan: dict[str, Any],
    build_root: Path,
    staged_prefix: Path,
    component: dict[str, Any],
    component_index: int,
    previous_checkpoint_sha256: str | None,
) -> dict[str, Any]:
    component_root = build_root / "components" / component["id"]
    log = component_root / "build.log"
    if not log.is_file():
        raise GraphError(f"component build log is missing: {component['id']}")
    checkpoint = {
        "schema": CHECKPOINT_SCHEMA,
        "build_input_id": plan["build_input_id"],
        "plan_sha256": canonical_sha256(plan),
        "component_id": component["id"],
        "component_index": component_index,
        "component_contract_sha256": canonical_sha256(component),
        "previous_checkpoint_sha256": previous_checkpoint_sha256,
        "build_log_sha256": sha256_file(log),
        "stage_tree": tree_state(staged_prefix),
    }
    if checkpoint["stage_tree"]["exists"] is not True:
        raise GraphError(f"component installed no staged package tree: {component['id']}")
    checkpoint["checkpoint_sha256"] = checkpoint_identity(checkpoint)
    write_json_atomic(checkpoint_path(build_root, component["id"]), checkpoint)
    return checkpoint


def validate_component_checkpoint(
    checkpoint: dict[str, Any],
    plan: dict[str, Any],
    build_root: Path,
    component: dict[str, Any],
    component_index: int,
    previous_checkpoint_sha256: str | None,
) -> None:
    identifier = component["id"]
    if checkpoint.get("schema") != CHECKPOINT_SCHEMA:
        raise GraphError(f"component checkpoint schema mismatch: {identifier}")
    expected_fields = {
        "build_input_id": plan["build_input_id"],
        "plan_sha256": canonical_sha256(plan),
        "component_id": identifier,
        "component_index": component_index,
        "component_contract_sha256": canonical_sha256(component),
        "previous_checkpoint_sha256": previous_checkpoint_sha256,
    }
    for field, expected in expected_fields.items():
        if checkpoint.get(field) != expected:
            raise GraphError(f"component checkpoint {field} mismatch: {identifier}")
    if checkpoint.get("checkpoint_sha256") != checkpoint_identity(checkpoint):
        raise GraphError(f"component checkpoint digest mismatch: {identifier}")
    stage = checkpoint.get("stage_tree")
    if not isinstance(stage, dict) or stage.get("schema") != TREE_SCHEMA:
        raise GraphError(f"component checkpoint stage tree is invalid: {identifier}")
    log = build_root / "components" / identifier / "build.log"
    if not log.is_file() or checkpoint.get("build_log_sha256") != sha256_file(log):
        raise GraphError(f"component build log differs from checkpoint: {identifier}")


def completed_component_checkpoints(
    plan: dict[str, Any], build_root: Path, staged_prefix: Path
) -> list[dict[str, Any]]:
    completed: list[dict[str, Any]] = []
    missing_seen = False
    previous: str | None = None
    for index, component in enumerate(plan["components"]):
        path = checkpoint_path(build_root, component["id"])
        if path.is_symlink():
            raise GraphError(f"component checkpoint must not be a symlink: {component['id']}")
        if not path.exists():
            missing_seen = True
            continue
        if not path.is_file():
            raise GraphError(f"component checkpoint is not a physical file: {component['id']}")
        if missing_seen:
            raise GraphError(
                f"component checkpoints are not contiguous at {component['id']}"
            )
        checkpoint = read_json(path)
        validate_component_checkpoint(
            checkpoint, plan, build_root, component, index, previous
        )
        completed.append(checkpoint)
        previous = checkpoint["checkpoint_sha256"]
    expected_stage = (
        completed[-1]["stage_tree"]
        if completed
        else {
            "schema": TREE_SCHEMA,
            "exists": False,
            "tree_sha256": canonical_sha256([]),
            "entry_count": 0,
            "file_count": 0,
            "total_file_bytes": 0,
        }
    )
    if tree_state(staged_prefix) != expected_stage:
        raise GraphError("staged package tree differs from the last durable checkpoint")
    return completed


def quarantine_interrupted_component(build_root: Path, component_id: str) -> None:
    component_root = build_root / "components" / component_id
    if not component_root.exists() and not component_root.is_symlink():
        return
    if component_root.is_symlink() or not component_root.is_dir():
        raise GraphError(f"interrupted component root is not a physical directory: {component_id}")
    interrupted = build_root / "interrupted-components"
    private_directory(interrupted)
    suffix = tree_state(component_root)["tree_sha256"][:16]
    destination = interrupted / f"{component_id}-{suffix}"
    if destination.exists():
        raise GraphError(f"interrupted component evidence already exists: {destination}")
    component_root.rename(destination)


def execute(
    contract: dict[str, Any],
    plan: dict[str, Any],
    repository: Path,
    resume: bool = False,
) -> dict[str, Any]:
    build_root = Path(plan["build_directory"])
    stage_root = Path(plan["stage_directory"])
    final_prefix = Path(plan["final_prefix"])
    staged_prefix = Path(plan["staged_prefix"])
    if final_prefix.exists() or final_prefix.is_symlink():
        raise GraphError("final package destination must not already exist")
    if resume:
        for name, path in (("build", build_root), ("stage", stage_root)):
            if (
                not path.is_dir()
                or path.is_symlink()
                or stat.S_IMODE(path.stat().st_mode) != 0o700
            ):
                raise GraphError(
                    f"resumed {name} directory is missing, linked, or not mode 0700"
                )
        stored_plan = read_json(build_root / "build-plan.json")
        if stored_plan != plan:
            raise GraphError("resumed build plan differs from the exact current plan")
        completed = completed_component_checkpoints(plan, build_root, staged_prefix)
        private_sources = verify_private_sources(
            contract, plan, repository, build_root
        )
        completed_count = len(completed)
        for component in plan["components"][completed_count + 1 :]:
            future_root = build_root / "components" / component["id"]
            if future_root.exists() or future_root.is_symlink():
                raise GraphError(
                    f"future component state exists beyond the resume frontier: {component['id']}"
                )
        if completed_count < len(plan["components"]):
            quarantine_interrupted_component(
                build_root, plan["components"][completed_count]["id"]
            )
    else:
        if build_root.exists() or stage_root.exists():
            raise GraphError("build and stage destinations must not already exist")
        private_directory(build_root)
        private_directory(stage_root)
        write_json_atomic(build_root / "build-plan.json", plan)
        private_sources = prepare_private_sources(
            contract, plan, repository, build_root
        )
        completed = []
    environment = build_environment(contract, plan, build_root / ".home")
    previous_checkpoint_sha256 = (
        completed[-1]["checkpoint_sha256"] if completed else None
    )
    completed_count = len(completed)
    for component_index, component in enumerate(
        plan["components"][completed_count:], start=completed_count
    ):
        identifier = component["id"]
        source = private_sources / component["source"] / component["source_subdirectory"]
        if not source.is_dir():
            raise GraphError(f"component source is missing: {source}")
        component_root = build_root / "components" / identifier
        private_directory(component_root)
        build = component_root / "build"
        build.mkdir()
        log = component_root / "build.log"
        provider = component["provider"]
        if provider == "cmake":
            cmake_component(contract, plan, component, source, build, environment, log)
        elif provider == "autotools":
            autotools_component(contract, plan, component, source, build, environment, log)
        elif provider == "openssl":
            openssl_component(contract, plan, component, source, build, environment, log)
        elif provider == "source-copy-make":
            source_copy_make_component(contract, plan, component, source, build, environment, log)
        else:
            raise GraphError(f"unsupported provider: {provider}")
        checkpoint = write_component_checkpoint(
            plan,
            build_root,
            staged_prefix,
            component,
            component_index,
            previous_checkpoint_sha256,
        )
        previous_checkpoint_sha256 = checkpoint["checkpoint_sha256"]
    verify_release_generation(
        repository,
        (repository / contract["release_lock"]).resolve(),
        Path(plan["archive_root"]),
        Path(plan["source_generation"]),
    )
    receipt = package_receipt(contract, plan)
    receipt["plan_sha256"] = canonical_sha256(plan)
    receipt["component_logs"] = {
        component["id"]: sha256_file(
            build_root / "components" / component["id"] / "build.log"
        )
        for component in plan["components"]
    }
    receipt["component_checkpoints"] = {
        component["id"]: read_json(checkpoint_path(build_root, component["id"]))[
            "checkpoint_sha256"
        ]
        for component in plan["components"]
    }
    write_json_atomic(build_root / "package-receipt.json", receipt)
    return receipt


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=".")
    parser.add_argument("--contract", default="contracts/postgresql-runtime-build.json")
    parser.add_argument("--archive-root", required=True)
    parser.add_argument("--source-generation", required=True)
    parser.add_argument("--toolchain-receipt")
    parser.add_argument("command", choices=("validate", "plan", "build", "resume"))
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_arguments(argv)
    repository = Path(arguments.repository).resolve()
    contract_path = Path(arguments.contract)
    if not contract_path.is_absolute():
        contract_path = repository / contract_path
    contract = read_json(contract_path)
    validate_contract(contract)
    if arguments.command == "validate":
        verify_compilers(contract)
        print(json.dumps({"schema": SCHEMA, "status": "valid"}, sort_keys=True))
        return 0
    if not arguments.toolchain_receipt:
        raise GraphError("plan, build, and resume require --toolchain-receipt")
    validate_environment(contract, os.environ)
    plan = create_plan(
        contract,
        repository,
        Path(arguments.archive_root),
        Path(arguments.source_generation),
        Path(arguments.toolchain_receipt),
    )
    if arguments.command == "plan":
        print(json.dumps(plan, indent=2, sort_keys=True))
        return 0
    receipt = execute(
        contract, plan, repository, resume=arguments.command == "resume"
    )
    print(json.dumps({"plan": plan, "package": receipt}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (GraphError, subprocess.CalledProcessError) as error:
        print(f"postgresql-runtime-build: {error}", file=sys.stderr)
        raise SystemExit(1) from error
