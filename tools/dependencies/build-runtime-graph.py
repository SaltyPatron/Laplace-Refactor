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

DEPENDENCY_TOOLS = Path(__file__).resolve().parent
if str(DEPENDENCY_TOOLS) not in sys.path:
    sys.path.insert(0, str(DEPENDENCY_TOOLS))

from package_receipts import (  # noqa: E402
    ReceiptError,
    verify_toolchain_package_receipt,
)


SCHEMA = "laplace.postgresql-runtime-build/v2"
PLAN_SCHEMA = "laplace.postgresql-runtime-plan/v2"
RECEIPT_SCHEMA = "laplace.postgresql-runtime-package/v2"
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
TEST_PACKAGE_GATES = {"required-pass", "record-exact-outcome-and-continue"}
TEST_ACTIVATION_GATES = {
    "component-test-pass",
    "separate-selected-runtime-provider-qualification",
}
PROVIDER_DIMENSIONS = {
    "kernel_sysname",
    "kernel_release",
    "kernel_version",
    "machine",
    "io_uring_disabled",
}
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


def component_test_policy(component: Mapping[str, Any]) -> dict[str, Any]:
    declared = component.get("test_policy")
    if declared is None:
        return {
            "scope": "upstream-component-test",
            "package_gate": "required-pass",
            "product_activation_gate": "component-test-pass",
            "provider_dimensions": [],
            "source_evidence": None,
        }
    if not isinstance(declared, dict):
        raise GraphError(f"{component.get('id', 'component')}.test_policy must be an object")
    return dict(declared)


def validate_component_test_policy(component: Mapping[str, Any]) -> None:
    identifier = require_string(component.get("id"), "component.id")
    policy = component_test_policy(component)
    require_string(policy.get("scope"), f"{identifier}.test_policy.scope")
    package_gate = require_string(
        policy.get("package_gate"), f"{identifier}.test_policy.package_gate"
    )
    if package_gate not in TEST_PACKAGE_GATES:
        raise GraphError(f"unsupported package test gate for {identifier}: {package_gate}")
    activation_gate = require_string(
        policy.get("product_activation_gate"),
        f"{identifier}.test_policy.product_activation_gate",
    )
    if activation_gate not in TEST_ACTIVATION_GATES:
        raise GraphError(
            f"unsupported activation test gate for {identifier}: {activation_gate}"
        )
    dimensions = require_string_list(
        policy.get("provider_dimensions", []),
        f"{identifier}.test_policy.provider_dimensions",
        allow_empty=True,
    )
    unknown_dimensions = sorted(set(dimensions) - PROVIDER_DIMENSIONS)
    if unknown_dimensions:
        raise GraphError(
            f"unsupported runtime-provider dimension for {identifier}: "
            + ", ".join(unknown_dimensions)
        )
    if package_gate == "required-pass":
        if activation_gate != "component-test-pass" or dimensions:
            raise GraphError(
                f"required-pass test policy cannot defer provider qualification: {identifier}"
            )
        return
    if activation_gate != "separate-selected-runtime-provider-qualification":
        raise GraphError(
            f"recorded host-coupled test requires separate provider qualification: {identifier}"
        )
    if not dimensions:
        raise GraphError(
            f"recorded host-coupled test must identify provider dimensions: {identifier}"
        )
    evidence = policy.get("source_evidence")
    if not isinstance(evidence, dict):
        raise GraphError(f"recorded test policy requires source evidence: {identifier}")
    evidence_path = require_string(
        evidence.get("path"), f"{identifier}.test_policy.source_evidence.path"
    )
    if Path(evidence_path).is_absolute() or ".." in Path(evidence_path).parts:
        raise GraphError(f"test-policy source evidence path is unsafe: {identifier}")
    digest = require_string(
        evidence.get("sha256"), f"{identifier}.test_policy.source_evidence.sha256"
    )
    if re.fullmatch(r"[0-9a-f]{64}", digest) is None:
        raise GraphError(f"test-policy source evidence digest is invalid: {identifier}")
    require_string(
        evidence.get("meaning"), f"{identifier}.test_policy.source_evidence.meaning"
    )


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
    if execution.get("make_runpath_encoding") != (
        "backslash-escaped-dollar-through-make-and-shell"
    ):
        raise GraphError("execution.make_runpath_encoding is invalid")
    if execution.get("autotools_libtool_runpath_policy") != (
        "disable-generated-hardcoding-and-runpath-variable"
    ):
        raise GraphError("execution.autotools_libtool_runpath_policy is invalid")
    source_path_policy = execution.get("source_path_policy")
    if not isinstance(source_path_policy, dict):
        raise GraphError("execution.source_path_policy must be an object")
    if source_path_policy.get("build_root_mapping") != ".":
        raise GraphError("runtime build roots must map to a stable relative compiler path")
    if source_path_policy.get("absolute_build_root_in_file_macro") != "forbidden":
        raise GraphError("absolute runtime build roots in __FILE__ must be forbidden")
    if source_path_policy.get("absolute_build_root_in_debug_info") != "forbidden":
        raise GraphError("absolute runtime build roots in debug information must be forbidden")
    for field in ("install_prefix", "build_root", "stage_root"):
        if not Path(require_string(execution.get(field), f"execution.{field}")).is_absolute():
            raise GraphError(f"execution.{field} must be absolute")
    if execution["install_prefix"] != "/opt/laplace/current":
        raise GraphError("runtime install_prefix must equal the product activation prefix")
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
        required_source_paths = require_string_list(
            component.get("required_source_paths"),
            f"{identifier}.required_source_paths",
        )
        for required_path in required_source_paths:
            relative = Path(required_path)
            if (
                relative.is_absolute()
                or ".." in relative.parts
                or relative.as_posix() != required_path
            ):
                raise GraphError(
                    f"{identifier}.required_source_paths contains an unsafe path: "
                    f"{required_path}"
                )
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
        validate_component_test_policy(component)

    qualification = contract.get("runtime_provider_qualification")
    if not isinstance(qualification, dict):
        raise GraphError("runtime_provider_qualification must be an object")
    if qualification.get("receipt_schema") != "laplace.runtime-provider-qualification/v1":
        raise GraphError("runtime-provider qualification receipt schema is invalid")
    if qualification.get("required_before_product_activation") is not True:
        raise GraphError("runtime-provider qualification must gate product activation")
    qualified_components = require_string_list(
        qualification.get("components"), "runtime_provider_qualification.components"
    )
    if not set(qualified_components).issubset(identifiers):
        raise GraphError("runtime-provider qualification names an unknown component")
    policy_components = [
        component["id"]
        for component in components
        if component_test_policy(component)["product_activation_gate"]
        == "separate-selected-runtime-provider-qualification"
    ]
    if qualified_components != policy_components:
        raise GraphError(
            "runtime-provider qualification components differ from component test policies"
        )
    require_string(qualification.get("policy"), "runtime_provider_qualification.policy")


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


def verify_toolchain_receipt(
    contract: dict[str, Any], receipt_path: Path
) -> dict[str, Any]:
    try:
        return verify_toolchain_package_receipt(contract["build_toolchain"], receipt_path)
    except ReceiptError as error:
        raise GraphError(str(error)) from error


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


def verify_component_source_requirements(
    component: Mapping[str, Any], source_root: Path
) -> list[dict[str, Any]]:
    """Verify the build and test entrypoints selected from the locked source tree."""

    evidence: list[dict[str, Any]] = []
    for relative_text in component["required_source_paths"]:
        path = source_root / relative_text
        if not path.is_file() or path.is_symlink():
            raise GraphError(
                f"required component source path is missing: "
                f"{component['id']}:{relative_text}"
            )
        evidence.append(
            {
                "path": relative_text,
                "sha256": sha256_file(path),
                "size": path.stat().st_size,
            }
        )
    return evidence


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
    install_prefix = ensure_external(
        Path(execution["install_prefix"]), repository, "install prefix"
    )
    build_directory = ensure_external(
        Path(execution["build_root"]) / build_id, repository, "build directory"
    )
    stage_directory = ensure_external(
        Path(execution["stage_root"]) / build_id, repository, "stage directory"
    )
    staged_prefix = stage_directory / "root" / install_prefix.relative_to("/")
    return {
        "schema": PLAN_SCHEMA,
        "build_input_id": build_id,
        "identity": identity,
        "source_generation": str(source_generation.resolve()),
        "archive_root": str(archive_root.resolve()),
        "build_directory": str(build_directory),
        "stage_directory": str(stage_directory),
        "install_prefix": str(install_prefix),
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
    build_root = Path(plan["build_directory"])
    mapped_root = flags["source_path_policy"]["build_root_mapping"]
    source_path_flags = [
        f"-ffile-prefix-map={build_root}={mapped_root}",
        f"-fdebug-prefix-map={build_root}={mapped_root}",
    ]
    return {
        "HOME": str(home),
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        "SOURCE_DATE_EPOCH": contract["execution"]["source_date_epoch"],
        "PATH": ":".join([*tool_directories, "/usr/bin", "/bin"]),
        "CC": compilers["c_compiler"]["path"],
        "CXX": compilers["cxx_compiler"]["path"],
        "CFLAGS": " ".join(
            [
                f"-B{Path(plan['toolchain_prefix']) / 'bin'}",
                *flags["c_flags"],
                *source_path_flags,
            ]
        ),
        "CXXFLAGS": " ".join(
            [
                f"-B{Path(plan['toolchain_prefix']) / 'bin'}",
                *flags["cxx_flags"],
                *source_path_flags,
            ]
        ),
        "CPPFLAGS": f"-I{staged / 'include'}",
        "LDFLAGS": " ".join(
            [
                f"-L{staged / 'lib'}",
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


def provider_environment(
    contract: dict[str, Any], environment: Mapping[str, str], provider: str
) -> dict[str, str]:
    """Encode canonical link policy for the selected build recipe.

    CMake receives RUNPATH through its typed cache argument.  Make-backed
    providers use a backslash-escaped, doubled dollar.  Make reduces ``$$`` to
    one dollar and the remaining backslash protects that dollar from both a
    direct recipe shell and a generator command that embeds the flags in a
    double-quoted value (ICU's pkgdata interface is one such command).
    """
    result = dict(environment)
    if provider == "cmake":
        return result
    if provider not in {"autotools", "openssl", "source-copy-make"}:
        raise GraphError(f"unsupported provider environment: {provider}")
    canonical = contract["execution"]["install_runpath"]
    make_encoded = canonical.replace("$", r"\$$")
    result["LDFLAGS"] = " ".join(
        [result["LDFLAGS"], f"-Wl,-rpath,{make_encoded}"]
    )
    return result


def constrain_generated_libtool_runpath(build: Path, log: Path) -> dict[str, Any]:
    """Make the common package RUNPATH the only libtool runtime-search law.

    GNU libtool normally adds the configured logical installation directory to
    executables linked with an uninstalled libtool library.  That behavior is
    correct for conventional prefix installation but would add an absolute
    ``/opt/laplace/current/lib`` entry beside the package-relative RUNPATH.
    The common provider already supplies the canonical RUNPATH through
    ``LDFLAGS``.  Disable both generated libtool fallback mechanisms after
    configure and receipt the exact generated-interface transition in the
    component build log.
    """

    libtool = build / "libtool"
    observation: dict[str, Any] = {
        "schema": "laplace.autotools-libtool-runpath-normalization/v1",
        "path": "libtool",
        "present": libtool.exists(),
        "applied": False,
        "before_sha256": None,
        "after_sha256": None,
    }
    if libtool.exists() or libtool.is_symlink():
        if not libtool.is_file() or libtool.is_symlink():
            raise GraphError("generated autotools libtool must be a physical file")
        original = libtool.read_text(encoding="utf-8")
        normalized = original
        replacements = (
            (r"(?m)^runpath_var=.*$", 'runpath_var=""', "runpath_var"),
            (
                r"(?m)^hardcode_libdir_flag_spec=.*$",
                'hardcode_libdir_flag_spec=""',
                "hardcode_libdir_flag_spec",
            ),
        )
        for pattern, replacement, field in replacements:
            normalized, count = re.subn(pattern, replacement, normalized)
            if count != 1:
                raise GraphError(
                    f"generated autotools libtool has {count} {field} assignments"
                )
        before = hashlib.sha256(original.encode("utf-8")).hexdigest()
        after = hashlib.sha256(normalized.encode("utf-8")).hexdigest()
        libtool.write_text(normalized, encoding="utf-8")
        observation.update(
            {
                "applied": True,
                "before_sha256": before,
                "after_sha256": after,
            }
        )
    with log.open("a", encoding="utf-8") as output:
        output.write(json.dumps(observation, sort_keys=True, separators=(",", ":")))
        output.write("\n")
    return observation


def run_logged(
    command: Sequence[str],
    cwd: Path,
    environment: Mapping[str, str],
    log: Path,
    *,
    require_success: bool = True,
) -> int:
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
        output.write(f"# laplace-process-return-code: {code}\n")
        if code >= 0:
            output.write(f"# laplace-exit-code: {code}\n")
        else:
            output.write(f"# laplace-signal: {-code}\n")
        output.flush()
        if code != 0 and require_success:
            raise GraphError(f"command exited {code}: {command[0]}")
        return code


def runtime_provider_observation(dimensions: Sequence[str]) -> dict[str, Any]:
    uname = os.uname()
    available: dict[str, Any] = {
        "kernel_sysname": uname.sysname,
        "kernel_release": uname.release,
        "kernel_version": uname.version,
        "machine": uname.machine,
    }
    if "io_uring_disabled" in dimensions:
        path = Path("/proc/sys/kernel/io_uring_disabled")
        try:
            available["io_uring_disabled"] = int(path.read_text(encoding="utf-8").strip())
        except (OSError, ValueError) as error:
            raise GraphError(f"cannot observe io_uring_disabled: {error}") from error
    return {dimension: available[dimension] for dimension in dimensions}


def execute_component_test(
    component: Mapping[str, Any],
    command: Sequence[str],
    cwd: Path,
    environment: Mapping[str, str],
    log: Path,
    source: Path,
) -> dict[str, Any]:
    policy = component_test_policy(component)
    package_gate = policy["package_gate"]
    evidence_record: dict[str, Any] | None = None
    evidence = policy.get("source_evidence")
    if isinstance(evidence, dict):
        evidence_path = source / evidence["path"]
        if not evidence_path.is_file() or evidence_path.is_symlink():
            raise GraphError(
                f"test-policy source evidence is missing: {component['id']}"
            )
        observed_digest = sha256_file(evidence_path)
        if observed_digest != evidence["sha256"]:
            raise GraphError(
                f"test-policy source evidence differs: {component['id']}"
            )
        evidence_record = {
            "path": evidence["path"],
            "sha256": observed_digest,
            "meaning": evidence["meaning"],
        }
    code = run_logged(
        command,
        cwd,
        environment,
        log,
        require_success=package_gate == "required-pass",
    )
    if code == 0:
        disposition = "passed"
        exit_code: int | None = 0
        signal: int | None = None
    elif code > 0:
        disposition = "failed-under-observed-runtime-provider"
        exit_code = code
        signal = None
    else:
        disposition = "terminated-by-signal-under-observed-runtime-provider"
        exit_code = None
        signal = -code
    return {
        "scope": policy["scope"],
        "command": list(command),
        "process_return_code": code,
        "exit_code": exit_code,
        "signal": signal,
        "disposition": disposition,
        "package_gate": package_gate,
        "product_activation_gate": policy["product_activation_gate"],
        "provider_observation": runtime_provider_observation(
            policy.get("provider_dimensions", [])
        ),
        "source_evidence": evidence_record,
    }


def validate_recorded_test_execution(
    component: Mapping[str, Any], execution: object
) -> None:
    identifier = component["id"]
    if not isinstance(execution, dict):
        raise GraphError(f"component test execution is invalid: {identifier}")
    policy = component_test_policy(component)
    for field in ("scope", "package_gate", "product_activation_gate"):
        if execution.get(field) != policy[field]:
            raise GraphError(f"component test {field} mismatch: {identifier}")
    command = execution.get("command")
    if not isinstance(command, list) or not command or any(
        not isinstance(item, str) or not item for item in command
    ):
        raise GraphError(f"component test command is invalid: {identifier}")
    return_code = execution.get("process_return_code")
    if not isinstance(return_code, int):
        raise GraphError(f"component test return code is invalid: {identifier}")
    expected_exit_code = return_code if return_code >= 0 else None
    expected_signal = -return_code if return_code < 0 else None
    if execution.get("exit_code") != expected_exit_code:
        raise GraphError(f"component test exit code mismatch: {identifier}")
    if execution.get("signal") != expected_signal:
        raise GraphError(f"component test signal mismatch: {identifier}")
    expected_disposition = "passed"
    if return_code > 0:
        expected_disposition = "failed-under-observed-runtime-provider"
    elif return_code < 0:
        expected_disposition = "terminated-by-signal-under-observed-runtime-provider"
    if execution.get("disposition") != expected_disposition:
        raise GraphError(f"component test disposition mismatch: {identifier}")
    if policy["package_gate"] == "required-pass" and return_code != 0:
        raise GraphError(f"required component test did not pass: {identifier}")
    provider = execution.get("provider_observation")
    dimensions = policy.get("provider_dimensions", [])
    if not isinstance(provider, dict) or list(provider) != dimensions:
        raise GraphError(f"component runtime-provider observation mismatch: {identifier}")
    evidence = policy.get("source_evidence")
    if evidence is None:
        if execution.get("source_evidence") is not None:
            raise GraphError(f"unexpected component source evidence: {identifier}")
    elif execution.get("source_evidence") != evidence:
        raise GraphError(f"component source evidence mismatch: {identifier}")


def cmake_component(
    contract: dict[str, Any],
    plan: dict[str, Any],
    component: dict[str, Any],
    source: Path,
    build: Path,
    environment: dict[str, str],
    log: Path,
) -> dict[str, Any]:
    tools = plan["tools"]
    compilers = contract["compiler"]
    final = Path(plan["install_prefix"])
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
        test_execution = execute_component_test(
            component,
            [tools["ctest"]["path"], "--test-dir", str(build), "--output-on-failure"],
            build.parent,
            environment,
            log,
            source,
        )
    elif component["test"] == "source-copy-make-check":
        test_source = build.parent / "test-source"
        shutil.copytree(source.parents[1], test_source, symlinks=True)
        test_execution = execute_component_test(
            component,
            [tools["make"]["path"], f"-j{contract['execution']['jobs']}", "check"],
            test_source,
            environment,
            log,
            source,
        )
    install_environment = {**environment, "DESTDIR": str(Path(plan["stage_directory"]) / "root")}
    run_logged([cmake, "--install", str(build)], build.parent, install_environment, log)
    return test_execution


def autotools_component(
    contract: dict[str, Any],
    plan: dict[str, Any],
    component: dict[str, Any],
    source: Path,
    build: Path,
    environment: dict[str, str],
    log: Path,
) -> dict[str, Any]:
    make = plan["tools"]["make"]["path"]
    run_logged(
        [str(source / "configure"), f"--prefix={plan['install_prefix']}", *component["configure_arguments"]],
        build,
        environment,
        log,
    )
    constrain_generated_libtool_runpath(build, log)
    run_logged([make, f"-j{contract['execution']['jobs']}"], build, environment, log)
    if component["test"] == "make-check":
        test_execution = execute_component_test(
            component,
            [make, f"-j{contract['execution']['jobs']}", "check"],
            build,
            environment,
            log,
            source,
        )
    install_environment = {**environment, "DESTDIR": str(Path(plan["stage_directory"]) / "root")}
    run_logged([make, "install"], build, install_environment, log)
    return test_execution


def openssl_component(
    contract: dict[str, Any],
    plan: dict[str, Any],
    component: dict[str, Any],
    source: Path,
    build: Path,
    environment: dict[str, str],
    log: Path,
) -> dict[str, Any]:
    make = plan["tools"]["make"]["path"]
    perl = plan["tools"]["perl"]["path"]
    final = Path(plan["install_prefix"])
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
    test_execution = execute_component_test(
        component,
        [make, f"-j{contract['execution']['jobs']}", "test"],
        build,
        environment,
        log,
        source,
    )
    destination_root = Path(plan["stage_directory"]) / "root"
    install_environment = {**environment, "DESTDIR": str(destination_root)}
    run_logged(
        [make, "install_sw", "install_ssldirs", f"DESTDIR={destination_root}"],
        build,
        install_environment,
        log,
    )
    return test_execution


def source_copy_make_component(
    contract: dict[str, Any],
    plan: dict[str, Any],
    component: dict[str, Any],
    source: Path,
    build: Path,
    environment: dict[str, str],
    log: Path,
) -> dict[str, Any]:
    make = plan["tools"]["make"]["path"]
    copied = build / "source"
    shutil.copytree(source, copied, symlinks=True)
    run_logged(
        [str(copied / "configure"), f"--prefix={plan['install_prefix']}", *component["configure_arguments"]],
        copied,
        environment,
        log,
    )
    run_logged([make, f"-j{contract['execution']['jobs']}", "all"], copied, environment, log)
    if component["test"] == "make-runtests":
        test_execution = execute_component_test(
            component,
            [make, "runtests"],
            copied,
            environment,
            log,
            source,
        )
    install_environment = {**environment, "DESTDIR": str(Path(plan["stage_directory"]) / "root")}
    run_logged([make, "install"], copied, install_environment, log)
    return test_execution


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
        "install_prefix": plan["install_prefix"],
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
    test_execution: dict[str, Any],
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
        "test_execution": test_execution,
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
    validate_recorded_test_execution(component, checkpoint.get("test_execution"))
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
    staged_prefix = Path(plan["staged_prefix"])
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
        source_root = private_sources / component["source"]
        verify_component_source_requirements(component, source_root)
        source = source_root / component["source_subdirectory"]
        if not source.is_dir():
            raise GraphError(f"component source is missing: {source}")
        component_root = build_root / "components" / identifier
        private_directory(component_root)
        build = component_root / "build"
        build.mkdir()
        log = component_root / "build.log"
        provider = component["provider"]
        component_environment = provider_environment(
            contract, environment, provider
        )
        if provider == "cmake":
            test_execution = cmake_component(
                contract, plan, component, source, build, component_environment, log
            )
        elif provider == "autotools":
            test_execution = autotools_component(
                contract, plan, component, source, build, component_environment, log
            )
        elif provider == "openssl":
            test_execution = openssl_component(
                contract, plan, component, source, build, component_environment, log
            )
        elif provider == "source-copy-make":
            test_execution = source_copy_make_component(
                contract, plan, component, source, build, component_environment, log
            )
        else:
            raise GraphError(f"unsupported provider: {provider}")
        checkpoint = write_component_checkpoint(
            plan,
            build_root,
            staged_prefix,
            component,
            component_index,
            previous_checkpoint_sha256,
            test_execution,
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
    checkpoint_documents = {
        component["id"]: read_json(checkpoint_path(build_root, component["id"]))
        for component in plan["components"]
    }
    receipt["component_logs"] = {
        component["id"]: sha256_file(
            build_root / "components" / component["id"] / "build.log"
        )
        for component in plan["components"]
    }
    receipt["component_checkpoints"] = {
        component["id"]: checkpoint_documents[component["id"]]["checkpoint_sha256"]
        for component in plan["components"]
    }
    receipt["component_test_executions"] = {
        component["id"]: checkpoint_documents[component["id"]]["test_execution"]
        for component in plan["components"]
    }
    qualification = contract["runtime_provider_qualification"]
    receipt["runtime_provider_qualification"] = {
        "schema": qualification["receipt_schema"],
        "complete": False,
        "required_before_product_activation": True,
        "required_components": qualification["components"],
        "requirements": {
            identifier: {
                "component_checkpoint_sha256": receipt["component_checkpoints"][identifier],
                "test_execution_sha256": canonical_sha256(
                    receipt["component_test_executions"][identifier]
                ),
                "observed_disposition": receipt["component_test_executions"][identifier][
                    "disposition"
                ],
            }
            for identifier in qualification["components"]
        },
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
