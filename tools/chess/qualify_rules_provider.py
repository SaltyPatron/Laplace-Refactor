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
        implementation_files = {
            str(path.relative_to(ROOT)): file_receipt(path)["sha256"]
            for path in (Path(__file__).resolve(), ROOT / "tools/chess/CMakeLists.txt",
                         ROOT / "tools/chess/cutechess_rules_provider.cpp",
                         ROOT / "tools/chess/cutechess_standard_board.hpp")
        }
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
                   f"-DLAPLACE_RULES_OUTPUT_ROOT={output}"]
        commands = [(command, 120),
                    (["cmake", "--build", str(build), "--clean-first", "--parallel", str(args.jobs)], 300)]
        log_path = output / "build.log"
        with log_path.open("wb") as log:
            for command, timeout in commands:
                report["commands"].append(command)
                completed = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=log,
                                           stderr=subprocess.STDOUT, env=environment, timeout=timeout)
                require(completed.returncode == 0, f"rules carrier build failed; inspect {log_path}")
        # Recheck the common source/executable owner after compilation.
        chess_tools.verify_installation(args.chess_prefix, selected, artifacts, "cutechess")
        require(chess_tools.digest(current_path) == current_digest, "tool source-build receipt changed")
        require(file_receipt(static_library) == library_receipt, "upstream static library changed")
        require(implementation_files == {
            path: chess_tools.digest(ROOT / path) for path in implementation_files
        }, "qualification implementation changed during build")
        report.update(status="passed", phase="source-built",
                      provider=file_receipt(build / "laplace_cutechess_rules_provider"),
                      defect_provider=file_receipt(build / "laplace_cutechess_rules_provider_noncanonical_defect"),
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
