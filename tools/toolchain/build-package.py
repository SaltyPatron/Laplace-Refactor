#!/usr/bin/env python3
"""Build and receipt the selected source-built build toolchain."""

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


CONTRACT_SCHEMA = "laplace.toolchain-build-contract/v1"
PLAN_SCHEMA = "laplace.toolchain-build-plan/v1"
PACKAGE_SCHEMA = "laplace.toolchain-package-receipt/v1"
CONSUMER_SCHEMA = "laplace.toolchain-consumer-manifest/v1"
EXPECTED_ORDER = [
    "perl",
    "texinfo",
    "gnu-make",
    "tcl",
    "expect",
    "dejagnu",
    "gnu-binutils",
    "pkgconf",
    "gnu-bison",
    "flex",
    "cmake",
    "ninja",
]
EXPECTED_VERSIONS = {
    "perl": "5.44.0",
    "texinfo": "7.3",
    "gnu-make": "4.4.1",
    "tcl": "8.6.18",
    "expect": "5.45.4",
    "dejagnu": "1.6.3",
    "gnu-binutils": "2.47",
    "pkgconf": "3.0.6",
    "gnu-bison": "3.8.2",
    "flex": "2.6.4",
    "cmake": "4.4.2",
    "ninja": "1.13.2",
}
EXPECTED_TEST_COMMANDS = {
    "perl": ["{make}", "test"],
    "texinfo": ["{make}", "-j{jobs}", "check"],
    "gnu-make": ["{make}", "-j{jobs}", "check"],
    "tcl": ["{make}", "-j{jobs}", "test"],
    "expect": ["{make}", "-j{jobs}", "test"],
    "dejagnu": ["{make}", "-j{jobs}", "check"],
    "gnu-binutils": ["{make}", "-j{jobs}", "check"],
    "pkgconf": ["{make}", "-j{jobs}", "check"],
    "gnu-bison": ["{make}", "-j{jobs}", "check"],
    "flex": ["{make}", "-j{jobs}", "check"],
    "cmake": ["{build}/bin/ctest", "--output-on-failure", "-j{jobs}"],
    "ninja": ["{ctest}", "--test-dir", "{build}", "--output-on-failure", "-j{jobs}"],
}
REQUIRED_TOOL_IDS = {
    "ar",
    "as",
    "bison",
    "cmake",
    "cpack",
    "ctest",
    "expect",
    "flex",
    "ld",
    "make",
    "makeinfo",
    "ninja",
    "nm",
    "objcopy",
    "objdump",
    "perl",
    "pkgconf",
    "ranlib",
    "readelf",
    "runtest",
    "strip",
    "tclsh8.6",
}
HASH_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ABSOLUTE_PATH_PATTERN = re.compile(r"(?<![A-Za-z0-9_.-])(/[A-Za-z0-9_+.,:@%/=-]+)")
NEEDED_PATTERN = re.compile(r"Shared library: \[([^]]+)\]")
RUNPATH_PATTERN = re.compile(r"Library (?:runpath|rpath): \[([^]]*)\]")
DEJAGNU_OUTCOME_PATTERN = re.compile(
    r"^(PASS|FAIL|ERROR|UNRESOLVED|XPASS|XFAIL|UNSUPPORTED|UNTESTED):"
)
LLVM_UNSELECTED_REASON = (
    "No compatible selected Clang and LLVMgold provider exists; the contract-owned "
    "gate prevents ambient LLVM activation."
)
EXPECTED_BINUTILS_UNTESTED_DISPOSITIONS = [
    {
        "line": "UNTESTED: pr33198 with -R .gnu.lto_* -R .gnu.debuglto_* -R .llvm.lto -N __gnu_lto_v1",
        "disposition": "unselected-optional-capability",
        "reason": LLVM_UNSELECTED_REASON,
        "capability_id": "llvm-lto-plugin-tests",
    },
    {
        "line": "UNTESTED: pr33198-fat with -R .gnu.lto_* -R .gnu.debuglto_* -R .llvm.lto -N __gnu_lto_v1",
        "disposition": "unselected-optional-capability",
        "reason": LLVM_UNSELECTED_REASON,
        "capability_id": "llvm-lto-plugin-tests",
    },
    {
        "line": "UNTESTED: pr33198 with -R .llvm.lto",
        "disposition": "unselected-optional-capability",
        "reason": LLVM_UNSELECTED_REASON,
        "capability_id": "llvm-lto-plugin-tests",
    },
    {
        "line": "UNTESTED: pr33198-fat with -R .llvm.lto",
        "disposition": "unselected-optional-capability",
        "reason": LLVM_UNSELECTED_REASON,
        "capability_id": "llvm-lto-plugin-tests",
    },
    {
        "line": "UNTESTED: pr33246-llvm with --strip-debug --enable-deterministic-archives",
        "disposition": "unselected-optional-capability",
        "reason": LLVM_UNSELECTED_REASON,
        "capability_id": "llvm-lto-plugin-tests",
    },
    {
        "line": "UNTESTED: pr33246-llvm-fat with --strip-debug --enable-deterministic-archives",
        "disposition": "unselected-optional-capability",
        "reason": LLVM_UNSELECTED_REASON,
        "capability_id": "llvm-lto-plugin-tests",
    },
    {
        "line": "UNTESTED: bootstrap with --static",
        "disposition": "plugin-enabled-static-bootstrap-incompatible",
        "reason": (
            "Upstream bootstrap.exp skips static bootstrap when selected plugin "
            "support requires dynamic loading."
        ),
        "evidence_predicates": [
            {
                "id": "static-link-probe-executed-successfully",
                "relative_path": "ld/ld.log",
                "contains": " -static cs",
                "until_contains": " -static-pie cs",
                "forbidden_before": "error:",
            },
            {
                "id": "nm-plugin-support-advertised",
                "relative_path": "ld/ld.log",
                "contains": "--plugin NAME      Load the specified plugin",
            },
        ],
    },
]


class ToolchainError(RuntimeError):
    pass


def reject_duplicate_object_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ToolchainError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def read_json(path: Path) -> dict[str, Any]:
    try:
        result = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_object_keys,
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ToolchainError(f"cannot read JSON document {path}: {error}") from error
    if not isinstance(result, dict):
        raise ToolchainError(f"JSON document must be an object: {path}")
    return result


def canonical_sha256(value: object) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def require_object(value: object, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ToolchainError(f"{name} must be an object")
    return value


def require_string(value: object, name: str) -> str:
    if not isinstance(value, str) or not value:
        raise ToolchainError(f"{name} must be a non-empty string")
    return value


def require_string_array(value: object, name: str) -> list[str]:
    if not isinstance(value, list) or not value:
        raise ToolchainError(f"{name} must be a non-empty string array")
    if any(not isinstance(item, str) or not item for item in value):
        raise ToolchainError(f"{name} must contain only non-empty strings")
    if len(set(value)) != len(value):
        raise ToolchainError(f"{name} contains duplicates")
    return value


def require_expected_outcome_dispositions(
    test_policy: Mapping[str, Any], name: str
) -> dict[str, list[dict[str, Any]]]:
    raw_capabilities = test_policy.get("unselected_optional_capabilities")
    if not isinstance(raw_capabilities, list) or not raw_capabilities:
        raise ToolchainError(f"{name}.unselected_optional_capabilities must be non-empty")
    capability_ids: set[str] = set()
    for index, raw_capability in enumerate(raw_capabilities):
        capability = require_object(
            raw_capability, f"{name}.unselected_optional_capabilities[{index}]"
        )
        capability_id = require_string(
            capability.get("id"),
            f"{name}.unselected_optional_capabilities[{index}].id",
        )
        if capability_id in capability_ids:
            raise ToolchainError(f"{name} contains duplicate capability id {capability_id}")
        capability_ids.add(capability_id)

    raw_outcomes = require_object(test_policy.get("expected_outcomes"), f"{name}.expected_outcomes")
    outcomes: dict[str, list[dict[str, Any]]] = {}
    lines: set[str] = set()
    bound_capability_ids: set[str] = set()
    for outcome, raw_entries in raw_outcomes.items():
        if not isinstance(raw_entries, list) or not raw_entries:
            raise ToolchainError(f"{name}.expected_outcomes.{outcome} must be a non-empty array")
        entries: list[dict[str, Any]] = []
        for index, raw_entry in enumerate(raw_entries):
            entry_name = f"{name}.expected_outcomes.{outcome}[{index}]"
            entry = require_object(raw_entry, entry_name)
            line = require_string(entry.get("line"), f"{entry_name}.line")
            if not line.startswith(f"{outcome}:"):
                raise ToolchainError(f"{entry_name}.line does not match outcome {outcome}")
            if line in lines:
                raise ToolchainError(f"{name} contains duplicate expected outcome line: {line}")
            lines.add(line)
            disposition = require_string(
                entry.get("disposition"), f"{entry_name}.disposition"
            )
            require_string(entry.get("reason"), f"{entry_name}.reason")
            capability_id = entry.get("capability_id")
            if disposition == "unselected-optional-capability":
                capability_id = require_string(capability_id, f"{entry_name}.capability_id")
                if capability_id not in capability_ids:
                    raise ToolchainError(
                        f"{entry_name} references unknown optional capability {capability_id}"
                    )
                bound_capability_ids.add(capability_id)
            elif disposition == "plugin-enabled-static-bootstrap-incompatible":
                if capability_id is not None:
                    raise ToolchainError(
                        f"{entry_name} static-bootstrap disposition cannot bind a capability"
                    )
                predicates = entry.get("evidence_predicates")
                if not isinstance(predicates, list) or not predicates:
                    raise ToolchainError(
                        f"{entry_name} static-bootstrap disposition requires evidence predicates"
                    )
                predicate_ids: set[str] = set()
                for predicate_index, raw_predicate in enumerate(predicates):
                    predicate_name = (
                        f"{entry_name}.evidence_predicates[{predicate_index}]"
                    )
                    predicate = require_object(raw_predicate, predicate_name)
                    predicate_id = require_string(
                        predicate.get("id"), f"{predicate_name}.id"
                    )
                    if predicate_id in predicate_ids:
                        raise ToolchainError(
                            f"{entry_name} contains duplicate evidence predicate {predicate_id}"
                        )
                    predicate_ids.add(predicate_id)
                    relative_path = require_string(
                        predicate.get("relative_path"), f"{predicate_name}.relative_path"
                    )
                    if Path(relative_path).is_absolute() or ".." in Path(relative_path).parts:
                        raise ToolchainError(
                            f"{predicate_name}.relative_path must remain build-relative"
                        )
                    require_string(predicate.get("contains"), f"{predicate_name}.contains")
                    until_contains = predicate.get("until_contains")
                    forbidden_before = predicate.get("forbidden_before")
                    if (until_contains is None) != (forbidden_before is None):
                        raise ToolchainError(
                            f"{predicate_name} must declare both until_contains and forbidden_before"
                        )
                    if until_contains is not None:
                        require_string(until_contains, f"{predicate_name}.until_contains")
                        require_string(forbidden_before, f"{predicate_name}.forbidden_before")
            else:
                raise ToolchainError(f"{entry_name} has unknown disposition {disposition}")
            entries.append(entry)
        outcomes[outcome] = entries
    missing_bindings = capability_ids - bound_capability_ids
    if missing_bindings:
        raise ToolchainError(
            f"{name} omits expected-outcome bindings for optional capabilities: "
            + ", ".join(sorted(missing_bindings))
        )
    return outcomes


def ensure_external(path: Path, repository: Path, name: str) -> Path:
    resolved = path.resolve()
    try:
        resolved.relative_to(repository.resolve())
    except ValueError:
        return resolved
    raise ToolchainError(f"{name} must be outside the repository: {resolved}")


def component_working_directory(
    component_id: str,
    source_mode: str,
    source: Path,
    component_build: Path,
    repository: Path,
) -> Path:
    candidate = source if source_mode == "private-copy-in-tree" else component_build
    return ensure_external(candidate, repository, f"{component_id} working directory")


def validate_contract(contract: dict[str, Any], repository: Path | None = None) -> None:
    if contract.get("schema") != CONTRACT_SCHEMA:
        raise ToolchainError(f"contract schema must be {CONTRACT_SCHEMA}")
    if contract.get("classification") != "source-built-build-toolchain":
        raise ToolchainError("contract classification must remain build-toolchain-specific")
    require_string(contract.get("release_lock"), "release_lock")
    roots = require_object(contract.get("logical_roots"), "logical_roots")
    for name in ("source_generation", "archive_root", "build_root", "work_root", "stage_root"):
        path = Path(require_string(roots.get(name), f"logical_roots.{name}"))
        if not path.is_absolute():
            raise ToolchainError(f"logical_roots.{name} must be absolute")
        if repository is not None:
            ensure_external(path, repository, f"logical_roots.{name}")
    activation = require_object(contract.get("activation"), "activation")
    if activation.get("scope") != "build-toolchain-only":
        raise ToolchainError("activation.scope must be build-toolchain-only")
    if activation.get("product_runtime_activation_eligible") is not False:
        raise ToolchainError("toolchain package can never claim product-runtime activation")
    if activation.get("consumer_contract") != "selected-tool-paths-and-hashes":
        raise ToolchainError("activation.consumer_contract must expose selected tool paths and hashes")

    environment = require_object(contract.get("environment"), "environment")
    rejected = require_string_array(
        environment.get("rejected_nonempty"), "environment.rejected_nonempty"
    )
    for required in (
        "CC",
        "CXX",
        "LD",
        "AR",
        "MAKEFLAGS",
        "LD_LIBRARY_PATH",
        "LIBRARY_PATH",
        "CPATH",
        "CMAKE_PREFIX_PATH",
        "PKG_CONFIG_PATH",
        "PERL",
        "PERL5LIB",
        "PYTHONPATH",
    ):
        if required not in rejected:
            raise ToolchainError(f"environment.rejected_nonempty must include {required}")
    require_string(environment.get("locale"), "environment.locale")
    epoch = require_string(environment.get("source_date_epoch"), "environment.source_date_epoch")
    if not epoch.isdecimal():
        raise ToolchainError("environment.source_date_epoch must be decimal")

    bootstrap = require_object(contract.get("bootstrap"), "bootstrap")
    if bootstrap.get("classification") != "host-platform-bootstrap":
        raise ToolchainError("bootstrap classification must remain explicit")
    tools = bootstrap.get("tools")
    if not isinstance(tools, list) or not tools:
        raise ToolchainError("bootstrap.tools must be a non-empty array")
    ids: set[str] = set()
    for index, tool_value in enumerate(tools):
        tool = require_object(tool_value, f"bootstrap.tools[{index}]")
        tool_id = require_string(tool.get("id"), f"bootstrap.tools[{index}].id")
        if tool_id in ids:
            raise ToolchainError(f"duplicate bootstrap tool id: {tool_id}")
        ids.add(tool_id)
        path = Path(require_string(tool.get("path"), f"bootstrap.tools[{index}].path"))
        if not path.is_absolute():
            raise ToolchainError(f"bootstrap tool path must be absolute: {path}")
        digest = require_string(tool.get("sha256"), f"bootstrap.tools[{index}].sha256")
        if not HASH_PATTERN.fullmatch(digest):
            raise ToolchainError(f"bootstrap tool digest must be lowercase SHA-256: {tool_id}")
        require_string(tool.get("version_argument"), f"bootstrap.tools[{index}].version_argument")
    for required in (
        "cc",
        "cxx",
        "ld",
        "ar",
        "nm",
        "ranlib",
        "readelf",
        "make",
        "python",
        "sh",
    ):
        if required not in ids:
            raise ToolchainError(f"bootstrap.tools must include {required}")
    system_abi = require_object(bootstrap.get("system_abi"), "bootstrap.system_abi")
    include_roots = system_abi.get("include_roots")
    if not isinstance(include_roots, list) or not include_roots:
        raise ToolchainError("bootstrap.system_abi.include_roots must be a non-empty array")
    for index, root_value in enumerate(include_roots):
        root = require_object(root_value, f"bootstrap.system_abi.include_roots[{index}]")
        path = Path(require_string(root.get("path"), f"include_roots[{index}].path"))
        if not path.is_absolute() or "/usr/local" in str(path):
            raise ToolchainError("system ABI include roots must be absolute and exclude /usr/local")
        digest = require_string(root.get("tree_sha256"), f"include_roots[{index}].tree_sha256")
        if not HASH_PATTERN.fullmatch(digest):
            raise ToolchainError("system ABI include root digest must be lowercase SHA-256")
        for field in ("file_count", "total_file_bytes"):
            if not isinstance(root.get(field), int) or root[field] < 0:
                raise ToolchainError(f"include_roots[{index}].{field} must be nonnegative")
    library_roots = require_string_array(
        system_abi.get("library_search_roots"),
        "bootstrap.system_abi.library_search_roots",
    )
    if any(not Path(path).is_absolute() or "/usr/local" in path for path in library_roots):
        raise ToolchainError("system ABI library roots must be absolute and exclude /usr/local")
    if system_abi.get("library_input_contract") != "resolved-linker-inputs-are-individually-receipted":
        raise ToolchainError("system ABI library inputs must be individually receipted")

    build = require_object(contract.get("build"), "build")
    if build.get("component_order") != EXPECTED_ORDER:
        raise ToolchainError("build.component_order violates the dependency order")
    if not isinstance(build.get("parallel_jobs"), int) or build["parallel_jobs"] < 1:
        raise ToolchainError("build.parallel_jobs must be positive")
    if build.get("directory_mode") != "0700":
        raise ToolchainError("build.directory_mode must be 0700")
    components = require_object(build.get("components"), "build.components")
    if set(components) != set(EXPECTED_ORDER):
        raise ToolchainError("build.components must exactly match component_order")
    declared_tools: set[str] = set()
    for component_id in EXPECTED_ORDER:
        component = require_object(components[component_id], f"build.components.{component_id}")
        if component.get("version") != EXPECTED_VERSIONS[component_id]:
            raise ToolchainError(f"selected version differs for {component_id}")
        if component.get("source_mode") not in (
            "private-copy-out-of-tree",
            "private-copy-in-tree",
        ):
            raise ToolchainError(f"unsupported source mode for {component_id}")
        for step in ("configure", "build", "test", "install"):
            require_string_array(component.get(step), f"build.components.{component_id}.{step}")
        if component["test"] != EXPECTED_TEST_COMMANDS[component_id]:
            raise ToolchainError(f"{component_id}.test must execute the complete selected suite")
        declared_tools.update(
            require_string_array(component.get("tools"), f"build.components.{component_id}.tools")
        )
    missing_tools = sorted(REQUIRED_TOOL_IDS - declared_tools)
    if missing_tools:
        raise ToolchainError(f"toolchain consumer manifest is missing tools: {', '.join(missing_tools)}")
    perl_configure = components["perl"]["configure"]
    for closed_local_path in (
        "-Dlocincpth= ",
        "-Dloclibpth= ",
        "-Dlibpth=/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib /lib",
    ):
        if closed_local_path not in perl_configure:
            raise ToolchainError(
                "Perl configure must use a nonempty blank sentinel to disable local paths"
            )
    if any("/usr/local" in argument for argument in perl_configure):
        raise ToolchainError("Perl configure must not admit /usr/local inputs")
    for include_root in include_roots:
        if not any(f"-isystem {include_root['path']}" in argument for argument in perl_configure):
            raise ToolchainError("Perl configure omits a receipted system ABI include root")
    if not any("-nostdinc" in argument for argument in perl_configure):
        raise ToolchainError("Perl configure must disable compiler default include search")
    for component_id in ("tcl", "expect"):
        configure = components[component_id]["configure"]
        if "--enable-shared" not in configure or "--disable-shared" in configure:
            raise ToolchainError(
                f"{component_id} must use the selected-prefix shared linkage contract"
            )
    binutils = components["gnu-binutils"]
    if "--disable-gprofng" not in binutils["configure"]:
        raise ToolchainError("gnu-binutils must exclude unselected gprofng")
    if binutils.get("feature_selection") != {
        "gprofng": "excluded-not-a-product-tool"
    }:
        raise ToolchainError("gnu-binutils gprofng feature selection must remain explicit")
    if "gprofng" in binutils["tools"]:
        raise ToolchainError("unselected gprofng cannot appear in the toolchain manifest")
    test_policy = require_object(
        binutils.get("test_policy"), "build.components.gnu-binutils.test_policy"
    )
    if test_policy.get("unset_environment") != ["SOURCE_DATE_EPOCH"]:
        raise ToolchainError("binutils tests must run without SOURCE_DATE_EPOCH")
    capabilities = test_policy.get("unselected_optional_capabilities")
    if capabilities != [
        {"id": "llvm-lto-plugin-tests", "commands": ["clang", "llvm-config"]}
    ]:
        raise ToolchainError("binutils optional LLVM test capability must remain unselected")
    if test_policy.get("result_format") != "dejagnu-sum":
        raise ToolchainError("binutils tests must publish DejaGNU summaries")
    if test_policy.get("forbidden_outcomes") != [
        "FAIL",
        "ERROR",
        "UNRESOLVED",
        "XPASS",
    ]:
        raise ToolchainError("binutils DejaGNU failure outcomes must remain fail-closed")
    expected_outcomes = require_expected_outcome_dispositions(
        test_policy, "build.components.gnu-binutils.test_policy"
    )
    if expected_outcomes != {
        "UNTESTED": EXPECTED_BINUTILS_UNTESTED_DISPOSITIONS
    }:
        raise ToolchainError("binutils expected untested outcomes must remain exact")

    receipt = require_object(contract.get("receipt"), "receipt")
    sections = require_string_array(receipt.get("required_sections"), "receipt.required_sections")
    required_sections = {
        "source_inputs",
        "source_normalization",
        "bootstrap_inputs",
        "component_steps",
        "installed_tools",
        "compiler_driver_traces",
        "linker_map_inputs",
        "package_tree",
        "consumer_manifest",
        "activation",
    }
    if set(sections) != required_sections:
        raise ToolchainError("receipt.required_sections must contain the complete receipt closure")
    if receipt.get("record_dynamic_dependencies") is not True:
        raise ToolchainError("receipt must record dynamic dependencies")
    if receipt.get("record_static_link_inputs") is not True:
        raise ToolchainError("receipt must record static linker inputs")


def validate_environment(contract: dict[str, Any], environment: Mapping[str, str]) -> None:
    contaminated = sorted(
        name for name in contract["environment"]["rejected_nonempty"] if environment.get(name)
    )
    if contaminated:
        raise ToolchainError(
            f"ambient tool or loader environment is contaminated: {', '.join(contaminated)}"
        )


def command_version(path: Path, argument: str, operand: str | None = None) -> str:
    command = [str(path), argument]
    if operand is not None:
        command.append(operand)
    result = subprocess.run(command, check=True, text=True, capture_output=True)
    output = (result.stdout + result.stderr).strip()
    return output.splitlines()[0] if output else "(no version output)"


def verify_bootstrap(contract: dict[str, Any]) -> dict[str, Any]:
    receipt: dict[str, Any] = {}
    for tool in contract["bootstrap"]["tools"]:
        path = Path(tool["path"])
        if not path.is_file() or not os.access(path, os.X_OK):
            raise ToolchainError(f"bootstrap tool is not executable: {path}")
        observed = sha256_file(path)
        if observed != tool["sha256"]:
            raise ToolchainError(
                f"bootstrap tool digest mismatch for {tool['id']}: expected {tool['sha256']}, observed {observed}"
            )
        receipt[tool["id"]] = {
            "path": str(path),
            "sha256": observed,
            "version": command_version(path, tool["version_argument"], tool.get("version_operand")),
        }
    system_abi = contract["bootstrap"]["system_abi"]
    include_receipts: list[dict[str, Any]] = []
    for root in system_abi["include_roots"]:
        path = Path(root["path"])
        if not path.is_dir() or path.is_symlink():
            raise ToolchainError(f"system ABI include root is not a directory: {path}")
        observed = package_tree(path)
        expected = {
            "sha256": root["tree_sha256"],
            "file_count": root["file_count"],
            "total_file_bytes": root["total_file_bytes"],
        }
        if observed != expected:
            raise ToolchainError(f"system ABI include root differs from contract: {path}")
        include_receipts.append({"path": str(path), **observed})
    library_receipts = []
    for logical in system_abi["library_search_roots"]:
        path = Path(logical)
        if not path.is_dir():
            raise ToolchainError(f"system ABI library root is not a directory: {path}")
        library_receipts.append({"path": logical, "resolved_path": str(path.resolve())})
    receipt["system_abi"] = {
        "include_roots": include_receipts,
        "library_search_roots": library_receipts,
        "library_input_contract": system_abi["library_input_contract"],
    }
    return receipt


def load_release_module(repository: Path) -> Any:
    path = repository / "tools/dependencies/release-assets.py"
    spec = importlib.util.spec_from_file_location("laplace_release_assets", path)
    if spec is None or spec.loader is None:
        raise ToolchainError(f"cannot load release verifier: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def verify_sources(
    contract: dict[str, Any], repository: Path
) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    release_lock_path = repository / contract["release_lock"]
    release_lock = read_json(release_lock_path)
    entries = require_object(release_lock.get("archives"), "release_lock.archives")
    archive_root = Path(contract["logical_roots"]["archive_root"])
    source_generation = Path(contract["logical_roots"]["source_generation"])
    release = load_release_module(repository)
    selected: dict[str, dict[str, Any]] = {}
    receipt: dict[str, dict[str, Any]] = {}
    for component_id in EXPECTED_ORDER:
        entry = entries.get(component_id)
        if not isinstance(entry, dict):
            raise ToolchainError(f"release lock does not contain selected source: {component_id}")
        if entry.get("version") != EXPECTED_VERSIONS[component_id]:
            raise ToolchainError(f"release lock version differs for {component_id}")
        try:
            verified = release.verify_entry(component_id, entry, archive_root)
            release.verify_imported_entry(
                component_id,
                entry,
                archive_root,
                source_generation,
            )
        except Exception as error:
            raise ToolchainError(f"source verification failed for {component_id}: {error}") from error
        selected[component_id] = entry
        receipt[component_id] = {
            **verified,
            "source_root": str((source_generation / component_id).resolve()),
        }
    return selected, receipt


def recipe_identity(contract: dict[str, Any], repository: Path) -> dict[str, Any]:
    paths = {
        "driver": repository / "tools/toolchain/build-package.py",
        "release_verifier": repository / "tools/dependencies/release-assets.py",
        "release_lock": repository / contract["release_lock"],
    }
    return {
        name: {
            "path": str(path.relative_to(repository)),
            "sha256": sha256_file(path),
        }
        for name, path in paths.items()
    }


def create_plan(contract: dict[str, Any], repository: Path) -> dict[str, Any]:
    validate_contract(contract, repository)
    bootstrap = verify_bootstrap(contract)
    selected, source_receipts = verify_sources(contract, repository)
    recipe = recipe_identity(contract, repository)
    source_normalization = {
        "algorithm": "all-non-symlink-source-objects-utime/v1",
        "source_date_epoch": contract["environment"]["source_date_epoch"],
        "directory_order": "children-before-parent",
        "follow_symlinks": False,
    }
    identity = {
        "contract": contract,
        "bootstrap": bootstrap,
        "sources": {
            component_id: {
                "version": selected[component_id]["version"],
                "archive_sha256": selected[component_id]["sha256"],
                "tree_sha256": selected[component_id]["tree_sha256"],
            }
            for component_id in EXPECTED_ORDER
        },
        "recipe": recipe,
        "source_normalization": source_normalization,
    }
    build_input_id = canonical_sha256(identity)
    roots = contract["logical_roots"]
    build_directory = Path(roots["build_root"]) / build_input_id
    work_directory = Path(roots["work_root"]) / build_input_id
    prefix = Path(roots["stage_root"]) / build_input_id / "toolchain"
    return {
        "schema": PLAN_SCHEMA,
        "build_input_id": build_input_id,
        "repository": str(repository),
        "build_directory": str(build_directory),
        "work_directory": str(work_directory),
        "prefix": str(prefix),
        "component_order": list(EXPECTED_ORDER),
        "source_inputs": source_receipts,
        "source_normalization": source_normalization,
        "bootstrap_inputs": bootstrap,
        "recipe": recipe,
        "parallel_jobs": contract["build"]["parallel_jobs"],
        "activation": dict(contract["activation"]),
    }


def private_directory(path: Path) -> None:
    path.mkdir(parents=True)
    path.chmod(0o700)
    if stat.S_IMODE(path.stat().st_mode) != 0o700:
        raise ToolchainError(f"directory must be private mode 0700: {path}")


def prepare_plan(plan: dict[str, Any], resume: bool) -> tuple[Path, Path, Path]:
    build = Path(plan["build_directory"])
    work = Path(plan["work_directory"])
    prefix = Path(plan["prefix"])
    plan_path = build / "build-plan.json"
    if resume:
        if not plan_path.is_file() or read_json(plan_path) != plan:
            raise ToolchainError("resume requires the exact persisted build plan")
        if not work.is_dir() or not prefix.is_dir():
            raise ToolchainError("resume requires existing work and staged prefix directories")
    else:
        if build.exists() or work.exists() or prefix.exists():
            raise ToolchainError("input-addressed build, work, and stage destinations must be absent")
        private_directory(build)
        private_directory(work)
        private_directory(prefix)
        plan_path.write_text(json.dumps(plan, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return build, work, prefix


def build_environment(
    contract: dict[str, Any], plan: dict[str, Any], component_id: str
) -> dict[str, str]:
    bootstrap = plan["bootstrap_inputs"]
    prefix = Path(plan["prefix"])
    home = Path(plan["build_directory"]) / ".home"
    temporary = Path(plan["work_directory"]) / "tmp"
    home.mkdir(exist_ok=True)
    temporary.mkdir(exist_ok=True)
    home.chmod(0o700)
    temporary.chmod(0o700)
    component_index = EXPECTED_ORDER.index(component_id)
    selected_binutils = component_index > EXPECTED_ORDER.index("gnu-binutils")
    selected_make = component_index > EXPECTED_ORDER.index("gnu-make")
    environment = {
        "HOME": str(home),
        "TMPDIR": str(temporary),
        "LANG": contract["environment"]["locale"],
        "LC_ALL": contract["environment"]["locale"],
        "SOURCE_DATE_EPOCH": contract["environment"]["source_date_epoch"],
        "PATH": f"{prefix / 'bin'}:/usr/bin:/bin",
        "CC": bootstrap["cc"]["path"],
        "CXX": bootstrap["cxx"]["path"],
        "CONFIG_SITE": "/dev/null",
    }
    if selected_binutils:
        for variable, tool in (
            ("LD", "ld"),
            ("AR", "ar"),
            ("AS", "as"),
            ("NM", "nm"),
            ("RANLIB", "ranlib"),
            ("STRIP", "strip"),
            ("OBJCOPY", "objcopy"),
            ("OBJDUMP", "objdump"),
            ("READELF", "readelf"),
        ):
            environment[variable] = str(prefix / "bin" / tool)
        environment["CFLAGS"] = f"-B{prefix / 'bin'}"
        environment["CXXFLAGS"] = f"-B{prefix / 'bin'}"
        environment["LDFLAGS"] = f"-B{prefix / 'bin'}"
    if selected_make:
        environment["MAKE"] = str(prefix / "bin/make")
    if component_index > EXPECTED_ORDER.index("perl"):
        environment["PERL"] = str(prefix / "bin/perl")
    if component_index > EXPECTED_ORDER.index("pkgconf"):
        environment["PKG_CONFIG"] = str(prefix / "bin/pkgconf")
        environment["PKG_CONFIG_LIBDIR"] = f"{prefix / 'lib/pkgconfig'}:{prefix / 'share/pkgconfig'}"
    return environment


def step_environment(
    component: Mapping[str, Any],
    component_id: str,
    step_name: str,
    component_build: Path,
    base: Mapping[str, str],
) -> tuple[dict[str, str], dict[str, Any]]:
    environment = dict(base)
    if step_name != "test" or "test_policy" not in component:
        return environment, {"status": "not-applicable"}

    policy = require_object(component["test_policy"], f"{component_id}.test_policy")
    unset = require_string_array(
        policy.get("unset_environment"), f"{component_id}.test_policy.unset_environment"
    )
    for name in unset:
        environment.pop(name, None)

    rejection_directory = component_build / "unselected-optional-tools"
    rejection_directory.mkdir(mode=0o700)
    rejected: dict[str, dict[str, str]] = {}
    capabilities = policy.get("unselected_optional_capabilities")
    if not isinstance(capabilities, list) or not capabilities:
        raise ToolchainError(f"{component_id} test policy has no optional capability declarations")
    for index, raw_capability in enumerate(capabilities):
        capability = require_object(raw_capability, f"optional capability {index}")
        capability_id = require_string(capability.get("id"), f"optional capability {index}.id")
        commands = require_string_array(
            capability.get("commands"), f"optional capability {capability_id}.commands"
        )
        for command in commands:
            if "/" in command:
                raise ToolchainError(f"optional command must be a basename: {command}")
            path = rejection_directory / command
            payload = (
                "#!/bin/sh\n"
                f"printf '%s\\n' 'Laplace: optional capability {capability_id} is not selected' >&2\n"
                "exit 127\n"
            )
            path.write_text(payload, encoding="utf-8")
            path.chmod(0o700)
            rejected[command] = {
                "capability": capability_id,
                "path": str(path),
                "sha256": sha256_file(path),
            }
    environment["PATH"] = f"{rejection_directory}:{environment['PATH']}"
    for command, receipt in rejected.items():
        selected = shutil.which(command, path=environment["PATH"])
        if selected != receipt["path"]:
            raise ToolchainError(f"ambient optional command escaped rejection: {command}")
    return environment, {
        "status": "verified",
        "unset_environment": unset,
        "unselected_optional_commands": rejected,
    }


def verify_dejagnu_results(
    component_id: str, component_build: Path, test_policy: Mapping[str, Any]
) -> dict[str, Any]:
    if test_policy.get("result_format") != "dejagnu-sum":
        return {"status": "not-applicable"}
    summary_paths = sorted(component_build.rglob("*.sum"))
    if not summary_paths:
        raise ToolchainError(f"{component_id} produced no DejaGNU summary files")
    counts = {
        outcome: 0
        for outcome in (
            "PASS",
            "FAIL",
            "ERROR",
            "UNRESOLVED",
            "XPASS",
            "XFAIL",
            "UNSUPPORTED",
            "UNTESTED",
        )
    }
    files: list[dict[str, Any]] = []
    findings: list[dict[str, str]] = []
    outcome_lines: dict[str, list[str]] = {outcome: [] for outcome in counts}
    forbidden = set(
        require_string_array(
            test_policy.get("forbidden_outcomes"),
            f"{component_id}.test_policy.forbidden_outcomes",
        )
    )
    for path in summary_paths:
        relative = str(path.relative_to(component_build))
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = DEJAGNU_OUTCOME_PATTERN.match(line)
            if match is None:
                continue
            outcome = match.group(1)
            counts[outcome] += 1
            outcome_lines[outcome].append(line)
            if outcome in forbidden:
                findings.append({"file": relative, "outcome": outcome, "line": line})
        files.append({"path": relative, "sha256": sha256_file(path)})
    if findings:
        first = findings[0]
        raise ToolchainError(
            f"{component_id} DejaGNU summary contains {len(findings)} forbidden outcomes; "
            f"first is {first['file']}: {first['line']}"
        )
    expected_outcomes = require_expected_outcome_dispositions(
        test_policy, f"{component_id}.test_policy"
    )
    adjudications: dict[str, list[dict[str, Any]]] = {}
    for outcome, entries in expected_outcomes.items():
        expected = [require_string(entry.get("line"), "expected outcome line") for entry in entries]
        observed = outcome_lines.get(outcome)
        if observed is None or sorted(observed) != sorted(expected):
            raise ToolchainError(
                f"{component_id} DejaGNU {outcome} outcomes differ from the selected capability contract"
            )
        outcome_adjudications: list[dict[str, Any]] = []
        for entry in entries:
            adjudication = {
                key: entry[key]
                for key in ("line", "disposition", "reason", "capability_id")
                if key in entry
            }
            evidence_receipts: list[dict[str, str]] = []
            for raw_predicate in entry.get("evidence_predicates", []):
                predicate = require_object(raw_predicate, "evidence predicate")
                relative_path = Path(
                    require_string(predicate.get("relative_path"), "evidence relative_path")
                )
                evidence_path = component_build / relative_path
                if not evidence_path.is_file():
                    raise ToolchainError(
                        f"{component_id} outcome evidence file is absent: {relative_path}"
                    )
                needle = require_string(predicate.get("contains"), "evidence contains")
                evidence_text = evidence_path.read_text(encoding="utf-8", errors="replace")
                start = evidence_text.find(needle)
                if start < 0:
                    raise ToolchainError(
                        f"{component_id} outcome evidence predicate is unsatisfied: "
                        f"{predicate['id']}"
                    )
                evidence_receipt = {
                    "id": require_string(predicate.get("id"), "evidence predicate id"),
                    "path": str(relative_path),
                    "sha256": sha256_file(evidence_path),
                    "contains": needle,
                }
                if "until_contains" in predicate:
                    until = require_string(
                        predicate.get("until_contains"), "evidence until_contains"
                    )
                    end = evidence_text.find(until, start + len(needle))
                    forbidden = require_string(
                        predicate.get("forbidden_before"), "evidence forbidden_before"
                    )
                    if end < 0 or forbidden in evidence_text[start:end]:
                        raise ToolchainError(
                            f"{component_id} outcome evidence predicate is unsatisfied: "
                            f"{predicate['id']}"
                        )
                    evidence_receipt["until_contains"] = until
                    evidence_receipt["forbidden_before"] = forbidden
                evidence_receipts.append(evidence_receipt)
            if evidence_receipts:
                adjudication["evidence"] = evidence_receipts
            outcome_adjudications.append(adjudication)
        adjudications[outcome] = outcome_adjudications
    return {
        "status": "verified",
        "format": "dejagnu-sum",
        "counts": counts,
        "expected_outcomes": expected_outcomes,
        "adjudications": adjudications,
        "summary_files": files,
    }


def format_command(
    template: list[str],
    source: Path,
    build: Path,
    prefix: Path,
    jobs: int,
    bootstrap: dict[str, dict[str, str]],
) -> list[str]:
    replacements = {
        "source": str(source),
        "build": str(build),
        "prefix": str(prefix),
        "jobs": str(jobs),
        "make": str(prefix / "bin/make") if (prefix / "bin/make").is_file() else "/usr/bin/make",
        "cmake": str(prefix / "bin/cmake"),
        "ctest": str(prefix / "bin/ctest"),
        "cc": bootstrap["cc"]["path"],
        "cxx": bootstrap["cxx"]["path"],
        "ld": bootstrap["ld"]["path"],
        "ar": bootstrap["ar"]["path"],
        "ranlib": bootstrap["ranlib"]["path"],
        "nm": bootstrap["nm"]["path"],
    }
    return [argument.format(**replacements) for argument in template]


def run_logged(
    command: Sequence[str], cwd: Path, environment: Mapping[str, str], log_path: Path
) -> dict[str, Any]:
    working_directory = str(cwd)
    supplied_pwd = environment.get("PWD")
    if supplied_pwd is not None and supplied_pwd != working_directory:
        raise ToolchainError(
            "execution environment PWD differs from the exact working directory"
        )
    child_environment = dict(environment)
    child_environment["PWD"] = working_directory
    with log_path.open("wb") as log:
        header = f"$ {json.dumps(list(command), separators=(',', ':'))}\n".encode("utf-8")
        log.write(header)
        log.flush()
        process = subprocess.Popen(
            list(command),
            cwd=cwd,
            env=child_environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        assert process.stdout is not None
        with process.stdout:
            while block := process.stdout.read(64 * 1024):
                sys.stdout.buffer.write(block)
                sys.stdout.buffer.flush()
                log.write(block)
        return_code = process.wait()
    result = {
        "command": list(command),
        "working_directory": working_directory,
        "execution_environment": {"PWD": working_directory},
        "exit_code": return_code,
        "log_path": str(log_path),
        "log_sha256": sha256_file(log_path),
    }
    if return_code != 0:
        raise ToolchainError(f"command exited {return_code}: {command[0]}")
    return result


def component_source(
    contract: dict[str, Any], repository: Path, component_id: str, work: Path
) -> Path:
    release_lock = read_json(repository / contract["release_lock"])
    entry = release_lock.get("archives", {}).get(component_id)
    if not isinstance(entry, dict):
        raise ToolchainError(f"release lock does not contain {component_id}")
    archive_root = Path(contract["logical_roots"]["archive_root"])
    release = load_release_module(repository)
    private = work / "private-sources" / component_id
    if not private.exists():
        private.parent.mkdir(parents=True, exist_ok=True)
        try:
            release.import_entry(component_id, entry, archive_root, private.parent)
            release.verify_imported_entry(
                component_id, entry, archive_root, private.parent
            )
            normalize_source_timestamps(
                private, int(contract["environment"]["source_date_epoch"])
            )
        except Exception as error:
            if private.exists():
                shutil.rmtree(private)
            raise ToolchainError(
                f"private source import failed for {component_id}: {error}"
            ) from error
    return private


def normalize_source_timestamps(source: Path, epoch: int) -> int:
    """Normalize dependency-sensitive source mtimes without dereferencing links."""
    if not source.is_dir() or source.is_symlink():
        raise ToolchainError(f"private source root is not a real directory: {source}")
    normalized = 0
    for root_value, _directories, files in os.walk(source, topdown=False, followlinks=False):
        root = Path(root_value)
        for name in files:
            path = root / name
            if path.is_symlink():
                continue
            os.utime(path, (epoch, epoch), follow_symlinks=False)
            normalized += 1
        os.utime(root, (epoch, epoch), follow_symlinks=False)
        normalized += 1
    return normalized


def verify_normalized_source_timestamps(source: Path, epoch: int) -> int:
    expected = epoch * 1_000_000_000
    observed = 0
    for root_value, _directories, files in os.walk(source, topdown=False, followlinks=False):
        root = Path(root_value)
        for name in files:
            path = root / name
            if path.is_symlink():
                continue
            if path.stat().st_mtime_ns != expected:
                raise ToolchainError(f"private source timestamp is not normalized: {path}")
            observed += 1
        if root.stat().st_mtime_ns != expected:
            raise ToolchainError(f"private source timestamp is not normalized: {root}")
        observed += 1
    return observed


def verify_component_configuration(component_id: str, source: Path) -> dict[str, Any]:
    if component_id != "perl":
        return {"status": "not-applicable"}
    config = source / "config.sh"
    if not config.is_file():
        raise ToolchainError("Perl configure did not produce config.sh")
    values: dict[str, str] = {}
    required = {
        "locincpth",
        "loclibpth",
        "glibpth",
        "libpth",
        "ccflags",
        "cppflags",
        "ldflags",
    }
    for line in config.read_text(encoding="utf-8").splitlines():
        key, separator, raw = line.partition("=")
        if separator and key in required and len(raw) >= 2 and raw[0] == raw[-1] == "'":
            values[key] = raw[1:-1]
    if set(values) != required:
        raise ToolchainError("Perl config.sh omits required dependency-boundary fields")
    expected_paths = "/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib /lib"
    if values["locincpth"] != " " or values["loclibpth"] != " ":
        raise ToolchainError("Perl configured local include/library paths are not closed")
    if values["glibpth"] != expected_paths or values["libpth"] != expected_paths:
        raise ToolchainError("Perl configured system ABI library paths differ from contract")
    if any("/usr/local" in value for value in values.values()):
        raise ToolchainError("Perl configured output admitted /usr/local")
    for include_root in (
        "/usr/lib/gcc/x86_64-linux-gnu/11/include",
        "/usr/include/x86_64-linux-gnu",
        "/usr/include",
    ):
        if f"-isystem {include_root}" not in values["ccflags"]:
            raise ToolchainError("Perl configured output omits system ABI include root")
    if "-nostdinc" not in values["ccflags"] or "-nostdinc" not in values["cppflags"]:
        raise ToolchainError("Perl configured output admits compiler default include search")
    return {"status": "verified", "fields": values}


def verify_component_prerequisites(component_id: str, prefix: Path) -> dict[str, Any]:
    if component_id != "gnu-binutils":
        return {"status": "not-applicable"}
    tools: dict[str, dict[str, str]] = {}
    for tool_id in ("expect", "runtest"):
        path = prefix / "bin" / tool_id
        if not path.is_file() or not os.access(path, os.X_OK):
            raise ToolchainError(
                f"binutils upstream suite requires selected {tool_id}: {path}"
            )
        tools[tool_id] = {
            "path": str(path),
            "sha256": sha256_file(path.resolve()),
            "version": tool_version(tool_id, path),
        }
    return {"status": "verified", "tools": tools}


def verify_component_configure_log(component_id: str, log_path: Path) -> dict[str, Any]:
    if component_id != "gnu-binutils":
        return {"status": "not-applicable"}
    text = log_path.read_text(encoding="utf-8", errors="replace")
    findings: dict[str, str] = {}
    for tool_id in ("expect", "runtest"):
        prefix = f"checking for {tool_id}... "
        lines = [line for line in text.splitlines() if line.startswith(prefix)]
        if not lines:
            raise ToolchainError(f"binutils configure omitted the {tool_id} discovery result")
        result = lines[-1][len(prefix) :]
        if result == "no":
            raise ToolchainError(f"binutils configure did not select {tool_id}")
        findings[tool_id] = result
    return {"status": "verified", "selected_test_tools": findings}


def verify_private_source_copy(
    contract: dict[str, Any], repository: Path, component_id: str, copy_parent: Path
) -> None:
    release_lock = read_json(repository / contract["release_lock"])
    entry = release_lock.get("archives", {}).get(component_id)
    if not isinstance(entry, dict):
        raise ToolchainError(f"release lock does not contain {component_id}")
    release = load_release_module(repository)
    try:
        release.verify_imported_entry(
            component_id,
            entry,
            Path(contract["logical_roots"]["archive_root"]),
            copy_parent,
        )
    except Exception as error:
        raise ToolchainError(f"private source verification failed for {component_id}: {error}") from error


def tool_version(tool_id: str, path: Path) -> str:
    if tool_id == "tclsh8.6":
        result = subprocess.run(
            [str(path)], input="puts [info patchlevel]\n", text=True, capture_output=True
        )
    elif tool_id == "expect":
        result = subprocess.run([str(path), "-v"], text=True, capture_output=True)
    else:
        result = subprocess.run([str(path), "--version"], text=True, capture_output=True)
    if result.returncode != 0:
        raise ToolchainError(f"installed tool version probe failed: {path}")
    output = (result.stdout + result.stderr).strip()
    return output.splitlines()[0] if output else "(no version output)"


def installed_tool_receipts(contract: dict[str, Any], prefix: Path) -> dict[str, dict[str, str]]:
    tools: dict[str, dict[str, str]] = {}
    for component_id in EXPECTED_ORDER:
        for tool_id in contract["build"]["components"][component_id]["tools"]:
            path = prefix / "bin" / tool_id
            if not path.is_file() or not os.access(path, os.X_OK):
                raise ToolchainError(f"installed tool is missing or not executable: {path}")
            resolved = path.resolve()
            tools[tool_id] = {
                "path": str(path),
                "sha256": sha256_file(resolved),
                "version": tool_version(tool_id, path),
            }
    missing = sorted(REQUIRED_TOOL_IDS - set(tools))
    if missing:
        raise ToolchainError(f"installed receipt omits tools: {', '.join(missing)}")
    return dict(sorted(tools.items()))


def absolute_existing_inputs(text: str) -> list[dict[str, Any]]:
    paths: set[Path] = set()
    for match in ABSOLUTE_PATH_PATTERN.findall(text):
        candidate = Path(match.rstrip("':\"),;"))
        if candidate.is_file():
            paths.add(candidate.resolve())
    return [
        {"path": str(path), "sha256": sha256_file(path), "size_bytes": path.stat().st_size}
        for path in sorted(paths)
    ]


def elf_needed(readelf: Path, binary: Path) -> list[str]:
    return elf_dynamic(readelf, binary)["needed"]


def elf_dynamic(readelf: Path, binary: Path) -> dict[str, Any]:
    result = subprocess.run([str(readelf), "-d", str(binary)], text=True, capture_output=True)
    if result.returncode != 0:
        raise ToolchainError(f"readelf failed for {binary}: {result.stderr.strip()}")
    runpaths: list[str] = []
    for value in RUNPATH_PATTERN.findall(result.stdout):
        runpaths.extend(item for item in value.split(":") if item)
    return {
        "path": str(binary),
        "sha256": sha256_file(binary.resolve()),
        "needed": sorted(set(NEEDED_PATTERN.findall(result.stdout))),
        "runpaths": runpaths,
    }


def verify_dynamic_linkage(
    receipt: dict[str, Any],
    prefix: Path,
    required_providers: Mapping[str, Path],
    expected_runpaths: list[Path],
) -> dict[str, Any]:
    missing = sorted(set(required_providers) - set(receipt["needed"]))
    if missing:
        raise ToolchainError(
            f"selected-prefix dynamic linkage omits: {', '.join(missing)}"
        )
    observed_runpaths = receipt["runpaths"]
    expected = [str(path) for path in expected_runpaths]
    if observed_runpaths != expected:
        raise ToolchainError(
            f"selected-prefix RUNPATH differs: expected {expected}, observed {observed_runpaths}"
        )
    providers: dict[str, dict[str, Any]] = {}
    for needed, provider in required_providers.items():
        resolved = provider.resolve()
        try:
            resolved.relative_to(prefix.resolve())
        except ValueError as error:
            raise ToolchainError(f"dynamic provider escapes selected prefix: {provider}") from error
        if not provider.is_file():
            raise ToolchainError(f"selected dynamic provider is missing: {provider}")
        providers[needed] = {
            "path": str(provider),
            "resolved_path": str(resolved),
            "sha256": sha256_file(resolved),
        }
    return {**receipt, "providers": providers}


def probe_installed_tool(
    command: Sequence[str], home: Path, standard_input: str | None = None
) -> str:
    environment = {
        "HOME": str(home),
        "PATH": "/usr/bin:/bin",
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
    }
    result = subprocess.run(
        list(command),
        input=standard_input,
        text=True,
        capture_output=True,
        env=environment,
    )
    if result.returncode != 0:
        raise ToolchainError(
            f"installed tool failed without loader overrides: {command[0]}: "
            f"{(result.stdout + result.stderr).strip()}"
        )
    return (result.stdout + result.stderr).strip()


def verify_component_installation(
    component_id: str,
    prefix: Path,
    bootstrap: Mapping[str, Mapping[str, str]],
    home: Path,
) -> dict[str, Any]:
    if component_id == "gnu-binutils":
        forbidden = [prefix / "bin/gprofng", prefix / "bin/gp-display-html"]
        present = [str(path) for path in forbidden if path.exists()]
        if present:
            raise ToolchainError(
                f"unselected gprofng artifacts entered the package: {', '.join(present)}"
            )
        return {
            "status": "verified",
            "excluded_features": {"gprofng": "absent"},
        }
    if component_id not in ("tcl", "expect"):
        return {"status": "not-applicable"}
    readelf = Path(bootstrap["readelf"]["path"])
    library_root = prefix / "lib"
    if component_id == "tcl":
        binary = prefix / "bin/tclsh8.6"
        provider = library_root / "libtcl8.6.so"
        linkage = verify_dynamic_linkage(
            elf_dynamic(readelf, binary),
            prefix,
            {"libtcl8.6.so": provider},
            [library_root],
        )
        version = probe_installed_tool(
            [str(binary)], home, "puts [info patchlevel]\n"
        )
        if version != EXPECTED_VERSIONS["tcl"]:
            raise ToolchainError(f"selected Tcl probe returned unexpected version: {version}")
        return {"status": "verified", "version": version, "linkage": linkage}

    binary = prefix / "bin/expect"
    expect_root = library_root / f"expect{EXPECTED_VERSIONS['expect']}"
    expect_provider = expect_root / f"libexpect{EXPECTED_VERSIONS['expect']}.so"
    tcl_provider = library_root / "libtcl8.6.so"
    linkage = verify_dynamic_linkage(
        elf_dynamic(readelf, binary),
        prefix,
        {
            f"libexpect{EXPECTED_VERSIONS['expect']}.so": expect_provider,
            "libtcl8.6.so": tcl_provider,
        },
        [library_root, expect_root],
    )
    library_linkage = verify_dynamic_linkage(
        elf_dynamic(readelf, expect_provider),
        prefix,
        {},
        [library_root],
    )
    version = probe_installed_tool([str(binary), "-v"], home)
    if EXPECTED_VERSIONS["expect"] not in version:
        raise ToolchainError(f"selected Expect probe returned unexpected version: {version}")
    return {
        "status": "verified",
        "version": version,
        "linkage": linkage,
        "library_linkage": library_linkage,
    }


def compiler_receipts(
    plan: dict[str, Any], prefix: Path, environment: Mapping[str, str]
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    receipt_root = Path(plan["build_directory"]) / "compiler-receipt"
    receipt_root.mkdir()
    compilers = {
        "c": (Path(plan["bootstrap_inputs"]["cc"]["path"]), ".c", "int main(void){return 0;}\n"),
        "cxx": (
            Path(plan["bootstrap_inputs"]["cxx"]["path"]),
            ".cpp",
            "int main(){return 0;}\n",
        ),
    }
    traces: dict[str, Any] = {}
    linker_inputs: dict[str, dict[str, Any]] = {}
    readelf = prefix / "bin/readelf"
    for language, (compiler, suffix, content) in compilers.items():
        source = receipt_root / f"probe{suffix}"
        output = receipt_root / f"probe-{language}"
        map_path = receipt_root / f"probe-{language}.map"
        source.write_text(content, encoding="utf-8")
        common = [str(compiler), f"-B{prefix / 'bin'}", str(source), "-o", str(output)]
        trace_process = subprocess.run(
            [*common, "-###"], env=dict(environment), text=True, capture_output=True
        )
        if trace_process.returncode != 0:
            raise ToolchainError(f"{language} compiler driver trace failed")
        trace_text = trace_process.stdout + trace_process.stderr
        compile_process = subprocess.run(
            [*common, f"-Wl,-Map={map_path}"],
            env=dict(environment),
            text=True,
            capture_output=True,
        )
        if compile_process.returncode != 0:
            raise ToolchainError(
                f"{language} compiler probe failed: {compile_process.stderr.strip()}"
            )
        if not map_path.is_file() or not output.is_file():
            raise ToolchainError(f"{language} compiler probe omitted link map or executable")
        link_map = map_path.read_text(encoding="utf-8", errors="replace")
        inputs = absolute_existing_inputs(trace_text + "\n" + link_map)
        if not inputs or not any(item["path"].endswith(".a") for item in inputs):
            raise ToolchainError(f"{language} compiler receipt omitted static linker inputs")
        for item in inputs:
            linker_inputs[item["path"]] = item
        linker = subprocess.run(
            [str(compiler), f"-B{prefix / 'bin'}", "-print-prog-name=ld"],
            env=dict(environment),
            check=True,
            text=True,
            capture_output=True,
        ).stdout.strip()
        linker_path = Path(linker)
        if not linker_path.is_absolute():
            found = shutil.which(linker, path=environment["PATH"])
            if found is None:
                raise ToolchainError(f"compiler-selected linker cannot be resolved: {linker}")
            linker_path = Path(found)
        linker_path = linker_path.resolve()
        traces[language] = {
            "compiler": str(compiler),
            "compiler_sha256": sha256_file(compiler),
            "driver_trace": trace_text,
            "driver_trace_sha256": hashlib.sha256(trace_text.encode("utf-8")).hexdigest(),
            "selected_linker": str(linker_path),
            "selected_linker_sha256": sha256_file(linker_path),
            "link_map_path": str(map_path),
            "link_map_sha256": sha256_file(map_path),
            "probe_path": str(output),
            "probe_sha256": sha256_file(output),
            "direct_elf_needed": elf_needed(readelf, output),
            "input_paths": [item["path"] for item in inputs],
        }
    return traces, [linker_inputs[path] for path in sorted(linker_inputs)]


def package_tree(prefix: Path, excluded: set[Path] | None = None) -> dict[str, Any]:
    digest = hashlib.sha256()
    file_count = 0
    total_bytes = 0
    excluded_resolved = {path.resolve() for path in (excluded or set())}
    for path in sorted(prefix.rglob("*"), key=lambda item: item.relative_to(prefix).as_posix()):
        if path.resolve() in excluded_resolved:
            continue
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
        elif path.is_dir():
            kind = b"directory"
            content = b""
        else:
            raise ToolchainError(f"package contains unsupported filesystem object: {path}")
        for field in (relative, kind, str(mode).encode("ascii"), content):
            digest.update(len(field).to_bytes(8, "big"))
            digest.update(field)
    return {
        "sha256": digest.hexdigest(),
        "file_count": file_count,
        "total_file_bytes": total_bytes,
    }


def write_consumer_manifest(
    plan: dict[str, Any], prefix: Path, tools: dict[str, dict[str, str]]
) -> tuple[Path, dict[str, Any]]:
    manifest = {
        "schema": CONSUMER_SCHEMA,
        "build_input_id": plan["build_input_id"],
        "prefix": str(prefix),
        "tools": tools,
        "activation": {
            "scope": "build-toolchain-only",
            "product_runtime_activation_eligible": False,
        },
    }
    path = prefix / "share/laplace/toolchain-manifest.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path, manifest


def execute_plan(
    contract: dict[str, Any], plan: dict[str, Any], resume: bool = False
) -> dict[str, Any]:
    build_root, work_root, prefix = prepare_plan(plan, resume)
    checkpoint_path = build_root / "completed-components.json"
    completed: list[str] = []
    if resume and checkpoint_path.is_file():
        checkpoint = read_json(checkpoint_path)
        if checkpoint.get("build_input_id") != plan["build_input_id"]:
            raise ToolchainError("resume checkpoint has a different build input identity")
        completed = checkpoint.get("completed", [])
        if not isinstance(completed, list) or completed != EXPECTED_ORDER[: len(completed)]:
            raise ToolchainError("resume checkpoint is not a valid dependency prefix")
    component_steps: dict[str, list[dict[str, Any]]] = {}
    source_normalization: dict[str, dict[str, Any]] = {}
    for component_id in EXPECTED_ORDER:
        if component_id in completed:
            continue
        component = contract["build"]["components"][component_id]
        component_build = build_root / "components" / component_id
        private_directory(component_build)
        prerequisite_receipt = verify_component_prerequisites(component_id, prefix)
        source = component_source(
            contract, Path(plan["repository"]), component_id, work_root
        )
        verify_private_source_copy(contract, Path(plan["repository"]), component_id, source.parent)
        source_normalization[component_id] = {
            "source_date_epoch": plan["source_normalization"]["source_date_epoch"],
            "normalized_object_count": verify_normalized_source_timestamps(
                source, int(plan["source_normalization"]["source_date_epoch"])
            ),
        }
        environment = build_environment(contract, plan, component_id)
        working_directory = component_working_directory(
            component_id,
            component["source_mode"],
            source,
            component_build,
            Path(plan["repository"]),
        )
        steps: list[dict[str, Any]] = []
        for step_name in ("configure", "build", "test", "install"):
            command = format_command(
                component[step_name],
                source,
                component_build,
                prefix,
                plan["parallel_jobs"],
                plan["bootstrap_inputs"],
            )
            step_environment_value, environment_contract = step_environment(
                component,
                component_id,
                step_name,
                component_build,
                environment,
            )
            step_receipt = run_logged(
                command,
                working_directory,
                step_environment_value,
                component_build / f"{step_name}.log",
            )
            if step_name == "configure":
                step_receipt["configuration_contract"] = verify_component_configuration(
                    component_id, source
                )
                step_receipt["prerequisite_contract"] = prerequisite_receipt
                step_receipt["configure_log_contract"] = verify_component_configure_log(
                    component_id, Path(step_receipt["log_path"])
                )
            if step_name == "test":
                step_receipt["environment_contract"] = environment_contract
                if "test_policy" in component:
                    step_receipt["test_result_contract"] = verify_dejagnu_results(
                        component_id,
                        component_build,
                        require_object(component["test_policy"], "test_policy"),
                    )
            if step_name == "install":
                step_receipt["installation_contract"] = verify_component_installation(
                    component_id,
                    prefix,
                    plan["bootstrap_inputs"],
                    Path(plan["build_directory"]) / ".home",
                )
            steps.append(step_receipt)
        component_steps[component_id] = steps
        completed.append(component_id)
        checkpoint_path.write_text(
            json.dumps(
                {"build_input_id": plan["build_input_id"], "completed": completed},
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )

    _, source_generation_after = verify_sources(
        contract, Path(plan["repository"])
    )
    if source_generation_after != plan["source_inputs"]:
        raise ToolchainError("canonical source generation changed during the build")
    tools = installed_tool_receipts(contract, prefix)
    environment = build_environment(contract, plan, "ninja")
    compiler_traces, linker_inputs = compiler_receipts(plan, prefix, environment)
    manifest_path, manifest = write_consumer_manifest(plan, prefix, tools)
    tree = package_tree(prefix)
    receipt = {
        "schema": PACKAGE_SCHEMA,
        "build_input_id": plan["build_input_id"],
        "build_plan_sha256": canonical_sha256(plan),
        "source_inputs": plan["source_inputs"],
        "source_normalization": {
            "recipe": plan["source_normalization"],
            "components": source_normalization,
        },
        "source_generation_after": source_generation_after,
        "bootstrap_inputs": plan["bootstrap_inputs"],
        "component_steps": component_steps,
        "installed_tools": tools,
        "compiler_driver_traces": compiler_traces,
        "linker_map_inputs": linker_inputs,
        "package_tree": tree,
        "package": {
            "prefix": str(prefix),
            "consumer_manifest_path": str(manifest_path),
            "consumer_manifest_sha256": sha256_file(manifest_path),
        },
        "consumer_manifest": manifest,
        "activation": {
            "scope": "build-toolchain-only",
            "product_runtime_activation_eligible": False,
            "disposition": "selected build inputs only; never product runtime activation",
        },
    }
    receipt_path = build_root / "toolchain-package-receipt.json"
    receipt_path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    verify_package(contract, prefix, receipt)
    return receipt


def verify_consumer_manifest(manifest: dict[str, Any], prefix: Path) -> None:
    if manifest.get("schema") != CONSUMER_SCHEMA:
        raise ToolchainError(f"consumer manifest schema must be {CONSUMER_SCHEMA}")
    if Path(require_string(manifest.get("prefix"), "consumer_manifest.prefix")) != prefix:
        raise ToolchainError("consumer manifest prefix differs from package prefix")
    activation = require_object(manifest.get("activation"), "consumer_manifest.activation")
    if activation.get("scope") != "build-toolchain-only" or activation.get(
        "product_runtime_activation_eligible"
    ) is not False:
        raise ToolchainError("consumer manifest illegally claims product-runtime activation")
    tools = require_object(manifest.get("tools"), "consumer_manifest.tools")
    missing = sorted(REQUIRED_TOOL_IDS - set(tools))
    if missing:
        raise ToolchainError(f"consumer manifest omits required tools: {', '.join(missing)}")
    for tool_id, value in tools.items():
        tool = require_object(value, f"consumer_manifest.tools.{tool_id}")
        path = Path(require_string(tool.get("path"), f"consumer_manifest.tools.{tool_id}.path"))
        try:
            path.relative_to(prefix)
        except ValueError as error:
            raise ToolchainError(f"consumer tool escapes package prefix: {tool_id}") from error
        if not path.is_file() or sha256_file(path.resolve()) != tool.get("sha256"):
            raise ToolchainError(f"consumer tool path or digest mismatch: {tool_id}")
        require_string(tool.get("version"), f"consumer_manifest.tools.{tool_id}.version")


def verify_package(
    contract: dict[str, Any], prefix: Path, receipt: dict[str, Any] | None = None
) -> dict[str, Any]:
    validate_contract(contract)
    prefix = prefix.resolve()
    manifest_path = prefix / "share/laplace/toolchain-manifest.json"
    manifest = read_json(manifest_path)
    verify_consumer_manifest(manifest, prefix)
    observed_tree = package_tree(prefix)
    result = {
        "schema": "laplace.toolchain-package-verification/v1",
        "prefix": str(prefix),
        "build_input_id": manifest["build_input_id"],
        "consumer_manifest_sha256": sha256_file(manifest_path),
        "package_tree": observed_tree,
        "product_runtime_activation_eligible": False,
        "status": "verified-build-toolchain-only",
    }
    if receipt is not None:
        if receipt.get("schema") != PACKAGE_SCHEMA:
            raise ToolchainError(f"package receipt schema must be {PACKAGE_SCHEMA}")
        if receipt.get("build_input_id") != manifest.get("build_input_id"):
            raise ToolchainError("package receipt and consumer manifest identities differ")
        if receipt.get("package_tree") != observed_tree:
            raise ToolchainError("package tree differs from its build receipt")
        activation = receipt.get("activation", {})
        if activation.get("product_runtime_activation_eligible") is not False:
            raise ToolchainError("package receipt illegally claims product-runtime activation")
        if not receipt.get("compiler_driver_traces") or not receipt.get("linker_map_inputs"):
            raise ToolchainError("package receipt omits compiler driver or linker/static inputs")
    return result


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=".")
    parser.add_argument("--contract", default="contracts/toolchain-build.json")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("validate-contract")
    subparsers.add_parser("plan")
    subparsers.add_parser("build")
    subparsers.add_parser("resume")
    verify = subparsers.add_parser("verify-package")
    verify.add_argument("--prefix", required=True)
    verify.add_argument("--receipt")
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_arguments(argv)
    repository = Path(arguments.repository).resolve()
    contract_path = Path(arguments.contract)
    if not contract_path.is_absolute():
        contract_path = repository / contract_path
    contract = read_json(contract_path)
    validate_contract(contract, repository)
    if arguments.command == "validate-contract":
        verify_sources(contract, repository)
        print(json.dumps({"schema": CONTRACT_SCHEMA, "status": "valid"}, sort_keys=True))
        return 0
    if arguments.command in ("plan", "build", "resume"):
        validate_environment(contract, os.environ)
        plan = create_plan(contract, repository)
        if arguments.command == "plan":
            print(json.dumps(plan, indent=2, sort_keys=True))
        else:
            receipt = execute_plan(contract, plan, resume=arguments.command == "resume")
            print(json.dumps({"plan": plan, "package": receipt}, indent=2, sort_keys=True))
        return 0
    if arguments.command == "verify-package":
        receipt = read_json(Path(arguments.receipt)) if arguments.receipt else None
        print(
            json.dumps(
                verify_package(contract, Path(arguments.prefix), receipt),
                indent=2,
                sort_keys=True,
            )
        )
        return 0
    raise ToolchainError(f"unknown command: {arguments.command}")


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (ToolchainError, subprocess.CalledProcessError) as error:
        print(f"toolchain-build: {error}", file=sys.stderr)
        raise SystemExit(1) from error
