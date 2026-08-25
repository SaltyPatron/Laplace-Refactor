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
    "gnu-make",
    "perl",
    "texinfo",
    "gnu-binutils",
    "pkgconf",
    "gnu-bison",
    "flex",
    "cmake",
    "ninja",
]
EXPECTED_VERSIONS = {
    "gnu-make": "4.4.1",
    "perl": "5.44.0",
    "texinfo": "7.3",
    "gnu-binutils": "2.47",
    "pkgconf": "3.0.6",
    "gnu-bison": "3.8.2",
    "flex": "2.6.4",
    "cmake": "4.4.2",
    "ninja": "1.13.2",
}
REQUIRED_TOOL_IDS = {
    "ar",
    "as",
    "bison",
    "cmake",
    "cpack",
    "ctest",
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
    "strip",
}
HASH_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ABSOLUTE_PATH_PATTERN = re.compile(r"(?<![A-Za-z0-9_.-])(/[A-Za-z0-9_+.,:@%/=-]+)")
NEEDED_PATTERN = re.compile(r"Shared library: \[([^]]+)\]")


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


def ensure_external(path: Path, repository: Path, name: str) -> Path:
    resolved = path.resolve()
    try:
        resolved.relative_to(repository.resolve())
    except ValueError:
        return resolved
    raise ToolchainError(f"{name} must be outside the repository: {resolved}")


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
    for required in ("cc", "cxx", "ld", "ar", "ranlib", "make", "python", "sh"):
        if required not in ids:
            raise ToolchainError(f"bootstrap.tools must include {required}")

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
            "immutable-out-of-tree",
            "private-copy-in-tree",
        ):
            raise ToolchainError(f"unsupported source mode for {component_id}")
        for step in ("configure", "build", "test", "install"):
            require_string_array(component.get(step), f"build.components.{component_id}.{step}")
        declared_tools.update(
            require_string_array(component.get("tools"), f"build.components.{component_id}.tools")
        )
    missing_tools = sorted(REQUIRED_TOOL_IDS - declared_tools)
    if missing_tools:
        raise ToolchainError(f"toolchain consumer manifest is missing tools: {', '.join(missing_tools)}")

    receipt = require_object(contract.get("receipt"), "receipt")
    sections = require_string_array(receipt.get("required_sections"), "receipt.required_sections")
    required_sections = {
        "source_inputs",
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


def verify_bootstrap(contract: dict[str, Any]) -> dict[str, dict[str, str]]:
    receipt: dict[str, dict[str, str]] = {}
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
    }
    build_input_id = canonical_sha256(identity)
    roots = contract["logical_roots"]
    build_directory = Path(roots["build_root"]) / build_input_id
    work_directory = Path(roots["work_root"]) / build_input_id
    prefix = Path(roots["stage_root"]) / build_input_id / "toolchain"
    return {
        "schema": PLAN_SCHEMA,
        "build_input_id": build_input_id,
        "build_directory": str(build_directory),
        "work_directory": str(work_directory),
        "prefix": str(prefix),
        "component_order": list(EXPECTED_ORDER),
        "source_inputs": source_receipts,
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
    if component_index > EXPECTED_ORDER.index("pkgconf"):
        environment["PKG_CONFIG"] = str(prefix / "bin/pkgconf")
        environment["PKG_CONFIG_LIBDIR"] = f"{prefix / 'lib/pkgconfig'}:{prefix / 'share/pkgconfig'}"
    return environment


def format_command(
    template: list[str], source: Path, build: Path, prefix: Path, jobs: int
) -> list[str]:
    replacements = {
        "source": str(source),
        "build": str(build),
        "prefix": str(prefix),
        "jobs": str(jobs),
        "make": str(prefix / "bin/make") if (prefix / "bin/make").is_file() else "/usr/bin/make",
        "cmake": str(prefix / "bin/cmake"),
        "ctest": str(prefix / "bin/ctest"),
    }
    return [argument.format(**replacements) for argument in template]


def run_logged(
    command: Sequence[str], cwd: Path, environment: Mapping[str, str], log_path: Path
) -> dict[str, Any]:
    with log_path.open("wb") as log:
        header = f"$ {json.dumps(list(command), separators=(',', ':'))}\n".encode("utf-8")
        log.write(header)
        log.flush()
        process = subprocess.Popen(
            list(command),
            cwd=cwd,
            env=dict(environment),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        assert process.stdout is not None
        while block := process.stdout.read(64 * 1024):
            sys.stdout.buffer.write(block)
            sys.stdout.buffer.flush()
            log.write(block)
        return_code = process.wait()
    result = {
        "command": list(command),
        "working_directory": str(cwd),
        "exit_code": return_code,
        "log_path": str(log_path),
        "log_sha256": sha256_file(log_path),
    }
    if return_code != 0:
        raise ToolchainError(f"command exited {return_code}: {command[0]}")
    return result


def component_source(
    contract: dict[str, Any], component_id: str, work: Path
) -> Path:
    immutable = Path(contract["logical_roots"]["source_generation"]) / component_id
    mode = contract["build"]["components"][component_id]["source_mode"]
    if mode == "immutable-out-of-tree":
        return immutable
    private = work / "private-sources" / component_id
    if not private.exists():
        private.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(immutable, private, symlinks=True)
    return private


def tool_version(path: Path) -> str:
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
                "version": tool_version(path),
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
    result = subprocess.run([str(readelf), "-d", str(binary)], text=True, capture_output=True)
    if result.returncode != 0:
        raise ToolchainError(f"readelf failed for compiler probe: {result.stderr.strip()}")
    return sorted(set(NEEDED_PATTERN.findall(result.stdout)))


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
    for component_id in EXPECTED_ORDER:
        if component_id in completed:
            continue
        component = contract["build"]["components"][component_id]
        component_build = build_root / "components" / component_id
        private_directory(component_build)
        source = component_source(contract, component_id, work_root)
        environment = build_environment(contract, plan, component_id)
        steps: list[dict[str, Any]] = []
        for step_name in ("configure", "build", "test", "install"):
            command = format_command(
                component[step_name],
                source,
                component_build,
                prefix,
                plan["parallel_jobs"],
            )
            steps.append(
                run_logged(
                    command,
                    source if component["source_mode"] == "private-copy-in-tree" else component_build,
                    environment,
                    component_build / f"{step_name}.log",
                )
            )
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
