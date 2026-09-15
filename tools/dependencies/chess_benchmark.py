#!/usr/bin/env python3
"""Measure recorded chess executables inside an explicit observed resource grant."""

from __future__ import annotations

import argparse
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


def validate_pgn(text: str, games: int) -> dict:
    blocks = re.split(r"(?=\[Event )", text)
    records = []
    plies = 0
    for block in filter(lambda value: value.strip(), blocks):
        tags = dict(re.findall(r'^\[(\w+) "([^"\n]*)"\]$', block, re.M))
        tools.require(tags.get("Result") in {"1-0", "0-1", "1/2-1/2"}, "PGN game is unfinished")
        tools.require(not re.search(r"(?:stall|crash|disconnect|illegal move|time forfeit)", block, re.I), "PGN records a process/protocol failure")
        records.append({"white": tags.get("White"), "black": tags.get("Black"), "result": tags["Result"]})
        movetext = re.sub(r"\{[^}]*\}|\[[^\]]*\]", " ", block)
        tokens = re.sub(r"\d+\.(?:\.\.)?", " ", movetext).split()
        game_plies = sum(token not in {"1-0", "0-1", "1/2-1/2", "*"} and not token.startswith("$") for token in tokens)
        if "PlyCount" in tags:
            tools.require(int(tags["PlyCount"]) == game_plies, "PGN PlyCount disagrees with serialized moves")
        plies += game_plies
    tools.require(len(records) == games, f"expected {games} PGNs; observed {len(records)}")
    expected = games // 2
    tools.require(sum(record["white"] == "Source-A" and record["black"] == "Source-B" for record in records) == expected, "PGN color pairing is unbalanced")
    tools.require(sum(record["white"] == "Source-B" and record["black"] == "Source-A" for record in records) == expected, "PGN reverse color pairing is unbalanced")
    return {"games": games, "plies": plies, "records": records, "result_fingerprint": tools.hashlib.sha256(json.dumps(records, sort_keys=True).encode()).hexdigest()}


def powers(limit: int) -> list[int]:
    return sorted({1, limit, *[2 ** index for index in range(limit.bit_length()) if 2 ** index <= limit]})


def plan(arguments: argparse.Namespace, host: dict, options: dict) -> dict:
    cpu = arguments.cpu_budget if arguments.cpu_budget is not None else math.floor(host["effective_cpu_equivalents"])
    memory = arguments.memory_mib or max(1, host["effective_memory_headroom_bytes"] // MIB - arguments.reserve_memory_mib)
    tools.require(0 < cpu <= host["effective_cpu_equivalents"], "CPU budget exceeds the observed affinity/quota boundary or less than one full CPU equivalent is available")
    tools.require(memory > 0 and memory * MIB <= host["effective_memory_headroom_bytes"], "memory budget exceeds current effective headroom")
    if not host["cgroups"]:
        tools.require(arguments.cpu_budget is not None and arguments.memory_mib is not None, "unobserved cgroup limits require explicit CPU and memory budgets")
    threads = integer_list(arguments.threads) if arguments.threads else powers(math.floor(cpu))
    hashes = integer_list(arguments.hash_mib)
    concurrency = integer_list(arguments.concurrency) if arguments.concurrency else powers(max(1, math.floor(cpu / arguments.game_threads)))
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
    return {"cpu_budget": cpu, "memory_mib": memory, "resident_engine_overhead_estimate_mib": arguments.engine_overhead_mib, "memory_estimate_is_measurement": False, "ponder": False, "stockfish": serial, "cutechess": games, "rejected": rejected}


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
        result.append(summary)
    return result


def main() -> int:
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
    parser.add_argument("--reserve-memory-mib", type=int, default=512)
    parser.add_argument("--engine-overhead-mib", type=int, default=256)
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--depth", type=int, default=10)
    parser.add_argument("--games", type=int, default=8)
    parser.add_argument("--game-depth", type=int, default=6)
    parser.add_argument("--max-moves", type=int, default=20)
    parser.add_argument("--game-threads", type=int, default=1)
    parser.add_argument("--game-hash-mib", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--seed", type=int, default=20260915)
    parser.add_argument("--syzygy-manifest", type=Path)
    arguments = parser.parse_args()
    report = {"schema": "laplace.chess-benchmark-receipt/v1", "completion": "initializing", "stockfish_samples": [], "cutechess_samples": [], "recommendations": None}
    output_created = False
    try:
        tools.require(arguments.samples >= 2 and arguments.warmups >= 0 and arguments.games > 0 and arguments.games % 2 == 0, "use at least two measured samples, nonnegative warmups, and a positive even game count")
        tools.require(min(arguments.depth, arguments.game_depth, arguments.max_moves, arguments.game_threads, arguments.game_hash_mib, arguments.engine_overhead_mib) > 0 and arguments.timeout > 0 and arguments.reserve_memory_mib >= 0, "depth, moves, threads, Hash, overhead and timeout must be positive")
        arguments.output.mkdir(parents=True, exist_ok=False)
        output_created = True
        arguments.output = arguments.output.resolve()
        host = host_observation()
        report["host"] = host
        selected, artifacts = tools.configuration()
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
        report["upstream_bench_source_sha256"] = None if arguments.stockfish else tools.digest(Path(installed["tools"]["stockfish"]["source"]) / "src/benchmark.cpp")
        report["repository_commit"] = tools.git(tools.ROOT, "rev-parse", "HEAD")
        report["topology_resource_epoch_sha256"] = tools.hashlib.sha256(json.dumps(host, sort_keys=True).encode()).hexdigest()
        report["timing_boundary"] = "spawn through leader completion and owned process-group cleanup; engine's search-only milliseconds also recorded; warmups excluded from summaries"
        report["limitations"] = ["Results belong to the recorded execution environment and granted resources.", "Fixed-depth SMP/hash configurations can visit different nodes and choose different moves; elapsed speedup is not identical-work scaling.", "A short finite sweep supports a provisional best measured setting, not universal tuning or playing strength/Elo.", "CuteChess games are deliberately move-limited self-play; games/s includes process and UCI lifecycle overhead.", "cgroup counters include unrelated workloads in the same cgroup; sampled RSS may double-count shared pages and miss short peaks; PSS is reported separately.", "CPU quota and memory headroom are observations, not reserved resources. No kernel, affinity or NUMA policy is changed."]
        receipt = arguments.output / "receipt.json"
        report["completion"] = "capabilities-observed-benchmarks-not-run"
        tools.json_write(receipt, report)
        if arguments.action == "observe":
            print(json.dumps({"receipt": str(receipt), "completion": report["completion"], "cpu_equivalents": host["effective_cpu_equivalents"], "memory_headroom_bytes": host["effective_memory_headroom_bytes"]}))
            return 0
        rng = random.Random(arguments.seed)
        for profile, configurations in (("stockfish", resource_plan["stockfish"]), ("cutechess", resource_plan["cutechess"])):
            for repetition in range(arguments.warmups + arguments.samples):
                order = configurations.copy()
                rng.shuffle(order)
                for configuration in order:
                    index = len(report[f"{profile}_samples"])
                    transcript = arguments.output / f"{profile}-{index:04d}.log"
                    if profile == "stockfish":
                        input_text = f'setoption name NumaPolicy value auto\nsetoption name UCI_LimitStrength value false\nsetoption name Skill Level value 20\nsetoption name MultiPV value 1\nbench {configuration["hash_mib"]} {configuration["threads"]} {arguments.depth} default depth\nquit\n'
                        measurement = measured_process([sf], transcript, host, arguments.timeout, resource_plan["memory_mib"] * MIB, input_text)
                        tools.require(measurement["completion"] == "completed", f"Stockfish benchmark failed: {measurement}")
                        measurement.update(parse_bench(transcript.read_text()))
                        measurement["wall_nodes_per_second"] = measurement["nodes"] / measurement["wall_seconds"]
                    else:
                        pgn = arguments.output / f"cutechess-{index:04d}.pgn"
                        command = [cc, "-engine", f"cmd={sf}", "name=Source-A", "-engine", f"cmd={sf}", "name=Source-B", "-each", "proto=uci", "tc=60", f"depth={arguments.game_depth}", f'option.Threads={configuration["threads"]}', f'option.Hash={configuration["hash_mib"]}', "option.UCI_LimitStrength=false", "option.Skill Level=20", "option.MultiPV=1", "option.NumaPolicy=auto", "-games", str(arguments.games), "-rounds", "1", "-repeat", "-concurrency", str(configuration["concurrency"]), "-maxmoves", str(arguments.max_moves), "-pgnout", str(pgn)]
                        measurement = measured_process(command, transcript, host, arguments.timeout, resource_plan["memory_mib"] * MIB, environment=cc_environment)
                        tools.require(measurement["completion"] == "completed" and "Finished match" in transcript.read_text(), f"CuteChess benchmark failed: {measurement}")
                        measurement.update(validate_pgn(pgn.read_text(), arguments.games))
                        measurement.update({"games_per_second": arguments.games / measurement["wall_seconds"], "plies_per_second": measurement["plies"] / measurement["wall_seconds"], "pgn": str(pgn), "pgn_sha256": tools.digest(pgn)})
                    measurement.update({"configuration": configuration, "warmup": repetition < arguments.warmups, "repetition": repetition})
                    report[f"{profile}_samples"].append(measurement)
                    report["completion"] = "running"
                    tools.json_write(receipt, report)
                    print(json.dumps({"profile": profile, "configuration": configuration, "repetition": repetition, "warmup": measurement["warmup"], "wall_seconds": measurement["wall_seconds"]}), flush=True)
        serial = aggregates(report["stockfish_samples"], "median_wall_nodes_per_second", "wall_nodes_per_second")
        games = aggregates(report["cutechess_samples"], "median_games_per_second", "games_per_second")
        report["host_after"] = host_observation()
        report["executable_sha256_after"] = {path: tools.digest(Path(path)) for path in (sf, cc)}
        report["stability_failures"] = stability_failures(host, report["host_after"], report["executable_sha256_before"], report["executable_sha256_after"], resource_plan)
        tools.require(not report["stability_failures"], "; ".join(report["stability_failures"]))
        for item in serial:
            baseline = next((value for value in serial if value["configuration"] == {"threads": 1, "hash_mib": item["configuration"]["hash_mib"]}), None)
            item["elapsed_speedup_vs_one_thread"] = baseline["wall_seconds_median"] / item["wall_seconds_median"] if baseline else None
            item["elapsed_parallel_efficiency"] = item["elapsed_speedup_vs_one_thread"] / item["configuration"]["threads"] if baseline else None
        fastest = min(serial, key=lambda value: value["wall_seconds_median"])
        throughput = max(games, key=lambda value: value["median_games_per_second"])
        report["summaries"] = {"stockfish": serial, "cutechess": games}
        report["recommendations"] = {"scope": "provisional best measured configuration within this finite sweep", "single_engine_fixed_depth_suite": fastest, "aggregate_game_throughput": throughput, "different_node_work_caveat": True, "playing_strength_inferred": False}
        report["completion"] = "completed-with-rejected-configurations" if resource_plan["rejected"] else "completed"
        tools.json_write(receipt, report)
        print(json.dumps({"receipt": str(receipt), "completion": report["completion"], "recommendations": report["recommendations"]}, indent=2))
        return 0
    except (tools.ChessToolError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        report.update({"completion": "failed", "error": str(error), "recommendations": None})
        if output_created:
            tools.json_write(arguments.output / "receipt.json", report)
        print(json.dumps({"completion": "failed", "error": str(error)}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
