#!/usr/bin/env python3
"""Measure recorded chess executables inside an explicit observed resource grant."""

from __future__ import annotations

import argparse
import io
import itertools
import json
import math
import os
from pathlib import Path
import platform
import random
import re
import resource
import signal
import statistics
import subprocess
import sys
import time

import chess_tools as tools
import chess_pgn

MIB = 1024 * 1024
PROCESS_METRICS = ("rss_bytes", "pss_bytes", "anon_huge_bytes", "explicit_hugetlb_bytes", "processes", "threads", "smaps_unreadable_processes")


def read(path: Path) -> str | None:
    try:
        return path.read_text().strip()
    except OSError:
        return None


def integer_list(value: str) -> list[int]:
    result = [int(part) for part in value.split(",")]
    tools.require(bool(result) and len(set(result)) == len(result) and min(result) > 0, "sweep values must be unique positive integers")
    return sorted(result)


def cpus(value: str) -> set[int]:
    result = set()
    for part in filter(None, value.split(",")):
        begin, _, end = part.partition("-")
        result.update(range(int(begin), int(end or begin) + 1))
    return result


def fields(text: str | None) -> dict[str, int]:
    result = {}
    for line in (text or "").splitlines():
        parts = line.replace(":", "").split()
        if len(parts) >= 2 and parts[1].isdigit():
            result[parts[0]] = int(parts[1])
    return result


def visible_cgroups(proc: Path = Path("/proc")) -> tuple[list[Path], str]:
    membership = next((line[3:] for line in (read(proc / "self/cgroup") or "").splitlines() if line.startswith("0::")), None)
    for line in (read(proc / "self/mountinfo") or "").splitlines():
        before, _, after = line.partition(" - ")
        if after.split()[:1] != ["cgroup2"] or membership is None:
            continue
        values = before.split()
        mount_root, mount_path = Path(values[3]), Path(values[4].replace("\\040", " "))
        try:
            relative = Path(membership).relative_to(mount_root)
        except ValueError:
            continue
        leaf = mount_path / relative
        chain = [leaf]
        while chain[-1] != mount_path:
            chain.append(chain[-1].parent)
        return chain, "visible-v2-hierarchy-only; namespace-hidden ancestors are unobservable"
    return [], "cgroup-v2 limits unavailable; require explicit CPU and memory budgets"


def resource_bounds(affinity_count: int, available_bytes: int, ancestors: list[dict]) -> tuple[float, int]:
    quota = float(affinity_count)
    memory = available_bytes
    for item in ancestors:
        raw = item.get("cpu.max")
        if raw:
            maximum, period = raw.split()
            if maximum != "max":
                quota = min(quota, int(maximum) / int(period))
        limit, usage = item.get("memory.max"), item.get("memory.current")
        if limit and limit != "max" and usage:
            memory = min(memory, max(0, int(limit) - int(usage)))
    return quota, memory


def process_identity() -> dict:
    stat = read(Path("/proc/self/stat"))
    observed_pid = int(stat.split(" ", 1)[0]) if stat else None
    runtime_pid = os.getpid()
    return {"runtime_pid": runtime_pid, "proc_self_pid": observed_pid, "consistent": runtime_pid == observed_pid, "rule": "never attribute /proc PID or process-group metrics when runtime and procfs identities disagree"}


def host_observation() -> dict:
    tools.require(platform.system() == "Linux", "this benchmark resource observer currently requires Linux /proc and cgroup observation")
    affinity = sorted(os.sched_getaffinity(0))
    cpuinfo = read(Path("/proc/cpuinfo")) or ""
    model = sorted(set(re.findall(r"^model name\s*:\s*(.+)$", cpuinfo, re.M)))
    flags = sorted(set(re.findall(r"^flags\s*:\s*(.+)$", cpuinfo, re.M)))
    topology = []
    for cpu in affinity:
        root = Path(f"/sys/devices/system/cpu/cpu{cpu}")
        topology.append({"cpu": cpu, "package": read(root / "topology/physical_package_id"), "core": read(root / "topology/core_id"), "siblings": read(root / "topology/thread_siblings_list"), "governor": read(root / "cpufreq/scaling_governor")})
    numa = [{"node": path.name, "affinity_cpus": sorted(cpus(read(path / "cpulist") or "") & set(affinity))} for path in sorted(Path("/sys/devices/system/node").glob("node[0-9]*"))]
    memory = fields(read(Path("/proc/meminfo")))
    chain, visibility = visible_cgroups()
    ancestors = [{"path": str(path), **{name: read(path / name) for name in ("cpu.max", "cpu.weight", "cpuset.cpus.effective", "cpuset.mems.effective", "memory.max", "memory.current", "memory.high")}} for path in chain]
    quota, headroom = resource_bounds(len(affinity), memory.get("MemAvailable", 0) * 1024, ancestors)
    return {"observed_at_unix": time.time(), "kernel": platform.release(), "machine": platform.machine(), "cpu_models": model, "cpu_flags": flags, "affinity": affinity, "visible_logical_cpus": len(affinity), "visible_physical_cores": len({(item["package"], item["core"]) for item in topology}), "topology": topology, "numa": numa, "memory_total_bytes": memory.get("MemTotal", 0) * 1024, "host_available_bytes": memory.get("MemAvailable", 0) * 1024, "effective_cpu_equivalents": quota, "effective_memory_headroom_bytes": headroom, "cgroups": ancestors, "cgroup_visibility": visibility, "load_average": os.getloadavg(), "transparent_hugepage_policy": read(Path("/sys/kernel/mm/transparent_hugepage/enabled")), "process_identity": process_identity(), "scope": "current execution environment; not proof of the user's separate deployment host"}


def group_pressure(host: dict) -> dict:
    return {item["path"]: {name: fields(read(Path(item["path"]) / name)) if name in ("cpu.stat", "memory.events") else read(Path(item["path"]) / name) for name in ("cpu.stat", "cpu.pressure", "memory.events", "memory.pressure", "memory.current")} for item in host["cgroups"]}


def numeric_delta(before: dict, after: dict) -> dict:
    result = {}
    for key, value in after.items():
        if isinstance(value, dict):
            result[key] = numeric_delta(before.get(key, {}), value)
        elif isinstance(value, int) and isinstance(before.get(key), int):
            result[key] = value - before[key]
    return result


def sample_group(group: int) -> dict:
    totals = dict.fromkeys(PROCESS_METRICS, 0)
    for directory in Path("/proc").iterdir():
        if not directory.name.isdigit():
            continue
        stat = read(directory / "stat")
        if not stat:
            continue
        values = stat[stat.rfind(")") + 2:].split()
        if int(values[2]) != group:
            continue
        status = fields(read(directory / "status"))
        map_text = read(directory / "smaps_rollup")
        maps = fields(map_text)
        totals["smaps_unreadable_processes"] += map_text is None
        totals["processes"] += 1
        totals["threads"] += status.get("Threads", 0)
        totals["rss_bytes"] += status.get("VmRSS", 0) * 1024
        totals["pss_bytes"] += maps.get("Pss", 0) * 1024
        totals["anon_huge_bytes"] += maps.get("AnonHugePages", 0) * 1024
        totals["explicit_hugetlb_bytes"] += (maps.get("Private_Hugetlb", 0) + maps.get("Shared_Hugetlb", 0)) * 1024
    if totals["smaps_unreadable_processes"]:
        for key in ("pss_bytes", "anon_huge_bytes", "explicit_hugetlb_bytes"):
            totals[key] = None
    return totals


def merge_peak(previous: dict, observed: dict) -> dict:
    return {key: max(value, observed[key]) if value is not None and observed[key] is not None else None for key, value in previous.items()}


def cleanup_group(process: subprocess.Popen) -> None:
    # The leader may already have exited while its engine children are alive.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def measured_process(command: list[str], transcript: Path, host: dict, timeout: float, memory_bytes: int, input_text: str | None = None, *, environment: dict[str, str] | None = None) -> dict:
    before_usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    before_pressure = group_pressure(host)
    started = time.perf_counter()
    identity = host.get("process_identity") or process_identity()
    proc_metrics_available = identity["consistent"]
    peak = dict.fromkeys(PROCESS_METRICS, 0 if proc_metrics_available else None)
    disposition = "completed"
    with transcript.open("w") as output:
        process = subprocess.Popen(command, stdin=subprocess.PIPE if input_text else subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT, start_new_session=True, text=True, env=environment)
        try:
            if input_text:
                assert process.stdin
                process.stdin.write(input_text)
                process.stdin.close()
            while process.poll() is None:
                if proc_metrics_available:
                    observed = sample_group(process.pid)
                    peak = merge_peak(peak, observed)
                elapsed = time.perf_counter() - started
                over_memory = proc_metrics_available and peak["rss_bytes"] > memory_bytes
                if elapsed > timeout or over_memory or transcript.stat().st_size > 64 * MIB:
                    disposition = "timeout" if elapsed > timeout else "sampled-memory-or-output-budget-exhausted"
                    os.killpg(process.pid, signal.SIGKILL)
                    break
                time.sleep(0.05)
            code = process.wait()
        finally:
            cleanup_group(process)
    elapsed = time.perf_counter() - started
    after_usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    after_pressure = group_pressure(host)
    return {"command": command, "input_sha256": tools.hashlib.sha256((input_text or "").encode()).hexdigest(), "wall_seconds": elapsed, "user_cpu_seconds": after_usage.ru_utime - before_usage.ru_utime, "system_cpu_seconds": after_usage.ru_stime - before_usage.ru_stime, "input_blocks": after_usage.ru_inblock - before_usage.ru_inblock, "output_blocks": after_usage.ru_oublock - before_usage.ru_oublock, "voluntary_context_switches": after_usage.ru_nvcsw - before_usage.ru_nvcsw, "involuntary_context_switches": after_usage.ru_nivcsw - before_usage.ru_nivcsw, "sampled_peak": peak, "sampling_period_seconds": 0.05, "process_metrics_available": proc_metrics_available, "memory_limit_enforcement": "sampled process-group RSS; brief peaks between observations may be missed" if proc_metrics_available else "unavailable: runtime/procfs PID mismatch; admission uses explicit estimates and cgroup limits only", "cgroup_counter_delta": numeric_delta(before_pressure, after_pressure), "pressure_before": before_pressure, "pressure_after": after_pressure, "transcript": str(transcript), "transcript_sha256": tools.digest(transcript), "exit_code": code, "completion": disposition if disposition != "completed" else ("completed" if code == 0 else "process-failed")}


def parse_options(transcript: str) -> dict:
    result = {}
    for line in transcript.splitlines():
        match = re.match(r"option name (.+?) type (\S+)(.*)$", line)
        if not match:
            continue
        name, kind, tail = match.groups()
        entry = {"type": kind, "raw": line}
        for field in ("default", "min", "max"):
            found = re.search(rf"\b{field} (.*?)(?= (?:min|max|var) |$)", tail)
            if found:
                entry[field] = found.group(1).strip()
        result[name] = entry
    tools.require("Threads" in result and "Hash" in result and "uciok" in transcript and "readyok" in transcript, "UCI capability readback is incomplete")
    return result


def parse_bench(transcript: str) -> dict:
    result = {}
    for label, key in (("Total time (ms)", "engine_milliseconds"), ("Nodes searched", "nodes"), ("Nodes/second", "engine_nodes_per_second")):
        found = re.findall(re.escape(label) + r"\s*:\s*(\d+)", transcript)
        tools.require(len(found) == 1 and int(found[0]) > 0, f"bench summary missing or ambiguous: {label}")
        result[key] = int(found[0])
    positions = re.findall(r"Position:\s*(\d+)/(\d+)", transcript)
    tools.require(bool(positions), "bench position completion receipt missing")
    total = int(positions[-1][1])
    tools.require([int(item[0]) for item in positions] == list(range(1, total + 1)), "bench did not complete its exact position set")
    tools.require("CRITICAL ERROR" not in transcript, "Stockfish rejected a benchmark command")
    result["position_count"] = total
    result["result_fingerprint"] = tools.hashlib.sha256("\n".join(line for line in transcript.splitlines() if line.startswith("bestmove ")).encode()).hexdigest()
    return result


def validate_pgn(text: str, games: int, *, provider: tuple | None = None,
                 diagnostic: bool = False, max_moves: int | None = None) -> dict:
    """Validate CuteChess's standard-start mainlines with the locked rules provider."""
    tools.require(games > 0 and games % 2 == 0, "PGN game count must be positive and even")
    tools.require(max_moves is None or (diagnostic and max_moves > 0), "move caps require explicit diagnostic mode")
    chess, pgn, _ = provider or chess_pgn.load_provider(tools.SCRATCH / "chess-pgn-validation")
    blocks = re.split(r"(?=^\[Event )", text, flags=re.M)
    records = []
    plies = 0
    for block in filter(lambda value: value.strip(), blocks):
        tools.require(block.lstrip().startswith('[Event "'), "PGN has content outside a game")
        tag_pattern = r'^\[(\w+) "((?:[^"\\\n]|\\.)*)"\][ \t]*$'
        tag_rows = re.findall(tag_pattern, block, re.M)
        tags = dict(tag_rows)
        tools.require(len(tags) == len(tag_rows), "PGN has duplicate header tags")
        tools.require(tags.get("Result") in {"1-0", "0-1", "1/2-1/2"}, "PGN game is unfinished")
        tools.require(not re.search(r"(?:stall|crash|disconnect|illegal move|time forfeit)", block, re.I), "PGN records a process/protocol failure")
        stream = io.StringIO(block)
        game = pgn.read_game(stream)
        tools.require(game is not None and not game.errors, f"PGN contains illegal or invalid moves: {getattr(game, 'errors', None)}")
        tools.require(pgn.read_game(stream) is None, "PGN block contains more than one game")
        board = game.board()
        tools.require(type(board) is chess.Board and not board.chess960 and board.fen() == chess.STARTING_FEN,
                      "benchmark PGN must start from the standard initial position")
        tools.require(board.is_valid(), "PGN initial position is invalid")
        # The upstream parser deliberately tolerates unknown text. For the plain
        # mainlines emitted by CuteChess, account for every serialized SAN token
        # and replay it through the same upstream rules provider as well.
        movetext = re.sub(tag_pattern, " ", block, flags=re.M)
        movetext = re.sub(r"\{[^{}]*\}|;[^\n]*", " ", movetext)
        tokens = re.sub(r"(?<!\S)\d+\.(?:\.\.)?", " ", movetext).split()
        tokens = [token for token in tokens if not re.fullmatch(r"\$\d+|[!?]+", token)]
        tools.require(bool(tokens) and tokens[-1] == tags["Result"], "PGN movetext result is missing or disagrees with Result")
        san_tokens = tokens[:-1]
        moves = list(game.mainline_moves())
        tools.require(len(san_tokens) == len(moves), "PGN contains unaccounted movetext or result markers")
        for token, move in zip(san_tokens, moves):
            tools.require(board.outcome(claim_draw=False) is None, "PGN continues after a terminal position")
            try:
                parsed = board.parse_san(re.sub(r"[!?]+$", "", token))
            except ValueError as error:
                raise tools.ChessToolError(f"PGN contains invalid SAN: {token}") from error
            tools.require(bool(move) and parsed == move and board.is_legal(move), "PGN contains an illegal or null move")
            board.push(move)
        game_plies = len(moves)
        if "PlyCount" in tags:
            tools.require(int(tags["PlyCount"]) == game_plies, "PGN PlyCount disagrees with serialized moves")
        termination = tags.get("Termination", "").lower()
        outcome = board.outcome(claim_draw=True)
        normal = termination in {"", "normal"} and not re.search(r"adjudicat|resign|maximal game length", block, re.I)
        if normal:
            tools.require(outcome is not None and outcome.result() == tags["Result"], "PGN result lacks a matching legal terminal outcome")
        else:
            tools.require(diagnostic and max_moves is not None and termination == "adjudication"
                          and "Draw by adjudication: maximal game length" in block
                          and tags["Result"] == "1/2-1/2" and game_plies == 2 * max_moves,
                          "PGN non-normal termination is not an explicitly capped diagnostic")
            tools.require(outcome is None or outcome.result() == tags["Result"], "PGN adjudication contradicts the legal terminal outcome")
        if max_moves is not None:
            tools.require(game_plies <= 2 * max_moves, "PGN exceeds the declared diagnostic move cap")
        records.append({"white": tags.get("White"), "black": tags.get("Black"), "result": tags["Result"],
                        "plies": game_plies, "termination": termination or "normal",
                        "normal_completion": normal, "board_outcome": outcome.termination.name if outcome else None,
                        "final_fen": board.fen(), "time_control": tags.get("TimeControl"),
                        "moves_sha256": tools.hashlib.sha256(" ".join(move.uci() for move in moves).encode()).hexdigest()})
        plies += game_plies
    tools.require(len(records) == games, f"expected {games} PGNs; observed {len(records)}")
    expected = games // 2
    tools.require(sum(record["white"] == "Source-A" and record["black"] == "Source-B" for record in records) == expected, "PGN color pairing is unbalanced")
    tools.require(sum(record["white"] == "Source-B" and record["black"] == "Source-A" for record in records) == expected, "PGN reverse color pairing is unbalanced")
    completed = sum(record["normal_completion"] for record in records)
    return {"games": games, "normal_completed_games": completed, "capped_diagnostic_games": games - completed,
            "plies": plies, "records": records, "legal_moves_validated": True,
            "result_fingerprint": tools.hashlib.sha256(json.dumps(records, sort_keys=True).encode()).hexdigest()}


def game_workload(arguments: argparse.Namespace) -> dict:
    tools.require(arguments.max_moves is None or (arguments.diagnostic and arguments.max_moves > 0),
                  "--max-moves requires --diagnostic and a positive move cap")
    return {"mode": "diagnostic" if arguments.diagnostic else "complete-legal-games",
            "search_limit": {"type": "depth", "depth": arguments.game_depth},
            "time_control": arguments.game_time_control, "max_moves": arguments.max_moves,
            "adjudication": "maximal game length" if arguments.max_moves is not None else "none",
            "start_position": "standard", "draw_claims": True,
            "throughput_scope": "capped/diagnostic episodes" if arguments.diagnostic else "legally completed game generation",
            "database_recording": {"measured": False, "recorded_games": None, "games_per_second": None}}


def cutechess_command(arguments: argparse.Namespace, configuration: dict, cc: str, sf: str, pgn: Path) -> list[str]:
    game_workload(arguments)
    command = [cc, "-engine", f"cmd={sf}", "name=Source-A", "-engine", f"cmd={sf}", "name=Source-B",
               "-each", "proto=uci", f"tc={arguments.game_time_control}", f"depth={arguments.game_depth}",
               f'option.Threads={configuration["threads"]}', f'option.Hash={configuration["hash_mib"]}',
               "option.UCI_LimitStrength=false", "option.Skill Level=20", "option.MultiPV=1", "option.NumaPolicy=auto",
               "-games", str(arguments.games), "-rounds", "1", "-repeat", "-concurrency", str(configuration["concurrency"])]
    if arguments.max_moves is not None:
        command += ["-maxmoves", str(arguments.max_moves)]
    return command + ["-pgnout", str(pgn)]


def game_rates(validation: dict, wall_seconds: float, diagnostic: bool) -> dict:
    tools.require(wall_seconds > 0, "game measurement must have positive elapsed time")
    key = "diagnostic_episodes_per_second" if diagnostic else "normal_completed_games_per_second"
    if not diagnostic:
        tools.require(validation["normal_completed_games"] == validation["games"], "incomplete games cannot supply full-game throughput")
    return {key: validation["games"] / wall_seconds, "plies_per_second": validation["plies"] / wall_seconds}


def remaining_timeout(deadline: float, per_case: float) -> float:
    remaining = deadline - time.monotonic()
    tools.require(remaining > 0, "overall benchmark timeout exhausted")
    return min(per_case, remaining)


def automatic_sweep_values(limit: int, physical_boundary: int | None) -> list[int]:
    values = {1, limit, *[2 ** index for index in range(limit.bit_length()) if 2 ** index <= limit]}
    if type(physical_boundary) is int and 0 < physical_boundary <= limit:
        values.add(physical_boundary)
    return sorted(values)


def plan(arguments: argparse.Namespace, host: dict, options: dict) -> dict:
    cpu = arguments.cpu_budget if arguments.cpu_budget is not None else math.floor(host["effective_cpu_equivalents"])
    tools.require(0 < cpu <= host["effective_cpu_equivalents"], "CPU budget exceeds the observed affinity/quota boundary or less than one full CPU equivalent is available")
    if not host["cgroups"]:
        tools.require(arguments.cpu_budget is not None and arguments.memory_mib is not None, "unobserved cgroup limits require explicit CPU and memory budgets")
    physical_cores = host.get("visible_physical_cores")
    threads = integer_list(arguments.threads) if arguments.threads else automatic_sweep_values(math.floor(cpu), physical_cores)
    hashes = integer_list(arguments.hash_mib)
    physical_games = physical_cores // arguments.game_threads if type(physical_cores) is int else None
    concurrency = integer_list(arguments.concurrency) if arguments.concurrency else automatic_sweep_values(max(1, math.floor(cpu / arguments.game_threads)), physical_games)
    if arguments.memory_mib is None:
        # Available host memory is a ceiling, not the sweep's working grant.
        # Charging nearly all available memory made unrelated background growth
        # invalidate a small completed experiment at the ending-headroom check.
        available = host["effective_memory_headroom_bytes"] // MIB - arguments.reserve_memory_mib
        tools.require(available > 0, "memory reserve leaves no experiment headroom")
        demands = [hash_mib + arguments.engine_overhead_mib for count, hash_mib in itertools.product(threads, hashes)
                   if count <= min(cpu, int(options["Threads"]["max"])) and hash_mib <= int(options["Hash"]["max"])]
        demands += [2 * count * (arguments.game_hash_mib + arguments.engine_overhead_mib) for count in concurrency
                    if count <= arguments.games and count * arguments.game_threads <= cpu
                    and arguments.game_threads <= int(options["Threads"]["max"]) and arguments.game_hash_mib <= int(options["Hash"]["max"])]
        fitted_demand = max((demand for demand in demands if demand <= available), default=0)
        memory = min(available, fitted_demand + arguments.reserve_memory_mib)
        memory_selection = "largest fitting sweep resident estimate plus explicit margin, capped below observed headroom"
    else:
        memory = arguments.memory_mib
        memory_selection = "explicit caller budget"
    tools.require(memory > 0 and memory * MIB <= host["effective_memory_headroom_bytes"], "memory budget exceeds current effective headroom")
    rejected, serial, games = [], [], []
    for count, hash_mib in itertools.product(threads, hashes):
        value = {"threads": count, "hash_mib": hash_mib}
        if count > cpu or count > int(options["Threads"]["max"]) or hash_mib > int(options["Hash"]["max"]) or hash_mib + arguments.engine_overhead_mib > memory:
            rejected.append({**value, "profile": "stockfish", "reason": "CPU, Hash option or memory grant exceeded"})
        else:
            serial.append(value)
    for count in concurrency:
        demand = 2 * count * (arguments.game_hash_mib + arguments.engine_overhead_mib)
        value = {"concurrency": count, "threads": arguments.game_threads, "hash_mib": arguments.game_hash_mib, "estimated_resident_memory_mib": demand}
        if count > arguments.games or count * arguments.game_threads > cpu or demand > memory or arguments.game_threads > int(options["Threads"]["max"]) or arguments.game_hash_mib > int(options["Hash"]["max"]):
            rejected.append({**value, "profile": "cutechess", "reason": "concurrency exceeds game count, active search CPU, Hash option or two-engine memory grant"})
        else:
            games.append(value)
    tools.require(serial and games, "no configuration fits the observed resource grant")
    resident_estimate = max([item["hash_mib"] + arguments.engine_overhead_mib for item in serial] + [item["estimated_resident_memory_mib"] for item in games])
    return {"cpu_budget": cpu, "memory_mib": memory, "memory_budget_selection": memory_selection, "estimated_peak_resident_mib": resident_estimate, "memory_margin_mib": max(0, memory - resident_estimate), "requested_reserve_memory_mib": arguments.reserve_memory_mib, "resident_engine_overhead_estimate_mib": arguments.engine_overhead_mib, "memory_estimate_is_measurement": False, "ponder": False, "stockfish": serial, "cutechess": games, "rejected": rejected}


def stability_failures(before: dict, after: dict, initial_files: dict, final_files: dict, resource_plan: dict) -> list[str]:
    failures = []
    if initial_files != final_files:
        failures.append("an executable changed during the benchmark")
    for key in ("affinity", "effective_cpu_equivalents", "topology", "numa"):
        if before.get(key) != after.get(key):
            failures.append(f"host {key} changed during the benchmark")
    controls = ("path", "cpu.max", "cpu.weight", "cpuset.cpus.effective", "cpuset.mems.effective", "memory.max", "memory.high")
    before_controls = [{key: item.get(key) for key in controls} for item in before["cgroups"]]
    after_controls = [{key: item.get(key) for key in controls} for item in after["cgroups"]]
    if before_controls != after_controls:
        failures.append("visible cgroup resource controls changed during the benchmark")
    if after["effective_memory_headroom_bytes"] < resource_plan["memory_mib"] * MIB:
        failures.append("ending memory headroom no longer covers the admitted memory budget")
    return failures


def aggregates(samples: list[dict], key: str, value: str) -> list[dict]:
    grouped = {}
    for sample in samples:
        if sample["warmup"]:
            continue
        identity = json.dumps(sample["configuration"], sort_keys=True)
        grouped.setdefault(identity, []).append(sample)
    result = []
    for identity, selected in grouped.items():
        rates = [sample[value] for sample in selected]
        available_rss = [sample["sampled_peak"]["rss_bytes"] for sample in selected if sample["sampled_peak"]["rss_bytes"] is not None]
        summary = {"configuration": json.loads(identity), "samples": len(selected), key: statistics.median(rates), "minimum": min(rates), "maximum": max(rates), "wall_seconds_median": statistics.median(sample["wall_seconds"] for sample in selected), "sampled_peak_rss_bytes": max(available_rss) if available_rss else None, "mean_active_cpu_equivalents_median": statistics.median((sample["user_cpu_seconds"] + sample["system_cpu_seconds"]) / sample["wall_seconds"] for sample in selected)}
        for metric in ("nodes", "engine_nodes_per_second", "engine_milliseconds", "plies_per_second"):
            if metric in selected[0]:
                summary[f"median_{metric}"] = statistics.median(sample[metric] for sample in selected)
        for metric in ("games", "normal_completed_games", "capped_diagnostic_games", "plies"):
            if metric in selected[0]:
                summary[f"total_{metric}"] = sum(sample[metric] for sample in selected)
        summary["total_wall_seconds"] = sum(sample["wall_seconds"] for sample in selected)
        result.append(summary)
    return result


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["observe", "run"])
    parser.add_argument("--prefix", type=Path, default=Path("/opt/laplace/tools/chess"))
    parser.add_argument("--stockfish", type=Path, help="compare another Stockfish 19 build; record its exact executable hash and compiler report separately")
    parser.add_argument("--output", type=Path, required=True, help="new directory for the receipt, raw transcripts and PGNs")
    parser.add_argument("--threads")
    parser.add_argument("--hash-mib", default="16,64,256")
    parser.add_argument("--concurrency")
    parser.add_argument("--cpu-budget", type=int)
    parser.add_argument("--memory-mib", type=int)
    parser.add_argument("--reserve-memory-mib", type=int, default=512, help="automatic budget: retain this host headroom and add up to this margin above estimated peak sweep residency")
    parser.add_argument("--engine-overhead-mib", type=int, default=256)
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--depth", type=int, default=10)
    parser.add_argument("--games", type=int, default=8)
    parser.add_argument("--game-depth", type=int, default=6)
    parser.add_argument("--game-time-control", default="60", help="CuteChess time control, retained with the depth limit")
    parser.add_argument("--diagnostic", action="store_true", help="diagnostic episodes only; never recommend full-game throughput")
    parser.add_argument("--max-moves", type=int, help="explicit diagnostic move-pair cap; requires --diagnostic; normal games are uncapped")
    parser.add_argument("--game-threads", type=int, default=1)
    parser.add_argument("--game-hash-mib", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--overall-timeout", type=float, default=1800, help="wall budget for the complete measured sweep, including warmups and validation")
    parser.add_argument("--seed", type=int, default=20260915)
    parser.add_argument("--syzygy-manifest", type=Path)
    return parser


def recommendations(serial: list[dict], games: list[dict], workload: dict) -> dict:
    return {"scope": "provisional best measured configuration within this finite sweep",
            "single_engine_fixed_depth_suite": min(serial, key=lambda value: value["wall_seconds_median"]),
            "aggregate_game_throughput": None if workload["mode"] == "diagnostic" else max(games, key=lambda value: value["median_normal_completed_games_per_second"]),
            "game_workload": workload, "different_node_work_caveat": True, "playing_strength_inferred": False,
            "database_recording_capacity_measured": False}


def main() -> int:
    arguments = argument_parser().parse_args()
    report = {"schema": "laplace.chess-benchmark-receipt/v2", "completion": "initializing", "stockfish_samples": [], "cutechess_samples": [], "recommendations": None,
              "database_recording": {"measured": False, "recorded_games": None, "games_per_second": None}}
    output_created = False
    try:
        tools.require(arguments.samples >= 2 and arguments.warmups >= 0 and arguments.games > 0 and arguments.games % 2 == 0, "use at least two measured samples, nonnegative warmups, and a positive even game count")
        tools.require(min(arguments.depth, arguments.game_depth, arguments.game_threads, arguments.game_hash_mib, arguments.engine_overhead_mib) > 0 and arguments.timeout > 0 and arguments.overall_timeout > 0 and math.isfinite(arguments.timeout) and math.isfinite(arguments.overall_timeout) and arguments.reserve_memory_mib >= 0, "depth, threads, Hash, overhead and finite timeouts must be positive")
        workload = game_workload(arguments)
        report["game_workload"] = workload
        arguments.output.mkdir(parents=True, exist_ok=False)
        output_created = True
        arguments.output = arguments.output.resolve()
        host = host_observation()
        report["host"] = host
        selected, artifacts = tools.configuration()
        provider = chess_pgn.load_provider(arguments.output / "validation-provider")
        report["pgn_validation_provider"] = provider[2]
        tools.json_write(arguments.output / "validation-provider.json", provider[2])
        report["pgn_validation_provider_receipt_sha256"] = tools.digest(arguments.output / "validation-provider.json")
        installed = tools.verify_installation(arguments.prefix, selected, artifacts)
        report["tool_identity"] = installed
        sf = installed["tools"]["stockfish"]["executable"]
        cc = installed["tools"]["cutechess"]["executable"]
        cc_environment = tools.tool_environment(installed["tools"]["cutechess"])
        if arguments.stockfish:
            sf = str(arguments.stockfish.resolve())
            tools.probe_stockfish([sf], artifacts[selected["releases"]["stockfish"]["network"]])
            report["stockfish_override"] = {"executable": sf, "sha256": tools.digest(Path(sf)), "source_build_provenance": "external override; identify its source/build receipt separately from the recorded Refactor build"}
        report["executable_sha256_before"] = {path: tools.digest(Path(path)) for path in (sf, cc)}
        capability = tools.execute([sf], input="uci\ncompiler\nisready\nquit\n")
        options = parse_options(capability)
        (arguments.output / "uci-capabilities.txt").write_text(capability)
        report["uci_options"] = options
        report["capabilities"] = {"stockfish_and_cutechess": "executed", "syzygy": tools.tablebase_readback(arguments.syzygy_manifest), "lichess": tools.lichess_readback(False), "laplace_chess_route": "not-implemented-by-external-tool-benchmark"}
        resource_plan = plan(arguments, host, options)
        report["resource_plan"] = resource_plan
        report["profile"] = {key: str(value) if isinstance(value, Path) else value for key, value in vars(arguments).items()}
        report["profile_contract_sha256"] = tools.digest(tools.ROOT / "contracts/chess-benchmark.json")
        report["benchmark_implementation_sha256"] = tools.digest(Path(__file__))
        report["dependency_tool_implementation_sha256"] = tools.digest(Path(tools.__file__))
        report["pgn_provider_implementation_sha256"] = tools.digest(Path(chess_pgn.__file__))
        report["upstream_bench_source_sha256"] = None if arguments.stockfish else tools.digest(Path(installed["tools"]["stockfish"]["source"]) / "src/benchmark.cpp")
        report["repository_commit"] = tools.git(tools.ROOT, "rev-parse", "HEAD")
        report["topology_resource_epoch_sha256"] = tools.hashlib.sha256(json.dumps(host, sort_keys=True).encode()).hexdigest()
        report["timing_boundary"] = "spawn through leader completion and owned process-group cleanup; engine's search-only milliseconds also recorded; warmups excluded from summaries"
        report["limitations"] = ["Results belong to the recorded execution environment and granted resources.", "Fixed-depth SMP/hash configurations can visit different nodes and choose different moves; elapsed speedup is not identical-work scaling.", "A finite sweep supports a provisional best measured setting, not universal tuning or playing strength/Elo.", "Game-generation rates include process and UCI lifecycle overhead; independent PGN validation runs after that measured interval.", "Database recording, persistence throughput and Laplace playing strength are unmeasured.", "Diagnostic episodes cannot establish complete-game throughput or recommend full-game capacity.", "cgroup counters include unrelated workloads in the same cgroup; sampled RSS may double-count shared pages and miss short peaks; PSS is reported separately.", "CPU quota and memory headroom are observations, not reserved resources. No kernel, affinity or NUMA policy is changed."]
        receipt = arguments.output / "receipt.json"
        report["completion"] = "capabilities-observed-benchmarks-not-run"
        tools.json_write(receipt, report)
        if arguments.action == "observe":
            print(json.dumps({"receipt": str(receipt), "completion": report["completion"], "cpu_equivalents": host["effective_cpu_equivalents"], "memory_headroom_bytes": host["effective_memory_headroom_bytes"]}))
            return 0
        rng = random.Random(arguments.seed)
        sweep_started = time.monotonic()
        deadline = sweep_started + arguments.overall_timeout
        report["overall_budget_seconds"] = arguments.overall_timeout
        for profile, configurations in (("stockfish", resource_plan["stockfish"]), ("cutechess", resource_plan["cutechess"])):
            for repetition in range(arguments.warmups + arguments.samples):
                order = configurations.copy()
                rng.shuffle(order)
                for configuration in order:
                    index = len(report[f"{profile}_samples"])
                    transcript = arguments.output / f"{profile}-{index:04d}.log"
                    report["active_case"] = {"profile": profile, "configuration": configuration,
                                             "repetition": repetition, "warmup": repetition < arguments.warmups,
                                             "transcript": str(transcript), "measurement": None, "validation": "pending"}
                    case_timeout = remaining_timeout(deadline, arguments.timeout)
                    report["active_case"]["timeout_seconds"] = case_timeout
                    if profile == "stockfish":
                        input_text = f'setoption name NumaPolicy value auto\nsetoption name UCI_LimitStrength value false\nsetoption name Skill Level value 20\nsetoption name MultiPV value 1\nbench {configuration["hash_mib"]} {configuration["threads"]} {arguments.depth} default depth\nquit\n'
                        report["active_case"]["command"] = [sf]
                        measurement = measured_process([sf], transcript, host, case_timeout, resource_plan["memory_mib"] * MIB, input_text)
                        report["active_case"]["measurement"] = measurement
                        tools.require(measurement["completion"] == "completed", f"Stockfish benchmark failed: {measurement}")
                        measurement.update(parse_bench(transcript.read_text()))
                        measurement["wall_nodes_per_second"] = measurement["nodes"] / measurement["wall_seconds"]
                    else:
                        pgn = arguments.output / f"cutechess-{index:04d}.pgn"
                        command = cutechess_command(arguments, configuration, cc, sf, pgn)
                        report["active_case"].update({"command": command, "pgn": str(pgn), "workload": workload})
                        measurement = measured_process(command, transcript, host, case_timeout, resource_plan["memory_mib"] * MIB, environment=cc_environment)
                        report["active_case"]["measurement"] = measurement
                        if pgn.is_file():
                            report["active_case"]["pgn_sha256"] = tools.digest(pgn)
                        tools.require(measurement["completion"] == "completed" and "Finished match" in transcript.read_text(), f"CuteChess benchmark failed: {measurement}")
                        validation_started = time.monotonic()
                        measurement.update(validate_pgn(pgn.read_text(), arguments.games, provider=provider, diagnostic=arguments.diagnostic, max_moves=arguments.max_moves))
                        measurement["pgn_validation_wall_seconds"] = time.monotonic() - validation_started
                        measurement.update(game_rates(measurement, measurement["wall_seconds"], arguments.diagnostic))
                        measurement.update({"pgn": str(pgn), "pgn_sha256": tools.digest(pgn), "workload": workload})
                    measurement.update({"configuration": configuration, "warmup": repetition < arguments.warmups, "repetition": repetition})
                    report[f"{profile}_samples"].append(measurement)
                    report.pop("active_case")
                    report["completion"] = "running"
                    tools.json_write(receipt, report)
                    print(json.dumps({"profile": profile, "configuration": configuration, "repetition": repetition, "warmup": measurement["warmup"], "wall_seconds": measurement["wall_seconds"]}), flush=True)
        serial = aggregates(report["stockfish_samples"], "median_wall_nodes_per_second", "wall_nodes_per_second")
        game_rate = "diagnostic_episodes_per_second" if arguments.diagnostic else "normal_completed_games_per_second"
        games = aggregates(report["cutechess_samples"], f"median_{game_rate}", game_rate)
        report["overall_sweep_wall_seconds"] = time.monotonic() - sweep_started
        remaining_timeout(deadline, arguments.timeout)
        report["host_after"] = host_observation()
        report["executable_sha256_after"] = {path: tools.digest(Path(path)) for path in (sf, cc)}
        report["stability_failures"] = stability_failures(host, report["host_after"], report["executable_sha256_before"], report["executable_sha256_after"], resource_plan)
        tools.require(not report["stability_failures"], "; ".join(report["stability_failures"]))
        for item in serial:
            baseline = next((value for value in serial if value["configuration"] == {"threads": 1, "hash_mib": item["configuration"]["hash_mib"]}), None)
            item["elapsed_speedup_vs_one_thread"] = baseline["wall_seconds_median"] / item["wall_seconds_median"] if baseline else None
            item["elapsed_parallel_efficiency"] = item["elapsed_speedup_vs_one_thread"] / item["configuration"]["threads"] if baseline else None
        report["summaries"] = {"stockfish": serial, "cutechess": games}
        report["recommendations"] = recommendations(serial, games, workload)
        report["completion"] = "completed-with-rejected-configurations" if resource_plan["rejected"] else "completed"
        tools.json_write(receipt, report)
        print(json.dumps({"receipt": str(receipt), "completion": report["completion"], "recommendations": report["recommendations"]}, indent=2))
        return 0
    except (tools.ChessToolError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        report.update({"completion": "failed", "error": str(error), "recommendations": None})
        if "active_case" in report:
            report["failed_case"] = {**report.pop("active_case"), "error": str(error)}
        if output_created:
            tools.json_write(arguments.output / "receipt.json", report)
        print(json.dumps({"completion": "failed", "error": str(error)}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
