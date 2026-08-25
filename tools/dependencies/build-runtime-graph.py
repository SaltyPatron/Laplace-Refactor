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
NEEDED_PATTERN = re.compile(r"Shared library: \[([^]]+)\]")
RUNPATH_PATTERN = re.compile(r"Library r(?:un)?path: \[([^]]*)\]", re.IGNORECASE)


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
    for field in ("c_flags", "cxx_flags", "rejected_environment"):
        require_string_list(execution.get(field), f"execution.{field}")
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
            "-x",
            language,
            "/dev/null",
            "-o",
            "/dev/null",
        ]
        process = subprocess.run(command, text=True, capture_output=True)
        trace = process.stderr + process.stdout
        if process.returncode != 0:
            raise GraphError(f"{role} driver trace failed")
        if selected_linker not in trace:
            raise GraphError(f"{role} driver trace did not select the packaged linker")
        absolute_paths: set[str] = set()
        for line in trace.splitlines():
            try:
                tokens = shlex.split(line)
            except ValueError:
                continue
            absolute_paths.update(token for token in tokens if token.startswith("/"))
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
        result[role] = {
            "command": command,
            "trace_sha256": hashlib.sha256(trace.encode("utf-8")).hexdigest(),
            "selected_linker": selected_linker,
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
    final = Path(plan["final_prefix"])
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
        "LDFLAGS": f"-L{staged / 'lib'} -Wl,-rpath,{final / 'lib'}",
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
            f"-DCMAKE_C_COMPILER={compilers['c_compiler']['path']}",
            f"-DCMAKE_CXX_COMPILER={compilers['cxx_compiler']['path']}",
            f"-DCMAKE_AR={tools['ar']['path']}",
            f"-DCMAKE_LINKER={tools['ld']['path']}",
            f"-DCMAKE_NM={tools['nm']['path']}",
            f"-DCMAKE_OBJCOPY={tools['objcopy']['path']}",
            f"-DCMAKE_OBJDUMP={tools['objdump']['path']}",
            f"-DCMAKE_RANLIB={tools['ranlib']['path']}",
            f"-DCMAKE_STRIP={tools['strip']['path']}",
            f"-DCMAKE_BUILD_RPATH={staged / 'lib'}",
            f"-DCMAKE_INSTALL_RPATH={final / 'lib'}",
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
    dynamic = subprocess.run([str(readelf), "-d", str(path)], text=True, capture_output=True)
    if dynamic.returncode != 0:
        raise GraphError(f"readelf failed for {path}: {dynamic.stderr.strip()}")
    runpaths: list[str] = []
    for match in RUNPATH_PATTERN.finditer(dynamic.stdout):
        runpaths.extend(item for item in match.group(1).split(":") if item)
    return {
        "needed": sorted(set(NEEDED_PATTERN.findall(dynamic.stdout))),
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


def execute(
    contract: dict[str, Any], plan: dict[str, Any], repository: Path
) -> dict[str, Any]:
    build_root = Path(plan["build_directory"])
    stage_root = Path(plan["stage_directory"])
    final_prefix = Path(plan["final_prefix"])
    if build_root.exists() or stage_root.exists() or final_prefix.exists():
        raise GraphError("build, stage, and final package destinations must not already exist")
    private_directory(build_root)
    private_directory(stage_root)
    environment = build_environment(contract, plan, build_root / ".home")
    (build_root / "build-plan.json").write_text(
        json.dumps(plan, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    private_sources = prepare_private_sources(contract, plan, repository, build_root)
    for component in plan["components"]:
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
    (build_root / "package-receipt.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return receipt


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=".")
    parser.add_argument("--contract", default="contracts/postgresql-runtime-build.json")
    parser.add_argument("--archive-root", required=True)
    parser.add_argument("--source-generation", required=True)
    parser.add_argument("--toolchain-receipt")
    parser.add_argument("command", choices=("validate", "plan", "build"))
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
        raise GraphError("plan and build require --toolchain-receipt")
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
    receipt = execute(contract, plan, repository)
    print(json.dumps({"plan": plan, "package": receipt}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (GraphError, subprocess.CalledProcessError) as error:
        print(f"postgresql-runtime-build: {error}", file=sys.stderr)
        raise SystemExit(1) from error
