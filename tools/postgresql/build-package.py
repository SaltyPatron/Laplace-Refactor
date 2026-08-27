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

DEPENDENCY_TOOLS = Path(__file__).resolve().parents[1] / "dependencies"
if str(DEPENDENCY_TOOLS) not in sys.path:
    sys.path.insert(0, str(DEPENDENCY_TOOLS))

from package_receipts import (  # noqa: E402
    ReceiptError,
    verify_recorded_package_tree,
    verify_installed_provider_selection,
    verify_toolchain_package_receipt,
)

CONTRACT_SCHEMA = "laplace.postgresql-build-contract/v2"
PLAN_SCHEMA = "laplace.postgresql-build-plan/v2"
PACKAGE_SCHEMA = "laplace.postgresql-package-receipt/v2"
NEEDED_PATTERN = re.compile(r"Shared library: \[([^]]+)\]")
EXPECTED_PERL_MODULES = {
    "IO::Pty": "1.31",
    "IO::Tty": "1.31",
    "IPC::Run": "20260402.0",
}
EXPECTED_RUNTIME_BUILD_EXECUTABLES = {
    "openssl": {
        "relative_path": "bin/openssl",
        "version_line": "OpenSSL 4.0.1 9 Jun 2026 (Library: OpenSSL 4.0.1 9 Jun 2026)",
    }
}


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
    receipt_verifier = (repository / "tools/dependencies/package_receipts.py").resolve()
    closure_verifier = (
        repository / contract["runtime_closure"]["recursive_verifier"]["path"]
    ).resolve()
    for name, path in (
        ("build driver", driver),
        ("release verifier", verifier),
        ("package receipt verifier", receipt_verifier),
        ("recursive ELF closure verifier", closure_verifier),
    ):
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
        "package_receipt_verifier": {
            "path": "tools/dependencies/package_receipts.py",
            "sha256": sha256_file(receipt_verifier),
        },
        "recursive_elf_closure_verifier": {
            "path": contract["runtime_closure"]["recursive_verifier"]["path"],
            "sha256": sha256_file(closure_verifier),
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
    build_toolchain = document.get("build_toolchain")
    installed_runtime_provider = document.get("installed_runtime_provider")
    runtime_package = document.get("runtime_package")
    input_closure = document.get("input_closure")
    build = document.get("build")
    execution = document.get("execution")
    environment = document.get("environment")
    closure = document.get("runtime_closure")
    if not all(
        isinstance(section, dict)
        for section in (
            source,
            toolchain,
            build_toolchain,
            installed_runtime_provider,
            runtime_package,
            input_closure,
            build,
            execution,
            environment,
            closure,
        )
    ):
        raise BuildError(
            "contract source, toolchains, runtime package, build, execution, environment, and closure must be objects"
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
    if build_toolchain.get("receipt_schema") != "laplace.toolchain-package-receipt/v1":
        raise BuildError("build_toolchain.receipt_schema is invalid")
    if build_toolchain.get("consumer_manifest_schema") != (
        "laplace.toolchain-consumer-manifest/v1"
    ):
        raise BuildError("build_toolchain.consumer_manifest_schema is invalid")
    require_string_array(
        build_toolchain.get("required_tools"), "build_toolchain.required_tools"
    )
    if build_toolchain.get("required_perl_modules") != EXPECTED_PERL_MODULES:
        raise BuildError("build_toolchain.required_perl_modules must remain exact")
    required_linker_inputs = build_toolchain.get("required_linker_map_inputs")
    if not isinstance(required_linker_inputs, dict) or not required_linker_inputs:
        raise BuildError("build_toolchain.required_linker_map_inputs must be present")
    linker_classes: set[str] = set()
    for role, requirement in required_linker_inputs.items():
        if not isinstance(role, str) or not role or not isinstance(requirement, dict):
            raise BuildError("build toolchain linker-map requirement is invalid")
        path = Path(require_string(requirement.get("path"), f"linker input {role}.path"))
        if not path.is_absolute():
            raise BuildError(f"linker input path must be absolute: {role}")
        require_string(requirement.get("soname"), f"linker input {role}.soname")
        classification = require_string(
            requirement.get("classification"), f"linker input {role}.classification"
        )
        if classification not in {"platform-abi", "packaged-compiler-support"}:
            raise BuildError(f"linker input classification is invalid: {role}")
        linker_classes.add(classification)
        if classification == "packaged-compiler-support" and not requirement.get(
            "package_relative_path"
        ):
            raise BuildError(f"packaged linker input lacks a package path: {role}")
    if linker_classes != {"platform-abi", "packaged-compiler-support"}:
        raise BuildError("linker-map requirements must select platform and packaged support")
    for field in (
        "lock",
        "lock_schema",
        "provider_selection_sha256",
        "provider",
        "provider_sha256",
        "version",
    ):
        require_string(installed_runtime_provider.get(field), f"installed_runtime_provider.{field}")
    required_provider_files = installed_runtime_provider.get("required_files")
    if not isinstance(required_provider_files, dict) or not required_provider_files:
        raise BuildError("installed runtime provider required file selection is missing")
    if runtime_package.get("receipt_schema") != "laplace.postgresql-runtime-package/v2":
        raise BuildError("runtime_package.receipt_schema is invalid")
    if runtime_package.get("install_prefix") != "/opt/laplace/current":
        raise BuildError("runtime package must target the stable product activation prefix")
    required_runtime_components = require_string_array(
        runtime_package.get("required_components"), "runtime_package.required_components"
    )
    if runtime_package.get("provider_qualification_receipt_schema") != (
        "laplace.runtime-provider-qualification/v1"
    ):
        raise BuildError("runtime provider qualification receipt schema is invalid")
    required_qualifications = require_string_array(
        runtime_package.get("required_provider_qualifications"),
        "runtime_package.required_provider_qualifications",
    )
    if not set(required_qualifications).issubset(required_runtime_components):
        raise BuildError("runtime provider qualification names an unknown component")
    if (
        runtime_package.get("required_build_executables")
        != EXPECTED_RUNTIME_BUILD_EXECUTABLES
    ):
        raise BuildError("runtime package build executable selection must remain exact")
    if input_closure.get("status") != "incomplete":
        raise BuildError("input_closure.status must remain incomplete until every build input is selected")
    selected_inputs = require_string_array(
        input_closure.get("selected_exact_inputs"), "input_closure.selected_exact_inputs"
    )
    if "PostgreSQL bundled tzdata 2026c" not in selected_inputs:
        raise BuildError("input_closure must bind PostgreSQL bundled tzdata 2026c")
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
    if any(argument.startswith("--with-system-tzdata") for argument in configure):
        raise BuildError("PostgreSQL must use its selected bundled tzdata")
    flags = require_string_array(build.get("c_flags"), "build.c_flags")
    for required in ("-fno-fast-math", "-ffp-contract=off"):
        if required not in flags:
            raise BuildError(f"build.c_flags must contain {required}")
    cxx_flags = require_string_array(build.get("cxx_flags"), "build.cxx_flags")
    for required in ("-fno-fast-math", "-ffp-contract=off"):
        if required not in cxx_flags:
            raise BuildError(f"build.cxx_flags must contain {required}")
    require_string_array(build.get("linker_flags"), "build.linker_flags")
    if build.get("install_subdirectory") != "pgsql-18":
        raise BuildError("PostgreSQL install_subdirectory must be pgsql-18")
    if build.get("install_runpath") != "$ORIGIN:$ORIGIN/../lib:$ORIGIN/../../lib":
        raise BuildError("PostgreSQL install_runpath must resolve package-owned libraries")
    source_path_policy = build.get("source_path_policy")
    if not isinstance(source_path_policy, dict):
        raise BuildError("build.source_path_policy must be an object")
    if source_path_policy.get("build_root_mapping") != ".":
        raise BuildError("PostgreSQL build roots must map to a stable relative compiler path")
    if source_path_policy.get("absolute_build_root_in_file_macro") != "forbidden":
        raise BuildError("absolute PostgreSQL build roots in __FILE__ must be forbidden")
    if source_path_policy.get("absolute_build_root_in_debug_info") != "forbidden":
        raise BuildError("absolute PostgreSQL build roots in debug information must be forbidden")
    targets = require_string_array(build.get("make_targets"), "build.make_targets")
    if targets != ["world-bin", "check-world", "install-world-bin"]:
        raise BuildError("build.make_targets must build, test, then install the complete binary world")
    if not isinstance(build.get("parallel_jobs"), int) or build["parallel_jobs"] < 1:
        raise BuildError("build.parallel_jobs must be positive")
    if build.get("build_directory_mode") != "0700":
        raise BuildError("build.build_directory_mode must be 0700")
    require_string(build.get("python"), "build.python")
    for field in ("install_prefix", "release_root", "build_root", "stage_root"):
        path = Path(require_string(execution.get(field), f"execution.{field}"))
        if not path.is_absolute():
            raise BuildError(f"execution.{field} must be absolute")
    if execution["install_prefix"] != runtime_package["install_prefix"]:
        raise BuildError("PostgreSQL and runtime install prefixes differ")
    rejected = require_string_array(environment.get("rejected_nonempty"), "environment.rejected_nonempty")
    for name in ("PGXS", "LD_LIBRARY_PATH", "CMAKE_PREFIX_PATH", "PKG_CONFIG_PATH"):
        if name not in rejected:
            raise BuildError(f"environment.rejected_nonempty must include {name}")
    categories = (
        require_string_array(closure.get("package_sonames"), "runtime_closure.package_sonames"),
        require_string_array(closure.get("system_abi_sonames"), "runtime_closure.system_abi_sonames"),
    )
    flattened = [item for category in categories for item in category]
    if len(flattened) != len(set(flattened)):
        raise BuildError("runtime closure SONAME categories overlap")
    policy = require_string(closure.get("activation_policy"), "runtime_closure.activation_policy")
    if policy != "blocked-until-selected-libraries-are-packaged-and-recursive-elf-closure-is-clean":
        raise BuildError("runtime closure activation policy is not fail-closed")
    verifier = closure.get("recursive_verifier")
    if not isinstance(verifier, dict):
        raise BuildError("runtime recursive verifier contract is missing")
    require_string(verifier.get("path"), "runtime_closure.recursive_verifier.path")
    require_string(verifier.get("schema"), "runtime_closure.recursive_verifier.schema")
    require_string(verifier.get("tool_version"), "runtime_closure.recursive_verifier.tool_version")
    require_string_array(
        verifier.get("search_subdirectories"),
        "runtime_closure.recursive_verifier.search_subdirectories",
    )
    require_string_array(
        verifier.get("required_zero_summary_fields"),
        "runtime_closure.recursive_verifier.required_zero_summary_fields",
    )


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


def verify_toolchain_receipt(
    contract: dict[str, Any], receipt_path: Path
) -> dict[str, Any]:
    try:
        return verify_toolchain_package_receipt(
            contract["build_toolchain"], receipt_path
        )
    except ReceiptError as error:
        raise BuildError(str(error)) from error


def verify_installed_runtime_provider(
    contract: dict[str, Any], repository: Path, readelf: Path
) -> dict[str, Any]:
    lock_path = repository / contract["installed_runtime_provider"]["lock"]
    try:
        return verify_installed_provider_selection(
            contract["installed_runtime_provider"], lock_path, readelf
        )
    except ReceiptError as error:
        raise BuildError(str(error)) from error


def verify_runtime_receipt(
    contract: dict[str, Any], receipt_path: Path
) -> dict[str, Any]:
    receipt = read_json(receipt_path)
    expected = contract["runtime_package"]
    if receipt.get("schema") != expected["receipt_schema"]:
        raise BuildError("runtime package receipt schema mismatch")
    build_input_id = require_string(
        receipt.get("build_input_id"), "runtime_package.build_input_id"
    )
    if not re.fullmatch(r"[0-9a-f]{64}", build_input_id):
        raise BuildError("runtime package build_input_id must be lowercase SHA-256")
    if receipt.get("install_prefix") != expected["install_prefix"]:
        raise BuildError("runtime package install prefix differs from the product prefix")
    prefix = Path(
        require_string(receipt.get("staged_prefix"), "runtime_package.staged_prefix")
    )
    checkpoints = receipt.get("component_checkpoints")
    component_logs = receipt.get("component_logs")
    component_tests = receipt.get("component_test_executions")
    if not all(
        isinstance(item, dict) for item in (checkpoints, component_logs, component_tests)
    ):
        raise BuildError(
            "runtime package component checkpoints logs and test executions are required"
        )
    required_components = expected["required_components"]
    if any(
        sorted(evidence) != sorted(required_components)
        for evidence in (checkpoints, component_logs, component_tests)
    ):
        raise BuildError("runtime package component evidence set is incomplete")
    if any(
        not isinstance(checkpoints[name], str)
        or re.fullmatch(r"[0-9a-f]{64}", checkpoints[name]) is None
        for name in required_components
    ):
        raise BuildError("runtime package component checkpoint identity is invalid")
    if any(
        not isinstance(component_logs[name], str)
        or re.fullmatch(r"[0-9a-f]{64}", component_logs[name]) is None
        for name in required_components
    ):
        raise BuildError("runtime package component log identity is invalid")
    if any(not isinstance(component_tests[name], dict) for name in required_components):
        raise BuildError("runtime package component test execution is invalid")
    qualification = receipt.get("runtime_provider_qualification")
    if not isinstance(qualification, dict):
        raise BuildError("runtime provider qualification requirement is missing")
    if qualification.get("schema") != expected["provider_qualification_receipt_schema"]:
        raise BuildError("runtime provider qualification schema mismatch")
    if qualification.get("complete") is not False:
        raise BuildError("runtime package cannot claim provider qualification completion")
    if qualification.get("required_before_product_activation") is not True:
        raise BuildError("runtime provider qualification no longer gates activation")
    required_qualifications = expected["required_provider_qualifications"]
    if qualification.get("required_components") != required_qualifications:
        raise BuildError("runtime provider qualification component set mismatch")
    requirements = qualification.get("requirements")
    if not isinstance(requirements, dict) or sorted(requirements) != sorted(
        required_qualifications
    ):
        raise BuildError("runtime provider qualification evidence set is incomplete")
    for name in required_qualifications:
        execution = component_tests[name]
        if execution.get("product_activation_gate") != (
            "separate-selected-runtime-provider-qualification"
        ):
            raise BuildError("runtime component bypasses provider qualification")
        if execution.get("package_gate") != "record-exact-outcome-and-continue":
            raise BuildError("runtime component test outcome was not retained")
        requirement = requirements[name]
        if not isinstance(requirement, dict):
            raise BuildError("runtime provider qualification requirement is invalid")
        if requirement.get("component_checkpoint_sha256") != checkpoints[name]:
            raise BuildError("runtime provider qualification checkpoint mismatch")
        if requirement.get("test_execution_sha256") != canonical_sha256(execution):
            raise BuildError("runtime provider qualification test identity mismatch")
        if requirement.get("observed_disposition") != execution.get("disposition"):
            raise BuildError("runtime provider qualification disposition mismatch")
    for name in required_components:
        execution = component_tests[name]
        command = execution.get("command")
        return_code = execution.get("process_return_code")
        if not isinstance(command, list) or not command or any(
            not isinstance(item, str) or not item for item in command
        ):
            raise BuildError("runtime component test command is invalid")
        if not isinstance(return_code, int):
            raise BuildError("runtime component test return code is invalid")
        expected_exit_code = return_code if return_code >= 0 else None
        expected_signal = -return_code if return_code < 0 else None
        if execution.get("exit_code") != expected_exit_code:
            raise BuildError("runtime component test exit code mismatch")
        if execution.get("signal") != expected_signal:
            raise BuildError("runtime component test signal mismatch")
        expected_disposition = "passed"
        if return_code > 0:
            expected_disposition = "failed-under-observed-runtime-provider"
        elif return_code < 0:
            expected_disposition = "terminated-by-signal-under-observed-runtime-provider"
        if execution.get("disposition") != expected_disposition:
            raise BuildError("runtime component test disposition mismatch")
        if name not in required_qualifications:
            if (
                execution.get("package_gate") != "required-pass"
                or execution.get("product_activation_gate") != "component-test-pass"
                or return_code != 0
            ):
                raise BuildError("ordinary runtime component test did not pass")
    plan_sha256 = require_string(
        receipt.get("plan_sha256"), "runtime_package.plan_sha256"
    )
    if re.fullmatch(r"[0-9a-f]{64}", plan_sha256) is None:
        raise BuildError("runtime package plan_sha256 must be lowercase SHA-256")
    files = receipt.get("files")
    try:
        tree = verify_recorded_package_tree(
            prefix, files, receipt.get("tree_sha256")
        )
    except ReceiptError as error:
        raise BuildError(str(error)) from error
    if receipt.get("file_count") != tree["file_count"]:
        raise BuildError("runtime package file count differs from its physical tree")
    if receipt.get("total_file_bytes") != tree["total_file_bytes"]:
        raise BuildError("runtime package byte count differs from its physical tree")
    records = {
        item.get("path"): item
        for item in files
        if isinstance(item, dict) and isinstance(item.get("path"), str)
    }
    selected_executables: dict[str, dict[str, str]] = {}
    for name, requirement in expected["required_build_executables"].items():
        relative = requirement["relative_path"]
        record = records.get(relative)
        if not isinstance(record, dict) or record.get("kind") != "file":
            raise BuildError(f"runtime package omits selected build executable: {name}")
        executable = prefix / relative
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise BuildError(f"runtime build executable is not executable: {name}")
        result = subprocess.run(
            [str(executable), "version"],
            text=True,
            capture_output=True,
            env={
                "HOME": str(prefix),
                "PATH": "/usr/bin:/bin",
                "LANG": "C.UTF-8",
                "LC_ALL": "C.UTF-8",
            },
        )
        version_line = (result.stdout + result.stderr).strip().splitlines()
        if result.returncode != 0 or not version_line:
            raise BuildError(f"runtime build executable probe failed: {name}")
        if version_line[0] != requirement["version_line"]:
            raise BuildError(f"runtime build executable version differs: {name}")
        selected_executables[name] = {
            "relative_path": relative,
            "sha256": require_string(record.get("sha256"), f"{name}.sha256"),
            "version_line": version_line[0],
        }
    if receipt.get("activation_eligible") is not False:
        raise BuildError("runtime component package cannot claim product activation")
    for field in (
        "build_input_closure_complete",
        "static_link_closure_verified",
        "recursive_runtime_closure_verified",
    ):
        if receipt.get(field) is not False:
            raise BuildError(f"runtime component package {field} proof state is invalid")
    return {
        "receipt_path": str(receipt_path.resolve()),
        "receipt_sha256": sha256_file(receipt_path),
        "build_input_id": build_input_id,
        "install_prefix": receipt["install_prefix"],
        "staged_prefix": str(prefix),
        "tree_sha256": tree["tree_sha256"],
        "file_count": tree["file_count"],
        "total_file_bytes": tree["total_file_bytes"],
        "component_checkpoints": {
            name: checkpoints[name] for name in required_components
        },
        "component_logs": {name: component_logs[name] for name in required_components},
        "component_test_executions": {
            name: component_tests[name] for name in required_components
        },
        "runtime_provider_qualification": qualification,
        "selected_build_executables": selected_executables,
        "plan_sha256": plan_sha256,
    }


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
    toolchain_receipt: Path,
    runtime_receipt: Path,
) -> dict[str, Any]:
    validate_contract(contract)
    release = selected_release(contract, repository)
    compilers = {
        "c": validate_compiler(contract, "c"),
        "cxx": validate_compiler(contract, "cxx"),
    }
    build_toolchain = verify_toolchain_receipt(contract, toolchain_receipt)
    installed_runtime_provider = verify_installed_runtime_provider(
        contract,
        repository,
        Path(build_toolchain["tools"]["readelf"]["path"]),
    )
    runtime_package = verify_runtime_receipt(contract, runtime_receipt)
    recipe = build_recipe_identity(contract, repository)
    source = ensure_external(source_root, repository, "source root")
    source_receipt = verify_release_import(contract, repository, archive_root, source)
    build_base = ensure_external(
        Path(contract["execution"]["build_root"]), repository, "build root"
    )
    stage_base = ensure_external(
        Path(contract["execution"]["stage_root"]), repository, "stage root"
    )
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
        "build_toolchain": build_toolchain,
        "installed_runtime_provider": installed_runtime_provider,
        "runtime_package": runtime_package,
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
    stage_directory = stage_base / build_id
    install_prefix = Path(contract["execution"]["install_prefix"])
    postgresql_install_prefix = install_prefix / contract["build"]["install_subdirectory"]
    staged_product_prefix = stage_directory / "root" / install_prefix.relative_to("/")
    staged_postgresql_prefix = (
        stage_directory / "root" / postgresql_install_prefix.relative_to("/")
    )
    release_prefix = Path(contract["execution"]["release_root"]) / build_id
    configure_command = [
        str(source / "configure"),
        f"--prefix={postgresql_install_prefix}",
        *contract["build"]["configure_arguments"],
    ]
    return {
        "schema": PLAN_SCHEMA,
        "build_input_id": build_id,
        "source_root": str(source),
        "source_verification_sha256": canonical_sha256(source_receipt),
        "build_directory": str(build_directory),
        "stage_directory": str(stage_directory),
        "install_prefix": str(install_prefix),
        "postgresql_install_prefix": str(postgresql_install_prefix),
        "staged_product_prefix": str(staged_product_prefix),
        "staged_postgresql_prefix": str(staged_postgresql_prefix),
        "release_prefix": str(release_prefix),
        "compilers": compilers,
        "build_toolchain": build_toolchain,
        "installed_runtime_provider": installed_runtime_provider,
        "runtime_package": runtime_package,
        "recipe": recipe,
        "configure_command": configure_command,
        "c_flags": contract["build"]["c_flags"],
        "cxx_flags": contract["build"]["cxx_flags"],
        "linker_flags": contract["build"]["linker_flags"],
        "make_targets": contract["build"]["make_targets"],
        "parallel_jobs": contract["build"]["parallel_jobs"],
    }


def build_environment(
    contract: dict[str, Any], plan: dict[str, Any], home_directory: Path
) -> dict[str, str]:
    compiler_directory = str(Path(contract["toolchain"]["c_compiler"]).parent)
    tools = plan["build_toolchain"]["tools"]
    tool_directories: list[str] = []
    for tool in tools.values():
        directory = str(Path(tool["path"]).parent)
        if directory not in tool_directories:
            tool_directories.append(directory)
    staged_product = Path(plan["staged_product_prefix"])
    openssl_receipt = plan["runtime_package"]["selected_build_executables"]["openssl"]
    openssl = staged_product / openssl_receipt["relative_path"]
    if (
        not openssl.is_file()
        or not os.access(openssl, os.X_OK)
        or sha256_file(openssl) != openssl_receipt["sha256"]
    ):
        raise BuildError("composed staged OpenSSL executable differs from its receipt")
    build_root = Path(plan["build_directory"])
    mapping = contract["build"]["source_path_policy"]["build_root_mapping"]
    source_path_flags = [
        f"-ffile-prefix-map={build_root}={mapping}",
        f"-fdebug-prefix-map={build_root}={mapping}",
    ]
    make_encoded_runpath = contract["build"]["install_runpath"].replace("$", "$$")
    linker_flags = [
        f"-B{Path(plan['build_toolchain']['prefix']) / 'bin'}",
        f"-L{staged_product / 'lib'}",
        *contract["build"]["linker_flags"],
        f"-Wl,-rpath,'{make_encoded_runpath}'",
    ]
    home_directory.mkdir(exist_ok=True)
    os.chmod(home_directory, 0o700)
    if stat.S_IMODE(home_directory.stat().st_mode) != 0o700:
        raise BuildError(f"build HOME must be private: {home_directory}")
    return {
        "HOME": str(home_directory.resolve()),
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        "PATH": ":".join(
            [str(openssl.parent), *tool_directories, compiler_directory, "/usr/bin", "/bin"]
        ),
        "CC": contract["toolchain"]["c_compiler"],
        "CXX": contract["toolchain"]["cxx_compiler"],
        "CFLAGS": " ".join(
            [
                f"-B{Path(plan['build_toolchain']['prefix']) / 'bin'}",
                *contract["build"]["c_flags"],
                *source_path_flags,
            ]
        ),
        "CXXFLAGS": " ".join(
            [
                f"-B{Path(plan['build_toolchain']['prefix']) / 'bin'}",
                *contract["build"]["cxx_flags"],
                *source_path_flags,
            ]
        ),
        "CPPFLAGS": f"-I{staged_product / 'include'}",
        "LDFLAGS": " ".join(linker_flags),
        "LD_LIBRARY_PATH": str(staged_product / "lib"),
        "PKG_CONFIG": tools["pkgconf"]["path"],
        "PKG_CONFIG_LIBDIR": ":".join(
            [
                str(staged_product / "lib/pkgconfig"),
                str(staged_product / "share/pkgconfig"),
            ]
        ),
        "PKG_CONFIG_SYSROOT_DIR": str(Path(plan["stage_directory"]) / "root"),
        "MAKE": tools["make"]["path"],
        "PERL": tools["perl"]["path"],
        "PROVE": tools["prove"]["path"],
        "OPENSSL": str(openssl),
        "BISON": tools["bison"]["path"],
        "FLEX": tools["flex"]["path"],
        "AR": tools["ar"]["path"],
        "AS": tools["as"]["path"],
        "LD": tools["ld"]["path"],
        "NM": tools["nm"]["path"],
        "RANLIB": tools["ranlib"]["path"],
        "STRIP": tools["strip"]["path"],
        "OBJCOPY": tools["objcopy"]["path"],
        "OBJDUMP": tools["objdump"]["path"],
        "PYTHON": contract["build"]["python"],
    }


def verify_build_input_execution(
    plan: dict[str, Any], environment: Mapping[str, str]
) -> dict[str, Any]:
    perl = Path(plan["build_toolchain"]["tools"]["perl"]["path"])
    modules = plan["build_toolchain"]["perl_modules"]
    module_receipts: dict[str, dict[str, str]] = {}
    for name, expected_version in EXPECTED_PERL_MODULES.items():
        selected = modules.get(name)
        if not isinstance(selected, dict) or selected.get("version") != expected_version:
            raise BuildError(f"selected Perl module receipt differs: {name}")
        program = (
            "my $module = shift; "
            "(my $file = $module) =~ s!::!/!g; $file .= '.pm'; "
            "require $file; "
            "no strict 'refs'; "
            "print ${$module . '::VERSION'}, qq{\\n}, $INC{$file}, qq{\\n};"
        )
        result = subprocess.run(
            [str(perl), "-e", program, name],
            text=True,
            capture_output=True,
            env=dict(environment),
        )
        lines = result.stdout.strip().splitlines()
        if result.returncode != 0 or len(lines) != 2:
            raise BuildError(f"selected Perl module execution failed: {name}")
        version, provider_value = lines
        provider = Path(provider_value)
        if version != expected_version or provider != Path(selected["path"]):
            raise BuildError(f"selected Perl module execution identity differs: {name}")
        if sha256_file(provider) != selected["sha256"]:
            raise BuildError(f"selected Perl module execution bytes differ: {name}")
        module_receipts[name] = {
            "path": str(provider),
            "sha256": selected["sha256"],
            "version": version,
        }

    openssl = Path(environment["OPENSSL"])
    selected_openssl = plan["runtime_package"]["selected_build_executables"]["openssl"]
    result = subprocess.run(
        [str(openssl), "version"],
        text=True,
        capture_output=True,
        env=dict(environment),
    )
    lines = (result.stdout + result.stderr).strip().splitlines()
    if result.returncode != 0 or not lines:
        raise BuildError("selected staged OpenSSL execution failed")
    if (
        lines[0] != selected_openssl["version_line"]
        or sha256_file(openssl) != selected_openssl["sha256"]
    ):
        raise BuildError("selected staged OpenSSL execution identity differs")
    return {
        "schema": "laplace.postgresql-build-input-preflight/v1",
        "perl": {
            "path": str(perl),
            "sha256": plan["build_toolchain"]["tools"]["perl"]["sha256"],
            "modules": module_receipts,
        },
        "openssl": {
            "path": str(openssl),
            "sha256": selected_openssl["sha256"],
            "version_line": lines[0],
        },
    }


def verify_configure_input_selection(
    log_path: Path, environment: Mapping[str, str], preflight: Mapping[str, Any]
) -> dict[str, str]:
    text = log_path.read_text(encoding="utf-8", errors="replace")
    expected = {
        "openssl_path": f"checking for OPENSSL... {environment['OPENSSL']}",
        "openssl_version": (
            "configure: using openssl: "
            f"{preflight['openssl']['version_line']}"
        ),
        "tap_modules": "checking for Perl modules required for TAP tests... yes",
    }
    missing = [name for name, line in expected.items() if line not in text]
    if missing:
        raise BuildError(
            "PostgreSQL configure did not retain exact selected inputs: "
            + ", ".join(missing)
        )
    if "checking for OPENSSL... /usr/bin/openssl" in text:
        raise BuildError("PostgreSQL configure selected ambient OpenSSL")
    return expected


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


def reverify_runtime_input(
    contract: dict[str, Any], plan: dict[str, Any]
) -> dict[str, Any]:
    selected = verify_runtime_receipt(
        contract, Path(plan["runtime_package"]["receipt_path"])
    )
    if selected != plan["runtime_package"]:
        raise BuildError("runtime package input differs from the exact build plan")
    return read_json(Path(selected["receipt_path"]))


def verify_runtime_bytes_in_composed_tree(
    plan: dict[str, Any], receipt: dict[str, Any], *, allow_additions: bool
) -> None:
    try:
        verify_recorded_package_tree(
            Path(plan["staged_product_prefix"]),
            receipt.get("files"),
            receipt.get("tree_sha256"),
            allow_additions=allow_additions,
        )
    except ReceiptError as error:
        raise BuildError(f"composed runtime package differs: {error}") from error


def copy_receipted_runtime_file(
    source_receipt: Mapping[str, Any], destination: Path, *, resume: bool
) -> dict[str, Any]:
    source = Path(require_string(source_receipt.get("path"), "runtime provider path"))
    expected_sha = require_string(source_receipt.get("sha256"), "runtime provider sha256")
    expected_size = source_receipt.get("size_bytes")
    if (
        not source.is_file()
        or source.is_symlink()
        or not isinstance(expected_size, int)
        or source.stat().st_size != expected_size
        or sha256_file(source) != expected_sha
    ):
        raise BuildError(f"selected runtime provider bytes differ: {source}")
    if resume:
        if (
            not destination.is_file()
            or destination.is_symlink()
            or destination.stat().st_size != expected_size
            or sha256_file(destination) != expected_sha
        ):
            raise BuildError(f"resumed packaged runtime provider differs: {destination}")
    else:
        if destination.exists() or destination.is_symlink():
            raise BuildError(f"runtime provider destination already exists: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination, follow_symlinks=False)
        if destination.stat().st_size != expected_size or sha256_file(destination) != expected_sha:
            raise BuildError(f"packaged runtime provider copy differs: {destination}")
    return {
        "source_path": str(source),
        "package_path": str(destination),
        "sha256": expected_sha,
        "size_bytes": expected_size,
    }


def package_selected_runtime_providers(
    plan: dict[str, Any], product_prefix: Path, *, resume: bool
) -> dict[str, Any]:
    packaged: dict[str, dict[str, Any]] = {}
    selected_provider = plan["installed_runtime_provider"]
    for role, source_receipt in selected_provider["files"].items():
        relative = source_receipt["package_relative_path"]
        packaged[f"installed-provider:{role}"] = copy_receipted_runtime_file(
            source_receipt, product_prefix / relative, resume=resume
        )
    for role, source_receipt in plan["build_toolchain"]["linker_map_inputs"].items():
        relative = source_receipt.get("package_relative_path")
        if relative is None:
            continue
        destination = product_prefix / relative
        packaged[f"toolchain-linker-input:{role}"] = copy_receipted_runtime_file(
            source_receipt, destination, resume=resume
        )
        for alias in source_receipt["package_aliases"]:
            alias_path = product_prefix / alias
            expected_target = os.path.relpath(destination, alias_path.parent)
            if resume:
                if not alias_path.is_symlink() or os.readlink(alias_path) != expected_target:
                    raise BuildError(f"resumed runtime provider alias differs: {alias_path}")
            else:
                if alias_path.exists() or alias_path.is_symlink():
                    raise BuildError(f"runtime provider alias already exists: {alias_path}")
                alias_path.parent.mkdir(parents=True, exist_ok=True)
                alias_path.symlink_to(expected_target)
            packaged[f"toolchain-linker-alias:{role}:{alias}"] = {
                "package_path": str(alias_path),
                "target": expected_target,
            }
    return {
        "schema": "laplace.postgresql-packaged-runtime-providers/v1",
        "installed_provider_lock_sha256": selected_provider["lock_sha256"],
        "provider_selection_sha256": selected_provider["provider_selection_sha256"],
        "files": packaged,
        "product_runtime_activated": False,
    }


def prepare_build_directory(
    contract: dict[str, Any], plan: dict[str, Any], resume: bool
) -> tuple[Path, dict[str, Any], dict[str, Any]]:
    build_directory = Path(plan["build_directory"])
    stage_directory = Path(plan["stage_directory"])
    product_prefix = Path(plan["staged_product_prefix"])
    postgresql_prefix = Path(plan["staged_postgresql_prefix"])
    plan_path = build_directory / "build-plan.json"
    runtime_receipt = reverify_runtime_input(contract, plan)
    if resume:
        if (
            not build_directory.is_dir()
            or build_directory.is_symlink()
            or not stage_directory.is_dir()
            or stage_directory.is_symlink()
            or not plan_path.is_file()
        ):
            raise BuildError(
                "resume requires physical build/stage directories and build-plan.json"
            )
        if postgresql_prefix.exists() or postgresql_prefix.is_symlink():
            raise BuildError("resume refuses an already-installed PostgreSQL subtree")
        if read_json(plan_path) != plan:
            raise BuildError("resume build plan differs from the requested exact plan")
        for name, path in (("build", build_directory), ("stage", stage_directory)):
            observed_mode = stat.S_IMODE(path.stat().st_mode)
            if observed_mode != 0o700:
                raise BuildError(
                    f"resume {name} directory mode must be 0700: observed {observed_mode:04o}"
                )
        verify_runtime_bytes_in_composed_tree(plan, runtime_receipt, allow_additions=True)
    else:
        if build_directory.exists() or stage_directory.exists():
            raise BuildError("build and stage destinations must not already exist")
        create_private_build_directory(build_directory)
        create_private_build_directory(stage_directory)
        plan_path.write_text(
            json.dumps(plan, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        source = Path(plan["runtime_package"]["staged_prefix"])
        product_prefix.parent.mkdir(parents=True)
        shutil.copytree(source, product_prefix, symlinks=True)
        verify_runtime_bytes_in_composed_tree(
            plan, runtime_receipt, allow_additions=False
        )
    packaged_runtime_providers = package_selected_runtime_providers(
        plan, product_prefix, resume=resume
    )
    return build_directory, runtime_receipt, packaged_runtime_providers


def execute_plan(
    contract: dict[str, Any], plan: dict[str, Any], resume: bool = False
) -> dict[str, Any]:
    build_directory, runtime_receipt, packaged_runtime_providers = prepare_build_directory(
        contract, plan, resume
    )
    prefix = Path(plan["staged_product_prefix"])
    log_path = build_directory / "build.log"
    environment = build_environment(contract, plan, build_directory / ".home")
    preflight = verify_build_input_execution(plan, environment)
    preflight_path = build_directory / "build-input-preflight.json"
    preflight_path.write_text(
        json.dumps(preflight, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    run_logged(plan["configure_command"], build_directory, environment, log_path)
    configure_input_selection = verify_configure_input_selection(
        log_path, environment, preflight
    )
    jobs = str(plan["parallel_jobs"])
    make = plan["build_toolchain"]["tools"]["make"]["path"]
    destination_root = Path(plan["stage_directory"]) / "root"
    for target in plan["make_targets"]:
        command = [make, f"-j{jobs}", target]
        target_environment = environment
        if target == "install-world-bin":
            command.append(f"DESTDIR={destination_root}")
            target_environment = {
                **environment,
                "DESTDIR": str(destination_root),
            }
        run_logged(command, build_directory, target_environment, log_path)
    verify_runtime_bytes_in_composed_tree(
        plan, runtime_receipt, allow_additions=True
    )
    receipt = verify_package(
        contract,
        prefix,
        Path(plan["build_toolchain"]["tools"]["readelf"]["path"]),
        repository=Path(__file__).resolve().parents[2],
        toolchain=plan["build_toolchain"],
        installed_provider=plan["installed_runtime_provider"],
        closure_output=build_directory / "recursive-elf-closure.json",
    )
    receipt.update(
        {
            "build_input_id": plan["build_input_id"],
            "build_plan_sha256": canonical_sha256(plan),
            "build_log_sha256": sha256_file(log_path),
            "build_input_preflight": preflight,
            "build_input_preflight_sha256": sha256_file(preflight_path),
            "configure_input_selection": configure_input_selection,
            "completed_targets": list(plan["make_targets"]),
            "recipe": plan["recipe"],
            "runtime_package": plan["runtime_package"],
            "installed_runtime_provider": plan["installed_runtime_provider"],
            "packaged_runtime_providers": packaged_runtime_providers,
            "build_toolchain": plan["build_toolchain"],
            "release_prefix": plan["release_prefix"],
        }
    )
    (build_directory / "package-receipt.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return receipt


def elf_needed(path: Path, readelf: Path) -> set[str]:
    with path.open("rb") as source:
        if source.read(4) != b"\x7fELF":
            return set()
    result = subprocess.run(
        [str(readelf), "-d", str(path)], text=True, capture_output=True
    )
    if result.returncode != 0:
        raise BuildError(f"readelf failed for {path}: {result.stderr.strip()}")
    return set(NEEDED_PATTERN.findall(result.stdout))


def package_tree(prefix: Path, readelf: Path) -> tuple[str, int, int, set[str]]:
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
            needed.update(elf_needed(path, readelf))
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
    return {
        "package": sorted(needed & package),
        "system_abi": sorted(needed & system),
        "unknown": sorted(needed - package - system),
    }


def verify_recursive_elf_closure(
    contract: dict[str, Any],
    repository: Path,
    prefix: Path,
    toolchain: Mapping[str, Any],
    installed_provider: Mapping[str, Any],
    output_path: Path,
) -> dict[str, Any]:
    verifier = contract["runtime_closure"]["recursive_verifier"]
    tool_path = (repository / verifier["path"]).resolve()
    if not tool_path.is_file():
        raise BuildError(f"recursive ELF closure verifier is missing: {tool_path}")
    platform_inputs = {
        role: item
        for role, item in toolchain["linker_map_inputs"].items()
        if item["classification"] == "platform-abi"
    }
    loader = platform_inputs.get("dynamic-loader")
    if loader is None:
        raise BuildError("toolchain receipt omits the selected dynamic loader")
    command = [
        sys.executable,
        str(tool_path),
        "--root",
        str(prefix),
        "--custom-prefix",
        str(prefix),
        "--process-cwd",
        "/",
        "--readelf",
        toolchain["tools"]["readelf"]["path"],
        "--loader",
        loader["path"],
        "--output",
        str(output_path),
        "--strict",
    ]
    search_directories: list[str] = []
    for relative in verifier["search_subdirectories"]:
        directory = prefix / relative
        if not directory.is_dir() or directory.is_symlink():
            raise BuildError(f"recursive ELF search directory is missing: {directory}")
        search_directories.append(str(directory))
        command.extend(("--search-dir", str(directory)))
    result = subprocess.run(command, text=True, capture_output=True)
    if not output_path.is_file():
        detail = result.stderr.strip() or result.stdout.strip()
        raise BuildError(
            "recursive ELF closure verifier produced no receipt"
            + (f": {detail}" if detail else "")
        )
    report = read_json(output_path)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise BuildError(
            f"recursive ELF closure verifier rejected the package ({result.returncode}): {detail}"
        )
    if report.get("schema") != verifier["schema"] or report.get("tool_version") != verifier[
        "tool_version"
    ]:
        raise BuildError("recursive ELF closure receipt schema or tool version differs")
    inputs = report.get("inputs")
    summary = report.get("summary")
    objects = report.get("objects")
    if not isinstance(inputs, dict) or not isinstance(summary, dict) or not isinstance(objects, list):
        raise BuildError("recursive ELF closure receipt structure is incomplete")
    if (
        inputs.get("roots") != [str(prefix)]
        or inputs.get("custom_prefix") != str(prefix)
        or inputs.get("process_cwd") != "/"
        or inputs.get("environment_library_path_used") is not False
        or sorted(inputs.get("explicit_search_directories", []))
        != sorted(search_directories)
    ):
        raise BuildError("recursive ELF closure receipt input boundary differs")
    for field in verifier["required_zero_summary_fields"]:
        if summary.get(field) != 0:
            raise BuildError(f"recursive ELF closure is not clean: {field}")
    by_path = {
        item.get("path"): item for item in objects if isinstance(item, dict)
    }
    external_objects = [
        item for item in objects if item.get("classification") == "external-prefix"
    ]
    if external_objects:
        raise BuildError("recursive ELF closure selected an external runtime provider")
    observed_host = {
        item.get("path"): item
        for item in objects
        if item.get("classification") == "host-system"
    }
    expected_host = {str(Path(item["path"]).resolve()): item for item in platform_inputs.values()}
    if set(observed_host) != set(expected_host):
        raise BuildError("recursive ELF closure host ABI object set differs")
    for path, expected in expected_host.items():
        item = observed_host[path]
        if item.get("sha256") != expected["sha256"] or item.get("elf", {}).get(
            "soname"
        ) != expected["soname"]:
            raise BuildError(f"recursive ELF closure host ABI identity differs: {path}")
    host_loader = report.get("host_loader")
    if not isinstance(host_loader, dict) or (
        host_loader.get("path") != str(Path(loader["path"]).resolve())
        or host_loader.get("sha256") != loader["sha256"]
    ):
        raise BuildError("recursive ELF closure loader identity differs")
    packaged_runtime_objects: dict[str, dict[str, Any]] = {}
    expected_packaged: list[tuple[str, Mapping[str, Any]]] = []
    expected_packaged.extend(
        (f"installed-provider:{role}", item)
        for role, item in installed_provider["files"].items()
        if item["class"] == "runtime-object"
    )
    expected_packaged.extend(
        (f"toolchain-linker-input:{role}", item)
        for role, item in toolchain["linker_map_inputs"].items()
        if item["classification"] == "packaged-compiler-support"
    )
    for role, expected in expected_packaged:
        packaged_path = str((prefix / expected["package_relative_path"]).resolve())
        item = by_path.get(packaged_path)
        if (
            not isinstance(item, dict)
            or item.get("classification") != "custom-prefix"
            or item.get("sha256") != expected["sha256"]
            or item.get("elf", {}).get("soname") != expected["soname"]
        ):
            raise BuildError(f"recursive ELF closure omitted selected packaged provider: {role}")
        packaged_runtime_objects[role] = {
            "path": packaged_path,
            "sha256": item["sha256"],
            "soname": item["elf"]["soname"],
        }
    return {
        "schema": "laplace.postgresql-recursive-elf-closure-receipt/v1",
        "verifier": {
            "path": verifier["path"],
            "sha256": sha256_file(tool_path),
            "schema": report["schema"],
            "tool_version": report["tool_version"],
        },
        "report_path": str(output_path.resolve()),
        "report_sha256": sha256_file(output_path),
        "summary": summary,
        "platform_abi_objects": {
            role: {
                "path": str(Path(item["path"]).resolve()),
                "sha256": item["sha256"],
                "soname": item["soname"],
            }
            for role, item in platform_inputs.items()
        },
        "packaged_runtime_objects": packaged_runtime_objects,
        "verified": True,
        "product_runtime_activated": False,
    }


def verify_package(
    contract: dict[str, Any],
    prefix: Path,
    readelf: Path,
    *,
    repository: Path,
    toolchain: Mapping[str, Any],
    installed_provider: Mapping[str, Any],
    closure_output: Path,
) -> dict[str, Any]:
    validate_contract(contract)
    prefix = prefix.resolve()
    pgsql = prefix / contract["build"]["install_subdirectory"]
    pg_config = pgsql / "bin/pg_config"
    postgres = pgsql / "bin/postgres"
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
    tree_sha, file_count, total_bytes, needed = package_tree(prefix, readelf)
    closure = classify_needed(contract, needed)
    if closure["unknown"]:
        raise BuildError("package has undeclared direct ELF dependencies")
    recursive_closure = verify_recursive_elf_closure(
        contract,
        repository,
        prefix,
        toolchain,
        installed_provider,
        closure_output,
    )
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
        "recursive_elf_closure": recursive_closure,
        "recursive_elf_closure_verified": True,
        "activation_eligible": False,
        "activation_disposition": (
            "blocked: build-input closure and runtime-provider qualification remain incomplete"
        ),
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
    plan.add_argument("--toolchain-receipt", required=True)
    plan.add_argument("--runtime-receipt", required=True)
    build = subparsers.add_parser("build")
    build.add_argument("--archive-root", required=True)
    build.add_argument("--source-root", required=True)
    build.add_argument("--toolchain-receipt", required=True)
    build.add_argument("--runtime-receipt", required=True)
    resume = subparsers.add_parser("resume")
    resume.add_argument("--archive-root", required=True)
    resume.add_argument("--source-root", required=True)
    resume.add_argument("--toolchain-receipt", required=True)
    resume.add_argument("--runtime-receipt", required=True)
    package = subparsers.add_parser("verify-package")
    package.add_argument("--prefix", required=True)
    package.add_argument("--toolchain-receipt", required=True)
    package.add_argument("--closure-output", required=True)
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
            Path(arguments.toolchain_receipt),
            Path(arguments.runtime_receipt),
        )
        if arguments.command in ("build", "resume"):
            receipt = execute_plan(contract, plan, resume=arguments.command == "resume")
            print(json.dumps({"plan": plan, "package": receipt}, indent=2, sort_keys=True))
        else:
            print(json.dumps(plan, indent=2, sort_keys=True))
        return 0
    if arguments.command == "verify-package":
        toolchain = verify_toolchain_receipt(
            contract, Path(arguments.toolchain_receipt)
        )
        installed_provider = verify_installed_runtime_provider(
            contract,
            repository,
            Path(toolchain["tools"]["readelf"]["path"]),
        )
        closure_output = Path(arguments.closure_output).resolve()
        print(
            json.dumps(
                verify_package(
                    contract,
                    Path(arguments.prefix),
                    Path(toolchain["tools"]["readelf"]["path"]),
                    repository=repository,
                    toolchain=toolchain,
                    installed_provider=installed_provider,
                    closure_output=closure_output,
                ),
                indent=2,
                sort_keys=True,
            )
        )
        return 0
    raise BuildError(f"unknown command: {arguments.command}")


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (BuildError, subprocess.CalledProcessError) as error:
        print(f"postgresql-build: {error}", file=sys.stderr)
        raise SystemExit(1) from error
