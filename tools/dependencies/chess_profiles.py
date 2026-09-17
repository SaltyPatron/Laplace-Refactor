#!/usr/bin/env python3
"""Activate measured chess settings and execute the verified source binaries."""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import queue
import re
import subprocess
import tempfile
import threading
import time

import chess_tools as tools
import chess_benchmark as bench

MIB = 1024 * 1024
PROFILE_SCHEMA = "laplace.chess-measured-profile/v1"
CONTROLS = ("cpu.max", "cpu.weight", "cpuset.cpus.effective",
            "cpuset.mems.effective", "memory.max", "memory.high")
HOST_FIELDS = ("kernel", "machine", "cpu_models", "cpu_flags", "affinity",
               "visible_logical_cpus", "visible_physical_cores", "topology",
               "numa", "memory_total_bytes", "effective_cpu_equivalents",
               "cgroup_visibility", "transparent_hugepage_policy")


def encoded(value: dict) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def document(path: Path, expected: str | None = None) -> tuple[dict, bytes]:
    tools.require(path.is_file() and not path.is_symlink() and
                  0 < path.stat().st_size <= 64 * MIB, "invalid measured-profile evidence file")
    data = path.read_bytes()
    tools.require(expected is None or sha(data) == expected, "measured-profile evidence SHA256 differs")
    value = json.loads(data)
    tools.require(isinstance(value, dict), "measured-profile evidence must be an object")
    return value, data


def write_atomic(path: Path, data: bytes, *, immutable: bool = False) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tools.require(not path.is_symlink(), "profile evidence cannot be a symlink")
    if immutable and path.exists():
        tools.require(path.read_bytes() == data, "immutable profile evidence differs")
        return
    descriptor, name = tempfile.mkstemp(prefix=".profile-", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if immutable:
            os.link(temporary, path)
        else:
            os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        temporary.unlink(missing_ok=True)


def host_signature(host: dict) -> dict:
    # PID/time/load and live memory usage are observations, not machine identity.
    # Unit/cgroup path names can change between runner jobs; exact resource
    # controls and their hierarchy remain part of the measured environment.
    tools.require(all(key in host for key in HOST_FIELDS) and
                  host.get("cgroups") and host.get("process_identity", {}).get("consistent"),
                  "complete Linux host/resource observation is required")
    return {**{key: host[key] for key in HOST_FIELDS},
            "cgroup_controls": [{key: entry.get(key) for key in CONTROLS}
                               for entry in host["cgroups"]]}


def tool_identity(installed: dict) -> dict:
    result = {}
    for name in ("stockfish", "cutechess"):
        tool = installed["tools"][name]
        result[name] = {key: tool[key] for key in
                        ("source", "revision", "source_archive_sha256", "executable", "sha256")}
        if name == "stockfish":
            result[name]["network"] = tool["checks"]["network"]
            result[name]["network_sha256"] = tool["checks"]["network_sha256"]
        else:
            result[name]["qt_prefix"] = tool.get("qt_prefix")
    return result


def positive(value, name: str) -> int:
    tools.require(type(value) is int and value > 0, name + " must be a positive integer")
    return value


def recommendations(report: dict) -> dict:
    tools.require(report.get("schema") == "laplace.chess-benchmark-receipt/v2" and
                  report.get("completion") in ("completed", "completed-with-rejected-configurations"),
                  "a completed v2 calibration receipt is required")
    tools.require(not report.get("stockfish_override") and
                  report.get("stability_failures") == [],
                  "external override or unstable calibration cannot activate a profile")
    tools.require(report["game_workload"]["mode"] == "complete-legal-games" and
                  report["game_workload"].get("max_moves") is None,
                  "diagnostic episodes cannot activate a measured game profile")
    for name in ("stockfish", "cutechess"):
        samples = report[name + "_samples"]
        tools.require(samples and all(sample.get("completion") == "completed" for sample in samples),
                      "calibration contains incomplete samples")
        measured = [sample for sample in samples if not sample["warmup"]]
        tools.require(measured and all(sum(
            other["configuration"] == sample["configuration"] for other in measured) >= 2
            for sample in measured), "each configuration requires repeated measured samples")
    tools.require(all(sample.get("capped_diagnostic_games") == 0 and
                      sample.get("games") == sample.get("normal_completed_games") and
                      sample["normal_completed_games"] > 0
                      for sample in report["cutechess_samples"]),
                  "calibration games lack complete normal outcomes")
    serial = bench.aggregates(report["stockfish_samples"], "median_wall_nodes_per_second",
                              "wall_nodes_per_second")
    games = bench.aggregates(report["cutechess_samples"], "median_normal_completed_games_per_second",
                             "normal_completed_games_per_second")
    calculated = bench.recommendations(serial, games, report["game_workload"])
    # The benchmark adds baseline speedup fields after aggregation. Compare the
    # actual chosen configurations and sample/rate evidence, not those annotations.
    for name in ("single_engine_fixed_depth_suite", "aggregate_game_throughput"):
        selected = report["recommendations"][name]
        tools.require(selected is not None and all(selected.get(key) == value
                      for key, value in calculated[name].items()),
                      "calibration recommendation does not match its completed samples")
    return calculated


def fit(configuration: dict, report: dict, host: dict, mode: str) -> dict:
    threads = positive(configuration["threads"], "Threads")
    hash_mib = positive(configuration["hash_mib"], "Hash")
    concurrency = positive(configuration.get("concurrency", 1), "concurrency")
    options = report["uci_options"]
    for name, value in (("Threads", threads), ("Hash", hash_mib)):
        tools.require(int(options[name]["min"]) <= value <= int(options[name]["max"]),
                      name + " exceeds the calibrated binary's UCI capability")
    overhead = positive(report["resource_plan"]["resident_engine_overhead_estimate_mib"], "engine overhead")
    margin = report["resource_plan"]["memory_margin_mib"]
    tools.require(type(margin) is int and margin >= 0, "invalid calibrated memory margin")
    resident = (2 * concurrency if mode == "games" else 1) * (hash_mib + overhead)
    cpu = threads * (concurrency if mode == "games" else 1)
    memory_mib = resident + margin
    tools.require(cpu <= host["effective_cpu_equivalents"] and
                  cpu <= report["resource_plan"]["cpu_budget"],
                  "selected Threads/concurrency exceed current or calibrated CPU grant")
    tools.require(memory_mib * MIB <= host["effective_memory_headroom_bytes"] and
                  memory_mib <= report["resource_plan"]["memory_mib"],
                  "selected Hash/concurrency exceed current or calibrated memory grant")
    return {"cpu_equivalents": cpu, "memory_mib": memory_mib,
            "estimated_resident_mib": resident, "memory_margin_mib": margin,
            "memory_estimate_is_measurement": False}


def validate(report: dict, installed: dict, host: dict, maximum_age: int) -> dict:
    positive(maximum_age, "maximum calibration age")
    now = time.time()
    before, after = report["host"], report["host_after"]
    tools.require(0 <= now - after["observed_at_unix"] <= maximum_age and
                  before["observed_at_unix"] <= after["observed_at_unix"],
                  "calibration receipt is stale or has an invalid observation time")
    tools.require(host_signature(before) == host_signature(after) == host_signature(host),
                  "machine, topology or visible resource controls changed since calibration")
    tools.require(tool_identity(report["tool_identity"]) == tool_identity(installed),
                  "calibrated source, binary, NNUE or Qt identity differs")
    expected_files = {tool["executable"]: tool["sha256"]
                      for tool in tool_identity(installed).values()}
    tools.require(report["executable_sha256_before"] == expected_files ==
                  report["executable_sha256_after"], "calibrated executable hashes differ")
    tools.require(report["benchmark_implementation_sha256"] == tools.digest(Path(bench.__file__)) and
                  report["profile_contract_sha256"] == tools.digest(tools.ROOT / "contracts/chess-benchmark.json"),
                  "calibration implementation or workload contract changed")
    return recommendations(report)


def observed(prefix: Path) -> tuple[dict, dict]:
    selected, artifacts = tools.configuration()
    installed = tools.verify_installation(prefix, selected, artifacts)
    sf = installed["tools"]["stockfish"]
    tools.require(not Path(sf["executable"]).is_symlink() and
                  Path(sf["executable"]).resolve() ==
                  (Path(sf["source"]) / "src" / "stockfish").resolve(),
                  "profile requires the direct recorded Stockfish source executable")
    return installed, bench.host_observation()


def activate(prefix: Path, receipt: Path, receipt_sha256: str,
             mode: str = "both", maximum_age: int = 604800) -> dict:
    tools.require(bool(re.fullmatch(r"[0-9a-f]{64}", receipt_sha256)),
                  "an explicit calibration SHA256 is required")
    report, data = document(receipt, receipt_sha256)
    installed, host = observed(prefix)
    selected = validate(report, installed, host, maximum_age)
    root = prefix.resolve()
    for item in installed["tools"].values():
        source = Path(item["source"]).resolve()
        tools.require(root != source and source not in root.parents,
                      "profile evidence must remain outside upstream source checkouts")
    archived = root / "calibrations" / receipt_sha256 / "receipt.json"
    write_atomic(archived, data, immutable=True)
    prepared = []
    # Validate every requested mode before publishing any active selection.
    for usage in ("analysis", "games") if mode == "both" else (mode,):
        tools.require(usage in ("analysis", "games"), "unknown measured profile mode")
        key = "single_engine_fixed_depth_suite" if usage == "analysis" else "aggregate_game_throughput"
        configuration = selected[key]["configuration"]
        grant = fit(configuration, report, host, usage)
        profile = {"schema": PROFILE_SCHEMA, "mode": usage,
                   "calibration": {"path": str(archived), "sha256": receipt_sha256},
                   "maximum_age_seconds": maximum_age, "configuration": configuration,
                   "tool_identity": tool_identity(installed), "host_signature": host_signature(host),
                   "resource_grant": grant, "measured_selection": selected[key],
                   "scope": selected["scope"], "database_recording_capacity_measured": False,
                   "playing_strength_inferred": False}
        profile_id = sha(encoded(profile))
        profile["profile_id"] = profile_id
        profile_path = root / "profiles" / (profile_id + ".json")
        profile_bytes = encoded(profile)
        pointer = {"schema": "laplace.chess-active-profile/v1", "mode": usage,
                   "profile_id": profile_id, "path": str(profile_path),
                   "sha256": sha(profile_bytes)}
        prepared.append((usage, profile_path, profile_bytes, pointer))
    for _, profile_path, profile_bytes, _ in prepared:
        write_atomic(profile_path, profile_bytes, immutable=True)
    activated = {}
    for usage, _, _, pointer in prepared:
        write_atomic(root / "profiles" / ("active-" + usage + ".json"), encoded(pointer))
        activated[usage] = pointer
    return {"schema": "laplace.chess-profile-activation/v1", "profiles": activated,
            "calibration_sha256": receipt_sha256, "database_recording_capacity_measured": False}


def active(prefix: Path, mode: str) -> tuple[dict, dict, dict, dict]:
    tools.require(mode in ("analysis", "games"), "run-profile requires analysis or games")
    root = prefix.resolve()
    pointer, _ = document(root / "profiles" / ("active-" + mode + ".json"))
    tools.require(pointer["schema"] == "laplace.chess-active-profile/v1" and pointer["mode"] == mode and
                  bool(re.fullmatch(r"[0-9a-f]{64}", pointer["profile_id"])),
                  "invalid active measured-profile selection")
    path = root / "profiles" / (pointer["profile_id"] + ".json")
    tools.require(str(path) == pointer["path"], "active profile escaped its addressed store")
    profile, _ = document(path, pointer["sha256"])
    body = {key: value for key, value in profile.items() if key != "profile_id"}
    tools.require(profile["schema"] == PROFILE_SCHEMA and profile["mode"] == mode and
                  sha(encoded(body)) == profile["profile_id"] == pointer["profile_id"],
                  "active measured-profile identity differs")
    calibration = profile["calibration"]
    receipt = root / "calibrations" / calibration["sha256"] / "receipt.json"
    tools.require(bool(re.fullmatch(r"[0-9a-f]{64}", calibration["sha256"])) and
                  str(receipt) == calibration["path"], "calibration escaped its addressed store")
    report, _ = document(receipt, calibration["sha256"])
    installed, host = observed(prefix)
    selected = validate(report, installed, host, profile["maximum_age_seconds"])
    key = "single_engine_fixed_depth_suite" if mode == "analysis" else "aggregate_game_throughput"
    tools.require(profile["configuration"] == selected[key]["configuration"] and
                  profile["tool_identity"] == tool_identity(installed) and
                  profile["host_signature"] == host_signature(host),
                  "active profile no longer matches authenticated calibration")
    fit(profile["configuration"], report, host, mode)
    return profile, report, installed, host


def uci_plan(profile: dict, report: dict, host: dict, text: str,
             network: dict) -> tuple[list[str], dict, dict]:
    tools.require(len(text.encode()) <= MIB and "\x00" not in text,
                  "UCI command file must be finite UTF-8 without NUL")
    caller = [line for line in text.splitlines() if line.strip()]
    tools.require(all(line == line.strip() for line in caller),
                  "UCI command lines must not have leading or trailing whitespace")
    tools.require(caller and ("quit" not in caller or caller[-1] == "quit") and
                  caller.count("quit") <= 1, "UCI commands after quit are not executable")
    configuration = dict(profile["configuration"])
    commands = ["uci", "setoption name Threads value " + str(configuration["threads"]),
                "setoption name Hash value " + str(configuration["hash_mib"]),
                "setoption name NumaPolicy value auto",
                "setoption name UCI_LimitStrength value false",
                "setoption name Skill Level value 20",
                "setoption name MultiPV value 1", "isready"]
    grant = fit(configuration, report, host, "analysis")
    snapshots = [dict(configuration)]
    allowed = {"uci", "isready", "setoption", "ucinewgame", "position", "go", "stop", "quit", "d", "eval", "compiler"}
    for line in caller:
        token = line.split()[0]
        tools.require(token in allowed, "unsupported finite UCI command: " + token)
        if token == "setoption":
            match = re.fullmatch(r"setoption\s+name\s+(.+?)(?:\s+value\s+(.*))?", line)
            tools.require(match is not None, "invalid UCI setoption")
            name, value = match.groups()
            normalized = name.lower()
            if normalized in ("threads", "hash"):
                tools.require(value is not None and value.isdigit(), "Threads and Hash require positive integers")
                configuration["threads" if normalized == "threads" else "hash_mib"] = int(value)
            elif normalized == "evalfile":
                tools.require(value is not None and Path(value).name == network["network"] and
                              Path(value).resolve() == Path(network["path"]).resolve(),
                              "EvalFile override differs from calibrated NNUE identity")
            elif normalized == "ponder":
                tools.require(value is not None and value.lower() == "false",
                              "ponder is outside the measured resource law")
        if token == "go":
            words = line.split()
            tools.require("infinite" not in words and "ponder" not in words and
                          any(key in words for key in ("depth", "nodes", "movetime", "wtime", "btime", "mate")),
                          "finite UCI input requires a bounded non-ponder search")
        current = fit(configuration, report, host, "analysis")
        grant = {**grant, "cpu_equivalents": max(grant["cpu_equivalents"], current["cpu_equivalents"]),
                 "memory_mib": max(grant["memory_mib"], current["memory_mib"])}
        snapshots.append(dict(configuration))
        commands.append(line)
    if caller[-1] != "quit":
        commands.append("quit")
    return commands, {"initial": profile["configuration"], "effective_final": configuration,
                      "configuration_sequence": snapshots, "caller_options_override_defaults": True}, grant


def assignments(tokens: list[str]) -> dict[str, str]:
    result = {}
    for token in tokens:
        key, separator, value = token.partition("=")
        tools.require(bool(separator) and bool(key), "engine options require key=value")
        for name in ("Threads", "Hash", "EvalFile", "Ponder"):
            if key.lower() == ("option." + name).lower():
                key = "option." + name
        result[key] = value
    return result


def cutechess_plan(profile: dict, report: dict, host: dict,
                   installed: dict, extra: list[str], output: Path) -> tuple[list[str], dict, dict]:
    tools.require(sum(len(value) for value in extra) <= MIB, "CuteChess arguments exceed finite boundary")
    groups = []
    for value in extra:
        if value.startswith("-"):
            groups.append([value])
        else:
            tools.require(bool(groups), "CuteChess options must start with an option name")
            groups[-1].append(value)
    engines, common, global_options = [], {}, {}
    for group in groups:
        if group[0] == "-engine":
            engines.append(assignments(group[1:]))
        elif group[0] == "-each":
            common.update(assignments(group[1:]))
        else:
            global_options[group[0]] = group[1:]
    tools.require(len(engines) in (0, 2), "measured game profile requires exactly two source engine slots")
    engines = engines or [{}, {}]
    configuration = profile["configuration"]
    concurrency_values = global_options.pop("-concurrency", [str(configuration["concurrency"])])
    tools.require(len(concurrency_values) == 1 and concurrency_values[0].isdigit(),
                  "CuteChess concurrency requires one positive integer")
    concurrency = int(concurrency_values[0])
    sf = installed["tools"]["stockfish"]
    defaults = {"proto": "uci", "tc": report["game_workload"]["time_control"],
                "depth": str(report["game_workload"]["search_limit"]["depth"]),
                "option.Threads": str(configuration["threads"]),
                "option.Hash": str(configuration["hash_mib"]),
                "option.UCI_LimitStrength": "false", "option.Skill Level": "20",
                "option.MultiPV": "1", "option.NumaPolicy": "auto"}
    command = [installed["tools"]["cutechess"]["executable"]]
    effective = []
    max_threads = max_hash = 0
    for index, overrides in enumerate(engines):
        values = {**defaults, **common, **overrides}
        tools.require(values.get("cmd", sf["executable"]) == sf["executable"] and
                      values["proto"] == "uci" and not any(key in values for key in ("conf", "arg", "initstr")),
                      "measured game slots require direct recorded UCI source engines")
        tools.require(values.get("ponder", "false").lower() == "false" and
                      values.get("option.Ponder", "false").lower() == "false",
                      "ponder is outside the measured resource law")
        if "option.EvalFile" in values:
            tools.require(Path(values["option.EvalFile"]).resolve() ==
                          (Path(sf["source"]) / "src" / sf["checks"]["network"]).resolve(),
                          "EvalFile override differs from calibrated NNUE identity")
        tools.require(values["option.Threads"].isdigit() and values["option.Hash"].isdigit(),
                      "Threads and Hash require positive integer values")
        threads, hash_mib = int(values["option.Threads"]), int(values["option.Hash"])
        fit({"threads": threads, "hash_mib": hash_mib, "concurrency": concurrency}, report, host, "games")
        max_threads, max_hash = max(max_threads, threads), max(max_hash, hash_mib)
        values = {"cmd": sf["executable"], "name": "Source-" + str(index + 1), **values}
        command += ["-engine", *(key + "=" + value for key, value in values.items())]
        effective.append(values)
    grant = fit({"threads": max_threads, "hash_mib": max_hash, "concurrency": concurrency}, report, host, "games")
    global_defaults = {"-games": [str(report["profile"]["games"])], "-rounds": ["1"],
                       "-repeat": [], "-pgnout": [str(output / "games.pgn")]}
    global_defaults.update(global_options)
    for key, values in global_defaults.items():
        command += [key, *values]
    command += ["-concurrency", str(concurrency)]
    return command, {"concurrency": concurrency, "engines": effective,
                     "caller_options_override_defaults": True,
                     "throughput_claim": "unmeasured invocation; calibration rate is not replayed evidence"}, grant


def execute_uci(command: list[str], commands: list[str], transcript: Path,
                host: dict, timeout: float, memory_bytes: int, environment: dict) -> dict:
    started = time.monotonic()
    received = queue.Queue()
    output_bytes = 0
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, bufsize=1,
                               start_new_session=True, env=environment)
    def reader():
        nonlocal output_bytes
        try:
            with transcript.open("w") as output:
                for line in process.stdout:
                    output_bytes += len(line.encode())
                    if output_bytes > 64 * MIB:
                        received.put(("error", "engine transcript exceeded 64 MiB"))
                        return
                    output.write(line)
                    output.flush()
                    received.put(("line", line.rstrip("\n")))
        except Exception as error:
            received.put(("error", str(error)))
        finally:
            received.put(("end", None))
    worker = threading.Thread(target=reader, daemon=True)
    worker.start()
    peak = 0
    def until(prefix):
        nonlocal peak
        while True:
            tools.require(time.monotonic() - started < timeout, "measured-profile invocation timed out")
            observed = bench.sample_group(process.pid)
            peak = max(peak, observed["rss_bytes"] or 0)
            tools.require(peak <= memory_bytes, "measured-profile invocation exceeded sampled memory grant")
            try:
                kind, line = received.get(timeout=0.05)
            except queue.Empty:
                continue
            tools.require(kind != "error", "engine output failure: " + str(line))
            if kind == "end":
                tools.require(prefix is None, "engine exited before " + str(prefix))
                return
            tools.require("CRITICAL ERROR" not in line, "engine reported a critical error")
            if prefix is not None and line.startswith(prefix):
                return
    try:
        for line in commands:
            process.stdin.write(line + "\n")
            process.stdin.flush()
            token = line.split()[0]
            if token == "uci":
                until("uciok")
            elif token == "isready":
                until("readyok")
            elif token == "go":
                until("bestmove ")
        process.stdin.close()
        until(None)
        code = process.wait(timeout=max(0.01, timeout - (time.monotonic() - started)))
        tools.require(code == 0, "source engine exited unsuccessfully")
        return {"command": command, "exit_code": code, "completion": "completed",
                "wall_seconds": time.monotonic() - started, "sampled_peak_rss_bytes": peak,
                "transcript": str(transcript), "transcript_sha256": tools.digest(transcript),
                "search_completion": "waited for bestmove after every finite go command",
                "memory_limit_enforcement": "sampled process-group RSS; brief peaks may be missed"}
    finally:
        bench.cleanup_group(process)
        worker.join(timeout=2)
        for stream in (process.stdin, process.stdout):
            if stream and not stream.closed:
                stream.close()


def run(prefix: Path, mode: str, output: Path, uci_input: Path | None,
        extra: list[str], timeout: float) -> int:
    tools.require(math.isfinite(timeout) and timeout > 0, "a positive finite invocation timeout is required")
    profile, report, installed, host = active(prefix, mode)
    output.mkdir(parents=True, exist_ok=False)
    output = output.resolve()
    result = {"schema": "laplace.chess-profile-invocation/v1", "completion": "preparing",
              "profile_id": profile["profile_id"], "calibration": profile["calibration"],
              "mode": mode, "host_before": host, "tool_identity": tool_identity(installed),
              "database_recording": {"measured": False, "recorded_games": None, "games_per_second": None},
              "playing_strength_inferred": False}
    try:
        if mode == "analysis":
            tools.require(uci_input is not None and not extra, "analysis requires --uci-input and no engine argv overrides")
            tools.require(uci_input.is_file() and not uci_input.is_symlink() and
                          uci_input.stat().st_size <= MIB, "invalid finite UCI input file")
            caller = uci_input.read_bytes()
            sf = installed["tools"]["stockfish"]
            network = {**sf["checks"], "path": str(Path(sf["source"]) / "src" / sf["checks"]["network"])}
            commands, configuration, grant = uci_plan(profile, report, host, caller.decode("utf-8"), network)
            command = [sf["executable"]]
            actual_input = ("\n".join(commands) + "\n").encode()
            (output / "caller-input.txt").write_bytes(caller)
            (output / "input.txt").write_bytes(actual_input)
            result.update({"command": command, "input_sha256": sha(actual_input),
                           "caller_input_sha256": sha(caller), "configuration": configuration,
                           "resource_grant": grant})
            tools.json_write(output / "receipt.json", result)
            result["execution"] = execute_uci(command, commands, output / "engine.log",
                                             host, timeout, grant["memory_mib"] * MIB, tools.tool_environment(sf))
        else:
            tools.require(uci_input is None, "game mode accepts CuteChess arguments, not --uci-input")
            command, configuration, grant = cutechess_plan(profile, report, host, installed, extra, output)
            result.update({"command": command, "caller_arguments": extra,
                           "configuration": configuration, "resource_grant": grant})
            tools.json_write(output / "receipt.json", result)
            result["execution"] = bench.measured_process(command, output / "cutechess.log", host,
                timeout, grant["memory_mib"] * MIB,
                environment=tools.tool_environment(installed["tools"]["cutechess"]))
            tools.require(result["execution"]["completion"] == "completed", "CuteChess profile invocation failed")
            transcript = (output / "cutechess.log").read_text()
            tools.require("Finished match" in transcript, "CuteChess did not finish the requested match")
            pgn_index = command.index("-pgnout") + 1
            tools.require(pgn_index < len(command), "CuteChess invocation has no PGN output path")
            pgn = Path(command[pgn_index])
            tools.require(pgn.is_file() and not pgn.is_symlink(), "CuteChess did not retain its requested PGN")
            result["generated_pgn"] = {"path": str(pgn.resolve()), "sha256": tools.digest(pgn),
                                       "byte_count": pgn.stat().st_size,
                                       "legal_terminal_validation": "not performed by profile invocation",
                                       "database_recorded": False}

        final_installed, final_host = observed(prefix)
        validate(report, final_installed, final_host, profile["maximum_age_seconds"])
        result.update({"completion": "completed", "host_after": final_host})
        tools.json_write(output / "receipt.json", result)
        print(json.dumps({"completion": result["completion"], "receipt": str(output / "receipt.json"),
                          "profile_id": profile["profile_id"]}))
        return 0
    except Exception as error:
        result.update({"completion": "failed", "error": str(error)})
        tools.json_write(output / "receipt.json", result)
        raise
