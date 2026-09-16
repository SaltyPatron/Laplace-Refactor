#!/usr/bin/env python3
"""Build a qualification carrier over an already source-built, locked CuteChess library."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/dependencies"))
import chess_tools  # noqa: E402


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def file_receipt(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), f"exact regular file required: {path}")
    return {"path": str(path.resolve()), "sha256": chess_tools.digest(path), "byte_count": path.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chess-prefix", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--native-build", type=Path, required=True)
    parser.add_argument("--postgres", action="store_true",
                        help="also build the durable LINE carrier against the selected native PostgreSQL build")
    args = parser.parse_args()
    output = args.output.resolve()
    require(output != ROOT and ROOT not in output.parents, "build evidence must be outside the repository")
    output.mkdir(parents=True, exist_ok=True)
    report = {"schema": "laplace.cutechess-rules-build/v1", "status": "failed",
              "native_rules_execution_completed": False, "canonical_chess_admission": False,
              "postgresql_admission": False, "recorded_game_rate_measured": False,
              "complete_fide_adjudication": False,
              "scope": "source-built external rules qualification carrier", "commands": []}
    try:
        require(platform.system() == "Linux", "this carrier qualifies Linux loader observations")
        require(1 <= args.jobs <= 64, "build jobs must be from 1 through 64")
        selected, artifacts = chess_tools.configuration()
        installation = chess_tools.verify_installation(args.chess_prefix, selected, artifacts, "cutechess")
        current_path = args.chess_prefix / "current.json"
        current_digest = chess_tools.digest(current_path)
        tool = installation["tools"]["cutechess"]
        locked = chess_tools.json_read(ROOT / "dependencies/lock.json")["dependencies"]["cutechess"]
        source = Path(tool["source"]).resolve(strict=True)
        require(output != source and source not in output.parents, "output cannot modify upstream source")
        work = Path(tool["build_log"]).resolve(strict=True).parent
        static_library = work / "libcutechess.a"
        library_receipt = file_receipt(static_library)
        qt_prefix = Path(tool["qt_prefix"]).resolve(strict=True)
        qt_version, version_files = chess_tools.qt_package_version(qt_prefix)
        require(qt_version == selected["qt"]["version"], "selected Qt SDK version differs")
        for path in version_files:
            require(qt_prefix in path.resolve().parents, "Qt version metadata escaped selected SDK")
        license_files = []
        for item in locked["licenses"]:
            license_path = source / item["path"]
            observed = file_receipt(license_path)
            require(observed["sha256"] == item["sha256"], "upstream license bytes differ")
            license_files.append({"path": item["path"], "sha256": observed["sha256"]})
        native_build = args.native_build.resolve(strict=True)
        native_engine = (native_build / "engine/liblaplace_engine.so").resolve(strict=True)
        native_engine_receipt = file_receipt(native_engine)
        native_cache = native_build / "CMakeCache.txt"
        require(f"CMAKE_HOME_DIRECTORY:INTERNAL={ROOT}" in native_cache.read_text().splitlines(),
                "native build belongs to another source tree")
        native_generated = native_build / "generated"
        require((native_generated / "laplace/contract/composition.h").is_file(),
                "native build has no generated common composition contract")
        native_headers = [file_receipt(path) for path in sorted(native_generated.rglob("*.h"))]
        report["native_build"] = {
            "root": str(native_build), "engine": native_engine_receipt,
            "cache": file_receipt(native_cache),
            "compile_commands": file_receipt(native_build / "compile_commands.json"),
            "generated_headers": native_headers,
            "scope": "Common native engine and generated public contracts; no PostgreSQL admission"}
        postgres_inputs = {}
        native_targets_path = native_build / "tests/chess-postgres-targets.json"
        selected_cxx = None
        if args.postgres:
            native_targets = json.loads(native_targets_path.read_text())
            require(native_targets.get("schema") == "laplace.chess-postgres-native-targets/v1",
                    "native PostgreSQL target receipt schema differs")
            require(Path(native_targets["source_root"]).resolve() == ROOT and
                    Path(native_targets["native_engine"]).resolve(strict=True) == native_engine,
                    "native PostgreSQL targets select another source or engine")
            cache_lines = native_cache.read_text().splitlines()
            compiler_lines = [line.split("=", 1)[1] for line in cache_lines
                              if line.startswith("CMAKE_CXX_COMPILER:FILEPATH=") or
                              line.startswith("CMAKE_CXX_COMPILER:STRING=")]
            require(len(compiler_lines) == 1, "common native compiler selection is absent or ambiguous")
            selected_cxx = Path(compiler_lines[0]).resolve(strict=True)
            pg_config_lines = [line.split("=", 1)[1] for line in cache_lines
                               if line.startswith("LAPLACE_PG_CONFIG:FILEPATH=")]
            require(pg_config_lines == [native_targets["pg_config"]],
                    "native PostgreSQL target receipt does not match configured pg_config")
            postgres_paths = {
                "native_targets": native_targets_path, "compiler": selected_cxx,
                "blake3_library": Path(native_targets["blake3_library"]).resolve(strict=True),
                "blake3_header": Path(native_targets["blake3_include"]) / "blake3.h",
                "pg_config": Path(native_targets["pg_config"]).resolve(strict=True),
                "libpq_header": Path(native_targets["pg_include"]) / "libpq-fe.h",
                "module": Path(native_targets["module"]).resolve(strict=True),
                "source_probe": Path(native_targets["source_probe"]).resolve(strict=True),
                "sql": Path(native_targets["sql"]),
                "source_sql": Path(native_targets["sql"]).parent / "source_admission_contract.sql",
                "unicode_sql": Path(native_targets["sql"]).parent / "unicode_root_contract.sql",
                "highway_sql": Path(native_targets["sql"]).parent / "highway_revalidation_bindings.sql",
                "extension_control": Path(native_targets["control_root"]) / "extension/laplace.control",
            }
            for path in sorted((Path(native_targets["control_root"]) / "extension").glob("*.sql")):
                postgres_paths["extension_sql_" + path.name] = path
            postgres_inputs = {name: file_receipt(path) for name, path in postgres_paths.items()}
            report["postgresql_build_inputs"] = postgres_inputs
        implementation_files = {
            str(path.relative_to(ROOT)): file_receipt(path)["sha256"]
            for path in (Path(__file__).resolve(), ROOT / "tools/chess/CMakeLists.txt",
                         ROOT / "tools/chess/cutechess_rules_provider.cpp",
                         ROOT / "tools/chess/cutechess_standard_board.hpp",
                         ROOT / "tools/chess/cutechess_trace_adapter.hpp",
                         ROOT / "tools/chess/cutechess_trace_adapter.cpp",
                         ROOT / "tests/cutechess_trace_adapter_tests.cpp",
                         ROOT / "engine/include/laplace/chess_line.h",
                         ROOT / "engine/src/chess_line.cpp",
                         ROOT / "engine/src/canonical_composition_plan.hpp",
                         ROOT / "contracts/chess-line.json",
                         ROOT / "tests/chess_line_tests.cpp",
                         ROOT / "engine/CMakeLists.txt", ROOT / "tests/CMakeLists.txt",
                         ROOT / "CMakeLists.txt")
        }
        if args.postgres:
            for relative in ("tools/chess/ChessPostgreSQL.cmake",
                             "tests/postgres/chess_line_contract_probe.cpp",
                             "tests/postgres/chess_line_contract.sql",
                             "tests/postgres/run_spi_test.sh",
                             "tools/tests/postgres_resource_guard.py",
                             ".github/workflows/chess-postgres-qualification.yml"):
                implementation_files[relative] = file_receipt(ROOT / relative)["sha256"]
        report.update(
            upstream={"upstream": locked["upstream"], "revision": locked["revision"],
                      "git_archive_sha256": locked["git_archive_sha256"],
                      "source": str(source), "license_files": license_files},
            static_library=library_receipt,
            qt={"prefix": str(qt_prefix), "version": qt_version,
                "version_metadata": [file_receipt(path) for path in version_files]},
            tool_source_build_receipt=file_receipt(current_path),
            dependency_lock_sha256=chess_tools.digest(ROOT / "dependencies/lock.json"),
            chess_selection_sha256=chess_tools.digest(ROOT / "dependencies/chess-release-selection.json"),
            implementation_files=implementation_files,
            source_verifier_sha256=chess_tools.digest(ROOT / "tools/dependencies/git_checkout.py"),
            source_builder_sha256=chess_tools.digest(ROOT / "tools/dependencies/chess_tools.py"))
        # Retain the exact licensed upstream source beside the built carrier.
        source_archive = output / "cutechess-source.tar"
        with source_archive.open("wb") as archive:
            archived = subprocess.run(["git", "-C", str(source), "archive", "--format=tar", "HEAD"],
                                      stdin=subprocess.DEVNULL, stdout=archive,
                                      stderr=subprocess.PIPE, timeout=120)
        require(archived.returncode == 0, "could not retain exact upstream source archive")
        archive_receipt = file_receipt(source_archive)
        require(archive_receipt["sha256"] == locked["git_archive_sha256"],
                "retained upstream source archive differs from the lock")
        shutil.copyfile(source / "COPYING", output / "COPYING.cutechess")
        retained_sources = output / "provider-source"
        retained_sources.mkdir(exist_ok=True)
        for path in implementation_files:
            destination = retained_sources / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / path, destination)
        report["upstream_source_archive"] = archive_receipt
        report["retained_license"] = file_receipt(output / "COPYING.cutechess")
        native_source_archive = output / "laplace-native-source.tar"
        with native_source_archive.open("wb") as archive:
            archived = subprocess.run(["git", "-C", str(ROOT), "archive", "--format=tar", "HEAD"],
                                      stdin=subprocess.DEVNULL, stdout=archive,
                                      stderr=subprocess.PIPE, timeout=120)
        require(archived.returncode == 0, "could not retain native source archive")
        report["native_source_archive"] = file_receipt(native_source_archive)
        retained_native = output / "native"
        retained_native.mkdir(exist_ok=True)
        for path in (native_engine, native_cache, native_build / "compile_commands.json"):
            shutil.copyfile(path, retained_native / path.name)
        for entry in native_headers:
            path = Path(entry["path"])
            destination = retained_native / "generated" / path.relative_to(native_generated)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
        report["retained_native_engine"] = file_receipt(retained_native / native_engine.name)
        require(report["retained_native_engine"]["sha256"] == native_engine_receipt["sha256"],
                "native retained engine copy changed")
        environment = chess_tools.qt_environment(qt_prefix)
        build = output / "build"
        command = ["cmake", "-S", str(ROOT / "tools/chess"), "-B", str(build),
                   "-UQt6*", "-DCMAKE_BUILD_TYPE=Release",
                   f"-DLAPLACE_CUTECHESS_SOURCE={source}",
                   f"-DLAPLACE_CUTECHESS_LIBRARY={static_library}",
                   f"-DLAPLACE_QT_PREFIX={qt_prefix}",
                   f"-DLAPLACE_QT_VERSION={qt_version}",
                   f"-DQt6_DIR={qt_prefix / 'lib/cmake/Qt6'}",
                   f"-DCMAKE_PREFIX_PATH={qt_prefix}",
                   f"-DLAPLACE_RULES_OUTPUT_ROOT={output}",
                   f"-DLAPLACE_NATIVE_BUILD_ROOT={native_build}",
                   f"-DLAPLACE_NATIVE_ENGINE={native_engine}"]
        if args.postgres:
            command.extend(["-DLAPLACE_CHESS_POSTGRES=ON",
                            f"-DCMAKE_CXX_COMPILER={selected_cxx}"])
        else:
            command.append("-DLAPLACE_CHESS_POSTGRES=OFF")
        commands = [(command, 120),
                    (["cmake", "--build", str(build), "--clean-first", "--parallel", str(args.jobs)], 300)]
        log_path = output / "build.log"
        postgres_linked_before = {}
        with log_path.open("wb") as log:
            for command, timeout in commands:
                report["commands"].append(command)
                completed = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=log,
                                           stderr=subprocess.STDOUT, env=environment, timeout=timeout)
                require(completed.returncode == 0, f"rules carrier build failed; inspect {log_path}")
                if args.postgres and command[1] == "-S":
                    linkage = json.loads((output / "postgres-line-linkage.json").read_text())
                    require(linkage.pop("schema") == "laplace.chess-postgres-linkage/v1",
                            "PostgreSQL linkage receipt schema differs")
                    postgres_linked_before = {
                        name: file_receipt(Path(path).resolve(strict=True))
                        for name, path in linkage.items() if name != "probe"}
        # Recheck the common source/executable owner after compilation.
        chess_tools.verify_installation(args.chess_prefix, selected, artifacts, "cutechess")
        require(chess_tools.digest(current_path) == current_digest, "tool source-build receipt changed")
        require(file_receipt(static_library) == library_receipt, "upstream static library changed")
        require(file_receipt(native_engine) == native_engine_receipt, "native engine changed during adapter build")
        require(implementation_files == {
            path: chess_tools.digest(ROOT / path) for path in implementation_files
        }, "qualification implementation changed during build")
        if args.postgres:
            require(postgres_inputs == {name: file_receipt(entry["path"])
                                        for name, entry in postgres_inputs.items()},
                    "selected native PostgreSQL build inputs changed during carrier compilation")
            linkage_path = output / "postgres-line-linkage.json"
            linkage = json.loads(linkage_path.read_text())
            require(linkage.pop("schema") == "laplace.chess-postgres-linkage/v1",
                    "PostgreSQL linkage receipt schema differs")
            linked_files = {name: file_receipt(Path(path).resolve(strict=True))
                            for name, path in linkage.items()}
            require(postgres_linked_before == {
                name: entry for name, entry in linked_files.items() if name != "probe"},
                "selected PostgreSQL linkage files changed during compilation")
            require(linked_files["blake3_library"] == postgres_inputs["blake3_library"] and
                    linked_files["module"] == postgres_inputs["module"] and
                    linked_files["source_probe"] == postgres_inputs["source_probe"],
                    "carrier linkage substituted a selected common provider")
            report["postgresql_carrier"] = {
                "linkage": file_receipt(linkage_path), "files": linked_files,
                "admission_executed": False,
                "scope": "Compiled qualification carrier; registered CTest executes actual durable admission"}
            retained_pg = output / "postgresql-inputs"
            retained_pg.mkdir(exist_ok=True)
            for name, entry in {**postgres_inputs, **linked_files}.items():
                if name == "compiler":
                    continue  # Tool identity is retained; do not redistribute the installed compiler.
                source_path = Path(entry["path"])
                destination = retained_pg / name / source_path.name
                destination.parent.mkdir(exist_ok=True)
                shutil.copyfile(source_path, destination)
                require(file_receipt(destination)["sha256"] == entry["sha256"],
                        f"retained PostgreSQL input changed: {name}")
        report.update(status="passed", phase="source-built",
                      provider=file_receipt(build / "laplace_cutechess_rules_provider"),
                      defect_provider=file_receipt(build / "laplace_cutechess_rules_provider_noncanonical_defect"),
                      trace_adapter_test=file_receipt(build / "laplace_cutechess_trace_adapter_tests"),
                      trace_adapter_library=file_receipt(build / "liblaplace_cutechess_trace_adapter.a"),
                      compile_commands=file_receipt(build / "compile_commands.json"),
                      build_log=file_receipt(log_path))
    except (OSError, ValueError, KeyError, RuntimeError, chess_tools.ChessToolError,
            subprocess.SubprocessError) as error:
        report["error"] = str(error)
    (output / "build-receipt.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
