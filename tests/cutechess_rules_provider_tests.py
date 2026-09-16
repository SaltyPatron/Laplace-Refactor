#!/usr/bin/env python3
"""Execute bounded conformance checks against the selected native CuteChess provider."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.dependencies import chess_tools

MANIFEST = ROOT / "tests/fixtures/cutechess-rules/manifest.json"
MAXIMUM_OUTPUT_BYTES = 64 * 1024 * 1024
WALL_TIMEOUT_SECONDS = 60
REQUEST_SCHEMA = "laplace.cutechess-rules-request/v1"
RESPONSE_SCHEMA = "laplace.cutechess-rules-response/v1"


class ConformanceError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ConformanceError(message)


def sha256(path: Path) -> str:
    return chess_tools.digest(path)


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def file_record(path: Path) -> dict:
    return {"path": str(path.resolve()), "sha256": sha256(path), "bytes": path.stat().st_size}


def declared_path(value: str) -> Path:
    path = Path(value)
    return (path if path.is_absolute() else ROOT / path).resolve(strict=True)


def verified_file(entry: dict, label: str, expected_path: Path | None = None) -> dict:
    require(isinstance(entry, dict), f"{label}: file receipt must be an object")
    require(isinstance(entry.get("path"), str), f"{label}: missing file path")
    path = declared_path(entry["path"])
    require(path.is_file(), f"{label}: not a retained file")
    if expected_path is not None:
        require(path == expected_path.resolve(strict=True), f"{label}: invocation path differs from receipt")
    actual = file_record(path)
    require(entry.get("sha256") == actual["sha256"], f"{label}: SHA256 mismatch")
    require(entry.get("byte_count") == actual["bytes"], f"{label}: byte-count mismatch")
    return actual


def subset(actual: object, expected: object, where: str) -> None:
    if isinstance(expected, dict):
        require(isinstance(actual, dict), f"{where}: expected an object")
        for key, value in expected.items():
            require(key in actual, f"{where}: missing {key}")
            subset(actual[key], value, f"{where}.{key}")
    elif isinstance(expected, list):
        require(isinstance(actual, list), f"{where}: expected an array")
        require(len(actual) == len(expected), f"{where}: array length {len(actual)} != {len(expected)}")
        for index, value in enumerate(expected):
            subset(actual[index], value, f"{where}[{index}]")
    else:
        require(type(actual) is type(expected) and actual == expected,
                f"{where}: {actual!r} != {expected!r}")


def square_checks(state: dict, expected: dict, where: str) -> None:
    pieces = state["piece_squares_a1_to_h8"]
    for index, piece in expected.items():
        offset = int(index)
        require(0 <= offset < 64, f"{where}: invalid fixture square index")
        subset(pieces[offset], piece, f"{where}.piece_squares_a1_to_h8[{offset}]")


def state_shape(state: dict, where: str) -> None:
    pieces = state.get("piece_squares_a1_to_h8")
    require(isinstance(pieces, list) and len(pieces) == 64, f"{where}: missing 64 typed squares")
    for index, piece in enumerate(pieces):
        if piece is None:
            continue
        require(isinstance(piece, dict) and set(piece) == {"side", "piece"},
                f"{where}[{index}]: invalid typed piece")
        require(piece["side"] in ("white", "black"), f"{where}[{index}]: invalid side")
        require(piece["piece"] in ("pawn", "knight", "bishop", "rook", "queen", "king"),
                f"{where}[{index}]: invalid standard piece")
    require(state.get("side_to_move") in ("white", "black"), f"{where}: invalid turn")
    for field in ("provider_en_passant_square", "legal_en_passant_square"):
        require(field in state, f"{where}: missing {field}")
        square = state[field]
        if square is not None:
            require(isinstance(square, dict) and set(square) == {"file", "rank"},
                    f"{where}: malformed {field}")
            require(all(type(square[axis]) is int and 0 <= square[axis] < 8
                        for axis in ("file", "rank")), f"{where}: out-of-board {field}")
    legal = state.get("legal_lan_moves")
    require(isinstance(legal, list) and all(isinstance(value, str) for value in legal),
            f"{where}: missing legal move realization")
    require(legal == sorted(set(legal)), f"{where}: legal moves are duplicated or unsorted")
    subset(state["draw_observations"], {
        "exact_repetition_verified": False,
        "complete_dead_position_adjudication": False,
    }, f"{where}.draw_observations")


def validate_trace(actual: dict, request: dict, expected: dict, policy: str, where: str) -> None:
    subset(actual, {"undo_exact": True, "replay_exact": True}, where)
    plies = actual.get("plies")
    require(isinstance(plies, list) and len(plies) == len(request["san"]),
            f"{where}: supplied move count was not realized")
    if "ply_count" in expected:
        require(len(plies) == expected["ply_count"], f"{where}: independent ply count mismatch")
    state_shape(actual["initial_position"], f"{where}.initial")
    state_shape(actual["final_position"], f"{where}.final")
    for index, (ply, spelling) in enumerate(zip(plies, request["san"])):
        subset(ply, {"supplied_spelling": spelling, "provider_legal": True}, f"{where}.plies[{index}]")
        require(isinstance(ply.get("canonical_san"), str) and ply["canonical_san"],
                f"{where}.plies[{index}]: absent canonical SAN")
        canonical = spelling == ply["canonical_san"]
        require(ply.get("lexically_canonical_san") is canonical,
                f"{where}.plies[{index}]: lexical canonicality flag is false testimony")
        if policy == "strict-san":
            require(canonical, f"{where}.plies[{index}]: strict SAN admitted a different spelling")
        state_shape(ply["position"], f"{where}.plies[{index}].position")
    for name in ("initial_position", "final_position", "perft"):
        if name in expected:
            subset(actual[name], expected[name], f"{where}.{name}")
    if "perft_depth" in request:
        perft = actual.get("perft")
        require(isinstance(perft, dict), f"{where}: missing perft receipt")
        require(perft.get("depth") == request["perft_depth"], f"{where}: wrong perft depth")
        for field in ("leaf_nodes_decimal", "visited_nodes_decimal"):
            require(isinstance(perft.get(field), str) and perft[field].isdigit(),
                    f"{where}: perft counter must be a decimal string")
        require(int(perft["visited_nodes_decimal"]) <= 5000000,
                f"{where}: provider exceeded declared visit budget")
    else:
        require(actual.get("perft") is None, f"{where}: unrequested perft work")
    for check in expected.get("ply_checks", []):
        index = check["index"]
        require(0 <= index < len(plies), f"{where}: fixture ply index outside trace")
        subset(plies[index], check.get("expected", {}), f"{where}.plies[{index}]")
        square_checks(plies[index]["position"], check.get("squares", {}), f"{where}.plies[{index}]")
    square_checks(actual["initial_position"], expected.get("initial_squares", {}), f"{where}.initial")
    square_checks(actual["final_position"], expected.get("final_squares", {}), f"{where}.final")
    for move in expected.get("final_legal_lan_absent", []):
        require(move not in actual["final_position"]["legal_lan_moves"],
                f"{where}: illegal move {move} appears in legal set")


def response_envelope(response: dict, returncode: int, policy: str, qt: dict) -> None:
    subset(response, {
        "schema": RESPONSE_SCHEMA, "variant": "standard",
        "canonical_chess_admission": False, "postgresql_admission": False,
        "complete_fide_adjudication": False, "recorded_game_rate_measured": False,
    }, "response")
    status = response.get("status")
    require(status in ("completed", "refused"), "invalid provider disposition")
    require(returncode == (0 if status == "completed" else 42),
            f"provider status/exit-code mismatch: {status}/{returncode}")
    if status == "completed":
        subset(response, {"notation_policy": policy, "runtime_qt_version": qt["version"]}, "response")
        libraries = response.get("runtime_qt_libraries")
        require(isinstance(libraries, list) and libraries, "missing mapped Qt runtime observations")
        prefix = Path(qt["prefix"]).resolve(strict=True)
        require(any(Path(path).name.startswith("libQt6Core.so") for path in libraries),
                "Qt Core was not actually mapped")
        for path_text in libraries:
            require(isinstance(path_text, str), "invalid mapped library path")
            path = Path(path_text).resolve(strict=True)
            require(path.is_file() and path.is_relative_to(prefix),
                    f"mapped Qt library is outside selected prefix: {path}")


def validate_case(case: dict, response: dict) -> None:
    expected = case["expected"]
    subset(response, {"status": expected["status"]}, case["id"])
    if expected["status"] == "refused":
        subset(response, {"refusal": expected["refusal"]}, case["id"])
        return
    traces = response.get("traces")
    requests = case["request"]["traces"]
    require(isinstance(traces, list) and len(traces) == len(requests),
            f"{case['id']}: trace count mismatch")
    require(len(expected["traces"]) == len(requests), f"{case['id']}: malformed fixture expectation")
    for index, (trace, request, expectation) in enumerate(zip(traces, requests, expected["traces"])):
        validate_trace(trace, request, expectation, case["request"]["notation_policy"],
                       f"{case['id']}.traces[{index}]")
    for left, right in expected.get("equal_final_legal_move_sets", []):
        require(traces[left]["final_position"]["legal_lan_moves"] ==
                traces[right]["final_position"]["legal_lan_moves"],
                f"{case['id']}: paired legal move sets differ")


def child_limits() -> None:
    resource.setrlimit(resource.RLIMIT_FSIZE, (MAXIMUM_OUTPUT_BYTES, MAXIMUM_OUTPUT_BYTES))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class NativeCalls:
    def __init__(self, artifact_dir: Path, qt: dict, summary: dict):
        self.artifact_dir = artifact_dir
        self.qt = qt
        self.environment = chess_tools.qt_environment(Path(qt["prefix"]))
        self.summary = summary
        self.number = 0

    def call(self, label: str, executable: Path, request: dict) -> dict:
        self.number += 1
        stem = f"{self.number:03d}-{label}"
        request_path = self.artifact_dir / f"{stem}.request.json"
        stdout_path = self.artifact_dir / f"{stem}.response.json"
        stderr_path = self.artifact_dir / f"{stem}.stderr.txt"
        payload = {"schema": REQUEST_SCHEMA, **request}
        write_json(request_path, payload)
        record = {"label": label, "executable": str(executable), "status": "started",
                  "request": file_record(request_path)}
        self.summary["invocations"].append(record)
        started = time.monotonic_ns()
        try:
            with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
                try:
                    completed = subprocess.run(
                        [str(executable)], input=request_path.read_bytes(),
                        stdout=stdout, stderr=stderr, env=self.environment,
                        timeout=WALL_TIMEOUT_SECONDS, check=False, preexec_fn=child_limits)
                except subprocess.TimeoutExpired as error:
                    record["status"] = "timeout"
                    raise ConformanceError(f"{label}: provider exceeded {WALL_TIMEOUT_SECONDS}s") from error
            record["returncode"] = completed.returncode
            require(stdout_path.stat().st_size <= MAXIMUM_OUTPUT_BYTES and
                    stderr_path.stat().st_size <= MAXIMUM_OUTPUT_BYTES,
                    f"{label}: child output exceeds bounded envelope")
            try:
                response = json.loads(stdout_path.read_bytes())
            except (ValueError, UnicodeError) as error:
                raise ConformanceError(f"{label}: stdout is not one JSON response") from error
            require(isinstance(response, dict), f"{label}: response is not an object")
            response_envelope(response, completed.returncode, request["notation_policy"], self.qt)
            record["status"] = response["status"]
            if response["status"] == "refused":
                record["refusal"] = response.get("refusal")
                record["detail"] = response.get("detail")
            if response["status"] == "completed":
                record["runtime_qt_libraries"] = [
                    file_record(Path(path)) for path in response["runtime_qt_libraries"]]
            return response
        finally:
            record["elapsed_nanoseconds"] = time.monotonic_ns() - started
            for key, path in (("response", stdout_path), ("stderr", stderr_path)):
                if path.exists():
                    record[key] = file_record(path)


def verify_build(arguments: argparse.Namespace, summary: dict) -> dict:
    build_path = arguments.build_receipt.resolve(strict=True)
    build = json.loads(build_path.read_text(encoding="utf-8"))
    subset(build, {
        "schema": "laplace.cutechess-rules-build/v1", "status": "passed", "phase": "source-built",
        "native_rules_execution_completed": False,
        "canonical_chess_admission": False, "postgresql_admission": False,
        "complete_fide_adjudication": False, "recorded_game_rate_measured": False,
    }, "build")
    summary["build_receipt"] = file_record(build_path)
    summary["provider"] = verified_file(build["provider"], "provider", arguments.provider)
    summary["defect_provider"] = verified_file(
        build["defect_provider"], "defect provider", arguments.defect_provider)
    require(summary["provider"]["path"] != summary["defect_provider"]["path"],
            "defect control must use a separate compiled executable")
    require(summary["provider"]["sha256"] != summary["defect_provider"]["sha256"],
            "defect executable is byte-identical to the selected provider")
    summary["static_library"] = verified_file(build["static_library"], "official static library")
    for field in ("tool_source_build_receipt", "compile_commands", "build_log"):
        summary[field] = verified_file(build[field], field)
    implementations = build.get("implementation_files")
    require(isinstance(implementations, dict) and implementations, "missing implementation fingerprints")
    for required in ("tools/chess/qualify_rules_provider.py", "tools/chess/CMakeLists.txt",
                     "tools/chess/cutechess_rules_provider.cpp", "tools/chess/cutechess_standard_board.hpp"):
        require(required in implementations, f"missing implementation fingerprint: {required}")
    summary["implementation_files"] = {}
    for relative, expected_hash in implementations.items():
        path = (ROOT / relative).resolve(strict=True)
        require(path.is_relative_to(ROOT), "implementation file escaped the repository")
        actual = file_record(path)
        require(actual["sha256"] == expected_hash, f"implementation fingerprint mismatch: {relative}")
        summary["implementation_files"][relative] = actual
    for field, relative in {
        "source_verifier_sha256": "tools/dependencies/git_checkout.py",
        "source_builder_sha256": "tools/dependencies/chess_tools.py",
        "dependency_lock_sha256": "dependencies/lock.json",
        "chess_selection_sha256": "dependencies/chess-release-selection.json",
    }.items():
        actual = file_record(ROOT / relative)
        require(build.get(field) == actual["sha256"], f"{field}: changed build input")
        summary[field] = actual["sha256"]
    lock = json.loads((ROOT / "dependencies/lock.json").read_text(encoding="utf-8"))["dependencies"]["cutechess"]
    upstream = build["upstream"]
    subset(upstream, {"upstream": lock["upstream"], "revision": lock["revision"], "git_archive_sha256": lock["git_archive_sha256"]},
           "build.upstream")
    require(isinstance(upstream.get("license_files"), list) and upstream["license_files"],
            "build receipt has no upstream license evidence")
    source = declared_path(upstream["source"])
    license_files = {entry["path"]: entry["sha256"] for entry in upstream["license_files"]}
    require(len(license_files) == len(upstream["license_files"]), "duplicate license evidence")
    require(license_files == {entry["path"]: entry["sha256"] for entry in lock["licenses"]},
            "upstream license fingerprints differ from dependency lock")
    summary["verified_license_files"] = []
    for relative, expected_hash in license_files.items():
        path = (source / relative).resolve(strict=True)
        require(path.is_relative_to(source), "license file escaped source root")
        actual = file_record(path)
        require(actual["sha256"] == expected_hash, "upstream license bytes differ from lock")
        summary["verified_license_files"].append(actual)
    summary["upstream"] = upstream
    qt = build["qt"]
    selected = json.loads((ROOT / "dependencies/chess-release-selection.json").read_text(encoding="utf-8"))
    require(qt.get("version") == selected["qt"]["version"], "build Qt version differs from source selection")
    require(Path(qt["prefix"]).is_dir(), "selected Qt prefix is not retained")
    require(isinstance(qt.get("version_metadata"), list) and qt["version_metadata"],
            "Qt version metadata evidence is missing")
    for entry in qt["version_metadata"]:
        metadata = verified_file(entry, "Qt version metadata")
        require(Path(metadata["path"]).is_relative_to(Path(qt["prefix"]).resolve(strict=True)),
                "Qt version metadata escaped selected prefix")
    summary["qt"] = qt
    return build


def execute(arguments: argparse.Namespace, summary: dict) -> None:
    require(sys.platform.startswith("linux"), "native provider conformance currently requires Linux")
    build = verify_build(arguments, summary)
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "laplace.cutechess-rules-fixtures/v1", "invalid fixture manifest")
    subset(build["upstream"], {
        "revision": manifest["upstream"]["revision"],
        "git_archive_sha256": manifest["upstream"]["git_archive_sha256"],
    }, "fixture provider pin")
    summary["manifest"] = file_record(MANIFEST)
    summary["runner"] = file_record(Path(__file__))
    corpus_case = next(case for case in manifest["cases"] if case["id"] == "upstream-117-ply")
    corpus_source = ROOT / corpus_case["source_attribution"]["local_path"]
    corpus_bytes = corpus_source.read_bytes()
    git_blob = hashlib.sha1(f"blob {len(corpus_bytes)}\0".encode("ascii") + corpus_bytes).hexdigest()
    require(git_blob == corpus_case["source_attribution"]["blob"], "117-ply corpus source bytes changed")
    summary["full_game_corpus_source"] = file_record(corpus_source)
    summary["common_qt_environment_owner"] = file_record(ROOT / "tools/dependencies/chess_tools.py")
    artifact_dir = arguments.output.parent / (arguments.output.stem + ".responses")
    artifact_dir.mkdir(parents=True, exist_ok=True)
    calls = NativeCalls(artifact_dir, build["qt"], summary)
    responses = {}
    ids = [case["id"] for case in manifest["cases"]]
    require(len(ids) == len(set(ids)), "fixture ids must be unique")
    for case in manifest["cases"]:
        response = calls.call(case["id"], arguments.provider.resolve(), case["request"])
        validate_case(case, response)
        responses[case["id"]] = response
        summary["cases"].append({
            "id": case["id"], "status": "passed",
            "source_attribution": case["source_attribution"],
            "trace_count": len(case["request"]["traces"]),
            "supplied_plies": sum(len(trace["san"]) for trace in case["request"]["traces"]),
            "provider_disposition": response["status"],
            "noncanonical_spellings": [
                {"trace": trace_index, "ply": ply_index,
                 "supplied": ply["supplied_spelling"], "rendered": ply["canonical_san"]}
                for trace_index, trace in enumerate(response.get("traces", []))
                for ply_index, ply in enumerate(trace["plies"])
                if not ply["lexically_canonical_san"]],
        })

    # Compare each trace's standalone response with its response in a real batch.
    # Large perft is intentionally not repeated: counts were checked independently above.
    for policy in ("strict-san", "provider-legal"):
        requests, expected_traces = [], []
        for case in manifest["cases"]:
            if case["expected"]["status"] != "completed" or case["request"]["notation_policy"] != policy:
                continue
            for index, trace_request in enumerate(case["request"]["traces"]):
                if trace_request.get("perft_depth", 0) > 2:
                    continue
                if len(case["request"]["traces"]) == 1:
                    standalone = responses[case["id"]]["traces"][0]
                else:
                    response = calls.call(
                        f"{case['id']}-standalone-{index}", arguments.provider.resolve(),
                        {"notation_policy": policy, "traces": [trace_request]})
                    require(response["status"] == "completed", "standalone replay was refused")
                    standalone = response["traces"][0]
                    require(standalone == responses[case["id"]]["traces"][index],
                            f"{case['id']}: trace changes between standalone and original batch")
                requests.append(trace_request)
                expected_traces.append(standalone)
        require(requests and len(requests) <= 32, "parity fixture batch exceeds provider envelope")
        response = calls.call(
            "batch-parity-" + policy, arguments.provider.resolve(),
            {"notation_policy": policy, "traces": requests})
        require(response["status"] == "completed", f"{policy}: parity batch was refused")
        require(response.get("traces") == expected_traces, f"{policy}: scalar/batch trace inequality")
        summary["parity"].append({"notation_policy": policy, "trace_count": len(requests), "equal": True})

    defect = manifest["defect_control"]
    control = next(case for case in manifest["cases"] if case["id"] == defect["case_id"])
    response = calls.call(defect["id"], arguments.defect_provider.resolve(), control["request"])
    subset(response, defect["required_defect_observation"], "actual compiled defect observation")
    detected = False
    try:
        validate_case(control, response)
    except ConformanceError:
        detected = True
    require(detected, "strict-SAN assertion did not catch the actual compiled defect")
    summary["defect_control"] = {
        "id": defect["id"], "detected": True,
        "meaning": defect["meaning"], "actual_response_status": response["status"],
    }
    # Revalidate executable bytes after the complete invocation set.
    verified_file(build["provider"], "provider after execution", arguments.provider)
    verified_file(build["defect_provider"], "defect provider after execution", arguments.defect_provider)
    summary["qualified_case_count"] = len(summary["cases"])
    summary["native_rules_execution_completed"] = True
    summary["status"] = "qualified"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider", type=Path, required=True)
    parser.add_argument("--defect-provider", type=Path, required=True)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    arguments.output = arguments.output.resolve()
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    summary = {
        "schema": "laplace.cutechess-rules-conformance/v1",
        "status": "failed", "variant": "standard",
        "native_rules_execution_completed": False,
        "canonical_chess_admission": False, "postgresql_admission": False,
        "complete_fide_adjudication": False, "recorded_game_rate_measured": False,
        "scope": "Upstream native provider qualification; perft counts legal move-tree leaves.",
        "child_limits": {"wall_seconds": WALL_TIMEOUT_SECONDS, "output_bytes_per_file": MAXIMUM_OUTPUT_BYTES,
                         "core_dump_bytes": 0},
        "cases": [], "parity": [], "invocations": [],
    }
    try:
        execute(arguments, summary)
    except (ConformanceError, OSError, ValueError, KeyError, TypeError, IndexError,
            chess_tools.ChessToolError, subprocess.SubprocessError) as error:
        summary["error"] = f"{type(error).__name__}: {error}"
    finally:
        write_json(arguments.output, summary)
    print(json.dumps({"status": summary["status"], "receipt": str(arguments.output),
                      "qualified_case_count": summary.get("qualified_case_count", 0),
                      "error": summary.get("error")}, sort_keys=True))
    return 0 if summary["status"] == "qualified" else 1


if __name__ == "__main__":
    raise SystemExit(main())
